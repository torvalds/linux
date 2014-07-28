/*
 * Function Driver for DDR controller
 *
 * Copyright (C) 2011-2014 Fuzhou Rockchip Electronics Co.,Ltd
 * Author:
 * hcy@rock-chips.com
 * yk@rock-chips.com
 *
 * v1.00
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/clk.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <linux/cpu.h>
#include <dt-bindings/clock/ddr.h>
#include <linux/rockchip/cru.h>
#include <linux/rk_fb.h>
#include "cpu_axi.h"

typedef uint32_t uint32;

#ifdef CONFIG_FB_ROCKCHIP
#define DDR_CHANGE_FREQ_IN_LCDC_VSYNC
#endif
/***********************************
 * Global Control Macro
 ***********************************/
//#define ENABLE_DDR_CLCOK_GPLL_PATH  //for RK3188

#define DDR3_DDR2_ODT_DISABLE_FREQ    (333)
#define DDR3_DDR2_DLL_DISABLE_FREQ    (333)
#define SR_IDLE                       (0x3)   //unit:32*DDR clk cycle, and 0 for disable auto self-refresh
#define PD_IDLE                       (0X40)  //unit:DDR clk cycle, and 0 for disable auto power-down

//#if (DDR3_DDR2_ODT_DISABLE_FREQ > DDR3_DDR2_DLL_DISABLE_FREQ)
//#error
//#endif

#define ddr_print(x...) printk( "DDR DEBUG: " x )

/***********************************
 * ARCH Relative Macro and Struction
 ***********************************/
#define SRAM_CODE_OFFSET        rockchip_sram_virt
#define SRAM_SIZE               rockchip_sram_size

/*
 * PMU
 */
//PMU_PWRDN_ST
#define pd_scu_pwr_st       (1<<11)
#define pd_hevc_pwr_st      (1<<10)
#define pd_gpu_pwr_st       (1<<9)
#define pd_video_pwr_st     (1<<8)
#define pd_vio_pwr_st       (1<<7)
#define pd_peri_pwr_st      (1<<6)
#define pd_bus_pwr_st      (1<<5)
//PMU_IDLE_REQ
#define idle_req_hevc_cfg   (1<<9)
#define idle_req_cpup_cfg   (1<<8)
#define idle_req_dma_cfg    (1<<7)
#define idle_req_alive_cfg  (1<<6)
#define idle_req_core_cfg   (1<<5)
#define idle_req_vio_cfg    (1<<4)
#define idle_req_video_cfg  (1<<3)
#define idle_req_gpu_cfg    (1<<2)
#define idle_req_peri_cfg   (1<<1)
#define idle_req_bus_cfg    (1<<0)

//PMU_IDLE_ST
#define idle_ack_hevc     (1<<25)
#define idle_ack_cpup     (1<<24)
#define idle_ack_dma      (1<<23)
#define idle_ack_alive    (1<<22)
#define idle_ack_core     (1<<21)
#define idle_ack_vio      (1<<20)
#define idle_ack_video    (1<<19)
#define idle_ack_gpu      (1<<18)
#define idle_ack_peir     (1<<17)
#define idle_ack_bus      (1<<16)

#define idle_hevc   (1<<9)
#define idle_cpup   (1<<8)
#define idle_dma    (1<<7)
#define idle_alive  (1<<6)
#define idle_core   (1<<5)
#define idle_vio    (1<<4)
#define idle_video  (1<<3)
#define idle_gpu    (1<<2)
#define idle_peri   (1<<1)
#define idle_bus    (1<<0)

//PMU_PWRMODE_CON
/* ch=0, channel a
   ch=1, channel b
 */
#define ddrio_ret_de_req(ch)  (1<<(21+(ch)))
#define ddrc_gating_en(ch)    (1<<(19+(ch)))
#define ddrio_ret_en(ch)      (1<<(17+(ch)))
#define sref_enter_en(ch)     (1<<(15+(ch)))

//PMU_PWR_STATE
#define SREF_EXIT             (1<<26)
#define DDR_IO_PWRUP          (1<<25)

//PMU registers
typedef volatile struct tagPMU_FILE
{
    uint32 PMU_WAKEUP_CFG[2];
    uint32 PMU_PWRDN_CON;
    uint32 PMU_PWRDN_ST;
    uint32 PMU_IDLE_REQ;
    uint32 PMU_IDLE_ST;
    uint32 PMU_PWRMODE_CON;
    uint32 PMU_PWR_STATE;
    uint32 PMU_OSC_CNT;
    uint32 PMU_PLL_CNT;
    uint32 PMU_STABL_CNT;
    uint32 PMU_DDR0IO_PWRON_CNT;
    uint32 PMU_DDR1IO_PWRON_CNT;
    uint32 PMU_CORE_PWRDN_CNT;
    uint32 PMU_CORE_PWRUP_CNT;
    uint32 PMU_GPU_PWRDWN_CNT;
    uint32 PMU_GPU_PWRUP_CNT;
    uint32 PMU_WAKEUP_RST_CLR_CNT;
    uint32 PMU_SFT_CON;
    uint32 PMU_DDR_SREF_ST;
    uint32 PMU_INT_CON;
    uint32 PMU_INT_ST;
    uint32 PMU_BOOT_ADDR_SEL;
    uint32 PMU_GRF_CON;
    uint32 PMU_GPIO_SR;
    uint32 PMU_GPIO0_A_PULL;
    uint32 PMU_GPIO0_B_PULL;
    uint32 PMU_GPIO0_C_PULL;
    uint32 PMU_GPIO0_A_DRV;
    uint32 PMU_GPIO0_B_DRV;
    uint32 PMU_GPIO0_C_DRV;
    uint32 PMU_GPIO_OP;
    uint32 PMU_GPIO0_SEL18;
    uint32 PMU_GPIO0_A_IOMUX;
    uint32 PMU_GPIO0_B_IOMUX;
    uint32 PMU_GPIO0_C_IOMUX;
    uint32 PMU_GPIO0_D_IOMUX;
    uint32 PMU_PMU_SYS_REG[4];
}PMU_FILE, *pPMU_FILE;

/*
 * CRU
 */
typedef enum PLL_ID_Tag
{
    APLL=0,
    DPLL,
    CPLL,
    GPLL,
    PLL_MAX
}PLL_ID;

#define PLL_RESET  (((0x1<<5)<<16) | (0x1<<5))
#define PLL_DE_RESET  (((0x1<<5)<<16) | (0x0<<5))
#define NR(n)      ((0x3F<<(8+16)) | (((n)-1)<<8))
#define NO(n)      ((0xF<<16) | ((n)-1))
#define NF(n)      ((0x1FFF<<16) | ((n)-1))
#define NB(n)      ((0xFFF<<16) | ((n)-1))

 //CRU Registers
typedef volatile struct tagCRU_STRUCT
{
    uint32 CRU_PLL_CON[5][4];
    uint32 CRU_MODE_CON;
    uint32 reserved1[(0x60-0x54)/4];
    uint32 CRU_CLKSEL_CON[43];
    uint32 reserved2[(0x160-0x10c)/4];
    uint32 CRU_CLKGATE_CON[19];
    uint32 reserved3[(0x1b0-0x1ac)/4];
    uint32 CRU_GLB_SRST_FST_VALUE;
    uint32 CRU_GLB_SRST_SND_VALUE;
    uint32 CRU_SOFTRST_CON[12];
    uint32 CRU_MISC_CON;
    uint32 CRU_GLB_CNT_TH;
    uint32 CRU_TSADC_RST_CON;
    uint32 reserved4[(0x200-0x1f4)/4];
    uint32 CRU_SDMMC_CON[2];
    uint32 CRU_SDIO0_CON[2];
    uint32 CRU_SDIO1_CON[2];
    uint32 CRU_EMMC_CON[2];
    // other regigster unused in boot
} CRU_REG, *pCRU_REG;

/*
 * GRF
 */
//REG FILE registers
typedef volatile struct tagREG_FILE
{
    uint32 GRF_GPIO1A_IOMUX;
    uint32 GRF_GPIO1B_IOMUX;
    uint32 GRF_GPIO1C_IOMUX;
    uint32 GRF_GPIO1D_IOMUX;
    uint32 GRF_GPIO2A_IOMUX;
    uint32 GRF_GPIO2B_IOMUX;
    uint32 GRF_GPIO2C_IOMUX;
    uint32 GRF_GPIO2D_IOMUX;
    uint32 GRF_GPIO3A_IOMUX;
    uint32 GRF_GPIO3B_IOMUX;
    uint32 GRF_GPIO3C_IOMUX;
    uint32 GRF_GPIO3DL_IOMUX;
    uint32 GRF_GPIO3DH_IOMUX;
    uint32 GRF_GPIO4AL_IOMUX;
    uint32 GRF_GPIO4AH_IOMUX;
    uint32 GRF_GPIO4BL_IOMUX;
    uint32 GRF_GPIO4BH_IOMUX;
    uint32 GRF_GPIO4C_IOMUX;
    uint32 GRF_GPIO4D_IOMUX;
    uint32 GRF_GPIO5A_IOMUX;
    uint32 GRF_GPIO5B_IOMUX;
    uint32 GRF_GPIO5C_IOMUX;
    uint32 GRF_GPIO5D_IOMUX;
    uint32 GRF_GPIO6A_IOMUX;
    uint32 GRF_GPIO6B_IOMUX;
    uint32 GRF_GPIO6C_IOMUX;
    uint32 GRF_GPIO6D_IOMUX;
    uint32 GRF_GPIO7A_IOMUX;
    uint32 GRF_GPIO7B_IOMUX;
    uint32 GRF_GPIO7CL_IOMUX;
    uint32 GRF_GPIO7CH_IOMUX;
    uint32 GRF_GPIO7D_IOMUX;
    uint32 GRF_GPIO8A_IOMUX;
    uint32 GRF_GPIO8B_IOMUX;
    uint32 GRF_GPIO8C_IOMUX;
    uint32 GRF_GPIO8D_IOMUX;
    uint32 reserved1[(0x100-0x90)/4];
    uint32 GRF_GPIO1L_SR;
    uint32 GRF_GPIO1H_SR;
    uint32 GRF_GPIO2L_SR;
    uint32 GRF_GPIO2H_SR;
    uint32 GRF_GPIO3L_SR;
    uint32 GRF_GPIO3H_SR;
    uint32 GRF_GPIO4L_SR;
    uint32 GRF_GPIO4H_SR;
    uint32 GRF_GPIO5L_SR;
    uint32 GRF_GPIO5H_SR;
    uint32 GRF_GPIO6L_SR;
    uint32 GRF_GPIO6H_SR;
    uint32 GRF_GPIO7L_SR;
    uint32 GRF_GPIO7H_SR;
    uint32 GRF_GPIO8L_SR;
    uint32 GRF_GPIO8H_SR;
    uint32 GRF_GPIO1A_P;
    uint32 GRF_GPIO1B_P;
    uint32 GRF_GPIO1C_P;
    uint32 GRF_GPIO1D_P;
    uint32 GRF_GPIO2A_P;
    uint32 GRF_GPIO2B_P;
    uint32 GRF_GPIO2C_P;
    uint32 GRF_GPIO2D_P;
    uint32 GRF_GPIO3A_P;
    uint32 GRF_GPIO3B_P;
    uint32 GRF_GPIO3C_P;
    uint32 GRF_GPIO3D_P;
    uint32 GRF_GPIO4A_P;
    uint32 GRF_GPIO4B_P;
    uint32 GRF_GPIO4C_P;
    uint32 GRF_GPIO4D_P;
    uint32 GRF_GPIO5A_P;
    uint32 GRF_GPIO5B_P;
    uint32 GRF_GPIO5C_P;
    uint32 GRF_GPIO5D_P;
    uint32 GRF_GPIO6A_P;
    uint32 GRF_GPIO6B_P;
    uint32 GRF_GPIO6C_P;
    uint32 GRF_GPIO6D_P;
    uint32 GRF_GPIO7A_P;
    uint32 GRF_GPIO7B_P;
    uint32 GRF_GPIO7C_P;
    uint32 GRF_GPIO7D_P;
    uint32 GRF_GPIO8A_P;
    uint32 GRF_GPIO8B_P;
    uint32 GRF_GPIO8C_P;
    uint32 GRF_GPIO8D_P;
    uint32 GRF_GPIO1A_E;
    uint32 GRF_GPIO1B_E;
    uint32 GRF_GPIO1C_E;
    uint32 GRF_GPIO1D_E;
    uint32 GRF_GPIO2A_E;
    uint32 GRF_GPIO2B_E;
    uint32 GRF_GPIO2C_E;
    uint32 GRF_GPIO2D_E;
    uint32 GRF_GPIO3A_E;
    uint32 GRF_GPIO3B_E;
    uint32 GRF_GPIO3C_E;
    uint32 GRF_GPIO3D_E;
    uint32 GRF_GPIO4A_E;
    uint32 GRF_GPIO4B_E;
    uint32 GRF_GPIO4C_E;
    uint32 GRF_GPIO4D_E;
    uint32 GRF_GPIO5A_E;
    uint32 GRF_GPIO5B_E;
    uint32 GRF_GPIO5C_E;
    uint32 GRF_GPIO5D_E;
    uint32 GRF_GPIO6A_E;
    uint32 GRF_GPIO6B_E;
    uint32 GRF_GPIO6C_E;
    uint32 GRF_GPIO6D_E;
    uint32 GRF_GPIO7A_E;
    uint32 GRF_GPIO7B_E;
    uint32 GRF_GPIO7C_E;
    uint32 GRF_GPIO7D_E;
    uint32 GRF_GPIO8A_E;
    uint32 GRF_GPIO8B_E;
    uint32 GRF_GPIO8C_E;
    uint32 GRF_GPIO8D_E;
    uint32 GRF_GPIO_SMT;
    uint32 GRF_SOC_CON[15];
    uint32 GRF_SOC_STATUS[23];
    uint32 reserved2[(0x2e0-0x2dc)/4];
    uint32 GRF_DMAC2_CON[4];
    uint32 GRF_DDRC0_CON0;
    uint32 GRF_DDRC1_CON0;
    uint32 GRF_CPU_CON[5];
    uint32 reserved3[(0x318-0x30c)/4];
    uint32 GRF_CPU_STATUS0;
    uint32 reserved4[(0x320-0x31c)/4];
    uint32 GRF_UOC0_CON[5];
    uint32 GRF_UOC1_CON[5];
    uint32 GRF_UOC2_CON[4];
    uint32 GRF_UOC3_CON[2];
    uint32 GRF_UOC4_CON[2];
    uint32 GRF_DLL_CON[3];
    uint32 GRF_DLL_STATUS[3];
    uint32 GRF_IO_VSEL;
    uint32 GRF_SARADC_TESTBIT;
    uint32 GRF_TSADC_TESTBIT_L;
    uint32 GRF_TSADC_TESTBIT_H;
    uint32 GRF_OS_REG[4];
    uint32 GRF_FAST_BOOT_ADDR;
    uint32 GRF_SOC_CON15;
    uint32 GRF_SOC_CON16;
} REG_FILE, *pREG_FILE;

/*
 * PCTL
 */
//SCTL
#define INIT_STATE                     (0)
#define CFG_STATE                      (1)
#define GO_STATE                       (2)
#define SLEEP_STATE                    (3)
#define WAKEUP_STATE                   (4)

//STAT
#define Init_mem                       (0)
#define Config                         (1)
#define Config_req                     (2)
#define Access                         (3)
#define Access_req                     (4)
#define Low_power                      (5)
#define Low_power_entry_req            (6)
#define Low_power_exit_req             (7)

//MCFG
#define mddr_lpddr2_clk_stop_idle(n)   ((n)<<24)
#define pd_idle(n)                     ((n)<<8)
#define mddr_en                        (2<<22)
#define lpddr2_en                      (3<<22)
#define ddr2_en                        (0<<5)
#define ddr3_en                        (1<<5)
#define lpddr2_s2                      (0<<6)
#define lpddr2_s4                      (1<<6)
#define mddr_lpddr2_bl_2               (0<<20)
#define mddr_lpddr2_bl_4               (1<<20)
#define mddr_lpddr2_bl_8               (2<<20)
#define mddr_lpddr2_bl_16              (3<<20)
#define ddr2_ddr3_bl_4                 (0)
#define ddr2_ddr3_bl_8                 (1)
#define tfaw_cfg(n)                    (((n)-4)<<18)
#define pd_exit_slow                   (0<<17)
#define pd_exit_fast                   (1<<17)
#define pd_type(n)                     ((n)<<16)
#define two_t_en(n)                    ((n)<<3)
#define bl8int_en(n)                   ((n)<<2)
#define cke_or_en(n)                   ((n)<<1)

//POWCTL
#define power_up_start                 (1<<0)

//POWSTAT
#define power_up_done                  (1<<0)

//DFISTSTAT0
#define dfi_init_complete              (1<<0)

//CMDTSTAT
#define cmd_tstat                      (1<<0)

//CMDTSTATEN
#define cmd_tstat_en                   (1<<1)

//MCMD
#define Deselect_cmd                   (0)
#define PREA_cmd                       (1)
#define REF_cmd                        (2)
#define MRS_cmd                        (3)
#define ZQCS_cmd                       (4)
#define ZQCL_cmd                       (5)
#define RSTL_cmd                       (6)
#define MRR_cmd                        (8)
#define DPDE_cmd                       (9)

#define lpddr2_op(n)                   ((n)<<12)
#define lpddr2_ma(n)                   ((n)<<4)

#define bank_addr(n)                   ((n)<<17)
#define cmd_addr(n)                    ((n)<<4)

#define start_cmd                      (1u<<31)

typedef union STAT_Tag
{
    uint32 d32;
    struct
    {
        unsigned ctl_stat : 3;
        unsigned reserved3 : 1;
        unsigned lp_trig : 3;
        unsigned reserved7_31 : 25;
    }b;
}STAT_T;

typedef union SCFG_Tag
{
    uint32 d32;
    struct
    {
        unsigned hw_low_power_en : 1;
        unsigned reserved1_5 : 5;
        unsigned nfifo_nif1_dis : 1;
        unsigned reserved7 : 1;
        unsigned bbflags_timing : 4;
        unsigned reserved12_31 : 20;
    } b;
}SCFG_T;

/* DDR Controller register struct */
typedef volatile struct DDR_REG_Tag
{
    //Operational State, Control, and Status Registers
    SCFG_T SCFG;                   //State Configuration Register
    volatile uint32 SCTL;                   //State Control Register
    STAT_T STAT;                   //State Status Register
    volatile uint32 INTRSTAT;               //Interrupt Status Register
    uint32 reserved0[(0x40-0x10)/4];
    //Initailization Control and Status Registers
    volatile uint32 MCMD;                   //Memory Command Register
    volatile uint32 POWCTL;                 //Power Up Control Registers
    volatile uint32 POWSTAT;                //Power Up Status Register
    volatile uint32 CMDTSTAT;               //Command Timing Status Register
    volatile uint32 CMDTSTATEN;             //Command Timing Status Enable Register
    uint32 reserved1[(0x60-0x54)/4];
    volatile uint32 MRRCFG0;                //MRR Configuration 0 Register
    volatile uint32 MRRSTAT0;               //MRR Status 0 Register
    volatile uint32 MRRSTAT1;               //MRR Status 1 Register
    uint32 reserved2[(0x7c-0x6c)/4];
    //Memory Control and Status Registers
    volatile uint32 MCFG1;                  //Memory Configuration 1 Register
    volatile uint32 MCFG;                   //Memory Configuration Register
    volatile uint32 PPCFG;                  //Partially Populated Memories Configuration Register
    volatile uint32 MSTAT;                  //Memory Status Register
    volatile uint32 LPDDR2ZQCFG;            //LPDDR2 ZQ Configuration Register
    uint32 reserved3;
    //DTU Control and Status Registers
    volatile uint32 DTUPDES;                //DTU Status Register
    volatile uint32 DTUNA;                  //DTU Number of Random Addresses Created Register
    volatile uint32 DTUNE;                  //DTU Number of Errors Register
    volatile uint32 DTUPRD0;                //DTU Parallel Read 0
    volatile uint32 DTUPRD1;                //DTU Parallel Read 1
    volatile uint32 DTUPRD2;                //DTU Parallel Read 2
    volatile uint32 DTUPRD3;                //DTU Parallel Read 3
    volatile uint32 DTUAWDT;                //DTU Address Width
    uint32 reserved4[(0xc0-0xb4)/4];
    //Memory Timing Registers
    volatile uint32 TOGCNT1U;               //Toggle Counter 1U Register
    volatile uint32 TINIT;                  //t_init Timing Register
    volatile uint32 TRSTH;                  //Reset High Time Register
    volatile uint32 TOGCNT100N;             //Toggle Counter 100N Register
    volatile uint32 TREFI;                  //t_refi Timing Register
    volatile uint32 TMRD;                   //t_mrd Timing Register
    volatile uint32 TRFC;                   //t_rfc Timing Register
    volatile uint32 TRP;                    //t_rp Timing Register
    volatile uint32 TRTW;                   //t_rtw Timing Register
    volatile uint32 TAL;                    //AL Latency Register
    volatile uint32 TCL;                    //CL Timing Register
    volatile uint32 TCWL;                   //CWL Register
    volatile uint32 TRAS;                   //t_ras Timing Register
    volatile uint32 TRC;                    //t_rc Timing Register
    volatile uint32 TRCD;                   //t_rcd Timing Register
    volatile uint32 TRRD;                   //t_rrd Timing Register
    volatile uint32 TRTP;                   //t_rtp Timing Register
    volatile uint32 TWR;                    //t_wr Timing Register
    volatile uint32 TWTR;                   //t_wtr Timing Register
    volatile uint32 TEXSR;                  //t_exsr Timing Register
    volatile uint32 TXP;                    //t_xp Timing Register
    volatile uint32 TXPDLL;                 //t_xpdll Timing Register
    volatile uint32 TZQCS;                  //t_zqcs Timing Register
    volatile uint32 TZQCSI;                 //t_zqcsi Timing Register
    volatile uint32 TDQS;                   //t_dqs Timing Register
    volatile uint32 TCKSRE;                 //t_cksre Timing Register
    volatile uint32 TCKSRX;                 //t_cksrx Timing Register
    volatile uint32 TCKE;                   //t_cke Timing Register
    volatile uint32 TMOD;                   //t_mod Timing Register
    volatile uint32 TRSTL;                  //Reset Low Timing Register
    volatile uint32 TZQCL;                  //t_zqcl Timing Register
    volatile uint32 TMRR;                   //t_mrr Timing Register
    volatile uint32 TCKESR;                 //t_ckesr Timing Register
    volatile uint32 TDPD;                   //t_dpd Timing Register
    uint32 reserved5[(0x180-0x148)/4];
    //ECC Configuration, Control, and Status Registers
    volatile uint32 ECCCFG;                   //ECC Configuration Register
    volatile uint32 ECCTST;                   //ECC Test Register
    volatile uint32 ECCCLR;                   //ECC Clear Register
    volatile uint32 ECCLOG;                   //ECC Log Register
    uint32 reserved6[(0x200-0x190)/4];
    //DTU Control and Status Registers
    volatile uint32 DTUWACTL;                 //DTU Write Address Control Register
    volatile uint32 DTURACTL;                 //DTU Read Address Control Register
    volatile uint32 DTUCFG;                   //DTU Configuration Control Register
    volatile uint32 DTUECTL;                  //DTU Execute Control Register
    volatile uint32 DTUWD0;                   //DTU Write Data 0
    volatile uint32 DTUWD1;                   //DTU Write Data 1
    volatile uint32 DTUWD2;                   //DTU Write Data 2
    volatile uint32 DTUWD3;                   //DTU Write Data 3
    volatile uint32 DTUWDM;                   //DTU Write Data Mask
    volatile uint32 DTURD0;                   //DTU Read Data 0
    volatile uint32 DTURD1;                   //DTU Read Data 1
    volatile uint32 DTURD2;                   //DTU Read Data 2
    volatile uint32 DTURD3;                   //DTU Read Data 3
    volatile uint32 DTULFSRWD;                //DTU LFSR Seed for Write Data Generation
    volatile uint32 DTULFSRRD;                //DTU LFSR Seed for Read Data Generation
    volatile uint32 DTUEAF;                   //DTU Error Address FIFO
    //DFI Control Registers
    volatile uint32 DFITCTRLDELAY;            //DFI tctrl_delay Register
    volatile uint32 DFIODTCFG;                //DFI ODT Configuration Register
    volatile uint32 DFIODTCFG1;               //DFI ODT Configuration 1 Register
    volatile uint32 DFIODTRANKMAP;            //DFI ODT Rank Mapping Register
    //DFI Write Data Registers
    volatile uint32 DFITPHYWRDATA;            //DFI tphy_wrdata Register
    volatile uint32 DFITPHYWRLAT;             //DFI tphy_wrlat Register
    uint32 reserved7[(0x260-0x258)/4];
    volatile uint32 DFITRDDATAEN;             //DFI trddata_en Register
    volatile uint32 DFITPHYRDLAT;             //DFI tphy_rddata Register
    uint32 reserved8[(0x270-0x268)/4];
    //DFI Update Registers
    volatile uint32 DFITPHYUPDTYPE0;          //DFI tphyupd_type0 Register
    volatile uint32 DFITPHYUPDTYPE1;          //DFI tphyupd_type1 Register
    volatile uint32 DFITPHYUPDTYPE2;          //DFI tphyupd_type2 Register
    volatile uint32 DFITPHYUPDTYPE3;          //DFI tphyupd_type3 Register
    volatile uint32 DFITCTRLUPDMIN;           //DFI tctrlupd_min Register
    volatile uint32 DFITCTRLUPDMAX;           //DFI tctrlupd_max Register
    volatile uint32 DFITCTRLUPDDLY;           //DFI tctrlupd_dly Register
    uint32 reserved9;
    volatile uint32 DFIUPDCFG;                //DFI Update Configuration Register
    volatile uint32 DFITREFMSKI;              //DFI Masked Refresh Interval Register
    volatile uint32 DFITCTRLUPDI;             //DFI tctrlupd_interval Register
    uint32 reserved10[(0x2ac-0x29c)/4];
    volatile uint32 DFITRCFG0;                //DFI Training Configuration 0 Register
    volatile uint32 DFITRSTAT0;               //DFI Training Status 0 Register
    volatile uint32 DFITRWRLVLEN;             //DFI Training dfi_wrlvl_en Register
    volatile uint32 DFITRRDLVLEN;             //DFI Training dfi_rdlvl_en Register
    volatile uint32 DFITRRDLVLGATEEN;         //DFI Training dfi_rdlvl_gate_en Register
    //DFI Status Registers
    volatile uint32 DFISTSTAT0;               //DFI Status Status 0 Register
    volatile uint32 DFISTCFG0;                //DFI Status Configuration 0 Register
    volatile uint32 DFISTCFG1;                //DFI Status configuration 1 Register
    uint32 reserved11;
    volatile uint32 DFITDRAMCLKEN;            //DFI tdram_clk_enalbe Register
    volatile uint32 DFITDRAMCLKDIS;           //DFI tdram_clk_disalbe Register
    volatile uint32 DFISTCFG2;                //DFI Status configuration 2 Register
    volatile uint32 DFISTPARCLR;              //DFI Status Parity Clear Register
    volatile uint32 DFISTPARLOG;              //DFI Status Parity Log Register
    uint32 reserved12[(0x2f0-0x2e4)/4];
    //DFI Low Power Registers
    volatile uint32 DFILPCFG0;                //DFI Low Power Configuration 0 Register
    uint32 reserved13[(0x300-0x2f4)/4];
    //DFI Training 2 Registers
    volatile uint32 DFITRWRLVLRESP0;          //DFI Training dif_wrlvl_resp Status 0 Register
    volatile uint32 DFITRWRLVLRESP1;          //DFI Training dif_wrlvl_resp Status 1 Register
    volatile uint32 DFITRWRLVLRESP2;          //DFI Training dif_wrlvl_resp Status 2 Register
    volatile uint32 DFITRRDLVLRESP0;          //DFI Training dif_rdlvl_resp Status 0 Register
    volatile uint32 DFITRRDLVLRESP1;          //DFI Training dif_rdlvl_resp Status 1 Register
    volatile uint32 DFITRRDLVLRESP2;          //DFI Training dif_rdlvl_resp Status 2 Register
    volatile uint32 DFITRWRLVLDELAY0;         //DFI Training dif_wrlvl_delay Configuration 0 Register
    volatile uint32 DFITRWRLVLDELAY1;         //DFI Training dif_wrlvl_delay Configuration 1 Register
    volatile uint32 DFITRWRLVLDELAY2;         //DFI Training dif_wrlvl_delay Configuration 2 Register
    volatile uint32 DFITRRDLVLDELAY0;         //DFI Training dif_rdlvl_delay Configuration 0 Register
    volatile uint32 DFITRRDLVLDELAY1;         //DFI Training dif_rdlvl_delay Configuration 1 Register
    volatile uint32 DFITRRDLVLDELAY2;         //DFI Training dif_rdlvl_delay Configuration 2 Register
    volatile uint32 DFITRRDLVLGATEDELAY0;     //DFI Training dif_rdlvl_gate_delay Configuration 0 Register
    volatile uint32 DFITRRDLVLGATEDELAY1;     //DFI Training dif_rdlvl_gate_delay Configuration 1 Register
    volatile uint32 DFITRRDLVLGATEDELAY2;     //DFI Training dif_rdlvl_gate_delay Configuration 2 Register
    volatile uint32 DFITRCMD;                 //DFI Training Command Register
    uint32 reserved14[(0x3f8-0x340)/4];
    //IP Status Registers
    volatile uint32 IPVR;                     //IP Version Register
    volatile uint32 IPTR;                     //IP Type Register
}DDR_REG_T, *pDDR_REG_T;

/*
 * PUBL
 */
//PIR
#define INIT                 (1<<0)
#define DLLSRST              (1<<1)
#define DLLLOCK              (1<<2)
#define ZCAL                 (1<<3)
#define ITMSRST              (1<<4)
#define DRAMRST              (1<<5)
#define DRAMINIT             (1<<6)
#define QSTRN                (1<<7)
#define EYETRN               (1<<8)
#define ICPC                 (1<<16)
#define DLLBYP               (1<<17)
#define CTLDINIT             (1<<18)
#define CLRSR                (1<<28)
#define LOCKBYP              (1<<29)
#define ZCALBYP              (1<<30)
#define INITBYP              (1u<<31)

//PGCR
#define DFTLMT(n)            ((n)<<3)
#define DFTCMP(n)            ((n)<<2)
#define DQSCFG(n)            ((n)<<1)
#define ITMDMD(n)            ((n)<<0)
#define RANKEN(n)            ((n)<<18)

//PGSR
#define IDONE                (1<<0)
#define DLDONE               (1<<1)
#define ZCDONE               (1<<2)
#define DIDONE               (1<<3)
#define DTDONE               (1<<4)
#define DTERR                (1<<5)
#define DTIERR               (1<<6)
#define DFTERR               (1<<7)
#define TQ                   (1u<<31)

//PTR0
#define tITMSRST(n)          ((n)<<18)
#define tDLLLOCK(n)          ((n)<<6)
#define tDLLSRST(n)          ((n)<<0)

//PTR1
#define tDINIT1(n)           ((n)<<19)
#define tDINIT0(n)           ((n)<<0)

//PTR2
#define tDINIT3(n)           ((n)<<17)
#define tDINIT2(n)           ((n)<<0)

//DSGCR
#define DQSGE(n)             ((n)<<8)
#define DQSGX(n)             ((n)<<5)

typedef union DCR_Tag
{
    uint32 d32;
    struct
    {
        unsigned DDRMD : 3;
        unsigned DDR8BNK : 1;
        unsigned PDQ : 3;
        unsigned MPRDQ : 1;
        unsigned DDRTYPE : 2;
        unsigned reserved10_26 : 17;
        unsigned NOSRA : 1;
        unsigned DDR2T : 1;
        unsigned UDIMM : 1;
        unsigned RDIMM : 1;
        unsigned TPD : 1;
    } b;
}DCR_T;

typedef volatile struct DATX8_REG_Tag
{
    volatile uint32 DXGCR;                 //DATX8 General Configuration Register
    volatile uint32 DXGSR[2];              //DATX8 General Status Register
    volatile uint32 DXDLLCR;               //DATX8 DLL Control Register
    volatile uint32 DXDQTR;                //DATX8 DQ Timing Register
    volatile uint32 DXDQSTR;               //DATX8 DQS Timing Register
    uint32 reserved[0x80-0x76];
}DATX8_REG_T;

/* DDR PHY register struct */
typedef volatile struct DDRPHY_REG_Tag
{
    volatile uint32 RIDR;                   //Revision Identification Register
    volatile uint32 PIR;                    //PHY Initialization Register
    volatile uint32 PGCR;                   //PHY General Configuration Register
    volatile uint32 PGSR;                   //PHY General Status Register
    volatile uint32 DLLGCR;                 //DLL General Control Register
    volatile uint32 ACDLLCR;                //AC DLL Control Register
    volatile uint32 PTR[3];                 //PHY Timing Registers 0-2
    volatile uint32 ACIOCR;                 //AC I/O Configuration Register
    volatile uint32 DXCCR;                  //DATX8 Common Configuration Register
    volatile uint32 DSGCR;                  //DDR System General Configuration Register
    DCR_T DCR;                    //DRAM Configuration Register
    volatile uint32 DTPR[3];                //DRAM Timing Parameters Register 0-2
    volatile uint32 MR[4];                    //Mode Register 0-3
    volatile uint32 ODTCR;                  //ODT Configuration Register
    volatile uint32 DTAR;                   //Data Training Address Register
    volatile uint32 DTDR[2];                //Data Training Data Register 0-1

    uint32 reserved1[0x30-0x18];
    uint32 DCU[0x38-0x30];
    uint32 reserved2[0x40-0x38];
    uint32 BIST[0x51-0x40];
    uint32 reserved3[0x60-0x51];

    volatile uint32 ZQ0CR[2];               //ZQ 0 Impedance Control Register 0-1
    volatile uint32 ZQ0SR[2];               //ZQ 0 Impedance Status Register 0-1
    volatile uint32 ZQ1CR[2];               //ZQ 1 Impedance Control Register 0-1
    volatile uint32 ZQ1SR[2];               //ZQ 1 Impedance Status Register 0-1
    volatile uint32 ZQ2CR[2];               //ZQ 2 Impedance Control Register 0-1
    volatile uint32 ZQ2SR[2];               //ZQ 2 Impedance Status Register 0-1
    volatile uint32 ZQ3CR[2];               //ZQ 3 Impedance Control Register 0-1
    volatile uint32 ZQ3SR[2];               //ZQ 3 Impedance Status Register 0-1

    DATX8_REG_T     DATX8[9];               //DATX8 Register
}DDRPHY_REG_T, *pDDRPHY_REG_T;

typedef union NOC_TIMING_Tag
{
    uint32 d32;
    struct
    {
        unsigned ActToAct : 6;
        unsigned RdToMiss : 6;
        unsigned WrToMiss : 6;
        unsigned BurstLen : 3;
        unsigned RdToWr : 5;
        unsigned WrToRd : 5;
        unsigned BwRatio : 1;
    } b;
}NOC_TIMING_T;

typedef union NOC_ACTIVATE_Tag
{
    uint32 d32;
    struct 
    {
        unsigned Rrd : 4;  //bit[0:3]
        unsigned Faw : 6;  //bit[4:9]
        unsigned Fawbank : 1; //bit 10
        unsigned reserved : 21;
    } b;
}NOC_ACTIVATE_T;

typedef volatile struct MSCH_REG_Tag
{
    volatile uint32 coreid;
    volatile uint32 revisionid;
    volatile uint32 ddrconf;
    volatile NOC_TIMING_T ddrtiming;
    volatile uint32 ddrmode;
    volatile uint32 readlatency;
    uint32 reserved1[(0x38-0x18)/4];
    volatile NOC_ACTIVATE_T activate;
    volatile uint32 devtodev;
}MSCH_REG, *pMSCH_REG;

#define CH_MAX                 (2)
#define DRAM_PHYS              (0)   //DRAM Channel a physical address start
#define pPMU_Reg               ((pPMU_FILE)RK_PMU_VIRT)
#define pCRU_Reg               ((pCRU_REG)RK_CRU_VIRT)
#define pGRF_Reg               ((pREG_FILE)RK_GRF_VIRT)
#define pDDR_REG(ch)           ((ch) ? ((pDDR_REG_T)(RK_DDR_VIRT + RK3288_DDR_PCTL_SIZE + RK3288_DDR_PUBL_SIZE)):((pDDR_REG_T)RK_DDR_VIRT))
#define pPHY_REG(ch)           ((ch) ? ((pDDRPHY_REG_T)(RK_DDR_VIRT + 2 * RK3288_DDR_PCTL_SIZE + RK3288_DDR_PUBL_SIZE)) : ((pDDRPHY_REG_T)(RK_DDR_VIRT + RK3288_DDR_PCTL_SIZE)))
#define pMSCH_REG(ch)          ((ch)? ((pMSCH_REG)(RK3288_SERVICE_BUS_VIRT+0x80)):((pMSCH_REG)(RK3288_SERVICE_BUS_VIRT)))
#define GET_DDR3_DS_ODT()      ((0x1<<28) | (0x2<<15) | (0x2<<10) | (0x19<<5) | 0x19)
#define GET_LPDDR2_DS_ODT()    ((0x1<<28) | (0x2<<15) | (0x2<<10) | (0x19<<5) | 0x19)
#define GET_LPDDR3_DS_ODT()    ((0x1<<28) | (0x2<<15) | (0x2<<10) | (0x19<<5) | 0x19)
#define DDR_GET_RANK_2_ROW15() (0)
#define DDR_GET_BANK_2_RANK()  (0)
#define DDR_HW_WAKEUP(ch,en)   do{pGRF_Reg->GRF_SOC_CON[0] = (1<<(16+5+ch)) | (en<<(5+ch));}while(0)
#define READ_GRF_REG()         (pGRF_Reg->GRF_SOC_CON[0])
#define GET_DPLL_LOCK_STATUS() (pGRF_Reg->GRF_SOC_STATUS[1] & (1<<5))
#define SET_DDR_PLL_SRC(src, div)   do{pCRU_Reg->CRU_CLKSEL_CON[26] = ((0x3|(0x1<<2))<<16)|(src<<2)| div;}while(0)
#define GET_DDR_PLL_SRC()           ((pCRU_Reg->CRU_CLKSEL_CON[26]&(1<<2)) ? GPLL : DPLL)
#define DDR_GPLL_CLK_GATE(en)       do{pCRU_Reg->CRU_CLKGATE_CON[0] = 0x02000000 | (en<<9);}while(0)
#define SET_DDRPHY_CLKGATE(ch,dis)  do{pCRU_Reg->CRU_CLKGATE_CON[4] = ((0x1<<(12+ch))<<16) | (dis<<(12+ch));}while(0)
#define READ_DDR_STRIDE()           (readl_relaxed(RK_SGRF_VIRT+0x8) &0x1F)

#define READ_CH_CNT()         (1+((pPMU_Reg->PMU_PMU_SYS_REG[2]>>12)&0x1))
#define READ_CH_INFO()        ((pPMU_Reg->PMU_PMU_SYS_REG[2]>>28)&0x3) 
#define READ_CH_ROW_INFO(ch)  ((pPMU_Reg->PMU_PMU_SYS_REG[2]>>(30+(ch)))&0x1)    //row_3_4:0=normal, 1=6Gb or 12Gb

#define SET_PLL_MODE(pll, mode) do{pCRU_Reg->CRU_MODE_CON = ((mode<<((pll)*4))|(0x3<<(16+(pll)*4)));}while(0)
#define SET_PLL_PD(pll, pd)     do{pCRU_Reg->CRU_PLL_CON[pll][3] = ((0x1<<1)<<16) | (pd<<1);}while(0)

#define READ_DRAMTYPE_INFO()    ((pPMU_Reg->PMU_PMU_SYS_REG[2]>>13)&0x7)
#define READ_CS_INFO(ch)        ((((pPMU_Reg->PMU_PMU_SYS_REG[2])>>(11+(ch)*16))&0x1)+1)
#define READ_BW_INFO(ch)        (2>>(((pPMU_Reg->PMU_PMU_SYS_REG[2])>>(2+(ch)*16))&0x3))
#define READ_COL_INFO(ch)       (9+(((pPMU_Reg->PMU_PMU_SYS_REG[2])>>(9+(ch)*16))&0x3))
#define READ_BK_INFO(ch)        (3-(((pPMU_Reg->PMU_PMU_SYS_REG[2])>>(8+(ch)*16))&0x1))
#define READ_ROW_INFO(ch,cs)    (13+(((pPMU_Reg->PMU_PMU_SYS_REG[2])>>(6-(2*cs)+(ch)*16))&0x3))     
#define READ_DIE_BW_INFO(ch)    (2>>((pPMU_Reg->PMU_PMU_SYS_REG[2]>>((ch)*16))&0x3))

static const uint16_t  ddr_cfg_2_rbc[] =
{
    /****************************/
    // [8:7]  bank(n:n bit bank)
    // [6:4]  row(12+n)
    // [3:2]  bank(n:n bit bank)
    // [1:0]  col(9+n)
    /****************************/
    //all config have (13col,3bank,16row,1cs)
    //bank,  row,   bank, col          col bank row(32bit)
    ((3<<7)|(3<<4)|(0<<2)|2),  // 0     11   8   15
    ((0<<7)|(1<<4)|(3<<2)|1),  // 1     10   8   13
    ((0<<7)|(2<<4)|(3<<2)|1),  // 2     10   8   14
    ((0<<7)|(3<<4)|(3<<2)|1),  // 3     10   8   15
    ((0<<7)|(4<<4)|(3<<2)|1),  // 4     10   8   16
    ((0<<7)|(1<<4)|(3<<2)|2),  // 5     11   8   13  // 32bit not use
    ((0<<7)|(2<<4)|(3<<2)|2),  // 6     11   8   14
    ((0<<7)|(3<<4)|(3<<2)|2),  // 7     11   8   15
    ((0<<7)|(1<<4)|(3<<2)|0),  // 8     9    8   13
    ((0<<7)|(2<<4)|(3<<2)|0),  // 9     9    8   14
    ((0<<7)|(3<<4)|(3<<2)|0),  // 10    9    8   15
    ((0<<7)|(2<<4)|(2<<2)|0),  // 11    9    4   14
    ((0<<7)|(1<<4)|(2<<2)|1),  // 12    10   4   13
    ((0<<7)|(0<<4)|(2<<2)|2),  // 13    11   4   12
    ((3<<7)|(4<<4)|(0<<2)|1),  // 14    10   8   16 / 10, 4,15 / 10, 8, 15
    ((0<<7)|(4<<4)|(3<<2)|2),  // 15    11   8   16
};

/***********************************
 * LPDDR2 define
 ***********************************/
//MR0 (Device Information)
#define  LPDDR2_DAI    (0x1)        // 0:DAI complete, 1:DAI still in progress
#define  LPDDR2_DI     (0x1<<1)     // 0:S2 or S4 SDRAM, 1:NVM
#define  LPDDR2_DNVI   (0x1<<2)     // 0:DNV not supported, 1:DNV supported
#define  LPDDR2_RZQI   (0x3<<3)     // 00:RZQ self test not supported, 01:ZQ-pin may connect to VDDCA or float
                                    // 10:ZQ-pin may short to GND.     11:ZQ-pin self test completed, no error condition detected.

//MR1 (Device Feature)
#define LPDDR2_BL4     (0x2)
#define LPDDR2_BL8     (0x3)
#define LPDDR2_BL16    (0x4)
#define LPDDR2_nWR(n)  (((n)-2)<<5)

//MR2 (Device Feature 2)
#define LPDDR2_RL3_WL1  (0x1)
#define LPDDR2_RL4_WL2  (0x2)
#define LPDDR2_RL5_WL2  (0x3)
#define LPDDR2_RL6_WL3  (0x4)
#define LPDDR2_RL7_WL4  (0x5)
#define LPDDR2_RL8_WL4  (0x6)

//MR3 (IO Configuration 1)
#define LPDDR2_DS_34    (0x1)
#define LPDDR2_DS_40    (0x2)
#define LPDDR2_DS_48    (0x3)
#define LPDDR2_DS_60    (0x4)
#define LPDDR2_DS_80    (0x6)
#define LPDDR2_DS_120   (0x7)   //optional

//MR4 (Device Temperature)
#define LPDDR2_tREF_MASK (0x7)
#define LPDDR2_4_tREF    (0x1)
#define LPDDR2_2_tREF    (0x2)
#define LPDDR2_1_tREF    (0x3)
#define LPDDR2_025_tREF  (0x5)
#define LPDDR2_025_tREF_DERATE    (0x6)

#define LPDDR2_TUF       (0x1<<7)

//MR8 (Basic configuration 4)
#define LPDDR2_S4        (0x0)
#define LPDDR2_S2        (0x1)
#define LPDDR2_N         (0x2)
#define LPDDR2_Density(mr8)  (8<<(((mr8)>>2)&0xf))   // Unit:MB
#define LPDDR2_IO_Width(mr8) (32>>(((mr8)>>6)&0x3))

//MR10 (Calibration)
#define LPDDR2_ZQINIT   (0xFF)
#define LPDDR2_ZQCL     (0xAB)
#define LPDDR2_ZQCS     (0x56)
#define LPDDR2_ZQRESET  (0xC3)

//MR16 (PASR Bank Mask)
// S2 SDRAM Only
#define LPDDR2_PASR_Full (0x0)
#define LPDDR2_PASR_1_2  (0x1)
#define LPDDR2_PASR_1_4  (0x2)
#define LPDDR2_PASR_1_8  (0x3)

//MR17 (PASR Segment Mask) 1Gb-8Gb S4 SDRAM only

//MR32 (DQ Calibration Pattern A)

//MR40 (DQ Calibration Pattern B)

/***********************************
 * LPDDR3 define
 ***********************************/
//MR0 (Device Information)
#define  LPDDR3_DAI    (0x1)        // 0:DAI complete, 1:DAI still in progress
#define  LPDDR3_RZQI   (0x3<<3)     // 00:RZQ self test not supported, 01:ZQ-pin may connect to VDDCA or float
                                    // 10:ZQ-pin may short to GND.     11:ZQ-pin self test completed, no error condition detected.
#define  LPDDR3_WL_SUPOT (1<<6)     // 0:DRAM does not support WL(Set B), 1:DRAM support WL(Set B)
#define  LPDDR3_RL3_SUPOT (1<<7)    // 0:DRAM does not support RL=3,nWR=3,WL=1; 1:DRAM supports RL=3,nWR=3,WL=1 for frequencies <=166

//MR1 (Device Feature)
#define LPDDR3_BL8     (0x3)
#define LPDDR3_nWR(n)  ((n)<<5)

//MR2 (Device Feature 2)
//WL Set A,default
#define LPDDR3_RL3_WL1   (0x1)       // <=166MHz,optional
#define LPDDR3_RL6_WL3   (0x4)       // <=400MHz
#define LPDDR3_RL8_WL4   (0x6)       // <=533MHz
#define LPDDR3_RL9_WL5   (0x7)       // <=600MHz
#define LPDDR3_RL10_WL6  (0x8)       // <=667MHz,default
#define LPDDR3_RL11_WL6  (0x9)       // <=733MHz
#define LPDDR3_RL12_WL6  (0xa)       // <=800MHz
#define LPDDR3_RL14_WL8  (0xc)       // <=933MHz
#define LPDDR3_RL16_WL8  (0xe)       // <=1066MHz
//WL Set B, optional
//#define LPDDR3_RL3_WL1   (0x1)       // <=166MHz,optional
//#define LPDDR3_RL6_WL3   (0x4)       // <=400MHz
//#define LPDDR3_RL8_WL4   (0x6)       // <=533MHz
//#define LPDDR3_RL9_WL5   (0x7)       // <=600MHz
#define LPDDR3_RL10_WL8  (0x8)       // <=667MHz,default
#define LPDDR3_RL11_WL9  (0x9)       // <=733MHz
#define LPDDR3_RL12_WL9  (0xa)       // <=800MHz
#define LPDDR3_RL14_WL11 (0xc)       // <=933MHz
#define LPDDR3_RL16_WL13 (0xe)       // <=1066MHz

#define LPDDR3_nWRE      (1<<4)      // 1:enable nWR programming > 9(defualt)
#define LPDDR3_WL_S      (1<<6)      // 1:Select WL Set B
#define LPDDR3_WR_LEVEL  (1<<7)      // 1:enable

//MR3 (IO Configuration 1)
#define LPDDR3_DS_34    (0x1)
#define LPDDR3_DS_40    (0x2)
#define LPDDR3_DS_48    (0x3)
#define LPDDR3_DS_60    (0x4)        //reserved
#define LPDDR3_DS_80    (0x6)        //reserved
#define LPDDR3_DS_34D_40U   (0x9)  
#define LPDDR3_DS_40D_48U   (0xa)
#define LPDDR3_DS_34D_48U   (0xb)

//MR4 (Device Temperature)
#define LPDDR3_tREF_MASK (0x7)
#define LPDDR3_LT_EXED   (0x0)       // SDRAM Low temperature operating limit exceeded
#define LPDDR3_4_tREF    (0x1)
#define LPDDR3_2_tREF    (0x2)
#define LPDDR3_1_tREF    (0x3)
#define LPDDR3_05_tREF   (0x4)
#define LPDDR3_025_tREF  (0x5)
#define LPDDR3_025_tREF_DERATE    (0x6)
#define LPDDR3_HT_EXED   (0x7)       // SDRAM High temperature operating limit exceeded

#define LPDDR3_TUF       (0x1<<7)    // 1:value has changed since last read of MR4

//MR8 (Basic configuration 4)
#define LPDDR3_S8        (0x3)
#define LPDDR3_Density(mr8)  (8<<((mr8>>2)&0xf))   // Unit:MB
#define LPDDR3_IO_Width(mr8) (32>>((mr8>>6)&0x3))

//MR10 (Calibration)
#define LPDDR3_ZQINIT   (0xFF)
#define LPDDR3_ZQCL     (0xAB)
#define LPDDR3_ZQCS     (0x56)
#define LPDDR3_ZQRESET  (0xC3)

//MR11 (ODT Control)
#define LPDDR3_ODT_60   (1)           //optional for 1333 and 1600
#define LPDDR3_ODT_120  (2)
#define LPDDR3_ODT_240  (3)
#define LPDDR3_ODT_DIS  (0)

//MR16 (PASR Bank Mask)

//MR17 (PASR Segment Mask) 1Gb-8Gb S4 SDRAM only

//MR32 (DQ Calibration Pattern A)

//MR40 (DQ Calibration Pattern B)

/***********************************
 * DDR3 define
 ***********************************/
//mr0 for ddr3
#define DDR3_BL8          (0)
#define DDR3_BC4_8        (1)
#define DDR3_BC4          (2)
#define DDR3_CL(n)        (((((n)-4)&0x7)<<4)|((((n)-4)&0x8)>>1))
#define DDR3_WR(n)        (((n)&0x7)<<9)
#define DDR3_DLL_RESET    (1<<8)
#define DDR3_DLL_DeRESET  (0<<8)

//mr1 for ddr3
#define DDR3_DLL_ENABLE    (0)
#define DDR3_DLL_DISABLE   (1)
#define DDR3_MR1_AL(n)  (((n)&0x3)<<3)

#define DDR3_DS_40            (0)
#define DDR3_DS_34            (1<<1)
#define DDR3_Rtt_Nom_DIS      (0)
#define DDR3_Rtt_Nom_60       (1<<2)
#define DDR3_Rtt_Nom_120      (1<<6)
#define DDR3_Rtt_Nom_40       ((1<<2)|(1<<6))

    //mr2 for ddr3
#define DDR3_MR2_CWL(n) ((((n)-5)&0x7)<<3)
#define DDR3_Rtt_WR_DIS       (0)
#define DDR3_Rtt_WR_60        (1<<9)
#define DDR3_Rtt_WR_120       (2<<9)

/***********************************
 * DDR2 define
 ***********************************/
//MR;                     //Mode Register
#define DDR2_BL4           (2)
#define DDR2_BL8           (3)
#define DDR2_CL(n)         (((n)&0x7)<<4)
#define DDR2_WR(n)        ((((n)-1)&0x7)<<9)
#define DDR2_DLL_RESET    (1<<8)
#define DDR2_DLL_DeRESET  (0<<8)

//EMR;                    //Extended Mode Register
#define DDR2_DLL_ENABLE    (0)
#define DDR2_DLL_DISABLE   (1)

#define DDR2_STR_FULL     (0)
#define DDR2_STR_REDUCE   (1<<1)
#define DDR2_AL(n)        (((n)&0x7)<<3)
#define DDR2_Rtt_Nom_DIS      (0)
#define DDR2_Rtt_Nom_150      (0x40)
#define DDR2_Rtt_Nom_75       (0x4)
#define DDR2_Rtt_Nom_50       (0x44)

/***********************************
 * LPDDR define
 ***********************************/
#define mDDR_BL2           (1)
#define mDDR_BL4           (2)
#define mDDR_BL8           (3)
#define mDDR_CL(n)         (((n)&0x7)<<4)

#define mDDR_DS_Full       (0)
#define mDDR_DS_1_2        (1<<5)
#define mDDR_DS_1_4        (2<<5)
#define mDDR_DS_1_8        (3<<5)
#define mDDR_DS_3_4        (4<<5)

static const uint8_t ddr3_cl_cwl[22][7]={
/*speed   0~330         331~400       401~533        534~666       667~800        801~933      934~1066
 * tCK    >3            2.5~3         1.875~2.5      1.5~1.875     1.25~1.5       1.07~1.25    0.938~1.07
 *        cl<<4, cwl    cl<<4, cwl    cl<<4, cwl              */
         {((5<<4)|5),   ((5<<4)|5),   0         ,    0,            0,             0,            0}, //DDR3_800D (5-5-5)
         {((5<<4)|5),   ((6<<4)|5),   0         ,    0,            0,             0,            0}, //DDR3_800E (6-6-6)

         {((5<<4)|5),   ((5<<4)|5),   ((6<<4)|6),    0,            0,             0,            0}, //DDR3_1066E (6-6-6)
         {((5<<4)|5),   ((6<<4)|5),   ((7<<4)|6),    0,            0,             0,            0}, //DDR3_1066F (7-7-7)
         {((5<<4)|5),   ((6<<4)|5),   ((8<<4)|6),    0,            0,             0,            0}, //DDR3_1066G (8-8-8)

         {((5<<4)|5),   ((5<<4)|5),   ((6<<4)|6),    ((7<<4)|7),   0,             0,            0}, //DDR3_1333F (7-7-7)
         {((5<<4)|5),   ((5<<4)|5),   ((7<<4)|6),    ((8<<4)|7),   0,             0,            0}, //DDR3_1333G (8-8-8)
         {((5<<4)|5),   ((6<<4)|5),   ((8<<4)|6),    ((9<<4)|7),   0,             0,            0}, //DDR3_1333H (9-9-9)
         {((5<<4)|5),   ((6<<4)|5),   ((8<<4)|6),    ((10<<4)|7),  0,             0,            0}, //DDR3_1333J (10-10-10)

         {((5<<4)|5),   ((5<<4)|5),   ((6<<4)|6),    ((7<<4)|7),   ((8<<4)|8),    0,            0}, //DDR3_1600G (8-8-8)
         {((5<<4)|5),   ((5<<4)|5),   ((6<<4)|6),    ((8<<4)|7),   ((9<<4)|8),    0,            0}, //DDR3_1600H (9-9-9)
         {((5<<4)|5),   ((5<<4)|5),   ((7<<4)|6),    ((9<<4)|7),   ((10<<4)|8),   0,            0}, //DDR3_1600J (10-10-10)
         {((5<<4)|5),   ((6<<4)|5),   ((8<<4)|6),    ((10<<4)|7),  ((11<<4)|8),   0,            0}, //DDR3_1600K (11-11-11)

         {((5<<4)|5),   ((5<<4)|5),   ((6<<4)|6),    ((8<<4)|7),   ((9<<4)|8),    ((11<<4)|9),  0}, //DDR3_1866J (10-10-10)
         {((5<<4)|5),   ((5<<4)|5),   ((7<<4)|6),    ((8<<4)|7),   ((10<<4)|8),   ((11<<4)|9),  0}, //DDR3_1866K (11-11-11)
         {((6<<4)|5),   ((6<<4)|5),   ((7<<4)|6),    ((9<<4)|7),   ((11<<4)|8),   ((12<<4)|9),  0}, //DDR3_1866L (12-12-12)
         {((6<<4)|5),   ((6<<4)|5),   ((8<<4)|6),    ((10<<4)|7),  ((11<<4)|8),   ((13<<4)|9),  0}, //DDR3_1866M (13-13-13)

         {((5<<4)|5),   ((5<<4)|5),   ((6<<4)|6),    ((7<<4)|7),   ((9<<4)|8),    ((10<<4)|9),  ((11<<4)|10)}, //DDR3_2133K (11-11-11)
         {((5<<4)|5),   ((5<<4)|5),   ((6<<4)|6),    ((8<<4)|7),   ((9<<4)|8),    ((11<<4)|9),  ((12<<4)|10)}, //DDR3_2133L (12-12-12)
         {((5<<4)|5),   ((5<<4)|5),   ((7<<4)|6),    ((9<<4)|7),   ((10<<4)|8),   ((12<<4)|9),  ((13<<4)|10)}, //DDR3_2133M (13-13-13)
         {((6<<4)|5),   ((6<<4)|5),   ((7<<4)|6),    ((9<<4)|7),   ((11<<4)|8),   ((13<<4)|9),  ((14<<4)|10)},  //DDR3_2133N (14-14-14)

         {((6<<4)|5),   ((6<<4)|5),   ((8<<4)|6),    ((10<<4)|7),  ((11<<4)|8),   ((13<<4)|9),  ((14<<4)|10)} //DDR3_DEFAULT
};

static const uint16_t ddr3_tRC_tFAW[22]={
/**    tRC    tFAW   */
    ((50<<8)|50), //DDR3_800D (5-5-5)
    ((53<<8)|50), //DDR3_800E (6-6-6)

    ((49<<8)|50), //DDR3_1066E (6-6-6)
    ((51<<8)|50), //DDR3_1066F (7-7-7)
    ((53<<8)|50), //DDR3_1066G (8-8-8)

    ((47<<8)|45), //DDR3_1333F (7-7-7)
    ((48<<8)|45), //DDR3_1333G (8-8-8)
    ((50<<8)|45), //DDR3_1333H (9-9-9)
    ((51<<8)|45), //DDR3_1333J (10-10-10)

    ((45<<8)|40), //DDR3_1600G (8-8-8)
    ((47<<8)|40), //DDR3_1600H (9-9-9)
    ((48<<8)|40), //DDR3_1600J (10-10-10)
    ((49<<8)|40), //DDR3_1600K (11-11-11)

    ((45<<8)|35), //DDR3_1866J (10-10-10)
    ((46<<8)|35), //DDR3_1866K (11-11-11)
    ((47<<8)|35), //DDR3_1866L (12-12-12)
    ((48<<8)|35), //DDR3_1866M (13-13-13)

    ((44<<8)|35), //DDR3_2133K (11-11-11)
    ((45<<8)|35), //DDR3_2133L (12-12-12)
    ((46<<8)|35), //DDR3_2133M (13-13-13)
    ((47<<8)|35), //DDR3_2133N (14-14-14)

    ((53<<8)|50)  //DDR3_DEFAULT
};

typedef enum DRAM_TYPE_Tag
{
    LPDDR = 0,
    DDR,
    DDR2,
    DDR3,
    LPDDR2,
    LPDDR3,

    DRAM_MAX
}DRAM_TYPE;

typedef struct PCTRL_TIMING_Tag
{
    uint32 ddrFreq;
    //Memory Timing Registers
    uint32 togcnt1u;               //Toggle Counter 1U Register
    uint32 tinit;                  //t_init Timing Register
    uint32 trsth;                  //Reset High Time Register
    uint32 togcnt100n;             //Toggle Counter 100N Register
    uint32 trefi;                  //t_refi Timing Register
    uint32 tmrd;                   //t_mrd Timing Register
    uint32 trfc;                   //t_rfc Timing Register
    uint32 trp;                    //t_rp Timing Register
    uint32 trtw;                   //t_rtw Timing Register
    uint32 tal;                    //AL Latency Register
    uint32 tcl;                    //CL Timing Register
    uint32 tcwl;                   //CWL Register
    uint32 tras;                   //t_ras Timing Register
    uint32 trc;                    //t_rc Timing Register
    uint32 trcd;                   //t_rcd Timing Register
    uint32 trrd;                   //t_rrd Timing Register
    uint32 trtp;                   //t_rtp Timing Register
    uint32 twr;                    //t_wr Timing Register
    uint32 twtr;                   //t_wtr Timing Register
    uint32 texsr;                  //t_exsr Timing Register
    uint32 txp;                    //t_xp Timing Register
    uint32 txpdll;                 //t_xpdll Timing Register
    uint32 tzqcs;                  //t_zqcs Timing Register
    uint32 tzqcsi;                 //t_zqcsi Timing Register
    uint32 tdqs;                   //t_dqs Timing Register
    uint32 tcksre;                 //t_cksre Timing Register
    uint32 tcksrx;                 //t_cksrx Timing Register
    uint32 tcke;                   //t_cke Timing Register
    uint32 tmod;                   //t_mod Timing Register
    uint32 trstl;                  //Reset Low Timing Register
    uint32 tzqcl;                  //t_zqcl Timing Register
    uint32 tmrr;                   //t_mrr Timing Register
    uint32 tckesr;                 //t_ckesr Timing Register
    uint32 tdpd;                   //t_dpd Timing Register
}PCTL_TIMING_T;

typedef union DTPR_0_Tag
{
    uint32 d32;
    struct
    {
        unsigned tMRD : 2;
        unsigned tRTP : 3;
        unsigned tWTR : 3;
        unsigned tRP : 4;
        unsigned tRCD : 4;
        unsigned tRAS : 5;
        unsigned tRRD : 4;
        unsigned tRC : 6;
        unsigned tCCD : 1;
    } b;
}DTPR_0_T;

typedef union DTPR_1_Tag
{
    uint32 d32;
    struct
    {
        unsigned tAOND : 2;
        unsigned tRTW : 1;
        unsigned tFAW : 6;
        unsigned tMOD : 2;
        unsigned tRTODT : 1;
        unsigned reserved12_15 : 4;
        unsigned tRFC : 8;
        unsigned tDQSCK : 3;
        unsigned tDQSCKmax : 3;
        unsigned reserved30_31 : 2;
    } b;
}DTPR_1_T;

typedef union DTPR_2_Tag
{
    uint32 d32;
    struct
    {
        unsigned tXS : 10;
        unsigned tXP : 5;
        unsigned tCKE : 4;
        unsigned tDLLK : 10;
        unsigned reserved29_31 : 3;
    } b;
}DTPR_2_T;

typedef struct PHY_TIMING_Tag
{
    DTPR_0_T  dtpr0;
    DTPR_1_T  dtpr1;
    DTPR_2_T  dtpr2;
    uint32    mr[4];   //LPDDR2 no MR0, mr[2] is mDDR MR1
    uint32    mr11;    //for LPDDR3 only
}PHY_TIMING_T;

typedef struct PCTL_REG_Tag
{
    uint32 SCFG;
    uint32 CMDTSTATEN;
    uint32 MCFG1;
    uint32 MCFG;
    PCTL_TIMING_T pctl_timing;
    //DFI Control Registers
    uint32 DFITCTRLDELAY;
    uint32 DFIODTCFG;
    uint32 DFIODTCFG1;
    uint32 DFIODTRANKMAP;
    //DFI Write Data Registers
    uint32 DFITPHYWRDATA;
    uint32 DFITPHYWRLAT;
    //DFI Read Data Registers
    uint32 DFITRDDATAEN;
    uint32 DFITPHYRDLAT;
    //DFI Update Registers
    uint32 DFITPHYUPDTYPE0;
    uint32 DFITPHYUPDTYPE1;
    uint32 DFITPHYUPDTYPE2;
    uint32 DFITPHYUPDTYPE3;
    uint32 DFITCTRLUPDMIN;
    uint32 DFITCTRLUPDMAX;
    uint32 DFITCTRLUPDDLY;
    uint32 DFIUPDCFG;
    uint32 DFITREFMSKI;
    uint32 DFITCTRLUPDI;
    //DFI Status Registers
    uint32 DFISTCFG0;
    uint32 DFISTCFG1;
    uint32 DFITDRAMCLKEN;
    uint32 DFITDRAMCLKDIS;
    uint32 DFISTCFG2;
    //DFI Low Power Register
    uint32 DFILPCFG0;
}PCTL_REG_T;

typedef struct PUBL_DQS_REG_Tag
{
    uint32 DX0GCR;
    uint32 DX0DLLCR;
    uint32 DX0DQTR;
    uint32 DX0DQSTR;

    uint32 DX1GCR;
    uint32 DX1DLLCR;
    uint32 DX1DQTR;
    uint32 DX1DQSTR;

    uint32 DX2GCR;
    uint32 DX2DLLCR;
    uint32 DX2DQTR;
    uint32 DX2DQSTR;

    uint32 DX3GCR;
    uint32 DX3DLLCR;
    uint32 DX3DQTR;
    uint32 DX3DQSTR;
}PUBL_DQS_REG;

typedef struct PUBL_REG_Tag
{
    uint32 PIR;
    uint32 PGCR;
    uint32 DLLGCR;
    uint32 ACDLLCR;
    uint32 PTR[3];
    uint32 ACIOCR;
    uint32 DXCCR;
    uint32 DSGCR;
    uint32 DCR;
    PHY_TIMING_T phy_timing;
    uint32 ODTCR;
    uint32 DTAR;
    uint32 ZQ0CR0;
    uint32 ZQ1CR0;
}PUBL_REG_T;

typedef struct SET_REG_Tag
{
    uint32 addr;
    uint32 val;
}SET_REG_T;

typedef struct BACKUP_REG_Tag
{
    uint32 tag;
    /* any addr = 0xFFFFFFFF, indicate invalid */
    uint32 pctlAddr[CH_MAX];
    PCTL_REG_T pctl;
    uint32 publAddr[CH_MAX];
    PUBL_REG_T publ;
    PUBL_DQS_REG dqs[CH_MAX];
    uint32 nocAddr[CH_MAX];
    MSCH_REG   noc[CH_MAX];

    uint32 pllpdAddr;
    uint32 pllpdMask;
    uint32 pllpdVal;

    uint32 dpllmodeAddr;
    uint32 dpllSlowMode;
    uint32 dpllNormalMode;
    uint32 dpllResetAddr;
    uint32 dpllReset;
    uint32 dpllDeReset;
    uint32 dpllConAddr;
    uint32 dpllCon[4];
    uint32 dpllLockAddr;
    uint32 dpllLockMask;
    uint32 dpllLockVal;

    uint32 ddrPllSrcDivAddr;
    uint32 ddrPllSrcDiv;

    uint32 retenDisAddr;
    uint32 retenDisVal;
    uint32 retenStAddr;
    uint32 retenStMask;
    uint32 retenStVal;

    /* ddr relative grf register */
    uint32 grfRegCnt;     //if no grf, set 0
    SET_REG_T grf[3];        //SET_REG_T grf[grfRegCnt];

    /* other ddr relative register */
    //uint32 otherRegCnt; // if = 0xFFFFFFFF, indicate invalid
    //SET_REG_T other[grfRegCnt];
    uint32 endTag;         //must = 0xFFFFFFFF
}BACKUP_REG_T;

typedef struct CHANNEL_INFO_Tag
{
    //inited data
    uint32        chNum;  //channel number,0:channel a; 1:channel b;
    pDDR_REG_T    pDDR_Reg;
    pDDRPHY_REG_T pPHY_Reg;
    pMSCH_REG     pMSCH_Reg;
    //need init data
    DRAM_TYPE     mem_type; // =DRAM_MAX, channel invalid
    uint32        ddr_speed_bin;    // used for ddr3 only
    uint32        ddr_capability_per_die;  // one chip cs capability
}CH_INFO,*pCH_INFO;

struct ddr_freq_t {
    unsigned long screen_ft_us;
    unsigned long long t0;
    unsigned long long t1;
    unsigned long t2;
};

typedef struct STRIDE_INFO_Tag
{
    uint32  size;
    uint32  halfCap;
}STRIDE_INFO;

static const STRIDE_INFO   gStrideInfo[]={
    {0x10000000,0x10000000},  // 256
    {0x20000000,0x20000000},  // 512
    {0x40000000,0x40000000},  // 1G
    {0x80000000,0x80000000},  // 2G
    
    {128,0x20000000},
    {256,0x20000000},
    {512,0x20000000},
    {4096,0x20000000},
    
    {128,0x40000000},
    {256,0x40000000},
    {512,0x40000000},
    {4096,0x40000000},

    {128,0x80000000},
    {256,0x80000000},
    {512,0x80000000},
    {4096,0x80000000},

    {128,0x60000000},
    {256,0x60000000},
    {512,0x60000000},
    {4096,0x60000000},

    {0,0x20000000},
    {0,0x40000000},
    {0,0x80000000},
    {0,0x80000000},  // 4GB

    {0,0},  //reserved
    {0,0},  //reserved
    
    {0,0},
    {128,0},
};

CH_INFO DEFINE_PIE_DATA(ddr_ch[2]);
static pCH_INFO p_ddr_ch[2];    //only used in kern, not pie
BACKUP_REG_T DEFINE_PIE_DATA(ddr_reg);
static BACKUP_REG_T *p_ddr_reg;
static __attribute__((aligned(4096))) uint32 ddr_data_training_buf[32+8192/4];  //data in two channel even use max stride
uint32 DEFINE_PIE_DATA(ddr_freq);
uint32 DEFINE_PIE_DATA(ddr_sr_idle);

/***********************************
 * ARCH Relative Data and Function
 ***********************************/
static __sramdata uint32 clkr;
static __sramdata uint32 clkf;
static __sramdata uint32 clkod;
uint32 DEFINE_PIE_DATA(ddr_select_gpll_div); // 0-Disable, 1-1:1, 2-2:1, 4-4:1
#if defined(ENABLE_DDR_CLCOK_GPLL_PATH)
static uint32 *p_ddr_select_gpll_div;
#endif

static void __sramfunc ddr_delayus(uint32 us);

static noinline uint32 ddr_get_pll_freq(PLL_ID pll_id)   //APLL-1;CPLL-2;DPLL-3;GPLL-4
{
    uint32 ret = 0;

     // freq = (Fin/NR)*NF/OD
    if(((pCRU_Reg->CRU_MODE_CON>>(pll_id*4))&3) == 1)             // DPLL Normal mode
        ret= 24 *((pCRU_Reg->CRU_PLL_CON[pll_id][1]&0x1fff)+1)    // NF = 2*(CLKF+1)
                /((((pCRU_Reg->CRU_PLL_CON[pll_id][0]>>8)&0x3f)+1)           // NR = CLKR+1
                *((pCRU_Reg->CRU_PLL_CON[pll_id][0]&0xF)+1));             // OD = 2^CLKOD
    else
        ret = 24;

    return ret;
}

/*****************************************
NR   NO     NF               Fout                       freq Step     finally use
1    8      12.5 - 62.5      37.5MHz  - 187.5MHz        3MHz          50MHz   <= 150MHz
1    6      12.5 - 62.5      50MHz    - 250MHz          4MHz          150MHz  <= 200MHz
1    4      12.5 - 62.5      75MHz    - 375MHz          6MHz          200MHz  <= 300MHz
1    2      12.5 - 62.5      150MHz   - 750MHz          12MHz         300MHz  <= 600MHz
1    1      12.5 - 62.5      300MHz   - 1500MHz         24MHz         600MHz  <= 1200MHz
******************************************/
static uint32 __sramfunc ddr_set_pll_rk3188_plus(uint32 nMHz, uint32 set)
{
    uint32 ret = 0;
    int delay;

    if(nMHz == 24)
    {
        ret = 24;
        goto out;
    }

    if(set==0)
    {
        if(nMHz <= 150)
        {
            clkod = 8;
        }
        else if(nMHz <= 200)
        {
            clkod = 6;
        }
        else if(nMHz <= 300)
        {
            clkod = 4;
        }
        else if(nMHz <= 600)
        {
            clkod = 2;
        }
        else
        {
            clkod = 1;
        }
        clkr = 1;
        clkf=(nMHz*clkr*clkod)/24;
        ret = (24*clkf)/(clkr*clkod);
    }
    else if(set == 1)
    {
        SET_DDR_PLL_SRC(1, (DATA(ddr_select_gpll_div)-1));  //clk_ddr_src = GPLL
        
        SET_PLL_MODE(DPLL,0);            //PLL slow-mode
        dsb();

        pCRU_Reg->CRU_PLL_CON[DPLL][3] = PLL_RESET;
        ddr_delayus(1);
        pCRU_Reg->CRU_PLL_CON[DPLL][0] = NR(clkr) | NO(clkod);
        pCRU_Reg->CRU_PLL_CON[DPLL][1] = NF(clkf);
        pCRU_Reg->CRU_PLL_CON[DPLL][2] = NB(clkf>>1);
        ddr_delayus(1);
        pCRU_Reg->CRU_PLL_CON[DPLL][3] = PLL_DE_RESET;
        dsb();
    }
    else
    {
        delay = 1000;
        while (delay > 0)
        {
            if (GET_DPLL_LOCK_STATUS())
                break;
            ddr_delayus(1);
            delay--;
        }

        SET_DDR_PLL_SRC(0, 0);  //clk_ddr_src = DDR PLL,clk_ddr_src:clk_ddrphy = 1:1
        SET_PLL_MODE(DPLL,1);            //PLL normal
        dsb();
    }
    
out:
    return ret;
}

uint32 PIE_FUNC(ddr_set_pll)(uint32 nMHz, uint32 set)
{
    return ddr_set_pll_rk3188_plus(nMHz,set);
}
EXPORT_PIE_SYMBOL(FUNC(ddr_set_pll));
static uint32 (*p_ddr_set_pll)(uint32 nMHz, uint32 set);

static void __sramfunc idle_port(void)
{
    register int i,j;
    uint32 clk_gate[19];

    pPMU_Reg->PMU_IDLE_REQ |= idle_req_core_cfg;
    dsb();
    while( (pPMU_Reg->PMU_IDLE_ST & idle_core) == 0 );

    //save clock gate status
    for(i=0;i<19;i++)
        clk_gate[i]=pCRU_Reg->CRU_CLKGATE_CON[i];

    //enable all clock gate for request idle
    for(i=0;i<19;i++)
        pCRU_Reg->CRU_CLKGATE_CON[i]=0xffff0000;

    i = pPMU_Reg->PMU_PWRDN_ST;
    j = idle_req_dma_cfg;
    
    if ( (i & pd_peri_pwr_st) == 0 )
    {
        j |= idle_req_peri_cfg;
    }

    if ( (i & pd_video_pwr_st) == 0 )
    {
        j |= idle_req_video_cfg;
    }

    if ( (i & pd_gpu_pwr_st) == 0 )
    {
        j |= idle_req_gpu_cfg;
    }

    if ( (i & pd_hevc_pwr_st) == 0 )
    {
        j |= idle_req_hevc_cfg;
    }

    if ( (i & pd_vio_pwr_st) == 0 )
    {
        j |= idle_req_vio_cfg;
    }

    pPMU_Reg->PMU_IDLE_REQ |= j;
    dsb();
    while( (pPMU_Reg->PMU_IDLE_ST & j) != j );

    //resume clock gate status
    for(i=0;i<19;i++)
        pCRU_Reg->CRU_CLKGATE_CON[i]=  (clk_gate[i] | 0xffff0000);
}

static void inline deidle_port(void)
{
    register int i,j;
    uint32 clk_gate[19];

    //save clock gate status
    for(i=0;i<19;i++)
        clk_gate[i]=pCRU_Reg->CRU_CLKGATE_CON[i];

    //enable all clock gate for request idle
    for(i=0;i<19;i++)
        pCRU_Reg->CRU_CLKGATE_CON[i]=0xffff0000;

    i = pPMU_Reg->PMU_PWRDN_ST;
    j = idle_req_dma_cfg;
    
    if ( (i & pd_peri_pwr_st) == 0 )
    {
        j |= idle_req_peri_cfg;
    }

    if ( (i & pd_video_pwr_st) == 0 )
    {
        j |= idle_req_video_cfg;
    }

    if ( (i & pd_gpu_pwr_st) == 0 )
    {
        j |= idle_req_gpu_cfg;
    }

    if ( (i & pd_hevc_pwr_st) == 0 )
    {
        j |= idle_req_hevc_cfg;
    }

    if ( (i & pd_vio_pwr_st) == 0 )
    {
        j |= idle_req_vio_cfg;
    }

    pPMU_Reg->PMU_IDLE_REQ &= ~j;
    dsb();
    while( (pPMU_Reg->PMU_IDLE_ST & j) != 0 );

    pPMU_Reg->PMU_IDLE_REQ &= ~idle_req_core_cfg;
    dsb();
    while( (pPMU_Reg->PMU_IDLE_ST & idle_core) != 0 );

    //resume clock gate status
    for(i=0;i<19;i++)
        pCRU_Reg->CRU_CLKGATE_CON[i]=  (clk_gate[i] | 0xffff0000);

}

/***********************************
 * Only DDR Relative Function
 ***********************************/

/****************************************************************************
Internal sram us delay function
Cpu highest frequency is 1.6 GHz
1 cycle = 1/1.6 ns
1 us = 1000 ns = 1000 * 1.6 cycles = 1600 cycles
*****************************************************************************/
__sramdata volatile uint32 loops_per_us;

#define LPJ_100MHZ  999456UL

static void __sramfunc ddr_delayus(uint32 us)
{
    do
    {
        volatile unsigned int i = (loops_per_us*us);
        if (i < 7) i = 7;
        barrier();
        asm volatile(".align 4; 1: subs %0, %0, #1; bne 1b;" : "+r" (i));
    } while (0);
}

void PIE_FUNC(ddr_copy)(uint64_t *pDest, uint64_t *pSrc, uint32 wword)
{
    uint32 i;

    for(i=0; i<wword; i++)
    {
        pDest[i] = pSrc[i];
    }
}
EXPORT_PIE_SYMBOL(FUNC(ddr_copy));

static void ddr_get_datatraing_addr(uint32 *pdtar)
{
    uint32          addr;
    uint32          stride;
    uint32          strideSize;
    uint32          halfCap;
    uint32          ch,chCnt;
    uint32          socAddr[2];
    uint32          chAddr[2];
    uint32          col;
    uint32          row;
    uint32          bank;
    uint32          bw;
    uint32          conf;

    for(ch=0,chCnt=0;ch<CH_MAX;ch++)
    {
        if(p_ddr_ch[ch]->mem_type != DRAM_MAX)
        {
            chCnt++;
        }
    }

    // caculate aglined physical address
    addr =  __pa((unsigned long)ddr_data_training_buf);
    ddr_print("addr=0x%x\n",addr);
    if(addr&0x3F)
    {
        addr += (64-(addr&0x3F));  // 64byte align
    }
    addr -= DRAM_PHYS;
    if(chCnt > 1)
    {
        //find stride info
        stride = READ_DDR_STRIDE(); 
        strideSize = gStrideInfo[stride].size;
        halfCap = gStrideInfo[stride].halfCap;
        ddr_print("stride=%d, size=%d, halfcap=%x\n", stride,strideSize,halfCap);
        //get soc addr
        if(addr & strideSize)  // odd stride size
        {
            socAddr[0] = addr + strideSize;
            socAddr[1] = addr;
        }
        else
        {
            socAddr[0] = addr;
            socAddr[1] = addr + strideSize;
        }
        ddr_print("socAddr[0]=0x%x, socAddr[1]=0x%x\n", socAddr[0], socAddr[1]);
        if((stride >= 0x10) && (stride <= 0x13))  // 3GB stride
        {
            //conver to ch addr
            if(addr < 0x40000000)
            {
                chAddr[0] = socAddr[0];
                chAddr[1] = socAddr[1] - strideSize;
            }
            else if(addr < 0x80000000)
            {
                chAddr[0] = socAddr[0] - 0x40000000 + strideSize;
                chAddr[1] = socAddr[1] - 0x40000000;
            }
            else if(addr < 0xA0000000)
            {
                chAddr[0] = socAddr[0] - 0x40000000;
                chAddr[1] = socAddr[1] - 0x40000000 - strideSize;
            }
            else
            {
                chAddr[0] = socAddr[0] - 0x60000000 + strideSize;
                chAddr[1] = socAddr[1] - 0x60000000;
            }
        }
        else
        {
            //conver to ch addr
            if(addr <  halfCap)
            {
                chAddr[0] = socAddr[0];
                chAddr[1] = socAddr[1] - strideSize;
            }
            else
            {
                chAddr[0] = socAddr[0] - halfCap + strideSize;
                chAddr[1] = socAddr[1] - halfCap;
            }
        }
        ddr_print("chAddr[0]=0x%x, chAddr[1]=0x%x\n", chAddr[0], chAddr[1]);
    }
    else
    {
        chAddr[0] = addr;
        chAddr[1] = addr;
    }

    for(ch=0,chCnt=0;ch<CH_MAX;ch++)
    {
        if(p_ddr_ch[ch]->mem_type != DRAM_MAX)
        {
            // find out col，row，bank,config
            row = READ_ROW_INFO(ch,0);
            bank = READ_BK_INFO(ch);
            col = READ_COL_INFO(ch);
            bw = READ_BW_INFO(ch);
            conf = p_ddr_ch[ch]->pMSCH_Reg->ddrconf;
            // according different address mapping, caculate DTAR register value
            pdtar[ch] = 0;
            pdtar[ch] |= ((chAddr[ch])>>bw) & ((0x1<<col)-1);  // col
            pdtar[ch] |= (((chAddr[ch])>>(bw+col+((ddr_cfg_2_rbc[conf]>>2)&0x3))) & ((0x1<<row)-1)) << 12;  // row
            if(((ddr_cfg_2_rbc[conf]>>7)&0x3)==3)
            {
                pdtar[ch] |= ((((chAddr[ch])>>(bw+col+row)) & ((0x1<<bank)-1))  << 28);  // bank
            }
            else
            {
                pdtar[ch] |= ((((chAddr[ch])>>(bw+col)) & 0x7) << 28);  // bank
            }
        }
    }
    ddr_print("dtar[0]=0x%x, dtar[1]=0x%x\n", pdtar[0], pdtar[1]);
}

static __sramfunc void ddr_reset_dll(uint32 ch)
{
    pDDR_REG_T    pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
    pDDRPHY_REG_T pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;
    
    pPHY_Reg->ACDLLCR &= ~0x40000000;
    pPHY_Reg->DATX8[0].DXDLLCR &= ~0x40000000;
    pPHY_Reg->DATX8[1].DXDLLCR &= ~0x40000000;
    if(!(pDDR_Reg->PPCFG & 1))
    {
        pPHY_Reg->DATX8[2].DXDLLCR &= ~0x40000000;
        pPHY_Reg->DATX8[3].DXDLLCR &= ~0x40000000;
    }
    ddr_delayus(1);
    pPHY_Reg->ACDLLCR |= 0x40000000;
    pPHY_Reg->DATX8[0].DXDLLCR |= 0x40000000;
    pPHY_Reg->DATX8[1].DXDLLCR |= 0x40000000;
    if(!(pDDR_Reg->PPCFG & 1))
    {
        pPHY_Reg->DATX8[2].DXDLLCR |= 0x40000000;
        pPHY_Reg->DATX8[3].DXDLLCR |= 0x40000000;
    }
    ddr_delayus(1);
}

static __sramfunc void ddr_move_to_Lowpower_state(uint32 ch)
{
    register uint32 value;
    register pDDR_REG_T    pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;

    while(1)
    {
        value = pDDR_Reg->STAT.b.ctl_stat;
        if(value == Low_power)
        {
            break;
        }
        switch(value)
        {
            case Init_mem:
                pDDR_Reg->SCTL = CFG_STATE;
                dsb();
                while((pDDR_Reg->STAT.b.ctl_stat) != Config);
            case Config:
                pDDR_Reg->SCTL = GO_STATE;
                dsb();
                while((pDDR_Reg->STAT.b.ctl_stat) != Access);
            case Access:
                pDDR_Reg->SCTL = SLEEP_STATE;
                dsb();
                while((pDDR_Reg->STAT.b.ctl_stat) != Low_power);
                break;
            default:  //Transitional state
                break;
        }
    }
}

static __sramfunc void ddr_move_to_Access_state(uint32 ch)
{
    register uint32 value;
    register pDDR_REG_T    pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
    register pDDRPHY_REG_T pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;

    //set auto self-refresh idle
    pDDR_Reg->MCFG1=(pDDR_Reg->MCFG1&0xffffff00) | DATA(ddr_sr_idle) | (1<<31);
    dsb();

    while(1)
    {
        value = pDDR_Reg->STAT.b.ctl_stat;
        if((value == Access)
           || ((pDDR_Reg->STAT.b.lp_trig == 1) && ((pDDR_Reg->STAT.b.ctl_stat) == Low_power)))
        {
            break;
        }
        switch(value)
        {
            case Low_power:
                pDDR_Reg->SCTL = WAKEUP_STATE;
                dsb();
                while((pDDR_Reg->STAT.b.ctl_stat) != Access);
                while((pPHY_Reg->PGSR & DLDONE) != DLDONE);  //wait DLL lock
                break;
            case Init_mem:
                pDDR_Reg->SCTL = CFG_STATE;
                dsb();
                while((pDDR_Reg->STAT.b.ctl_stat) != Config);
            case Config:
                pDDR_Reg->SCTL = GO_STATE;
                dsb();
                while(!(((pDDR_Reg->STAT.b.ctl_stat) == Access)
                      || ((pDDR_Reg->STAT.b.lp_trig == 1) && ((pDDR_Reg->STAT.b.ctl_stat) == Low_power))));
                break;
            default:  //Transitional state
                break;
        }
    }
    /* de_hw_wakeup :enable auto sr if sr_idle != 0 */
    DDR_HW_WAKEUP(ch,0);
}

static __sramfunc void ddr_move_to_Config_state(uint32 ch)
{
    register uint32 value;
    register pDDR_REG_T    pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
    register pDDRPHY_REG_T pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;

    /* hw_wakeup :disable auto sr */
    DDR_HW_WAKEUP(ch,1);
	dsb();

    while(1)
    {
        value = pDDR_Reg->STAT.b.ctl_stat;
        if(value == Config)
        {
            break;
        }
        switch(value)
        {
            case Low_power:
                pDDR_Reg->SCTL = WAKEUP_STATE;
                dsb();
                while((pDDR_Reg->STAT.b.ctl_stat) != Access);
                while((pPHY_Reg->PGSR & DLDONE) != DLDONE);  //wait DLL lock
            case Access:
            case Init_mem:
                pDDR_Reg->SCTL = CFG_STATE;
                dsb();
                while((pDDR_Reg->STAT.b.ctl_stat) != Config);
                break;
            default:  //Transitional state
                break;
        }
    }
}

//arg包括bank_addr和cmd_addr
static void __sramfunc ddr_send_command(uint32 ch, uint32 rank, uint32 cmd, uint32 arg)
{
    pDDR_REG_T    pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
    
    pDDR_Reg->MCMD = (start_cmd | (rank<<20) | arg | cmd);
    dsb();
    while(pDDR_Reg->MCMD & start_cmd);
}

//对type类型的DDR的几个cs进行DTT
//0  DTT成功
//!0 DTT失败
static uint32 __sramfunc ddr_data_training_trigger(uint32 ch)
{
    uint32        cs;
    pDDR_REG_T    pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
    pDDRPHY_REG_T pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;

    // disable auto refresh
    pDDR_Reg->TREFI = 0;
    dsb();
    if((DATA(ddr_ch[ch]).mem_type != LPDDR2)
       && (DATA(ddr_ch[ch]).mem_type != LPDDR3))
    {
        // passive window
        pPHY_Reg->PGCR |= (1<<1);
    }
    // clear DTDONE status
    pPHY_Reg->PIR |= CLRSR;
    cs = ((pPHY_Reg->PGCR>>18) & 0xF);
    pPHY_Reg->PGCR = (pPHY_Reg->PGCR & (~(0xF<<18))) | (1<<18);  //use cs0 dtt
    // trigger DTT
    pPHY_Reg->PIR |= INIT | QSTRN | LOCKBYP | ZCALBYP | CLRSR | ICPC;
    return cs;
}
//对type类型的DDR的几个cs进行DTT
//0  DTT成功
//!0 DTT失败
static uint32 __sramfunc ddr_data_training(uint32 ch, uint32 cs)
{
    uint32        i,byte;
    pDDR_REG_T    pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
    pDDRPHY_REG_T pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;
    
    // wait echo byte DTDONE
    while((pPHY_Reg->DATX8[0].DXGSR[0] & 1) != 1);
    while((pPHY_Reg->DATX8[1].DXGSR[0] & 1) != 1);
    if(!(pDDR_Reg->PPCFG & 1))
    {
        while((pPHY_Reg->DATX8[2].DXGSR[0] & 1) != 1);
        while((pPHY_Reg->DATX8[3].DXGSR[0] & 1) != 1);
        byte=4;
    }
    pPHY_Reg->PGCR = (pPHY_Reg->PGCR & (~(0xF<<18))) | (cs<<18);  //restore cs
    for(i=0;i<byte;i++)
    {
        pPHY_Reg->DATX8[i].DXDQSTR = (pPHY_Reg->DATX8[i].DXDQSTR & (~((0x7<<3)|(0x3<<14))))
                                      | ((pPHY_Reg->DATX8[i].DXDQSTR & 0x7)<<3)
                                      | (((pPHY_Reg->DATX8[i].DXDQSTR>>12) & 0x3)<<14);
    }
    // send some auto refresh to complement the lost while DTT，//测到1个CS的DTT最长时间是10.7us。最多补2次刷新
    if(cs > 1)
    {
        ddr_send_command(ch,cs, REF_cmd, 0);
        ddr_send_command(ch,cs, REF_cmd, 0);
        ddr_send_command(ch,cs, REF_cmd, 0);
        ddr_send_command(ch,cs, REF_cmd, 0);
    }
    else
    {
        ddr_send_command(ch,cs, REF_cmd, 0);
        ddr_send_command(ch,cs, REF_cmd, 0);
    }
    if((DATA(ddr_ch[ch]).mem_type != LPDDR2)
       && (DATA(ddr_ch[ch]).mem_type != LPDDR3))
    {
        // active window
        pPHY_Reg->PGCR &= ~(1<<1);
    }
    // resume auto refresh
    pDDR_Reg->TREFI = DATA(ddr_reg).pctl.pctl_timing.trefi;

    if(pPHY_Reg->PGSR & DTERR)
    {
        return (-1);
    }
    else
    {
        return 0;
    }
}

static void __sramfunc ddr_set_dll_bypass(uint32 ch, uint32 freq)
{
    pDDR_REG_T    pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
    pDDRPHY_REG_T pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;
    
    if(freq<=150)
    {
        pPHY_Reg->DLLGCR &= ~(1<<23);
        pPHY_Reg->ACDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[0].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[1].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[2].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[3].DXDLLCR |= 0x80000000;
        pPHY_Reg->PIR |= DLLBYP;
    }
    else if(freq<=250)
    {
        pPHY_Reg->DLLGCR |= (1<<23);
        pPHY_Reg->ACDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[0].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[1].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[2].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[3].DXDLLCR |= 0x80000000;
        pPHY_Reg->PIR |= DLLBYP;
    }
    else
    {
        pPHY_Reg->DLLGCR &= ~(1<<23);
        pPHY_Reg->ACDLLCR &= ~0x80000000;
        pPHY_Reg->DATX8[0].DXDLLCR &= ~0x80000000;
        pPHY_Reg->DATX8[1].DXDLLCR &= ~0x80000000;
        if(!(pDDR_Reg->PPCFG & 1))
        {
            pPHY_Reg->DATX8[2].DXDLLCR &= ~0x80000000;
            pPHY_Reg->DATX8[3].DXDLLCR &= ~0x80000000;
        }
        pPHY_Reg->PIR &= ~DLLBYP;
    }
}

static noinline uint32 ddr_get_parameter(uint32 nMHz)
{
    uint32 tmp;
    uint32 ret = 0;
    uint32 al;
    uint32 bl,bl_tmp;
    uint32 cl;
    uint32 cwl;
    PCTL_TIMING_T *p_pctl_timing=&(p_ddr_reg->pctl.pctl_timing);
    PHY_TIMING_T  *p_publ_timing=&(p_ddr_reg->publ.phy_timing);
    volatile NOC_TIMING_T  *p_noc_timing=&(p_ddr_reg->noc[0].ddrtiming);
    volatile NOC_ACTIVATE_T  *p_noc_activate=&(p_ddr_reg->noc[0].activate);
    uint32 ch;
    uint32 mem_type;
    uint32 ddr_speed_bin=DDR3_DEFAULT;
    uint32 ddr_capability_per_die=0;

    for(ch=0;ch<CH_MAX;ch++)
    {
        if(p_ddr_ch[ch]->mem_type != DRAM_MAX)
        {
            mem_type = p_ddr_ch[ch]->mem_type;
            if(ddr_speed_bin == DDR3_DEFAULT)
            {
                ddr_speed_bin = p_ddr_ch[ch]->ddr_speed_bin;
            }
            else
            {
                ddr_speed_bin = (ddr_speed_bin > p_ddr_ch[ch]->ddr_speed_bin) ? ddr_speed_bin : p_ddr_ch[ch]->ddr_speed_bin;
            }
            if(ddr_capability_per_die == 0)
            {
                ddr_capability_per_die = p_ddr_ch[ch]->ddr_capability_per_die;
            }
            else
            {
                ddr_capability_per_die = (ddr_capability_per_die > p_ddr_ch[ch]->ddr_capability_per_die) ? ddr_capability_per_die : p_ddr_ch[ch]->ddr_capability_per_die;
            }
            break;
        }
    }

    p_pctl_timing->togcnt1u = nMHz;
    p_pctl_timing->togcnt100n = nMHz/10;
    p_pctl_timing->tinit = 200;
    p_pctl_timing->trsth = 500;

    if(mem_type == DDR3)
    {
        if(ddr_speed_bin > DDR3_DEFAULT){
            ret = -1;
            goto out;
        }

        #define DDR3_tREFI_7_8_us    (78)  //unit 100ns
        #define DDR3_tMRD            (4)   //tCK
        #define DDR3_tRFC_512Mb      (90)  //ns
        #define DDR3_tRFC_1Gb        (110) //ns
        #define DDR3_tRFC_2Gb        (160) //ns
        #define DDR3_tRFC_4Gb        (300) //ns
        #define DDR3_tRFC_8Gb        (350) //ns
        #define DDR3_tRTW            (2)   //register min valid value
        #define DDR3_tRAS            (37)  //ns
        #define DDR3_tRRD            (10)  //ns
        #define DDR3_tRTP            (7)   //ns
        #define DDR3_tWR             (15)  //ns
        #define DDR3_tWTR            (7)   //ns
        #define DDR3_tXP             (7)   //ns
        #define DDR3_tXPDLL          (24)  //ns
        #define DDR3_tZQCS           (80)  //ns
        #define DDR3_tZQCSI          (0)   //ns
        #define DDR3_tDQS            (1)   //tCK
        #define DDR3_tCKSRE          (10)  //ns
        #define DDR3_tCKE_400MHz     (7)   //ns
        #define DDR3_tCKE_533MHz     (6)   //ns
        #define DDR3_tMOD            (15)  //ns
        #define DDR3_tRSTL           (100) //ns
        #define DDR3_tZQCL           (320) //ns
        #define DDR3_tDLLK           (512) //tCK

        al = 0;
        bl = 8;
        if(nMHz <= 330)
        {
            tmp = 0;
        }
        else if(nMHz<=400)
        {
            tmp = 1;
        }
        else if(nMHz<=533)
        {
            tmp = 2;
        }
        else if(nMHz<=666)
        {
            tmp = 3;
        }
        else if(nMHz<=800)
        {
            tmp = 4;
        }
        else if(nMHz<=933)
        {
            tmp = 5;
        }
        else
        {
            tmp = 6;
        }
        
        if(nMHz < 300)       //when dll bypss cl = cwl = 6;
        {
            cl = 6;
            cwl = 6;
        }
        else
        {
            cl = (ddr3_cl_cwl[ddr_speed_bin][tmp] >> 4)&0xf;
            cwl = ddr3_cl_cwl[ddr_speed_bin][tmp] & 0xf;
        }
        if(cl == 0)
            ret = -4;
        if(nMHz <= DDR3_DDR2_ODT_DISABLE_FREQ)
        {
            p_publ_timing->mr[1] = DDR3_DS_40 | DDR3_Rtt_Nom_DIS;
        }
        else
        {
            p_publ_timing->mr[1] = DDR3_DS_40 | DDR3_Rtt_Nom_120;
        }
        p_publ_timing->mr[2] = DDR3_MR2_CWL(cwl) /* | DDR3_Rtt_WR_60 */;
        p_publ_timing->mr[3] = 0;
        /**************************************************
         * PCTL Timing
         **************************************************/
        /*
         * tREFI, average periodic refresh interval, 7.8us
         */
        p_pctl_timing->trefi = DDR3_tREFI_7_8_us;
        /*
         * tMRD, 4 tCK
         */
        p_pctl_timing->tmrd = DDR3_tMRD & 0x7;
        p_publ_timing->dtpr0.b.tMRD = DDR3_tMRD-4;
        /*
         * tRFC, 90ns(512Mb),110ns(1Gb),160ns(2Gb),300ns(4Gb),350ns(8Gb)
         */
        if(ddr_capability_per_die <= 0x4000000)         // 512Mb 90ns
        {
            tmp = DDR3_tRFC_512Mb;
        }
        else if(ddr_capability_per_die <= 0x8000000)    // 1Gb 110ns
        {
            tmp = DDR3_tRFC_1Gb;
        }
        else if(ddr_capability_per_die <= 0x10000000)   // 2Gb 160ns
        {
            tmp = DDR3_tRFC_2Gb;
        }
        else if(ddr_capability_per_die <= 0x20000000)   // 4Gb 300ns
        {
            tmp = DDR3_tRFC_4Gb;
        }
        else    // 8Gb  350ns
        {
            tmp = DDR3_tRFC_8Gb;
        }
        p_pctl_timing->trfc = (tmp*nMHz+999)/1000;
        p_publ_timing->dtpr1.b.tRFC = ((tmp*nMHz+999)/1000);
        /*
         * tXSR, =tDLLK=512 tCK
         */
        p_pctl_timing->texsr = DDR3_tDLLK;
        p_publ_timing->dtpr2.b.tXS = DDR3_tDLLK;
        /*
         * tRP=CL
         */
        p_pctl_timing->trp = cl;
        p_publ_timing->dtpr0.b.tRP = cl;
        /*
         * WrToMiss=WL*tCK + tWR + tRP + tRCD
         */
        p_noc_timing->b.WrToMiss = (cwl+((DDR3_tWR*nMHz+999)/1000)+cl+cl);
        /*
         * tRC=tRAS+tRP
         */
        p_pctl_timing->trc = ((((ddr3_tRC_tFAW[ddr_speed_bin]>>8)*nMHz+999)/1000)&0x3F);
        p_noc_timing->b.ActToAct = (((ddr3_tRC_tFAW[ddr_speed_bin]>>8)*nMHz+999)/1000);
        p_publ_timing->dtpr0.b.tRC = (((ddr3_tRC_tFAW[ddr_speed_bin]>>8)*nMHz+999)/1000);

        p_pctl_timing->trtw = (cl+2-cwl);//DDR3_tRTW;
        p_publ_timing->dtpr1.b.tRTW = 0;
        p_noc_timing->b.RdToWr = (cl+2-cwl);
        p_pctl_timing->tal = al;
        p_pctl_timing->tcl = cl;
        p_pctl_timing->tcwl = cwl;
        /*
         * tRAS, 37.5ns(400MHz)     37.5ns(533MHz)
         */
        p_pctl_timing->tras = (((DDR3_tRAS*nMHz+(nMHz>>1)+999)/1000)&0x3F);
        p_publ_timing->dtpr0.b.tRAS = ((DDR3_tRAS*nMHz+(nMHz>>1)+999)/1000);
        /*
         * tRCD=CL
         */
        p_pctl_timing->trcd = cl;
        p_publ_timing->dtpr0.b.tRCD = cl;
        /*
         * tRRD = max(4nCK, 7.5ns), DDR3-1066(1K), DDR3-1333(2K), DDR3-1600(2K)
         *        max(4nCK, 10ns), DDR3-800(1K,2K), DDR3-1066(2K)
         *        max(4nCK, 6ns), DDR3-1333(1K), DDR3-1600(1K)
         *
         */
        tmp = ((DDR3_tRRD*nMHz+999)/1000);
        if(tmp < 4)
        {
            tmp = 4;
        }
        p_pctl_timing->trrd = (tmp&0xF);
        p_publ_timing->dtpr0.b.tRRD = tmp;
        p_noc_activate->b.Rrd = tmp;
        /*
         * tRTP, max(4 tCK,7.5ns)
         */
        tmp = ((DDR3_tRTP*nMHz+(nMHz>>1)+999)/1000);
        if(tmp < 4)
        {
            tmp = 4;
        }
        p_pctl_timing->trtp = tmp&0xF;
        p_publ_timing->dtpr0.b.tRTP = tmp;
        /*
         * RdToMiss=tRTP+tRP + tRCD - (BL/2 * tCK)
         */
        p_noc_timing->b.RdToMiss = (tmp+cl+cl-(bl>>1));
        /*
         * tWR, 15ns
         */
        tmp = ((DDR3_tWR*nMHz+999)/1000);
        p_pctl_timing->twr = tmp&0x1F;
        if(tmp<9)
        {
            tmp = tmp - 4;
        }
        else
        {
            tmp += (tmp&0x1) ? 1:0;
            tmp = tmp>>1;
        }
        bl_tmp = (bl == 8) ? DDR3_BL8 : DDR3_BC4;
        p_publ_timing->mr[0] = bl_tmp | DDR3_CL(cl) | DDR3_WR(tmp);

        /*
         * tWTR, max(4 tCK,7.5ns)
         */
        tmp = ((DDR3_tWTR*nMHz+(nMHz>>1)+999)/1000);
        if(tmp < 4)
        {
            tmp = 4;
        }
        p_pctl_timing->twtr = tmp&0xF;
        p_publ_timing->dtpr0.b.tWTR = tmp;
        /*
         * WrToRd=WL+tWTR
         */
        p_noc_timing->b.WrToRd = (tmp+cwl);
        /*
         * tXP, max(3 tCK, 7.5ns)(<933MHz)
         */
        tmp = ((DDR3_tXP*nMHz+(nMHz>>1)+999)/1000);
        if(tmp < 3)
        {
            tmp = 3;
        }
        p_pctl_timing->txp = tmp&0x7;
        /*
         * tXPDLL, max(10 tCK,24ns)
         */
        tmp = ((DDR3_tXPDLL*nMHz+999)/1000);
        if(tmp < 10)
        {
            tmp = 10;
        }
        p_pctl_timing->txpdll = tmp & 0x3F;
        p_publ_timing->dtpr2.b.tXP = tmp;
        /*
         * tZQCS, max(64 tCK, 80ns)
         */
        tmp = ((DDR3_tZQCS*nMHz+999)/1000);
        if(tmp < 64)
        {
            tmp = 64;
        }
        p_pctl_timing->tzqcs = tmp&0x7F;
        /*
         * tZQCSI,
         */
        p_pctl_timing->tzqcsi = DDR3_tZQCSI;
        /*
         * tDQS,
         */
        p_pctl_timing->tdqs = DDR3_tDQS;
        /*
         * tCKSRE, max(5 tCK, 10ns)
         */
        tmp = ((DDR3_tCKSRE*nMHz+999)/1000);
        if(tmp < 5)
        {
            tmp = 5;
        }
        p_pctl_timing->tcksre = tmp & 0x1F;
        /*
         * tCKSRX, max(5 tCK, 10ns)
         */
        p_pctl_timing->tcksrx = tmp & 0x1F;
        /*
         * tCKE, max(3 tCK,7.5ns)(400MHz) max(3 tCK,5.625ns)(533MHz)
         */
        if(nMHz>=533)
        {
            tmp = ((DDR3_tCKE_533MHz*nMHz+999)/1000);
        }
        else
        {
            tmp = ((DDR3_tCKE_400MHz*nMHz+(nMHz>>1)+999)/1000);
        }
        if(tmp < 3)
        {
            tmp = 3;
        }
        p_pctl_timing->tcke = tmp & 0x7;
        /*
         * tCKESR, =tCKE + 1tCK
         */
        p_pctl_timing->tckesr = (tmp+1)&0xF;
        p_publ_timing->dtpr2.b.tCKE = tmp+1;
        /*
         * tMOD, max(12 tCK,15ns)
         */
        tmp = ((DDR3_tMOD*nMHz+999)/1000);
        if(tmp < 12)
        {
            tmp = 12;
        }
        p_pctl_timing->tmod = tmp&0x1F;
        p_publ_timing->dtpr1.b.tMOD = (tmp-12);
        /*
         * tRSTL, 100ns
         */
        p_pctl_timing->trstl = ((DDR3_tRSTL*nMHz+999)/1000)&0x7F;
        /*
         * tZQCL, max(256 tCK, 320ns)
         */
        tmp = ((DDR3_tZQCL*nMHz+999)/1000);
        if(tmp < 256)
        {
            tmp = 256;
        }
        p_pctl_timing->tzqcl = tmp&0x3FF;
        /*
         * tMRR, 0 tCK
         */
        p_pctl_timing->tmrr = 0;
        /*
         * tDPD, 0
         */
        p_pctl_timing->tdpd = 0;

        /**************************************************
         * PHY Timing
         **************************************************/
        /*
         * tCCD, BL/2 for DDR2 and 4 for DDR3
         */
        p_publ_timing->dtpr0.b.tCCD = 0;
        /*
         * tDQSCKmax,5.5ns
         */
        p_publ_timing->dtpr1.b.tDQSCKmax = 0;
        /*
         * tRTODT, 0:ODT may be turned on immediately after read post-amble
         *         1:ODT may not be turned on until one clock after the read post-amble
         */
        p_publ_timing->dtpr1.b.tRTODT = 1;
        /*
         * tFAW,40ns(400MHz 1KB page) 37.5ns(533MHz 1KB page) 50ns(400MHz 2KB page)   50ns(533MHz 2KB page)
         */
        tmp = (((ddr3_tRC_tFAW[ddr_speed_bin]&0x0ff)*nMHz+999)/1000);
        p_publ_timing->dtpr1.b.tFAW = tmp;
        p_noc_activate->b.Fawbank = 1;
        p_noc_activate->b.Faw = tmp;
        /*
         * tAOND_tAOFD
         */
        p_publ_timing->dtpr1.b.tAOND = 0;
        /*
         * tDLLK,512 tCK
         */
        p_publ_timing->dtpr2.b.tDLLK = DDR3_tDLLK;
        /**************************************************
         * NOC Timing
         **************************************************/
        p_noc_timing->b.BurstLen = (bl>>1);
    }
    else if(mem_type == LPDDR2)
    {
        #define LPDDR2_tREFI_3_9_us    (39)  //unit 100ns
        #define LPDDR2_tREFI_7_8_us    (78)  //unit 100ns
        #define LPDDR2_tMRD            (5)   //tCK
        #define LPDDR2_tRFC_8Gb        (210)  //ns
        #define LPDDR2_tRFC_4Gb        (130)  //ns
        #define LPDDR2_tRPpb_4_BANK             (24)  //ns
        #define LPDDR2_tRPab_SUB_tRPpb_4_BANK   (0)   //ns
        #define LPDDR2_tRPpb_8_BANK             (24)  //ns
        #define LPDDR2_tRPab_SUB_tRPpb_8_BANK   (3)   //ns
        #define LPDDR2_tRTW          (1)   //tCK register min valid value
        #define LPDDR2_tRAS          (42)  //ns
        #define LPDDR2_tRCD          (24)  //ns
        #define LPDDR2_tRRD          (10)  //ns
        #define LPDDR2_tRTP          (7)   //ns
        #define LPDDR2_tWR           (15)  //ns
        #define LPDDR2_tWTR_GREAT_200MHz         (7)  //ns
        #define LPDDR2_tWTR_LITTLE_200MHz        (10) //ns
        #define LPDDR2_tXP           (7)  //ns
        #define LPDDR2_tXPDLL        (0)
        #define LPDDR2_tZQCS         (90) //ns
        #define LPDDR2_tZQCSI        (0)
        #define LPDDR2_tDQS          (1)
        #define LPDDR2_tCKSRE        (1)  //tCK
        #define LPDDR2_tCKSRX        (2)  //tCK
        #define LPDDR2_tCKE          (3)  //tCK
        #define LPDDR2_tMOD          (0)
        #define LPDDR2_tRSTL         (0)
        #define LPDDR2_tZQCL         (360)  //ns
        #define LPDDR2_tMRR          (2)    //tCK
        #define LPDDR2_tCKESR        (15)   //ns
        #define LPDDR2_tDPD_US       (500)  //us
        #define LPDDR2_tFAW_GREAT_200MHz    (50)  //ns
        #define LPDDR2_tFAW_LITTLE_200MHz   (60)  //ns
        #define LPDDR2_tDLLK         (2)  //tCK
        #define LPDDR2_tDQSCK_MAX    (3)  //tCK
        #define LPDDR2_tDQSCK_MIN    (0)  //tCK
        #define LPDDR2_tDQSS         (1)  //tCK

        uint32 trp_tmp;
        uint32 trcd_tmp;
        uint32 tras_tmp;
        uint32 trtp_tmp;
        uint32 twr_tmp;

        al = 0;
        bl = 8;
        /*     1066 933 800 667 533 400 333
         * RL,   8   7   6   5   4   3   3
         * WL,   4   4   3   2   2   1   1
         */
        if(nMHz<=200)
        {
            cl = 3;
            cwl = 1;
            p_publ_timing->mr[2] = LPDDR2_RL3_WL1;
        }
        else if(nMHz<=266)
        {
            cl = 4;
            cwl = 2;
            p_publ_timing->mr[2] = LPDDR2_RL4_WL2;
        }
        else if(nMHz<=333)
        {
            cl = 5;
            cwl = 2;
            p_publ_timing->mr[2] = LPDDR2_RL5_WL2;
        }
        else if(nMHz<=400)
        {
            cl = 6;
            cwl = 3;
            p_publ_timing->mr[2] = LPDDR2_RL6_WL3;
        }
        else if(nMHz<=466)
        {
            cl = 7;
            cwl = 4;
            p_publ_timing->mr[2] = LPDDR2_RL7_WL4;
        }
        else //(nMHz<=1066)
        {
            cl = 8;
            cwl = 4;
            p_publ_timing->mr[2] = LPDDR2_RL8_WL4;
        }
        p_publ_timing->mr[3] = LPDDR2_DS_34;
        p_publ_timing->mr[0] = 0;
        /**************************************************
         * PCTL Timing
         **************************************************/
        /*
         * tREFI, average periodic refresh interval, 15.6us(<256Mb) 7.8us(256Mb-1Gb) 3.9us(2Gb-8Gb)
         */
        if(ddr_capability_per_die >= 0x10000000)   // 2Gb
        {
            p_pctl_timing->trefi = LPDDR2_tREFI_3_9_us;
        }
        else
        {
            p_pctl_timing->trefi = LPDDR2_tREFI_7_8_us;
        }

        /*
         * tMRD, (=tMRW), 5 tCK
         */
        p_pctl_timing->tmrd = LPDDR2_tMRD & 0x7;
        p_publ_timing->dtpr0.b.tMRD = 3;
        /*
         * tRFC, 90ns(<=512Mb) 130ns(1Gb-4Gb) 210ns(8Gb)
         */
        if(ddr_capability_per_die >= 0x40000000)   // 8Gb
        {
            p_pctl_timing->trfc = (LPDDR2_tRFC_8Gb*nMHz+999)/1000;
            p_publ_timing->dtpr1.b.tRFC = ((LPDDR2_tRFC_8Gb*nMHz+999)/1000);
            /*
             * tXSR, max(2tCK,tRFC+10ns)
             */
            tmp=(((LPDDR2_tRFC_8Gb+10)*nMHz+999)/1000);
        }
        else
        {
            p_pctl_timing->trfc = (LPDDR2_tRFC_4Gb*nMHz+999)/1000;
            p_publ_timing->dtpr1.b.tRFC = ((LPDDR2_tRFC_4Gb*nMHz+999)/1000);
            tmp=(((LPDDR2_tRFC_4Gb+10)*nMHz+999)/1000);
        }
        if(tmp<2)
        {
            tmp=2;
        }
        p_pctl_timing->texsr = tmp&0x3FF;
        p_publ_timing->dtpr2.b.tXS = tmp;

        /*
         * tRP, max(3tCK, 4-bank:15ns(Fast) 18ns(Typ) 24ns(Slow), 8-bank:18ns(Fast) 21ns(Typ) 27ns(Slow))
         */
        //if(pPHY_Reg->DCR.b.DDR8BNK)
        if(1)
        {
            trp_tmp = ((LPDDR2_tRPpb_8_BANK*nMHz+999)/1000);
            if(trp_tmp<3)
            {
                trp_tmp=3;
            }
            p_pctl_timing->trp = ((((LPDDR2_tRPab_SUB_tRPpb_8_BANK*nMHz+999)/1000) & 0x3)<<16) | (trp_tmp&0xF);
        }
        else
        {
            trp_tmp = ((LPDDR2_tRPpb_4_BANK*nMHz+999)/1000);
            if(trp_tmp<3)
            {
                trp_tmp=3;
            }
            p_pctl_timing->trp = (LPDDR2_tRPab_SUB_tRPpb_4_BANK<<16) | (trp_tmp&0xF);
        }
        p_publ_timing->dtpr0.b.tRP = trp_tmp;
        /*
         * tRAS, max(3tCK,42ns)
         */
        tras_tmp=((LPDDR2_tRAS*nMHz+999)/1000);
        if(tras_tmp<3)
        {
            tras_tmp=3;
        }
        p_pctl_timing->tras = (tras_tmp&0x3F);
        p_publ_timing->dtpr0.b.tRAS = tras_tmp;

        /*
         * tRCD, max(3tCK, 15ns(Fast) 18ns(Typ) 24ns(Slow))
         */
        trcd_tmp = ((LPDDR2_tRCD*nMHz+999)/1000);
        if(trcd_tmp<3)
        {
            trcd_tmp=3;
        }
        p_pctl_timing->trcd = (trcd_tmp&0xF);
        p_publ_timing->dtpr0.b.tRCD = trcd_tmp;

        /*
         * tRTP, max(2tCK, 7.5ns)
         */
        trtp_tmp = ((LPDDR2_tRTP*nMHz+(nMHz>>1)+999)/1000);
        if(trtp_tmp<2)
        {
            trtp_tmp = 2;
        }
        p_pctl_timing->trtp = trtp_tmp&0xF;
        p_publ_timing->dtpr0.b.tRTP = trtp_tmp;

        /*
         * tWR, max(3tCK,15ns)
         */
        twr_tmp=((LPDDR2_tWR*nMHz+999)/1000);
        if(twr_tmp<3)
        {
            twr_tmp=3;
        }
        p_pctl_timing->twr = twr_tmp&0x1F;
        bl_tmp = (bl == 16) ? LPDDR2_BL16 : ((bl == 8) ? LPDDR2_BL8 : LPDDR2_BL4);
        p_publ_timing->mr[1] = bl_tmp | LPDDR2_nWR(twr_tmp);

        /*         
         * WrToMiss=WL*tCK + tWR + tRP + tRCD         
         */
        p_noc_timing->b.WrToMiss = (cwl+twr_tmp+trp_tmp+trcd_tmp);
        /*
         * RdToMiss=tRTP + tRP + tRCD - (BL/2 * tCK)
         */
        p_noc_timing->b.RdToMiss = (trtp_tmp+trp_tmp+trcd_tmp-(bl>>1));
        /*
         * tRC=tRAS+tRP
         */
        p_pctl_timing->trc = ((tras_tmp+trp_tmp)&0x3F);
        p_noc_timing->b.ActToAct = (tras_tmp+trp_tmp);
        p_publ_timing->dtpr0.b.tRC = (tras_tmp+trp_tmp);

        /*
         * RdToWr=(cl+2-cwl)
         */
        p_pctl_timing->trtw = (cl+2-cwl);//LPDDR2_tRTW;   
        p_publ_timing->dtpr1.b.tRTW = 0;
        p_noc_timing->b.RdToWr = (cl+2-cwl);
        p_pctl_timing->tal = al;
        p_pctl_timing->tcl = cl;
        p_pctl_timing->tcwl = cwl;
        /*
         * tRRD, max(2tCK,10ns)
         */
        tmp=((LPDDR2_tRRD*nMHz+999)/1000);
        if(tmp<2)
        {
            tmp=2;
        }
        p_pctl_timing->trrd = (tmp&0xF);
        p_publ_timing->dtpr0.b.tRRD = tmp;
        p_noc_activate->b.Rrd = tmp;
        /*
         * tWTR, max(2tCK, 7.5ns(533-266MHz)  10ns(200-166MHz))
         */
        if(nMHz > 200)
        {
            tmp=((LPDDR2_tWTR_GREAT_200MHz*nMHz+(nMHz>>1)+999)/1000);
        }
        else
        {
            tmp=((LPDDR2_tWTR_LITTLE_200MHz*nMHz+999)/1000);
        }
        if(tmp<2)
        {
            tmp=2;
        }
        p_pctl_timing->twtr = tmp&0xF;
        p_publ_timing->dtpr0.b.tWTR = tmp;
        /*
         * WrToRd=WL+tWTR
         */
        p_noc_timing->b.WrToRd = (cwl+tmp);
        /*
         * tXP, max(2tCK,7.5ns)
         */
        tmp=((LPDDR2_tXP*nMHz+(nMHz>>1)+999)/1000);
        if(tmp<2)
        {
            tmp=2;
        }
        p_pctl_timing->txp = tmp&0x7;
        p_publ_timing->dtpr2.b.tXP = tmp;
        /*
         * tXPDLL, 0ns
         */
        p_pctl_timing->txpdll = LPDDR2_tXPDLL;
        /*
         * tZQCS, 90ns
         */
        p_pctl_timing->tzqcs = ((LPDDR2_tZQCS*nMHz+999)/1000)&0x7F;
        /*
         * tZQCSI,
         */
        //if(pDDR_Reg->MCFG &= lpddr2_s4)
        if(1)
        {
            p_pctl_timing->tzqcsi = LPDDR2_tZQCSI;
        }
        else
        {
            p_pctl_timing->tzqcsi = 0;
        }
        /*
         * tDQS,
         */
        p_pctl_timing->tdqs = LPDDR2_tDQS;
        /*
         * tCKSRE, 1 tCK
         */
        p_pctl_timing->tcksre = LPDDR2_tCKSRE;
        /*
         * tCKSRX, 2 tCK
         */
        p_pctl_timing->tcksrx = LPDDR2_tCKSRX;
        /*
         * tCKE, 3 tCK
         */
        p_pctl_timing->tcke = LPDDR2_tCKE;
        p_publ_timing->dtpr2.b.tCKE = LPDDR2_tCKE;
        /*
         * tMOD, 0 tCK
         */
        p_pctl_timing->tmod = LPDDR2_tMOD;
        p_publ_timing->dtpr1.b.tMOD = LPDDR2_tMOD;
        /*
         * tRSTL, 0 tCK
         */
        p_pctl_timing->trstl = LPDDR2_tRSTL;
        /*
         * tZQCL, 360ns
         */
        p_pctl_timing->tzqcl = ((LPDDR2_tZQCL*nMHz+999)/1000)&0x3FF;
        /*
         * tMRR, 2 tCK
         */
        p_pctl_timing->tmrr = LPDDR2_tMRR;
        /*
         * tCKESR, max(3tCK,15ns)
         */
        tmp = ((LPDDR2_tCKESR*nMHz+999)/1000);
        if(tmp < 3)
        {
            tmp = 3;
        }
        p_pctl_timing->tckesr = tmp&0xF;
        /*
         * tDPD, 500us
         */
        p_pctl_timing->tdpd = LPDDR2_tDPD_US;

        /**************************************************
         * PHY Timing
         **************************************************/
        /*
         * tCCD, BL/2 for DDR2 and 4 for DDR3
         */
        p_publ_timing->dtpr0.b.tCCD = 0;
        /*
         * tDQSCKmax,5.5ns
         */
        p_publ_timing->dtpr1.b.tDQSCKmax = LPDDR2_tDQSCK_MAX;
        /*
         * tDQSCKmin,2.5ns
         */
        p_publ_timing->dtpr1.b.tDQSCK = LPDDR2_tDQSCK_MIN;
        /*
         * tRTODT, 0:ODT may be turned on immediately after read post-amble
         *         1:ODT may not be turned on until one clock after the read post-amble
         */
        p_publ_timing->dtpr1.b.tRTODT = 1;
        /*
         * tFAW,max(8tCK, 50ns(200-533MHz)  60ns(166MHz))
         */
        if(nMHz>=200)
        {
            tmp=((LPDDR2_tFAW_GREAT_200MHz*nMHz+999)/1000);
        }
        else
        {
            tmp=((LPDDR2_tFAW_LITTLE_200MHz*nMHz+999)/1000);
        }
        if(tmp<8)
        {
            tmp=8;
        }
        p_publ_timing->dtpr1.b.tFAW = tmp;        
        p_noc_activate->b.Fawbank = 1;
        p_noc_activate->b.Faw = tmp;
        /*
         * tAOND_tAOFD
         */
        p_publ_timing->dtpr1.b.tAOND = 0;
        /*
         * tDLLK,0
         */
        p_publ_timing->dtpr2.b.tDLLK = LPDDR2_tDLLK;
        /**************************************************
         * NOC Timing
         **************************************************/
        p_noc_timing->b.BurstLen = (bl>>1);
    }
    else if(mem_type == LPDDR3)
    {
        #define LPDDR3_tREFI_3_9_us    (39)  //unit 100ns
        #define LPDDR3_tMRD            (10)   //tCK
        #define LPDDR3_tRFC_8Gb        (210)  //ns
        #define LPDDR3_tRFC_4Gb        (130)  //ns
        #define LPDDR3_tRPpb_8_BANK             (24)  //ns
        #define LPDDR3_tRPab_SUB_tRPpb_8_BANK   (3)   //ns
        #define LPDDR3_tRTW          (1)   //tCK register min valid value
        #define LPDDR3_tRAS          (42)  //ns
        #define LPDDR3_tRCD          (24)  //ns
        #define LPDDR3_tRRD          (10)  //ns
        #define LPDDR3_tRTP          (7)   //ns
        #define LPDDR3_tWR           (15)  //ns
        #define LPDDR3_tWTR          (7)  //ns
        #define LPDDR3_tXP           (7)  //ns
        #define LPDDR3_tXPDLL        (0)
        #define LPDDR3_tZQCS         (90) //ns
        #define LPDDR3_tZQCSI        (0)
        #define LPDDR3_tDQS          (1)
        #define LPDDR3_tCKSRE        (2)  //tCK
        #define LPDDR3_tCKSRX        (2)  //tCK
        #define LPDDR3_tCKE          (3)  //tCK
        #define LPDDR3_tMOD          (0)
        #define LPDDR3_tRSTL         (0)
        #define LPDDR3_tZQCL         (360)  //ns
        #define LPDDR3_tMRR          (4)    //tCK
        #define LPDDR3_tCKESR        (15)   //ns
        #define LPDDR3_tDPD_US       (500)   //us
        #define LPDDR3_tFAW          (50)  //ns
        #define LPDDR3_tDLLK         (2)  //tCK
        #define LPDDR3_tDQSCK_MAX    (3)  //tCK
        #define LPDDR3_tDQSCK_MIN    (0)  //tCK
        #define LPDDR3_tDQSS         (1)  //tCK

        uint32 trp_tmp;
        uint32 trcd_tmp;
        uint32 tras_tmp;
        uint32 trtp_tmp;
        uint32 twr_tmp;

        al = 0;
        bl = 8;
        /* Only support Write Latency Set A here
         *     1066 933 800 733 667 600 533 400 166
         * RL,   16  14  12  11  10  9   8   6   3
         * WL,   8   8   6   6   6   5   4   3   1
         */
        if(nMHz<=166)
        {
            cl = 3;
            cwl = 1;
            p_publ_timing->mr[2] = LPDDR3_RL3_WL1;
        }
        else if(nMHz<=400)
        {
            cl = 6;
            cwl = 3;
            p_publ_timing->mr[2] = LPDDR3_RL6_WL3;
        }
        else if(nMHz<=533)
        {
            cl = 8;
            cwl = 4;
            p_publ_timing->mr[2] = LPDDR3_RL8_WL4;
        }
        else if(nMHz<=600)
        {
            cl = 9;
            cwl = 5;
            p_publ_timing->mr[2] = LPDDR3_RL9_WL5;
        }
        else if(nMHz<=667)
        {
            cl = 10;
            cwl = 6;
            p_publ_timing->mr[2] = LPDDR3_RL10_WL6;
        }
        else if(nMHz<=733)
        {
            cl = 11;
            cwl = 6;
            p_publ_timing->mr[2] = LPDDR3_RL11_WL6;
        }
        else if(nMHz<=800)
        {
            cl = 12;
            cwl = 6;
            p_publ_timing->mr[2] = LPDDR3_RL12_WL6;
        }
        else if(nMHz<=933)
        {
            cl = 14;
            cwl = 8;
            p_publ_timing->mr[2] = LPDDR3_RL14_WL8;
        }
        else //(nMHz<=1066)
        {
            cl = 16;
            cwl = 8;
            p_publ_timing->mr[2] = LPDDR3_RL16_WL8;
        }
        p_publ_timing->mr[3] = LPDDR3_DS_34;
        if(nMHz <= DDR3_DDR2_ODT_DISABLE_FREQ)
        {
            p_publ_timing->mr11 = LPDDR3_ODT_DIS;
        }
        else
        {
            p_publ_timing->mr11 = LPDDR3_ODT_240;
        }
        p_publ_timing->mr[0] = 0;
        /**************************************************
         * PCTL Timing
         **************************************************/
        /*
         * tREFI, average periodic refresh interval, 3.9us(4Gb-16Gb)
         */
        p_pctl_timing->trefi = LPDDR3_tREFI_3_9_us;

        /*
         * tMRD, (=tMRW), 10 tCK
         */
        p_pctl_timing->tmrd = LPDDR3_tMRD & 0x7;
        p_publ_timing->dtpr0.b.tMRD = 3;  //max value
        /*
         * tRFC, 130ns(4Gb) 210ns(>4Gb)
         */
        if(ddr_capability_per_die > 0x20000000)   // >4Gb
        {
            p_pctl_timing->trfc = (LPDDR3_tRFC_8Gb*nMHz+999)/1000;
            p_publ_timing->dtpr1.b.tRFC = ((LPDDR3_tRFC_8Gb*nMHz+999)/1000);
            /*
             * tXSR, max(2tCK,tRFC+10ns)
             */
            tmp=(((LPDDR3_tRFC_8Gb+10)*nMHz+999)/1000);
        }
        else
        {
            p_pctl_timing->trfc = (LPDDR3_tRFC_4Gb*nMHz+999)/1000;
            p_publ_timing->dtpr1.b.tRFC = ((LPDDR3_tRFC_4Gb*nMHz+999)/1000);
            tmp=(((LPDDR3_tRFC_4Gb+10)*nMHz+999)/1000);
        }
        if(tmp<2)
        {
            tmp=2;
        }
        p_pctl_timing->texsr = tmp&0x3FF;
        p_publ_timing->dtpr2.b.tXS = tmp;

        /*
         * tRP, max(3tCK, 18ns(Fast) 21ns(Typ) 27ns(Slow))
         */
        //if(pPHY_Reg->DCR.b.DDR8BNK)
        if(1)
        {
            trp_tmp = ((LPDDR3_tRPpb_8_BANK*nMHz+999)/1000);
            if(trp_tmp<3)
            {
                trp_tmp=3;
            }
            p_pctl_timing->trp = ((((LPDDR3_tRPab_SUB_tRPpb_8_BANK*nMHz+999)/1000) & 0x3)<<16) | (trp_tmp&0xF);
        }
        p_publ_timing->dtpr0.b.tRP = trp_tmp;
        /*
         * tRAS, max(3tCK,42ns)
         */
        tras_tmp=((LPDDR3_tRAS*nMHz+999)/1000);
        if(tras_tmp<3)
        {
            tras_tmp=3;
        }
        p_pctl_timing->tras = (tras_tmp&0x3F);
        p_publ_timing->dtpr0.b.tRAS = tras_tmp;

        /*
         * tRCD, max(3tCK, 15ns(Fast) 18ns(Typ) 24ns(Slow))
         */
        trcd_tmp = ((LPDDR3_tRCD*nMHz+999)/1000);
        if(trcd_tmp<3)
        {
            trcd_tmp=3;
        }
        p_pctl_timing->trcd = (trcd_tmp&0xF);
        p_publ_timing->dtpr0.b.tRCD = trcd_tmp;

        /*
         * tRTP, max(4tCK, 7.5ns)
         */
        trtp_tmp = ((LPDDR3_tRTP*nMHz+(nMHz>>1)+999)/1000);
        if(trtp_tmp<4)
        {
            trtp_tmp = 4;
        }
        p_pctl_timing->trtp = trtp_tmp&0xF;
        p_publ_timing->dtpr0.b.tRTP = trtp_tmp;

        /*
         * tWR, max(4tCK,15ns)
         */
        twr_tmp=((LPDDR3_tWR*nMHz+999)/1000);
        if(twr_tmp<4)
        {
            twr_tmp=4;
        }
        p_pctl_timing->twr = twr_tmp&0x1F;
        bl_tmp = LPDDR3_BL8;
        p_publ_timing->mr[1] = bl_tmp | LPDDR2_nWR(twr_tmp);

        /*
         * WrToMiss=WL*tCK + tWR + tRP + tRCD
         */
        p_noc_timing->b.WrToMiss = (cwl+twr_tmp+trp_tmp+trcd_tmp);
        /*
         * RdToMiss=tRTP + tRP + tRCD - (BL/2 * tCK)
         */
        p_noc_timing->b.RdToMiss = (trtp_tmp+trp_tmp+trcd_tmp-(bl>>1));
        /*
         * tRC=tRAS+tRP
         */
        p_pctl_timing->trc = ((tras_tmp+trp_tmp)&0x3F);
        p_noc_timing->b.ActToAct = (tras_tmp+trp_tmp);
        p_publ_timing->dtpr0.b.tRC = (tras_tmp+trp_tmp);

        /*
         * RdToWr=(cl+2-cwl)
         */
        p_pctl_timing->trtw = (cl+2-cwl);//LPDDR2_tRTW;
        p_publ_timing->dtpr1.b.tRTW = 0;
        p_noc_timing->b.RdToWr = (cl+2-cwl);
        p_pctl_timing->tal = al;
        p_pctl_timing->tcl = cl;
        p_pctl_timing->tcwl = cwl;
        /*
         * tRRD, max(2tCK,10ns)
         */
        tmp=((LPDDR3_tRRD*nMHz+999)/1000);
        if(tmp<2)
        {
            tmp=2;
        }
        p_pctl_timing->trrd = (tmp&0xF);
        p_publ_timing->dtpr0.b.tRRD = tmp;
        p_noc_activate->b.Rrd = tmp;
        /*
         * tWTR, max(4tCK, 7.5ns)
         */
        tmp=((LPDDR3_tWTR*nMHz+(nMHz>>1)+999)/1000);
        if(tmp<4)
        {
            tmp=4;
        }
        p_pctl_timing->twtr = tmp&0xF;
        p_publ_timing->dtpr0.b.tWTR = tmp;
        /*
         * WrToRd=WL+tWTR
         */
        p_noc_timing->b.WrToRd = (cwl+tmp);
        /*
         * tXP, max(3tCK,7.5ns)
         */
        tmp=((LPDDR3_tXP*nMHz+(nMHz>>1)+999)/1000);
        if(tmp<3)
        {
            tmp=3;
        }
        p_pctl_timing->txp = tmp&0x7;
        p_publ_timing->dtpr2.b.tXP = tmp;
        /*
         * tXPDLL, 0ns
         */
        p_pctl_timing->txpdll = LPDDR3_tXPDLL;
        /*
         * tZQCS, 90ns
         */
        p_pctl_timing->tzqcs = ((LPDDR3_tZQCS*nMHz+999)/1000)&0x7F;
        /*
         * tZQCSI,
         */
        p_pctl_timing->tzqcsi = LPDDR3_tZQCSI;
        /*
         * tDQS,
         */
        p_pctl_timing->tdqs = LPDDR3_tDQS;
        /*
         * tCKSRE=tCPDED, 2 tCK
         */
        p_pctl_timing->tcksre = LPDDR3_tCKSRE;
        /*
         * tCKSRX, 2 tCK
         */
        p_pctl_timing->tcksrx = LPDDR3_tCKSRX;
        /*
         * tCKE, (max 7.5ns,3 tCK)
         */
        tmp=((7*nMHz+(nMHz>>1)+999)/1000);
        if(tmp<LPDDR3_tCKE)
        {
            tmp=LPDDR3_tCKE;
        }
        p_pctl_timing->tcke = tmp;
        p_publ_timing->dtpr2.b.tCKE = tmp;
        /*
         * tMOD, 0 tCK
         */
        p_pctl_timing->tmod = LPDDR3_tMOD;
        p_publ_timing->dtpr1.b.tMOD = LPDDR3_tMOD;
        /*
         * tRSTL, 0 tCK
         */
        p_pctl_timing->trstl = LPDDR3_tRSTL;
        /*
         * tZQCL, 360ns
         */
        p_pctl_timing->tzqcl = ((LPDDR3_tZQCL*nMHz+999)/1000)&0x3FF;
        /*
         * tMRR, 4 tCK
         */
        p_pctl_timing->tmrr = LPDDR3_tMRR;
        /*
         * tCKESR, max(3tCK,15ns)
         */
        tmp = ((LPDDR3_tCKESR*nMHz+999)/1000);
        if(tmp < 3)
        {
            tmp = 3;
        }
        p_pctl_timing->tckesr = tmp&0xF;
        /*
         * tDPD, 500us
         */
        p_pctl_timing->tdpd = LPDDR3_tDPD_US;

        /**************************************************
         * PHY Timing
         **************************************************/
        /*
         * tCCD, BL/2 for DDR2 and 4 for DDR3
         */
        p_publ_timing->dtpr0.b.tCCD = 0;
        /*
         * tDQSCKmax,5.5ns
         */
        p_publ_timing->dtpr1.b.tDQSCKmax = LPDDR3_tDQSCK_MAX;
        /*
         * tDQSCKmin,2.5ns
         */
        p_publ_timing->dtpr1.b.tDQSCK = LPDDR3_tDQSCK_MIN;
        /*
         * tRTODT, 0:ODT may be turned on immediately after read post-amble
         *         1:ODT may not be turned on until one clock after the read post-amble
         */
        p_publ_timing->dtpr1.b.tRTODT = 1;
        /*
         * tFAW,max(8tCK, 50ns)
         */
        tmp=((LPDDR3_tFAW*nMHz+999)/1000);
        if(tmp<8)
        {
            tmp=8;
        }
        p_publ_timing->dtpr1.b.tFAW = tmp;
        p_noc_activate->b.Fawbank = 1;
        p_noc_activate->b.Faw = tmp;
        /*
         * tAOND_tAOFD
         */
        p_publ_timing->dtpr1.b.tAOND = 0;
        /*
         * tDLLK,0
         */
        p_publ_timing->dtpr2.b.tDLLK = LPDDR3_tDLLK;
        /**************************************************
         * NOC Timing
         **************************************************/
        p_noc_timing->b.BurstLen = (bl>>1);
    }

out:
    return ret;
}

static uint32 __sramfunc ddr_update_timing(uint32 ch)
{
    uint32 i,bl_tmp=0;
    PCTL_TIMING_T *p_pctl_timing=&(DATA(ddr_reg).pctl.pctl_timing);
    PHY_TIMING_T  *p_publ_timing=&(DATA(ddr_reg).publ.phy_timing);
    volatile NOC_TIMING_T  *p_noc_timing=&(DATA(ddr_reg).noc[0].ddrtiming);
    volatile NOC_ACTIVATE_T  *p_noc_activate=&(DATA(ddr_reg).noc[0].activate);
    pDDR_REG_T    pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
    pDDRPHY_REG_T pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;
    pMSCH_REG     pMSCH_Reg= DATA(ddr_ch[ch]).pMSCH_Reg;

    FUNC(ddr_copy)((uint64_t *)&(pDDR_Reg->TOGCNT1U), (uint64_t*)&(p_pctl_timing->togcnt1u), 17);
    pPHY_Reg->DTPR[0] = p_publ_timing->dtpr0.d32;
    pPHY_Reg->DTPR[1] = p_publ_timing->dtpr1.d32;
    pPHY_Reg->DTPR[2] = p_publ_timing->dtpr2.d32;
    pMSCH_Reg->ddrtiming.d32 = (pMSCH_Reg->ddrtiming.b.BwRatio) | p_noc_timing->d32;
    pMSCH_Reg->activate.d32 = p_noc_activate->d32;
    // Update PCTL BL
    if(DATA(ddr_ch[ch]).mem_type == DDR3)
    {
        bl_tmp = ((p_publ_timing->mr[0] & 0x3) == DDR3_BL8) ? ddr2_ddr3_bl_8 : ddr2_ddr3_bl_4;
        pDDR_Reg->MCFG = (pDDR_Reg->MCFG & (~(0x1|(0x3<<18)|(0x1<<17)|(0x1<<16)))) | bl_tmp | tfaw_cfg(5)|pd_exit_slow|pd_type(1);
        if(DATA(ddr_freq) <= DDR3_DDR2_DLL_DISABLE_FREQ)
        {
            pDDR_Reg->DFITRDDATAEN   = pDDR_Reg->TCL-3;
        }
        else
        {
            pDDR_Reg->DFITRDDATAEN   = pDDR_Reg->TCL-2;
        }
        pDDR_Reg->DFITPHYWRLAT   = pDDR_Reg->TCWL-1;
    }    
    else if((DATA(ddr_ch[ch]).mem_type == LPDDR2)||(DATA(ddr_ch[ch]).mem_type == LPDDR3))    
    {
        if(((p_publ_timing->mr[1]) & 0x7) == LPDDR2_BL8)
        {
            bl_tmp = mddr_lpddr2_bl_8;
        }
        else if(((p_publ_timing->mr[1]) & 0x7) == LPDDR2_BL4)
        {
            bl_tmp = mddr_lpddr2_bl_4;
        }
        else //if(((p_publ_timing->mr[1]) & 0x7) == LPDDR2_BL16)
        {
            bl_tmp = mddr_lpddr2_bl_16;
        }        
        if((DATA(ddr_freq)>=200)||(DATA(ddr_ch[ch]).mem_type == LPDDR3))        
        {
            pDDR_Reg->MCFG = (pDDR_Reg->MCFG & (~((0x3<<20)|(0x3<<18)|(0x1<<17)|(0x1<<16)))) | bl_tmp | tfaw_cfg(5)|pd_exit_fast|pd_type(1);
        }
        else
        {
            pDDR_Reg->MCFG = (pDDR_Reg->MCFG & (~((0x3<<20)|(0x3<<18)|(0x1<<17)|(0x1<<16)))) | bl_tmp | tfaw_cfg(6)|pd_exit_fast|pd_type(1);
        }
        i = ((pPHY_Reg->DTPR[1] >> 27) & 0x7) - ((pPHY_Reg->DTPR[1] >> 24) & 0x7);
        pPHY_Reg->DSGCR = (pPHY_Reg->DSGCR & (~(0x3F<<5))) | (i<<5) | (i<<8);  //tDQSCKmax-tDQSCK
        pDDR_Reg->DFITRDDATAEN   = pDDR_Reg->TCL-1;
        pDDR_Reg->DFITPHYWRLAT   = pDDR_Reg->TCWL;
    }

    return 0;
}

static uint32 __sramfunc ddr_update_mr(uint32 ch)
{
    PHY_TIMING_T  *p_publ_timing=&(DATA(ddr_reg).publ.phy_timing);
    uint32         cs,dll_off;
    pDDRPHY_REG_T  pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;

    cs = ((pPHY_Reg->PGCR>>18) & 0xF);
    dll_off = (pPHY_Reg->MR[1] & DDR3_DLL_DISABLE) ? 1:0;
    FUNC(ddr_copy)((uint64_t *)&(pPHY_Reg->MR[0]), (uint64_t*)&(p_publ_timing->mr[0]), 2);
    if(DATA(ddr_ch[ch]).mem_type == DDR3)
    {
        ddr_send_command(ch,cs, MRS_cmd, bank_addr(0x2) | cmd_addr((p_publ_timing->mr[2])));
        if(DATA(ddr_freq)>DDR3_DDR2_DLL_DISABLE_FREQ)
        {
            if(dll_off)  // off -> on
            {
                ddr_send_command(ch,cs, MRS_cmd, bank_addr(0x1) | cmd_addr((p_publ_timing->mr[1])));  //DLL enable
                ddr_send_command(ch,cs, MRS_cmd, bank_addr(0x0) | cmd_addr(((p_publ_timing->mr[0]))| DDR3_DLL_RESET));  //DLL reset
                ddr_delayus(1);  //at least 200 DDR cycle
                ddr_send_command(ch,cs, MRS_cmd, bank_addr(0x0) | cmd_addr((p_publ_timing->mr[0])));
            }
            else // on -> on
            {
                ddr_send_command(ch,cs, MRS_cmd, bank_addr(0x1) | cmd_addr((p_publ_timing->mr[1])));
                ddr_send_command(ch,cs, MRS_cmd, bank_addr(0x0) | cmd_addr((p_publ_timing->mr[0])));
            }
        }
        else
        {
            pPHY_Reg->MR[1] = (((p_publ_timing->mr[1])) | DDR3_DLL_DISABLE);
            ddr_send_command(ch,cs, MRS_cmd, bank_addr(0x1) | cmd_addr(((p_publ_timing->mr[1])) | DDR3_DLL_DISABLE));  //DLL disable
            ddr_send_command(ch,cs, MRS_cmd, bank_addr(0x0) | cmd_addr((p_publ_timing->mr[0])));
        }
    }    
    else if((DATA(ddr_ch[ch]).mem_type == LPDDR2)||(DATA(ddr_ch[ch]).mem_type == LPDDR3))    
    {
        ddr_send_command(ch,cs, MRS_cmd, lpddr2_ma(0x1) | lpddr2_op((p_publ_timing->mr[1])));
        ddr_send_command(ch,cs, MRS_cmd, lpddr2_ma(0x2) | lpddr2_op((p_publ_timing->mr[2])));
        ddr_send_command(ch,cs, MRS_cmd, lpddr2_ma(0x3) | lpddr2_op((p_publ_timing->mr[3])));        
        if(DATA(ddr_ch[ch]).mem_type == LPDDR3)
        {
            ddr_send_command(ch,cs, MRS_cmd, lpddr2_ma(11) | lpddr2_op((p_publ_timing->mr11)));
        }
    }
    else //mDDR
    {
        ddr_send_command(ch,cs, MRS_cmd, bank_addr(0x0) | cmd_addr((p_publ_timing->mr[0])));
        ddr_send_command(ch,cs, MRS_cmd, bank_addr(0x1) | cmd_addr((p_publ_timing->mr[2]))); //mr[2] is mDDR MR1
    }
    return 0;
}

static void __sramfunc ddr_update_odt(uint32 ch)
{
    uint32        cs,tmp;
    pDDR_REG_T    pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
    pDDRPHY_REG_T pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;

    //adjust DRV and ODT
    if((DATA(ddr_ch[ch]).mem_type == DDR3) || (DATA(ddr_ch[ch]).mem_type == LPDDR3))
    {
        if(DATA(ddr_freq) <= DDR3_DDR2_ODT_DISABLE_FREQ)
        {
            pPHY_Reg->DATX8[0].DXGCR &= ~(0x3<<9);  //dynamic RTT disable
            pPHY_Reg->DATX8[1].DXGCR &= ~(0x3<<9);
            if(!(pDDR_Reg->PPCFG & 1))
            {
                pPHY_Reg->DATX8[2].DXGCR &= ~(0x3<<9);
                pPHY_Reg->DATX8[3].DXGCR &= ~(0x3<<9);
            }
        }
        else
        {
            pPHY_Reg->DATX8[0].DXGCR |= (0x3<<9);  //dynamic RTT enable
            pPHY_Reg->DATX8[1].DXGCR |= (0x3<<9);
            if(!(pDDR_Reg->PPCFG & 1))
            {
                pPHY_Reg->DATX8[2].DXGCR |= (0x3<<9);
                pPHY_Reg->DATX8[3].DXGCR |= (0x3<<9);
            }
        }
    }
    else
    {
        pPHY_Reg->DATX8[0].DXGCR &= ~(0x3<<9);  //dynamic RTT disable
        pPHY_Reg->DATX8[1].DXGCR &= ~(0x3<<9);
        if(!(pDDR_Reg->PPCFG & 1))
        {
            pPHY_Reg->DATX8[2].DXGCR &= ~(0x3<<9);
            pPHY_Reg->DATX8[3].DXGCR &= ~(0x3<<9);
        }
    }
    if(DATA(ddr_ch[ch]).mem_type == LPDDR2)
    {
        tmp = GET_LPDDR2_DS_ODT();  //DS=34ohm,ODT=171ohm
    }
    else if(DATA(ddr_ch[ch]).mem_type == LPDDR3)
    {
        tmp = GET_LPDDR3_DS_ODT();  //DS=34ohm,ODT=171ohm
    }
    else
    {
        tmp = GET_DDR3_DS_ODT();  //DS=34ohm,ODT=171ohm
    }
    cs = ((pPHY_Reg->PGCR>>18) & 0xF);
    if(cs > 1)
    {
        pPHY_Reg->ZQ1CR[0] = tmp;
        dsb();
    }
    pPHY_Reg->ZQ0CR[0] = tmp;
    dsb();
}

static void __sramfunc ddr_selfrefresh_enter(uint32 nMHz)
{
    uint32 ch;
    pDDR_REG_T    pDDR_Reg;
    pDDRPHY_REG_T pPHY_Reg;

    for(ch=0;ch<CH_MAX;ch++)
    {
        pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
        pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;

        if(DATA(ddr_ch[ch]).mem_type != DRAM_MAX)
        {
            ddr_move_to_Lowpower_state(ch);
            pDDR_Reg->TZQCSI = 0;
        }
    }
}

#if defined(CONFIG_ARCH_RK3066B)
static __sramdata uint32 data8_dqstr[25][4];
static __sramdata uint32 min_ddr_freq,dqstr_flag=false;

int ddr_get_datatraing_value_3168(bool end_flag,uint32 dqstr_value,uint32 min_freq)
{
    if(end_flag == true)
    {
        dqstr_flag = true;    //complete learn data training value flag
        min_ddr_freq = min_freq;
        return 0;
    }

    data8_dqstr[dqstr_value][0]=pPHY_Reg->DATX8[0].DXDQSTR;
    data8_dqstr[dqstr_value][1]=pPHY_Reg->DATX8[0].DXDQSTR;
    data8_dqstr[dqstr_value][2]=pPHY_Reg->DATX8[0].DXDQSTR;
    data8_dqstr[dqstr_value][3]=pPHY_Reg->DATX8[0].DXDQSTR;

    ddr_print("training %luMhz[%d]:0x%x-0x%x-0x%x-0x%x\n",
        clk_get_rate(clk_get(NULL, "ddr"))/1000000,dqstr_value,data8_dqstr[dqstr_value][0],data8_dqstr[dqstr_value][1],
        data8_dqstr[dqstr_value][2],data8_dqstr[dqstr_value][3]);
    return 0;
}

static void __sramfunc ddr_set_pll_enter_3168(uint32 freq_slew)
{
    uint32 value_1u,value_100n;
    ddr_move_to_Config_state();

    if(freq_slew == 1)
    {
        value_100n = DATA(ddr_reg).pctl.pctl_timing.togcnt100n;
        value_1u = DATA(ddr_reg).pctl.pctl_timing.togcnt1u;
        DATA(ddr_reg).pctl.pctl_timing.togcnt1u = pDDR_Reg->TOGCNT1U;
        DATA(ddr_reg).pctl.pctl_timing.togcnt100n = pDDR_Reg->TOGCNT100N;
        ddr_update_timing();
        ddr_update_mr();
        DATA(ddr_reg).pctl.pctl_timing.togcnt100n = value_100n;
        DATA(ddr_reg).pctl.pctl_timing.togcnt1u = value_1u;
    }
    else
    {
        pDDR_Reg->TOGCNT100N = DATA(ddr_reg).pctl.pctl_timing.togcnt100n;
        pDDR_Reg->TOGCNT1U = DATA(ddr_reg).pctl.pctl_timing.togcnt1u;
    }

    pDDR_Reg->TZQCSI = 0;
    ddr_move_to_Lowpower_state();

    ddr_set_dll_bypass(0);  //dll bypass
    SET_DDRPHY_CLKGATE(ch,1);  //disable DDR PHY clock
    dsb();
}

void __sramlocalfunc ddr_set_pll_exit_3168(uint32 freq_slew,uint32 dqstr_value)
{
    SET_DDRPHY_CLKGATE(ch,0);  //enable DDR PHY clock
    dsb();
    ddr_set_dll_bypass(DATA(ddr_freq));
    ddr_reset_dll();

    if(dqstr_flag==true)
    {
        pPHY_Reg->DATX8[0].DXDQSTR=data8_dqstr[dqstr_value][0];
        pPHY_Reg->DATX8[1].DXDQSTR=data8_dqstr[dqstr_value][1];
        pPHY_Reg->DATX8[2].DXDQSTR=data8_dqstr[dqstr_value][2];
        pPHY_Reg->DATX8[3].DXDQSTR=data8_dqstr[dqstr_value][3];
    }

    ddr_update_odt();
    ddr_move_to_Config_state();
    if(freq_slew == 1)
    {
        pDDR_Reg->TOGCNT100N = DATA(ddr_reg).pctl.pctl_timing.togcnt100n;
        pDDR_Reg->TOGCNT1U = DATA(ddr_reg).pctl.pctl_timing.togcnt1u;
        pDDR_Reg->TZQCSI = DATA(ddr_reg).pctl.pctl_timing.tzqcsi;
    }
    else
    {
        ddr_update_timing();
        ddr_update_mr();
    }
    ddr_data_training();
    ddr_move_to_Access_state();
}
#endif

static void __sramfunc ddr_chb_update_timing_odt(void)
{
    ddr_set_dll_bypass(1,0); //always use dll bypass
    ddr_update_timing(1);
    ddr_update_odt(1);
}

/* Make sure ddr_SRE_2_SRX paramter less than 4 */
static void __sramfunc ddr_SRE_2_SRX(uint32 freq, uint32 freq_slew,uint32 dqstr_value)
{
    uint32 n,ch;
    uint32 cs[CH_MAX];
    
    /** 2. ddr enter self-refresh mode or precharge power-down mode */
    idle_port();
#if defined(CONFIG_ARCH_RK3066B)
    ddr_set_pll_enter_3168(freq_slew);
#else
    ddr_selfrefresh_enter(freq);
#endif

    /** 3. change frequence  */
    FUNC(ddr_set_pll)(freq,1);
    DATA(ddr_freq) = freq;

    /** 5. Issues a Mode Exit command   */
#if defined(CONFIG_ARCH_RK3066B)
    ddr_set_pll_exit_3168(freq_slew,dqstr_value);
#else
    //ddr_selfrefresh_exit();
    if(DATA(ddr_ch[1]).mem_type != DRAM_MAX)
    {
        ddr_chb_update_timing_odt();
    }
    ddr_set_dll_bypass(0,0); //always use dll bypass
    ddr_update_timing(0);
    ddr_update_odt(0);
    FUNC(ddr_set_pll)(freq,2);
    for(ch=0;ch<CH_MAX;ch++)
    {
        if(DATA(ddr_ch[ch]).mem_type != DRAM_MAX)
        {
            ddr_set_dll_bypass(ch,DATA(ddr_freq));
            ddr_reset_dll(ch);
            //ddr_delayus(10);   //wait DLL lock

            ddr_move_to_Config_state(ch);
            ddr_update_mr(ch);
            cs[ch] = ddr_data_training_trigger(ch);
        }
    }
    for(ch=0;ch<CH_MAX;ch++)
    {
        if(DATA(ddr_ch[ch]).mem_type != DRAM_MAX)
        {
            n = ddr_data_training(ch,cs[ch]);
            ddr_move_to_Access_state(ch);
            if(n!=0)
            {
                sram_printascii("DTT failed!\n");
            }
        }
    }
#endif
    deidle_port();
    dsb();
}

struct ddr_change_freq_sram_param {
    uint32 arm_freq;
    uint32 freq;
    uint32 freq_slew;
    uint32 dqstr_value;
};

void PIE_FUNC(ddr_change_freq_sram)(void *arg)
{
    struct ddr_change_freq_sram_param *param = arg;
    loops_per_us = LPJ_100MHZ * param->arm_freq / 1000000;
    /* Make sure ddr_SRE_2_SRX paramter less than 4 */
    ddr_SRE_2_SRX(param->freq, param->freq_slew, param->dqstr_value);
}
EXPORT_PIE_SYMBOL(FUNC(ddr_change_freq_sram));

typedef struct freq_tag{
    uint32_t nMHz;
    struct ddr_freq_t *p_ddr_freq_t;
}freq_t;

static noinline uint32 ddr_change_freq_sram(void *arg)
{
    uint32 freq;
    uint32 freq_slew=0;
    uint32 dqstr_value=0;
    unsigned long flags;
    struct ddr_change_freq_sram_param param;
    volatile uint32 n;
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;
    uint32 i;
    uint32 gpllvaluel;
    freq_t *p_freq_t=(freq_t *)arg;    
    uint32 nMHz=p_freq_t->nMHz;
	struct rk_screen screen;
	int dclk_div = 0;

#if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
    struct ddr_freq_t *p_ddr_freq_t=p_freq_t->p_ddr_freq_t;
#endif

#if defined(CONFIG_ARCH_RK3066B)
    if(dqstr_flag==true)
    {
        dqstr_value=((nMHz-min_ddr_freq+1)/25 + 1) /2;
        freq_slew = (nMHz>ddr_freq)? 1 : 0;
    }
#endif

	rk_fb_get_prmry_screen(&screen);
	if (screen.lcdc_id == 0)
		dclk_div = (cru_readl(RK3288_CRU_CLKSELS_CON(27)) >> 8) & 0xff;
	else if (screen.lcdc_id == 1)
		dclk_div = (cru_readl(RK3288_CRU_CLKSELS_CON(29)) >> 8) & 0xff;

    param.arm_freq = ddr_get_pll_freq(APLL);
    gpllvaluel = ddr_get_pll_freq(GPLL);
    if((200 < gpllvaluel) ||( gpllvaluel <1600))      //GPLL:200MHz~1600MHz
    {
        if( gpllvaluel > 800)     //800-1600MHz  /4:200MHz-400MHz
        {
            *kern_to_pie(rockchip_pie_chunk, &DATA(ddr_select_gpll_div)) = 4;
        }
        else if( gpllvaluel > 400)    //400-800MHz  /2:200MHz-400MHz
        {
            *kern_to_pie(rockchip_pie_chunk, &DATA(ddr_select_gpll_div)) = 2;
        }
        else        //200-400MHz  /1:200MHz-400MHz
        {
            *kern_to_pie(rockchip_pie_chunk, &DATA(ddr_select_gpll_div)) = 1;
        }
    }
    else
    {
        ddr_print("GPLL frequency = %dMHz,Not suitable for ddr_clock \n",gpllvaluel);
    }
    freq=p_ddr_set_pll(nMHz,0);

    ddr_get_parameter(freq);

    /** 1. Make sure there is no host access */
    local_irq_save(flags);
    local_fiq_disable();
    flush_tlb_all();
    isb();

#if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
    if(p_ddr_freq_t->screen_ft_us > 0)
    {
        p_ddr_freq_t->t1 = cpu_clock(0);
        p_ddr_freq_t->t2 = (uint32)(p_ddr_freq_t->t1 - p_ddr_freq_t->t0);   //ns

        //if test_count exceed maximum test times,ddr_freq_t.screen_ft_us == 0xfefefefe by ddr_freq.c
        if( (p_ddr_freq_t->t2 > p_ddr_freq_t->screen_ft_us*1000) && (p_ddr_freq_t->screen_ft_us != 0xfefefefe))
        {
            freq = 0;
            goto end;
        }
        else
        {
	     rk_fb_poll_wait_frame_complete();
        }
    }
#endif
    for(i=0;i<SRAM_SIZE/4096;i++)
    {
        n=temp[1024*i];
        barrier();
    }

    for(i=0;i<CH_MAX;i++)
    {
        if(p_ddr_ch[i]->mem_type != DRAM_MAX)
        {
            n= p_ddr_ch[i]->pDDR_Reg->SCFG.d32;
            n= p_ddr_ch[i]->pPHY_Reg->RIDR;
            n= p_ddr_ch[i]->pMSCH_Reg->ddrconf;
        }
    }
    n= pCRU_Reg->CRU_PLL_CON[0][0];
    n= pPMU_Reg->PMU_WAKEUP_CFG[0];
    n= READ_GRF_REG();
    dsb();

    param.freq = freq;
    param.freq_slew = freq_slew;
    param.dqstr_value = dqstr_value;
	rk_fb_set_prmry_screen_status(SCREEN_PREPARE_DDR_CHANGE);
	if (screen.lcdc_id == 0)
		cru_writel(0 | CRU_W_MSK_SETBITS(0xff, 8, 0xff),
		RK3288_CRU_CLKSELS_CON(27));
	else if (screen.lcdc_id == 1)
		cru_writel(0 | CRU_W_MSK_SETBITS(0xff, 8, 0xff),
		RK3288_CRU_CLKSELS_CON(29));

    call_with_stack(fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_change_freq_sram)),
                    &param,
                    rockchip_sram_stack-(NR_CPUS-1)*PAUSE_CPU_STACK_SIZE);

	if (screen.lcdc_id == 0)
		cru_writel(0 | CRU_W_MSK_SETBITS(dclk_div, 8, 0xff),
		RK3288_CRU_CLKSELS_CON(27));
	else if (screen.lcdc_id == 1)
		cru_writel(0 | CRU_W_MSK_SETBITS(dclk_div, 8, 0xff),
		RK3288_CRU_CLKSELS_CON(29));
	rk_fb_set_prmry_screen_status(SCREEN_UNPREPARE_DDR_CHANGE);

#if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
end:
#endif
    local_fiq_enable();
    local_irq_restore(flags);
    return freq;
}

#if defined(ENABLE_DDR_CLCOK_GPLL_PATH)
static uint32 ddr_change_freq_gpll_dpll(uint32 nMHz)
{
    uint32 gpll_freq,gpll_div;
    struct ddr_freq_t ddr_freq_t;
    ddr_freq_t.screen_ft_us = 0;

    if(true == ddr_rk3188_dpll_is_good)
    {
        gpllvaluel = ddr_get_pll_freq(GPLL);

        if((200 < gpllvaluel) ||( gpllvaluel <1600))      //GPLL:200MHz~1600MHz
        {
            gpll_div = (gpllvaluel+nMHz-1)/nMHz;
            if( gpllvaluel > 800)     //800-1600MHz  /4:200MHz-400MHz
            {
                gpll_freq = gpllvaluel/4;
                gpll_div = 4;
            }
            else if( gpllvaluel > 400)    //400-800MHz  /2:200MHz-400MHz
            {
                gpll_freq = gpllvaluel/2;
                gpll_div = 2;
            }
            else        //200-400MHz  /1:200MHz-400MHz
            {
                gpll_freq = gpllvaluel;
                gpll_div = 1;
            }

            *p_ddr_select_gpll_div=gpll_div;    //select GPLL
            ddr_change_freq_sram(gpll_freq,ddr_freq_t);
            *p_ddr_select_gpll_div=0;

            p_ddr_set_pll(nMHz,0); //count DPLL
            p_ddr_set_pll(nMHz,2); //lock DPLL only,but not select DPLL
        }
        else
        {
            ddr_print("GPLL frequency = %dMHz,Not suitable for ddr_clock \n",gpllvaluel);
        }
    }

    return ddr_change_freq_sram(nMHz,ddr_freq_t);

}
#endif

bool DEFINE_PIE_DATA(cpu_pause[NR_CPUS]);
volatile bool *DATA(p_cpu_pause);
static inline bool is_cpu0_paused(unsigned int cpu) { smp_rmb(); return DATA(cpu_pause)[0]; }
static inline void set_cpuX_paused(unsigned int cpu, bool pause) { DATA(cpu_pause)[cpu] = pause; smp_wmb(); }
static inline bool is_cpuX_paused(unsigned int cpu) { smp_rmb(); return DATA(p_cpu_pause)[cpu]; }
static inline void set_cpu0_paused(bool pause) { DATA(p_cpu_pause)[0] = pause; smp_wmb();}

#define MAX_TIMEOUT (16000000UL << 6) //>0.64s

/* Do not use stack, safe on SMP */
void PIE_FUNC(_pause_cpu)(void *arg)
{       
    unsigned int cpu = (unsigned int)arg;
    
    set_cpuX_paused(cpu, true);
    while (is_cpu0_paused(cpu));
    set_cpuX_paused(cpu, false);
}

static void pause_cpu(void *info)
{
    unsigned int cpu = raw_smp_processor_id();

    call_with_stack(fn_to_pie(rockchip_pie_chunk, &FUNC(_pause_cpu)),
            (void *)cpu,
            rockchip_sram_stack-(cpu-1)*PAUSE_CPU_STACK_SIZE);
}

static void wait_cpu(void *info)
{
}

static int call_with_single_cpu(u32 (*fn)(void *arg), void *arg)
{
    u32 timeout = MAX_TIMEOUT;
    unsigned int cpu;
    unsigned int this_cpu = smp_processor_id();
    int ret = 0;

    cpu_maps_update_begin();
    local_bh_disable();
    set_cpu0_paused(true);
    smp_call_function((smp_call_func_t)pause_cpu, NULL, 0);

    for_each_online_cpu(cpu) {
        if (cpu == this_cpu)
            continue;
        while (!is_cpuX_paused(cpu) && --timeout);
        if (timeout == 0) {
            pr_err("pause cpu %d timeout\n", cpu);
            goto out;
        }
    }

    ret = fn(arg);

out:
    set_cpu0_paused(false);
    local_bh_enable();
    smp_call_function(wait_cpu, NULL, true);
    cpu_maps_update_done();

    return ret;
}

void PIE_FUNC(ddr_adjust_config)(void *arg)
{
    uint32 value[CH_MAX];
    uint32 ch;
    pDDR_REG_T    pDDR_Reg;
    pDDRPHY_REG_T pPHY_Reg;

    for(ch=0;ch<CH_MAX;ch++)
    {
        if(DATA(ddr_ch[ch]).mem_type != DRAM_MAX)
        {
            value[ch] = ((uint32 *)arg)[ch];
            pDDR_Reg = DATA(ddr_ch[ch]).pDDR_Reg;
            pPHY_Reg = DATA(ddr_ch[ch]).pPHY_Reg;
            
            //enter config state
            ddr_move_to_Config_state(ch);

            //set data training address
            pPHY_Reg->DTAR = value[ch];

            //set auto power down idle
            pDDR_Reg->MCFG=(pDDR_Reg->MCFG&0xffff00ff)|(PD_IDLE<<8);

            //CKDV=00
            pPHY_Reg->PGCR &= ~(0x3<<12);

            //enable the hardware low-power interface
            pDDR_Reg->SCFG.b.hw_low_power_en = 1;

            if(pDDR_Reg->PPCFG & 1)
            {
                pPHY_Reg->DATX8[2].DXGCR &= ~(1);          //disable byte
                pPHY_Reg->DATX8[3].DXGCR &= ~(1);
                pPHY_Reg->DATX8[2].DXDLLCR |= 0x80000000;  //disable DLL
                pPHY_Reg->DATX8[3].DXDLLCR |= 0x80000000;
            }

            ddr_update_odt(ch);

            //enter access state
            ddr_move_to_Access_state(ch);
        }
    }
}
EXPORT_PIE_SYMBOL(FUNC(ddr_adjust_config));

static uint32 _ddr_adjust_config(void *dtar)
{
    uint32 i;
    unsigned long flags;
    volatile uint32 n;
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;
    
     /** 1. Make sure there is no host access */
    local_irq_save(flags);
    local_fiq_disable();
    flush_tlb_all();
    isb();

    for(i=0;i<SRAM_SIZE/4096;i++)
    {
        n=temp[1024*i];
        barrier();
    }
    for(i=0;i<CH_MAX;i++)
    {
        if(p_ddr_ch[i]->mem_type != DRAM_MAX)
        {
            n= p_ddr_ch[i]->pDDR_Reg->SCFG.d32;
            n= p_ddr_ch[i]->pPHY_Reg->RIDR;
            n= p_ddr_ch[i]->pMSCH_Reg->ddrconf;
        }
    }
    n= pCRU_Reg->CRU_PLL_CON[0][0];
    n= pPMU_Reg->PMU_WAKEUP_CFG[0];
    n= READ_GRF_REG();
    dsb();

    call_with_stack(fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_adjust_config)),
                    (void *)dtar,
                    rockchip_sram_stack-(NR_CPUS-1)*PAUSE_CPU_STACK_SIZE);
    local_fiq_enable();
    local_irq_restore(flags);
    return 0;
}

static void ddr_adjust_config(void)
{
    uint32 dtar[CH_MAX];
    uint32 i;

    //get data training address before idle port
    ddr_get_datatraing_addr(dtar);

    call_with_single_cpu(&_ddr_adjust_config, (void*)dtar);
    //_ddr_adjust_config(dtar);
    //disable unused channel
    for(i=0;i<CH_MAX;i++)
    {
        if(p_ddr_ch[i]->mem_type != DRAM_MAX)
        {
            //FIXME
        }
    }
}

static int __ddr_change_freq(uint32_t nMHz, struct ddr_freq_t ddr_freq_t)
{
    freq_t freq;
    int ret = 0;

    freq.nMHz = nMHz;
    freq.p_ddr_freq_t = &ddr_freq_t;
    ret = call_with_single_cpu(&ddr_change_freq_sram, 
                               (void*)&freq);

    return ret;
}

static int _ddr_change_freq(uint32 nMHz)
{
	struct ddr_freq_t ddr_freq_t;
        #if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
	unsigned long remain_t, vblank_t, pass_t;
	static unsigned long reserve_t = 800;//us
	unsigned long long tmp;
	int test_count=0;
        #endif
        int ret;

	memset(&ddr_freq_t, 0x00, sizeof(ddr_freq_t));

#if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
	do
	{
		ddr_freq_t.screen_ft_us = rk_fb_get_prmry_screen_ft();
		ddr_freq_t.t0 = rk_fb_get_prmry_screen_framedone_t();
		if (!ddr_freq_t.screen_ft_us)
			return __ddr_change_freq(nMHz, ddr_freq_t);

		tmp = cpu_clock(0) - ddr_freq_t.t0;
		do_div(tmp, 1000);
		pass_t = tmp;
		//lost frame interrupt
		while (pass_t > ddr_freq_t.screen_ft_us){
			int n = pass_t/ddr_freq_t.screen_ft_us;

			//printk("lost frame int, pass_t:%lu\n", pass_t);
			pass_t -= n*ddr_freq_t.screen_ft_us;
			ddr_freq_t.t0 += n*ddr_freq_t.screen_ft_us*1000;
		}

		remain_t = ddr_freq_t.screen_ft_us - pass_t;
		if (remain_t < reserve_t) {
			//printk("remain_t(%lu) < reserve_t(%lu)\n", remain_t, reserve_t);
			vblank_t = rk_fb_get_prmry_screen_vbt();
			usleep_range(remain_t+vblank_t, remain_t+vblank_t);
			continue;
		}

		//test 10 times
		test_count++;
                if(test_count > 10)
                {
			ddr_freq_t.screen_ft_us = 0xfefefefe;
                }
		//printk("ft:%lu, pass_t:%lu, remaint_t:%lu, reservet_t:%lu\n",
		//	ddr_freq_t.screen_ft_us, (unsigned long)pass_t, remain_t, reserve_t);
		usleep_range(remain_t-reserve_t, remain_t-reserve_t);
		flush_tlb_all();

		ret = __ddr_change_freq(nMHz, ddr_freq_t);
		if (ret) {
			return ret;
		} else {
			if (reserve_t < 10000)
				reserve_t += 200;
		}
	}while(1);
#else
	ret = __ddr_change_freq(nMHz, ddr_freq_t);
#endif

	return ret;
}

static long _ddr_round_rate(uint32 nMHz)
{
	return p_ddr_set_pll(nMHz, 0);
}

static void _ddr_set_auto_self_refresh(bool en)
{
    //set auto self-refresh idle
    *kern_to_pie(rockchip_pie_chunk, &DATA(ddr_sr_idle)) = en ? SR_IDLE : 0;
}

#define PERI_ACLK_DIV_MASK 0x1f
#define PERI_ACLK_DIV_OFF 0

#define PERI_HCLK_DIV_MASK 0x3
#define PERI_HCLK_DIV_OFF 8

#define PERI_PCLK_DIV_MASK 0x3
#define PERI_PCLK_DIV_OFF 12
#if 0
static __sramdata u32 cru_sel32_sram;
static void __sramfunc ddr_suspend(void)
{
    u32 i;
    volatile u32 n;
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;
    int pll_id;

	pll_id=GET_DDR_PLL_SRC();
    /** 1. Make sure there is no host access */
    flush_cache_all();
    outer_flush_all();
    //flush_tlb_all();

    for(i=0;i<SRAM_SIZE/4096;i++)
    {
        n=temp[1024*i];
        barrier();
    }

    n= pDDR_Reg->SCFG.d32;
    n= pPHY_Reg->RIDR;
    n= pCRU_Reg->CRU_PLL_CON[0][0];
    n= pPMU_Reg->PMU_WAKEUP_CFG[0];
    n= *(volatile uint32_t *)SysSrv_DdrConf;
    n= READ_GRF_REG();
    dsb();

    ddr_selfrefresh_enter(0);

    SET_PLL_MODE(pll_id, 0);   //PLL slow-mode
    dsb();
    ddr_delayus(1);
    SET_PLL_PD(pll_id, 1);         //PLL power-down
    dsb();
    ddr_delayus(1);
    if(pll_id==GPLL)
    {
    	cru_sel32_sram=   pCRU_Reg->CRU_CLKSEL_CON[10];

    	pCRU_Reg->CRU_CLKSEL_CON[10]=CRU_W_MSK_SETBITS(0, PERI_ACLK_DIV_OFF, PERI_ACLK_DIV_MASK)
    				   | CRU_W_MSK_SETBITS(0, PERI_HCLK_DIV_OFF, PERI_HCLK_DIV_MASK)
    				   |CRU_W_MSK_SETBITS(0, PERI_PCLK_DIV_OFF, PERI_PCLK_DIV_MASK);
    }
    pPHY_Reg->DSGCR = pPHY_Reg->DSGCR&(~((0x1<<28)|(0x1<<29)));  //CKOE
}

static void __sramfunc ddr_resume(void)
{
    int delay=1000;
    int pll_id;

    pll_id=GET_DDR_PLL_SRC();
	pPHY_Reg->DSGCR = pPHY_Reg->DSGCR|((0x1<<28)|(0x1<<29));  //CKOE
	dsb();

	if(pll_id==GPLL)
	pCRU_Reg->CRU_CLKSEL_CON[10]=0xffff0000|cru_sel32_sram;

    SET_PLL_PD(pll_id, 0);         //PLL no power-down
    dsb();
    while (delay > 0)
    {
        if (GET_DPLL_LOCK_STATUS())
            break;
        ddr_delayus(1);
        delay--;
    }

    SET_PLL_MODE(pll_id, 1);   //PLL normal
    dsb();

    ddr_selfrefresh_exit();
}
#endif

//pArg:指针内容表示pll pd or not。
void ddr_reg_save(uint32 *pArg)
{
    uint32        ch;
    pDDR_REG_T    pDDR_Reg=NULL;
    pDDRPHY_REG_T pPHY_Reg=NULL;
    pMSCH_REG     pMSCH_Reg;
    
    p_ddr_reg->tag = 0x56313031;
    if(p_ddr_ch[0]->mem_type != DRAM_MAX)
    {
        p_ddr_reg->pctlAddr[0] = RK3288_DDR_PCTL0_PHYS;
        p_ddr_reg->publAddr[0] = RK3288_DDR_PUBL0_PHYS;
        p_ddr_reg->nocAddr[0] = RK3288_SERVICE_BUS_PHYS;
        pDDR_Reg = p_ddr_ch[0]->pDDR_Reg;
        pPHY_Reg = p_ddr_ch[0]->pPHY_Reg;
    }
    else
    {
        p_ddr_reg->pctlAddr[0] = 0xFFFFFFFF;
        p_ddr_reg->publAddr[0] = 0xFFFFFFFF;
        p_ddr_reg->nocAddr[0] = 0xFFFFFFFF;
    }
    if(p_ddr_ch[1]->mem_type != DRAM_MAX)
    {
        p_ddr_reg->pctlAddr[1] = RK3288_DDR_PCTL1_PHYS;
        p_ddr_reg->publAddr[1] = RK3288_DDR_PUBL1_PHYS;
        p_ddr_reg->nocAddr[1] = RK3288_SERVICE_BUS_PHYS+0x80;
        if((pDDR_Reg == NULL) || (pPHY_Reg == NULL))
        {
            pDDR_Reg = p_ddr_ch[1]->pDDR_Reg;
            pPHY_Reg = p_ddr_ch[1]->pPHY_Reg; 
        }
    }
    else
    {
        p_ddr_reg->pctlAddr[1] = 0xFFFFFFFF;
        p_ddr_reg->publAddr[1] = 0xFFFFFFFF;
        p_ddr_reg->nocAddr[1] = 0xFFFFFFFF;
    }
    
    //PCTLR    
    (fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_copy)))((uint64_t*)&(p_ddr_reg->pctl.pctl_timing.togcnt1u), (uint64_t *)&(pDDR_Reg->TOGCNT1U), 17);
    p_ddr_reg->pctl.SCFG = pDDR_Reg->SCFG.d32;
    p_ddr_reg->pctl.CMDTSTATEN = pDDR_Reg->CMDTSTATEN;
    p_ddr_reg->pctl.MCFG1 = pDDR_Reg->MCFG1;
    p_ddr_reg->pctl.MCFG = pDDR_Reg->MCFG;
    p_ddr_reg->pctl.pctl_timing.ddrFreq = *kern_to_pie(rockchip_pie_chunk, &DATA(ddr_freq));
    p_ddr_reg->pctl.DFITCTRLDELAY = pDDR_Reg->DFITCTRLDELAY;
    p_ddr_reg->pctl.DFIODTCFG = pDDR_Reg->DFIODTCFG;
    p_ddr_reg->pctl.DFIODTCFG1 = pDDR_Reg->DFIODTCFG1;
    p_ddr_reg->pctl.DFIODTRANKMAP = pDDR_Reg->DFIODTRANKMAP;
    p_ddr_reg->pctl.DFITPHYWRDATA = pDDR_Reg->DFITPHYWRDATA;
    p_ddr_reg->pctl.DFITPHYWRLAT = pDDR_Reg->DFITPHYWRLAT;
    p_ddr_reg->pctl.DFITRDDATAEN = pDDR_Reg->DFITRDDATAEN;
    p_ddr_reg->pctl.DFITPHYRDLAT = pDDR_Reg->DFITPHYRDLAT;
    p_ddr_reg->pctl.DFITPHYUPDTYPE0 = pDDR_Reg->DFITPHYUPDTYPE0;
    p_ddr_reg->pctl.DFITPHYUPDTYPE1 = pDDR_Reg->DFITPHYUPDTYPE1;
    p_ddr_reg->pctl.DFITPHYUPDTYPE2 = pDDR_Reg->DFITPHYUPDTYPE2;
    p_ddr_reg->pctl.DFITPHYUPDTYPE3 = pDDR_Reg->DFITPHYUPDTYPE3;
    p_ddr_reg->pctl.DFITCTRLUPDMIN = pDDR_Reg->DFITCTRLUPDMIN;
    p_ddr_reg->pctl.DFITCTRLUPDMAX = pDDR_Reg->DFITCTRLUPDMAX;
    p_ddr_reg->pctl.DFITCTRLUPDDLY = pDDR_Reg->DFITCTRLUPDDLY;

    p_ddr_reg->pctl.DFIUPDCFG = pDDR_Reg->DFIUPDCFG;
    p_ddr_reg->pctl.DFITREFMSKI = pDDR_Reg->DFITREFMSKI;
    p_ddr_reg->pctl.DFITCTRLUPDI = pDDR_Reg->DFITCTRLUPDI;
    p_ddr_reg->pctl.DFISTCFG0 = pDDR_Reg->DFISTCFG0;
    p_ddr_reg->pctl.DFISTCFG1 = pDDR_Reg->DFISTCFG1;
    p_ddr_reg->pctl.DFITDRAMCLKEN = pDDR_Reg->DFITDRAMCLKEN;
    p_ddr_reg->pctl.DFITDRAMCLKDIS = pDDR_Reg->DFITDRAMCLKDIS;
    p_ddr_reg->pctl.DFISTCFG2 = pDDR_Reg->DFISTCFG2;
    p_ddr_reg->pctl.DFILPCFG0 = pDDR_Reg->DFILPCFG0;

    //PUBL  
    p_ddr_reg->publ.phy_timing.dtpr0.d32 = pPHY_Reg->DTPR[0];
    (fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_copy)))((uint64_t*)&(p_ddr_reg->publ.phy_timing.dtpr1), (uint64_t *)&(pPHY_Reg->DTPR[1]), 3);
    p_ddr_reg->publ.PIR = pPHY_Reg->PIR;
    p_ddr_reg->publ.PGCR = pPHY_Reg->PGCR;
    p_ddr_reg->publ.DLLGCR = pPHY_Reg->DLLGCR;
    p_ddr_reg->publ.ACDLLCR = pPHY_Reg->ACDLLCR;
    p_ddr_reg->publ.PTR[0] = pPHY_Reg->PTR[0];
    p_ddr_reg->publ.PTR[1] = pPHY_Reg->PTR[1];
    p_ddr_reg->publ.PTR[2] = pPHY_Reg->PTR[2];
    p_ddr_reg->publ.ACIOCR = pPHY_Reg->ACIOCR;
    p_ddr_reg->publ.DXCCR = pPHY_Reg->DXCCR;
    p_ddr_reg->publ.DSGCR = pPHY_Reg->DSGCR;
    p_ddr_reg->publ.DCR = pPHY_Reg->DCR.d32;
    p_ddr_reg->publ.ODTCR = pPHY_Reg->ODTCR;
    p_ddr_reg->publ.DTAR = pPHY_Reg->DTAR;
    p_ddr_reg->publ.ZQ0CR0 = (pPHY_Reg->ZQ0SR[0] & 0x0FFFFFFF) | (0x1<<28);
    p_ddr_reg->publ.ZQ1CR0 = (pPHY_Reg->ZQ1SR[0] & 0x0FFFFFFF) | (0x1<<28);

    for(ch=0;ch<CH_MAX;ch++)
    {
        if(p_ddr_ch[0]->mem_type != DRAM_MAX)
        {
            pPHY_Reg = p_ddr_ch[ch]->pPHY_Reg;         
            p_ddr_reg->dqs[ch].DX0GCR = pPHY_Reg->DATX8[0].DXGCR;
            p_ddr_reg->dqs[ch].DX0DLLCR = pPHY_Reg->DATX8[0].DXDLLCR;
            p_ddr_reg->dqs[ch].DX0DQTR = pPHY_Reg->DATX8[0].DXDQTR;
            p_ddr_reg->dqs[ch].DX0DQSTR = pPHY_Reg->DATX8[0].DXDQSTR;

            p_ddr_reg->dqs[ch].DX1GCR = pPHY_Reg->DATX8[1].DXGCR;
            p_ddr_reg->dqs[ch].DX1DLLCR = pPHY_Reg->DATX8[1].DXDLLCR;
            p_ddr_reg->dqs[ch].DX1DQTR = pPHY_Reg->DATX8[1].DXDQTR;
            p_ddr_reg->dqs[ch].DX1DQSTR = pPHY_Reg->DATX8[1].DXDQSTR;

            p_ddr_reg->dqs[ch].DX2GCR = pPHY_Reg->DATX8[2].DXGCR;
            p_ddr_reg->dqs[ch].DX2DLLCR = pPHY_Reg->DATX8[2].DXDLLCR;
            p_ddr_reg->dqs[ch].DX2DQTR = pPHY_Reg->DATX8[2].DXDQTR;
            p_ddr_reg->dqs[ch].DX2DQSTR = pPHY_Reg->DATX8[2].DXDQSTR;

            p_ddr_reg->dqs[ch].DX3GCR = pPHY_Reg->DATX8[3].DXGCR;
            p_ddr_reg->dqs[ch].DX3DLLCR = pPHY_Reg->DATX8[3].DXDLLCR;
            p_ddr_reg->dqs[ch].DX3DQTR = pPHY_Reg->DATX8[3].DXDQTR;
            p_ddr_reg->dqs[ch].DX3DQSTR = pPHY_Reg->DATX8[3].DXDQSTR;

            //NOC
            pMSCH_Reg= p_ddr_ch[ch]->pMSCH_Reg;        
            p_ddr_reg->noc[ch].ddrconf = pMSCH_Reg->ddrconf;
            p_ddr_reg->noc[ch].ddrtiming.d32 = pMSCH_Reg->ddrtiming.d32;
            p_ddr_reg->noc[ch].ddrmode = pMSCH_Reg->ddrmode;
            p_ddr_reg->noc[ch].readlatency = pMSCH_Reg->readlatency;
            p_ddr_reg->noc[ch].activate.d32 = pMSCH_Reg->activate.d32;
            p_ddr_reg->noc[ch].devtodev = pMSCH_Reg->devtodev;
        }
    }

    //PLLPD
    p_ddr_reg->pllpdAddr = (uint32_t)pArg;  //pll power-down tag addr
    p_ddr_reg->pllpdMask = 1;
    p_ddr_reg->pllpdVal = 1;

    //DPLL
    p_ddr_reg->dpllmodeAddr = RK3288_CRU_PHYS + 0x50;  //APCRU_MODE_CON
    p_ddr_reg->dpllSlowMode = ((3<<4)<<16) | (0<<4);
    p_ddr_reg->dpllNormalMode = ((3<<4)<<16) | (1<<4);
    p_ddr_reg->dpllResetAddr = RK3288_CRU_PHYS + 0x1c; //APCRU_DPLL_CON3
    p_ddr_reg->dpllReset = (((0x1<<5)<<16) | (0x1<<5));
    p_ddr_reg->dpllDeReset = (((0x1<<5)<<16) | (0x0<<5));
    p_ddr_reg->dpllConAddr = RK3288_CRU_PHYS + 0x10;   //APCRU_DPLL_CON0
    p_ddr_reg->dpllCon[0] = pCRU_Reg->CRU_PLL_CON[DPLL][0] | (0xFFFF<<16);
    p_ddr_reg->dpllCon[1] = pCRU_Reg->CRU_PLL_CON[DPLL][1] | (0xFFFF<<16);
    p_ddr_reg->dpllCon[2] = pCRU_Reg->CRU_PLL_CON[DPLL][2] | (0xFFFF<<16);
    p_ddr_reg->dpllCon[3] = pCRU_Reg->CRU_PLL_CON[DPLL][3] | (0xFFFF<<16);
    p_ddr_reg->dpllLockAddr = RK3288_GRF_PHYS + 0x284;  //GRF_SOC_STATUS1
    p_ddr_reg->dpllLockMask = (1<<5);
    p_ddr_reg->dpllLockVal = (1<<5);

    //SET_DDR_PLL_SRC
    p_ddr_reg->ddrPllSrcDivAddr = RK3288_CRU_PHYS + 0xc8;
    p_ddr_reg->ddrPllSrcDiv = (pCRU_Reg->CRU_CLKSEL_CON[26] & 0x7) | (0x7<<16);

    p_ddr_reg->retenDisAddr = RK3288_PMU_PHYS+0x18;  //pmu_pwrmode_con
    p_ddr_reg->retenDisVal = (3<<21);  //OR operation
    p_ddr_reg->retenStAddr = RK3288_PMU_PHYS+0x1c;  //pmu_pwrmode_con
    p_ddr_reg->retenStMask = (1<<6);
    p_ddr_reg->retenStVal = (0<<6);

    p_ddr_reg->grfRegCnt = 3;
    //DDR_16BIT,DDR_HW_WAKEUP,DDR_TYPE
    p_ddr_reg->grf[0].addr = RK3288_GRF_PHYS + 0x244;
    p_ddr_reg->grf[0].val = (pGRF_Reg->GRF_SOC_CON[0] & ((0x3<<8)|(0x3<<5)|(0x3<<3))) | (((0x3<<8)|(0x3<<5)|(0x3<<3))<<16);
    
    //LPDDR_TYPE
    p_ddr_reg->grf[1].addr = RK3288_GRF_PHYS + 0x24c;
    p_ddr_reg->grf[1].val = (pGRF_Reg->GRF_SOC_CON[2] & (0x3f<<8)) | ((0x3f<<8)<<16);

    //STRIDE
    p_ddr_reg->grf[2].addr = RK3288_SGRF_PHYS + 0x8;
    p_ddr_reg->grf[2].val = READ_DDR_STRIDE() | (0x1F<<16);

    p_ddr_reg->endTag = 0xFFFFFFFF;
}

__attribute__((aligned(4)))   uint32 ddr_reg_resume[]=
{
#include "ddr_reg_resume.inc"
};

char * ddr_get_resume_code_info(u32 *size)
{
    *size=sizeof(ddr_reg_resume);
    
    return (char *)ddr_reg_resume;

}
EXPORT_SYMBOL(ddr_get_resume_code_info);

char * ddr_get_resume_data_info(u32 *size)
{
    *size=sizeof(DATA(ddr_reg));
    return (char *) kern_to_pie(rockchip_pie_chunk, &DATA(ddr_reg));
}
EXPORT_SYMBOL(ddr_get_resume_data_info);


static int ddr_init(uint32 dram_speed_bin, uint32 freq)
{
    uint32 tmp;
    uint32 die=1;
    uint32 gsr,dqstr;
    struct clk *clk;
    uint32 ch,cap=0,cs_cap;

    ddr_print("version 1.00 20140603 \n");

    p_ddr_reg = kern_to_pie(rockchip_pie_chunk, &DATA(ddr_reg));
    p_ddr_set_pll = fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_set_pll));
    DATA(p_cpu_pause) = kern_to_pie(rockchip_pie_chunk, &DATA(cpu_pause[0]));

    tmp = clk_get_rate(clk_get(NULL, "clk_ddr"))/1000000;
    *kern_to_pie(rockchip_pie_chunk, &DATA(ddr_freq)) = tmp;
    *kern_to_pie(rockchip_pie_chunk, &DATA(ddr_sr_idle)) = 0;
    
    for(ch=0;ch<CH_MAX;ch++)
    {
        p_ddr_ch[ch] = kern_to_pie(rockchip_pie_chunk, &DATA(ddr_ch[ch]));
        
        p_ddr_ch[ch]->chNum = ch;
        p_ddr_ch[ch]->pDDR_Reg = pDDR_REG(ch);
        p_ddr_ch[ch]->pPHY_Reg = pPHY_REG(ch);
        p_ddr_ch[ch]->pMSCH_Reg = pMSCH_REG(ch);

        if(!(READ_CH_INFO()&(1<<ch)))
        {
            p_ddr_ch[ch]->mem_type = DRAM_MAX;
            continue;
        }
        else
        {
            if(ch)
            {
                ddr_print("Channel b: \n");
            }
            else
            {
                ddr_print("Channel a: \n");
            }
            tmp = p_ddr_ch[ch]->pPHY_Reg->DCR.b.DDRMD;
            if((tmp ==  LPDDR2) && (READ_DRAMTYPE_INFO() == 6))
            {
                tmp = LPDDR3;
            }
            switch(tmp)
            {
                case DDR3:
                    ddr_print("DDR3 Device\n");
                    break;
                case LPDDR3:
                    ddr_print("LPDDR3 Device\n");
                    break;
                case LPDDR2:
                    ddr_print("LPDDR2 Device\n");
                    break;
                default:
                    ddr_print("Unkown Device\n");
                    tmp = DRAM_MAX;
                    break;
            }
            p_ddr_ch[ch]->mem_type = tmp;
            if(tmp == DRAM_MAX)
            {
                p_ddr_ch[ch]->mem_type = DRAM_MAX;
                continue;
            }
        }
        
        p_ddr_ch[ch]->ddr_speed_bin = dram_speed_bin;
        //get capability per chip, not total size, used for calculate tRFC
        die = (8<<READ_BW_INFO(ch))/(8<<READ_DIE_BW_INFO(ch));
        cap = (1 << (READ_ROW_INFO(ch,0)+READ_COL_INFO(ch)+READ_BK_INFO(ch)+READ_BW_INFO(ch)));
        cs_cap = cap;
        if(READ_CS_INFO(ch) > 1)
        {
            cap += cap >> (READ_ROW_INFO(ch,0)-READ_ROW_INFO(ch,1));
        }
        if(READ_CH_ROW_INFO(ch))
        {
            cap = cap*3/4;
        }
        p_ddr_ch[ch]->ddr_capability_per_die = cs_cap/die;
        ddr_print("Bus Width=%d Col=%d Bank=%d Row=%d CS=%d Total Capability=%dMB\n",
                                                                        READ_BW_INFO(ch)*16,\
                                                                        READ_COL_INFO(ch), \
                                                                        (0x1<<(READ_BK_INFO(ch))), \
                                                                        READ_ROW_INFO(ch,0), \
                                                                        READ_CS_INFO(ch), \
                                                                        (cap>>20));
    }
    
    ddr_adjust_config();

    clk = clk_get(NULL, "clk_ddr");
    if (IS_ERR(clk)) {
        ddr_print("failed to get ddr clk\n");
        clk = NULL;
    }
    if(freq != 0)
        tmp = clk_set_rate(clk, 1000*1000*freq);
    else
        tmp = clk_set_rate(clk, clk_get_rate(clk));
    ddr_print("init success!!! freq=%luMHz\n", clk ? clk_get_rate(clk)/1000000 : freq);

    for(ch=0;ch<CH_MAX;ch++)
    {
        if(p_ddr_ch[ch]->mem_type != DRAM_MAX)
        {            
            if(ch)
            {
                ddr_print("Channel b: \n");
            }
            else
            {
                ddr_print("Channel a: \n");
            }
            for(tmp=0;tmp<4;tmp++)
            {
                gsr = p_ddr_ch[ch]->pPHY_Reg->DATX8[tmp].DXGSR[0];
                dqstr = p_ddr_ch[ch]->pPHY_Reg->DATX8[tmp].DXDQSTR;
                ddr_print("DTONE=0x%x, DTERR=0x%x, DTIERR=0x%x, DTPASS=%d,%d, DGSL=%d,%d extra clock, DGPS=%d,%d\n", \
                                                                    (gsr&0xF), ((gsr>>4)&0xF), ((gsr>>8)&0xF), \
                                                                    ((gsr>>13)&0x7), ((gsr>>16)&0x7),\
                                                                    (dqstr&0x7), ((dqstr>>3)&0x7),\
                                                                    ((((dqstr>>12)&0x3)+1)*90), ((((dqstr>>14)&0x3)+1)*90));
            }
            ddr_print("ZERR=%x, ZDONE=%x, ZPD=0x%x, ZPU=0x%x, OPD=0x%x, OPU=0x%x\n", \
                                                        (p_ddr_ch[ch]->pPHY_Reg->ZQ0SR[0]>>30)&0x1, \
                                                        (p_ddr_ch[ch]->pPHY_Reg->ZQ0SR[0]>>31)&0x1, \
                                                        p_ddr_ch[ch]->pPHY_Reg->ZQ0SR[1]&0x3,\
                                                        (p_ddr_ch[ch]->pPHY_Reg->ZQ0SR[1]>>2)&0x3,\
                                                        (p_ddr_ch[ch]->pPHY_Reg->ZQ0SR[1]>>4)&0x3,\
                                                        (p_ddr_ch[ch]->pPHY_Reg->ZQ0SR[1]>>6)&0x3);
            ddr_print("DRV Pull-Up=0x%x, DRV Pull-Dwn=0x%x\n", p_ddr_ch[ch]->pPHY_Reg->ZQ0SR[0]&0x1F, (p_ddr_ch[ch]->pPHY_Reg->ZQ0SR[0]>>5)&0x1F);
            ddr_print("ODT Pull-Up=0x%x, ODT Pull-Dwn=0x%x\n", (p_ddr_ch[ch]->pPHY_Reg->ZQ0SR[0]>>10)&0x1F, (p_ddr_ch[ch]->pPHY_Reg->ZQ0SR[0]>>15)&0x1F);
        }
    }

    return 0;
}

