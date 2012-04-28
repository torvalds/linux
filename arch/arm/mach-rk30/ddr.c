/*
 * arch/arm/mach-rk30/ddr.c
 *
 * Function Driver for DDR controller
 *
 * Copyright (C) 2011 Fuzhou Rockchip Electronics Co.,Ltd
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

#include <mach/sram.h>
#include <mach/ddr.h>

typedef uint32_t uint32;
#define PMU_BASE_ADDR           RK30_PMU_BASE
#define SDRAMC_BASE_ADDR        RK30_DDR_PCTL_BASE
#define DDR_PUBL_BASE           RK30_DDR_PUBL_BASE
#define CRU_BASE_ADDR           RK30_CRU_BASE
#define REG_FILE_BASE_ADDR      RK30_GRF_BASE
#define SysSrv_DdrConf          (RK30_CPU_AXI_BUS_BASE+0x08)
#define SysSrv_DdrTiming        (RK30_CPU_AXI_BUS_BASE+0x0c)
#define SysSrv_DdrMode          (RK30_CPU_AXI_BUS_BASE+0x10)
#define SysSrv_ReadLatency      (RK30_CPU_AXI_BUS_BASE+0x14)

#define ddr_print(x...) printk( "DDR DEBUG: " x )

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
 * DDR3 define
 ***********************************/
//mr0 for ddr3
#define DDR3_BL8          (0)
#define DDR3_BC4_8        (1)
#define DDR3_BC4          (2)
#define DDR3_CL(n)        (((((n)-4)&0x7)<<4)|((((n)-4)&0x8)>>1))
#define DDR3_WR(n)        (((n)&0x7)<<9)
#define DDR3_MR0_DLL_RESET    (1<<8)
#define DDR3_MR0_DLL_NOR   (0<<8)
    
    //mr1 for ddr3
#define DDR3_MR1_AL(n)  (((n)&0x7)<<3)
    
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
    
//EMR;                    //Extended Mode Register      
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


//PMU_MISC_CON1
#define idle_req_cpu_cfg    (1<<1)
#define idle_req_peri_cfg   (1<<2)
#define idle_req_gpu_cfg    (1<<3)
#define idle_req_video_cfg  (1<<4)
#define idle_req_vio_cfg    (1<<5)

//PMU_PWRDN_ST
#define idle_cpu    (1<<26)
#define idle_peri   (1<<25)
#define idle_gpu    (1<<24)
#define idle_video  (1<<23)
#define idle_vio    (1<<22)

//PMU registers
typedef volatile struct tagPMU_FILE
{
    uint32 PMU_WAKEUP_CFG[2];
    uint32 PMU_PWRDN_CON;
    uint32 PMU_PWRDN_ST;
    uint32 PMU_INT_CON;
    uint32 PMU_INT_ST;
    uint32 PMU_MISC_CON;
    uint32 PMU_OSC_CNT;
    uint32 PMU_PLL_CNT;
    uint32 PMU_PMU_CNT;
    uint32 PMU_DDRIO_PWRON_CNT;
    uint32 PMU_WAKEUP_RST_CLR_CNT;
    uint32 PMU_SCU_PWRDWN_CNT;
    uint32 PMU_SCU_PWRUP_CNT;
    uint32 PMU_MISC_CON1;
    uint32 PMU_GPIO6_CON;
    uint32 PMU_PMU_SYS_REG[4];
} PMU_FILE, *pPMU_FILE;

#define pPMU_Reg ((pPMU_FILE)PMU_BASE_ADDR)

#define PLL_RESET  (((0x1<<5)<<16) | (0x1<<5))
#define PLL_DE_RESET  (((0x1<<5)<<16) | (0x0<<5))
#define NR(n)      ((0x3F<<(8+16)) | (((n)-1)<<8))
#define NO(n)      ((0xF<<16) | ((n)-1))
#define NF(n)      ((0x1FFF<<16) | ((n)-1))
#define NB(n)      ((0xFFF<<16) | ((n)-1))
 //CRU Registers
typedef volatile struct tagCRU_STRUCT
{
    uint32 CRU_PLL_CON[4][4];
    uint32 CRU_MODE_CON;
    uint32 CRU_CLKSEL_CON[35];
    uint32 CRU_CLKGATE_CON[10];
    uint32 reserved1[2];
    uint32 CRU_GLB_SRST_FST_VALUE;
    uint32 CRU_GLB_SRST_SND_VALUE;
    uint32 reserved2[2];
    uint32 CRU_SOFTRST_CON[9];
    uint32 CRU_MISC_CON;
    uint32 reserved3[2];
    uint32 CRU_GLB_CNT_TH;
} CRU_REG, *pCRU_REG;

#define pCRU_Reg ((pCRU_REG)CRU_BASE_ADDR)

#define bank2_to_rank_en   ((1<<2) | ((1<<2)<<16))
#define bank2_to_rank_dis   ((0<<2) | ((1<<2)<<16))
#define rank_to_row15_en   ((1<<1) | ((1<<1)<<16))
#define rank_to_row15_dis   ((0<<1) | ((1<<1)<<16))

typedef struct tagGPIO_LH
{
    uint32 GPIOL;
    uint32 GPIOH;
}GPIO_LH_T;

typedef struct tagGPIO_IOMUX
{
    uint32 GPIOA_IOMUX;
    uint32 GPIOB_IOMUX;
    uint32 GPIOC_IOMUX;
    uint32 GPIOD_IOMUX;
}GPIO_IOMUX_T;

//REG FILE registers
typedef volatile struct tagREG_FILE
{
    GPIO_LH_T GRF_GPIO_DIR[7];
    GPIO_LH_T GRF_GPIO_DO[7];
    GPIO_LH_T GRF_GPIO_EN[7];
    GPIO_IOMUX_T GRF_GPIO_IOMUX[7];
    GPIO_LH_T GRF_GPIO_PULL[7];
    uint32 GRF_SOC_CON[3];
    uint32 GRF_SOC_STATUS0;
    uint32 GRF_DMAC1_CON[3];
    uint32 GRF_DMAC2_CON[4];
    uint32 GRF_UOC0_CON[3];
    uint32 GRF_UOC1_CON[4];
    uint32 GRF_DDRC_CON0;
    uint32 GRF_DDRC_STAT;
    uint32 reserved[(0x1c8-0x1a0)/4];
    uint32 GRF_OS_REG[4];
} REG_FILE, *pREG_FILE;

#define pGRF_Reg ((pREG_FILE)REG_FILE_BASE_ADDR)

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

#define pDDR_Reg ((pDDR_REG_T)SDRAMC_BASE_ADDR)

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

#define pPHY_Reg ((pDDRPHY_REG_T)DDR_PUBL_BASE)

typedef enum DRAM_TYPE_Tag
{
    LPDDR = 0,
    DDR,
    DDR2,
    DDR3,
    LPDDR2,

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
}PHY_TIMING_T;

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

typedef struct DDR_TIMING_Tag
{
    PCTL_TIMING_T  pctl_timing;
    PHY_TIMING_T   phy_timing;
    NOC_TIMING_T   noc_timing;
}DDR_TIMING_T;

__sramdata DDR_TIMING_T ddr_timing;

typedef struct DDR_CONFIG_2_RBC_Tag
{
    unsigned int row;
    unsigned int bank;
    unsigned int col;
}DDR_CONFIG_2_RBC_T;

DDR_CONFIG_2_RBC_T  ddr_cfg_2_rbc[16] = 
{
    {15,3,11},  // bank ahead
    {15,3,10},
    {14,3,10},
    {13,3,10},
    {15,3,11},
    {14,3,11},
    {13,3,11},
    {14,3,9},
    {13,3,9},
    {15,2,11},
    {14,2,11},
    {15,2,10},
    {14,2,10},
    {14,2,9},
    {13,2,9},
    {15,3,10}   // bank ahead
};

uint32_t ddrDataTraining[32];

uint32_t __sramdata ddr3_cl_cwl[22][4]={
/*   0~330           330~400         400~533        speed
* tCK  >3             2.5~3          1.875~2.5     1.875~1.5
*    cl<<16, cwl    cl<<16, cwl     cl<<16, cwl              */
    {((5<<16)|5),   ((5<<16)|5),    0          ,   0}, //DDR3_800D
    {((5<<16)|5),   ((6<<16)|5),    0          ,   0}, //DDR3_800E

    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6),   0}, //DDR3_1066E
    {((5<<16)|5),   ((6<<16)|5),    ((7<<16)|6),   0}, //DDR3_1066F
    {((5<<16)|5),   ((6<<16)|5),    ((8<<16)|6),   0}, //DDR3_1066G

    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6),   ((7<<16)|7)}, //DDR3_1333F
    {((5<<16)|5),   ((5<<16)|5),    ((7<<16)|6),   ((8<<16)|7)}, //DDR3_1333G
    {((5<<16)|5),   ((6<<16)|5),    ((7<<16)|6),   ((9<<16)|7)}, //DDR3_1333H
    {((5<<16)|5),   ((6<<16)|5),    ((8<<16)|6),   ((10<<16)|7)}, //DDR3_1333J

    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6),   ((7<<16)|7)}, //DDR3_1600G
    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6),   ((8<<16)|7)}, //DDR3_1600H
    {((5<<16)|5),   ((5<<16)|5),    ((7<<16)|6),   ((9<<16)|7)}, //DDR3_1600J
    {((5<<16)|5),   ((6<<16)|5),    ((7<<16)|6),   ((10<<16)|7)}, //DDR3_1600K

    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6),   ((8<<16)|7)}, //DDR3_1866J
    {((5<<16)|5),   ((5<<16)|5),    ((7<<16)|6),   ((8<<16)|7)}, //DDR3_1866K
    {((6<<16)|5),   ((6<<16)|5),    ((7<<16)|6),   ((9<<16)|7)}, //DDR3_1866L
    {((6<<16)|5),   ((6<<16)|5),    ((8<<16)|6),   ((10<<16)|7)}, //DDR3_1866M

    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6),   ((7<<16)|7)}, //DDR3_2133K
    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6),   ((8<<16)|7)}, //DDR3_2133L
    {((5<<16)|5),   ((5<<16)|5),    ((7<<16)|6),   ((9<<16)|7)}, //DDR3_2133M
    {((6<<16)|5),   ((6<<16)|5),    ((7<<16)|6),   ((9<<16)|7)},  //DDR3_2133N

    {((6<<16)|5),   ((6<<16)|5),    ((8<<16)|6),   ((10<<16)|7)} //DDR3_DEFAULT

};
uint32_t __sramdata ddr3_tRC_tFAW[22]={
/**    tRC    tFAW   */
    ((50<<16)|50), //DDR3_800D
    ((53<<16)|50), //DDR3_800E

    ((49<<16)|50), //DDR3_1066E
    ((51<<16)|50), //DDR3_1066F
    ((53<<16)|50), //DDR3_1066G

    ((47<<16)|45), //DDR3_1333F
    ((48<<16)|45), //DDR3_1333G
    ((50<<16)|45), //DDR3_1333H
    ((51<<16)|45), //DDR3_1333J

    ((45<<16)|40), //DDR3_1600G
    ((47<<16)|40), //DDR3_1600H
    ((48<<16)|40), //DDR3_1600J
    ((49<<16)|40), //DDR3_1600K

    ((45<<16)|35), //DDR3_1866J
    ((46<<16)|35), //DDR3_1866K
    ((47<<16)|35), //DDR3_1866L
    ((48<<16)|35), //DDR3_1866M

    ((44<<16)|35), //DDR3_2133K
    ((45<<16)|35), //DDR3_2133L
    ((46<<16)|35), //DDR3_2133M
    ((47<<16)|35), //DDR3_2133N

    ((53<<16)|50)  //DDR3_DEFAULT
};
static __sramdata uint32_t mem_type;    // 0:DDR2, 1:DDR3, 2:LPDDR
static __sramdata uint32_t ddr_type;    // used for ddr3 only
static __sramdata uint32_t capability;  // one chip cs capability

static __sramdata uint32_t al;
static __sramdata uint32_t bl;
static __sramdata uint32_t cl;
static __sramdata uint32_t cwl;

static __sramdata uint32_t ddr_freq;

/****************************************************************************
Internal sram us delay function
Cpu highest frequency is 1.2 GHz
1 cycle = 1/1.2 ns
1 us = 1000 ns = 1000 * 1.2 cycles = 1200 cycles
*****************************************************************************/
static __sramdata uint32_t loops_per_us;

#define LPJ_100MHZ  499728UL

/*static*/ void __sramlocalfunc delayus(uint32_t us)
{   
    uint32_t count;
     
    count = loops_per_us*us;
    while(count--)  // 3 cycles
        barrier();
}

static __sramfunc void copy(uint32 *pDest, uint32 *pSrc, uint32 words)
{
    uint32 i;

    for(i=0; i<words; i++)
    {
        pDest[i] = pSrc[i];
    }
}

static uint32 get_row(void)
{
    uint32 i;
    uint32 row;

    i = *(volatile uint32*)SysSrv_DdrConf;
    row = ddr_cfg_2_rbc[i].row;
    if(pGRF_Reg->GRF_SOC_CON[2] &  (1<<1))
    {
        row += 1;
    }
    return row;
}

static uint32 get_bank(void)
{
    uint32 i;

    i = *(volatile uint32*)SysSrv_DdrConf;
    return ddr_cfg_2_rbc[i].bank;
}

static uint32 get_col(void)
{
    uint32 i;

    i = *(volatile uint32*)SysSrv_DdrConf;
    return ddr_cfg_2_rbc[i].col;
}

static uint32_t get_datatraing_addr(void)
{
    uint32_t          value=0;
    uint32_t          addr;
    uint32_t          col = 0;
    uint32_t          row = 0;
    uint32_t          bank = 0;
    
    // caculate aglined physical address 
    addr =  __pa((unsigned long)ddrDataTraining);
    if(addr&0x3F)
    {
        addr += (64-(addr&0x3F));
    }
    addr -= 0x60000000;
    // find out col，row，bank
    row = get_row();
    bank = get_bank();
    col = get_col();
    // according different address mapping, caculate DTAR register value
    switch(*(volatile uint32*)SysSrv_DdrConf)
    {
        case 0:
        case 15:
            value |= (addr>>2) & ((0x1<<col)-1);  // col
            value |= ((addr>>(2+col)) & ((0x1<<row)-1)) << 12;  // row
            value |= ((addr>>(2+col+row)) & ((0x1<<bank)-1)) << 28;  // bank
            break;
        default:
            value |= (addr>>2) & ((0x1<<col)-1);  // col
            value |= ((addr>>(2+col+bank)) & ((0x1<<row)-1)) << 12;  // row
            value |= ((addr>>(2+col)) & ((0x1<<bank)-1)) << 28;  // bank
            break;
    }

    return value;
}

static __sramfunc void idle_port(void)
{
    pPMU_Reg->PMU_MISC_CON1 = (pPMU_Reg->PMU_MISC_CON1 & (~(0x1F<<1)))
                                        | idle_req_cpu_cfg
                                        | idle_req_peri_cfg
                                        | idle_req_gpu_cfg
                                        | idle_req_video_cfg
                                        | idle_req_vio_cfg;
    while(((pPMU_Reg->PMU_PWRDN_ST) & (idle_peri
                                        | idle_gpu
                                        | idle_cpu
                                        | idle_video
                                        | idle_vio)) != (idle_peri
                                        | idle_gpu
                                        | idle_cpu
                                        | idle_video
                                        | idle_vio));
}


static __sramfunc void deIdle_port(void)
{
    pPMU_Reg->PMU_MISC_CON1 &= ~(idle_req_peri_cfg
                                 | idle_req_gpu_cfg
                                 | idle_req_cpu_cfg
                                 | idle_req_video_cfg
                                 | idle_req_vio_cfg);
}

static __sramlocalfunc void reset_dll(void)
{
    pPHY_Reg->ACDLLCR &= ~0x40000000;
    pPHY_Reg->DATX8[0].DXDLLCR &= ~0x40000000;
    pPHY_Reg->DATX8[1].DXDLLCR &= ~0x40000000;
    pPHY_Reg->DATX8[2].DXDLLCR &= ~0x40000000;
    pPHY_Reg->DATX8[3].DXDLLCR &= ~0x40000000;
    delayus(10);
    pPHY_Reg->ACDLLCR |= 0x40000000;
    pPHY_Reg->DATX8[0].DXDLLCR |= 0x40000000;
    pPHY_Reg->DATX8[1].DXDLLCR |= 0x40000000;
    pPHY_Reg->DATX8[2].DXDLLCR |= 0x40000000;
    pPHY_Reg->DATX8[3].DXDLLCR |= 0x40000000;
    delayus(10);
}
static __sramfunc void move_to_Lowpower_state(void)
{
    volatile uint32 value;

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
                while((pDDR_Reg->STAT.b.ctl_stat) != Config);
            case Config:
                pDDR_Reg->SCTL = GO_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Access);
            case Access:
                pDDR_Reg->SCTL = SLEEP_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Low_power);
                break;
            default:  //Transitional state
                break;
        }
    }
}

static __sramfunc void move_to_Access_state(void)
{
    volatile uint32 value;

    while(1)
    {
        value = pDDR_Reg->STAT.b.ctl_stat;
        if(value == Access)
        {
            break;
        }
        switch(value)

        {
            case Low_power:
                pDDR_Reg->SCTL = WAKEUP_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Access);
                while((pPHY_Reg->PGSR & DLDONE) != DLDONE);  //wait DLL lock
                break;
            case Init_mem:
                pDDR_Reg->SCTL = CFG_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Config);
            case Config:
                pDDR_Reg->SCTL = GO_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Access);
                break;
            default:  //Transitional state
                break;
        }
    }
}

static __sramfunc void move_to_Config_state(void)
{
    volatile uint32 value;

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
                while((pDDR_Reg->STAT.b.ctl_stat) != Access);
                while((pPHY_Reg->PGSR & DLDONE) != DLDONE);  //wait DLL lock
            case Access:
            case Init_mem:
                pDDR_Reg->SCTL = CFG_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Config);
                break;
            default:  //Transitional state
                break;
        }
    }
}

//arg包括bank_addr和cmd_addr
static void __sramlocalfunc send_command(uint32 rank, uint32 cmd, uint32 arg)
{
    uint32 i;
    pDDR_Reg->MCMD = (start_cmd | (rank<<20) | arg | cmd);
    for (i = 0; i < 10; i ++) {;}
    while(pDDR_Reg->MCMD & start_cmd);
}

//对type类型的DDR的几个cs进行DTT
//0  DTT成功
//!0 DTT失败
static uint32_t __sramlocalfunc data_training(void)
{
    uint32 i,value;

    value = pDDR_Reg->TREFI;
    pDDR_Reg->TREFI = 0;
    
    pPHY_Reg->PIR = INIT | CLRSR;
    delayus(1);
    pPHY_Reg->PIR = INIT | QSTRN | LOCKBYP | ZCALBYP | CLRSR;
    for (i = 0; i < 10; i ++) {;}
    while((pPHY_Reg->PGSR & (IDONE | DTDONE)) != (IDONE | DTDONE));
    
    pDDR_Reg->TREFI = value>>3;
    delayus(20);
    pDDR_Reg->TREFI = value;

    if(pPHY_Reg->PGSR & DTERR)
    {
        return (-1);
    }
    else
    {
        return 0;
    }
}

static void __sramlocalfunc phy_dll_bypass_set(uint32 freq)
{
    if(freq<=150)
    {
        pPHY_Reg->DLLGCR &= ~(1<<23);
        pPHY_Reg->ACDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[0].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[1].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[2].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[3].DXDLLCR |= 0x80000000;
    }
    else if(freq<=250)
    {
        pPHY_Reg->DLLGCR |= (1<<23);
        pPHY_Reg->ACDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[0].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[1].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[2].DXDLLCR |= 0x80000000;
        pPHY_Reg->DATX8[3].DXDLLCR |= 0x80000000;
    }
    else
    {
        pPHY_Reg->DLLGCR &= ~(1<<23);
        pPHY_Reg->ACDLLCR &= ~0x80000000;
        pPHY_Reg->DATX8[0].DXDLLCR &= ~0x80000000;
        pPHY_Reg->DATX8[1].DXDLLCR &= ~0x80000000;
        pPHY_Reg->DATX8[2].DXDLLCR &= ~0x80000000;
        pPHY_Reg->DATX8[3].DXDLLCR &= ~0x80000000;
    }
}

static __sramdata uint32_t clkr;
static __sramdata uint32_t clkf;
static __sramdata uint32_t clkod;
/*****************************************
NR   NO     NF               Fout                       freq Step     finally use
1    8      12.5 - 62.5      37.5MHz  - 187.5MHz        3MHz          50MHz   <= 150MHz
1    6      12.5 - 62.5      50MHz    - 250MHz          4MHz          150MHz  <= 200MHz
1    4      12.5 - 62.5      75MHz    - 375MHz          6MHz          200MHz  <= 300MHz
1    2      12.5 - 62.5      150MHz   - 750MHz          12MHz         300MHz  <= 600MHz
1    1      12.5 - 62.5      300MHz   - 1500MHz         24MHz         600MHz  <= 1200MHz      
******************************************/
static uint32_t __sramlocalfunc ddr_set_pll(uint32_t nMHz, uint32_t set)
{
    uint32_t ret = 0;
    int delay = 1000;
    uint32_t pll_id=1;  //DPLL
    //NO一定要偶数,NR尽量小，jitter就会小
    
    if(nMHz == 24)
    {
        ret = 24;
        goto out;
    }
    
    if(!set)
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
    else
    {
        pCRU_Reg->CRU_MODE_CON = (0x3<<((pll_id*4) +  16)) | (0x0<<(pll_id*4));            //PLL slow-mode
    
        pCRU_Reg->CRU_PLL_CON[pll_id][3] = PLL_RESET;
        pCRU_Reg->CRU_PLL_CON[pll_id][0] = NR(clkr) | NO(clkod);
        pCRU_Reg->CRU_PLL_CON[pll_id][1] = NF(clkf);
        pCRU_Reg->CRU_PLL_CON[pll_id][2] = NB(clkf>>1);
        delayus(1);
        pCRU_Reg->CRU_PLL_CON[pll_id][3] = PLL_DE_RESET;

        while (delay > 0) 
        {
    	    delayus(1);
    		//if (pGRF_Reg->GRF_SOC_STATUS0 & (0x1<<4))
    		//	break;
    		delay--;
    	}
        
        pCRU_Reg->CRU_CLKSEL_CON[26] = ((0x3 | (0x1<<8))<<16)
                                                  | (0x0<<8)     //clk_ddr_src = DDR PLL
                                                  | 0;           //clk_ddr_src:clk_ddrphy = 1:1
        
        pCRU_Reg->CRU_MODE_CON = (0x3<<((pll_id*4) +  16))  | (0x1<<(pll_id*4));            //PLL normal
    }
out:
    return ret;
}

static __sramfunc void ddr_adjust_config(uint32_t dram_type)
{
    uint32 value;
    unsigned long save_sp;

    DDR_SAVE_SP(save_sp);

    //get data training address before idle port
    value = get_datatraing_addr();

    //enter config state
    idle_port();
    move_to_Config_state();

    //extend capability for debug
    pGRF_Reg->GRF_SOC_CON[2] = rank_to_row15_en;

    //set data training address
    pPHY_Reg->DTAR = value;

    //set auto power down idle
    pDDR_Reg->MCFG=(pDDR_Reg->MCFG&0xffff00ff)|(0x40<<8);

    //adjust DRV and ODT
    if(dram_type == DDR3)
    {
        pPHY_Reg->ZQ0CR[1] = 0x5B;  //DS=40ohm,ODT=60ohm
        pPHY_Reg->ZQ0CR[0] |= (1<<30);  //trigger
    }
    if (dram_type == DDR2)
    {
        pPHY_Reg->ZQ0CR[1] = 0x4B;  //DS=40ohm,ODT=75ohm
        pPHY_Reg->ZQ0CR[0] |= (1<<30);  //trigger
    }
    else
    {
        pPHY_Reg->ZQ0CR[1] = 0x1B;  //DS=40ohm,ODT=120ohm
        pPHY_Reg->ZQ0CR[0] |= (1<<30);  //trigger
    }    
    delayus(10);
    while(!(pPHY_Reg->ZQ0SR[0] & (0x1u<<31)));
    if(pPHY_Reg->ZQ0SR[0] & (0x1u<<30))
    {
        ddr_print("ZQCR error!\n");
    }

    //enter access state
    move_to_Access_state();
    deIdle_port();

    DDR_RESTORE_SP(save_sp);
}

static uint32_t ddr_get_parameter(uint32_t nMHz)
{
    uint32_t tmp;
    uint32_t ret = 0;
    DDR_TIMING_T  *p_ddr_timing=&ddr_timing;

    p_ddr_timing->pctl_timing.togcnt1u = nMHz;
    p_ddr_timing->pctl_timing.togcnt100n = nMHz/10;
    p_ddr_timing->pctl_timing.tinit = 200;
    p_ddr_timing->pctl_timing.trsth = 500;

    if(mem_type == DDR3)
    {
        if(ddr_type > DDR3_DEFAULT){
            ret = -1;
            goto out;
        }

        #define DDR3_tREFI_7_8_us    (78)
        #define DDR3_tMRD            (4)
        #define DDR3_tRFC_512Mb      (90)
        #define DDR3_tRFC_1Gb        (110)
        #define DDR3_tRFC_2Gb        (160)
        #define DDR3_tRFC_4Gb        (300)
        #define DDR3_tRFC_8Gb        (350)
        #define DDR3_tRTW            (2)   //register min valid value
        #define DDR3_tRAS            (37)
        #define DDR3_tRRD            (10)
        #define DDR3_tRTP            (7)
        #define DDR3_tWR             (15)
        #define DDR3_tWTR            (7)
        #define DDR3_tXP             (7)
        #define DDR3_tXPDLL          (24)
        #define DDR3_tZQCS           (80)
        #define DDR3_tZQCSI          (10000)
        #define DDR3_tDQS            (1)
        #define DDR3_tCKSRE          (10)
        #define DDR3_tCKE_400MHz     (7)
        #define DDR3_tCKE_533MHz     (6)
        #define DDR3_tMOD            (15)
        #define DDR3_tRSTL           (100)
        #define DDR3_tZQCL           (320)
        #define DDR3_tDLLK           (512)

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
        else //666MHz
        {
            tmp = 3;
        }
        cl = ddr3_cl_cwl[ddr_type][tmp] >> 16;
        cwl = ddr3_cl_cwl[ddr_type][tmp] & 0x0ff;
        if(cl == 0)
            ret = -4;
        p_ddr_timing->phy_timing.mr[1] = DDR3_DS_40 | DDR3_Rtt_Nom_120;
        p_ddr_timing->phy_timing.mr[2] = DDR3_MR2_CWL(cwl) /* | DDR3_Rtt_WR_60 */;
        p_ddr_timing->phy_timing.mr[3] = 0;
        /**************************************************
         * PCTL Timing
         **************************************************/
        /*
         * tREFI, average periodic refresh interval, 7.8us
         */
        p_ddr_timing->pctl_timing.trefi = DDR3_tREFI_7_8_us;
        /*
         * tMRD, 4 tCK
         */
        p_ddr_timing->pctl_timing.tmrd = DDR3_tMRD & 0x7;
        p_ddr_timing->phy_timing.dtpr0.b.tMRD = DDR3_tMRD-4;
        /*
         * tRFC, 90ns(512Mb),110ns(1Gb),160ns(2Gb),300ns(4Gb),350ns(8Gb)
         */
        if(capability <= 0x4000000)         // 512Mb 90ns
        {
            tmp = DDR3_tRFC_512Mb;
        }
        else if(capability <= 0x8000000)    // 1Gb 110ns
        {
            tmp = DDR3_tRFC_1Gb;
        }
        else if(capability <= 0x10000000)   // 2Gb 160ns
        {
            tmp = DDR3_tRFC_2Gb;
        }
        else if(capability <= 0x20000000)   // 4Gb 300ns
        {
            tmp = DDR3_tRFC_4Gb;
        }
        else    // 8Gb  350ns
        {
            tmp = DDR3_tRFC_8Gb;
        }
        p_ddr_timing->pctl_timing.trfc = (tmp*nMHz+999)/1000;
        p_ddr_timing->phy_timing.dtpr1.b.tRFC = ((tmp*nMHz+999)/1000)&0xFF;
        /*
         * tXSR, =tDLLK=512 tCK
         */
        p_ddr_timing->pctl_timing.texsr = DDR3_tDLLK;
        p_ddr_timing->phy_timing.dtpr2.b.tXS = DDR3_tDLLK;
        /*
         * tRP=CL
         */
        p_ddr_timing->pctl_timing.trp = cl;
        p_ddr_timing->phy_timing.dtpr0.b.tRP = cl;
        /*
         * WrToMiss=WL*tCK + tWR + tRP + tRCD
         */
        p_ddr_timing->noc_timing.b.WrToMiss = ((cwl+((DDR3_tWR*nMHz+999)/1000)+cl+cl)&0x3F);
        /*
         * tRC=tRAS+tRP
         */
        p_ddr_timing->pctl_timing.trc = ((((ddr3_tRC_tFAW[ddr_type]>>16)*nMHz+999)/1000)&0x3F);
        p_ddr_timing->noc_timing.b.ActToAct = ((((ddr3_tRC_tFAW[ddr_type]>>16)*nMHz+999)/1000)&0x3F);
        p_ddr_timing->phy_timing.dtpr0.b.tRC = (((ddr3_tRC_tFAW[ddr_type]>>16)*nMHz+999)/1000)&0xF;

        p_ddr_timing->pctl_timing.trtw = (cl+2-cwl);//DDR3_tRTW;
        p_ddr_timing->phy_timing.dtpr1.b.tRTW = 0;
        p_ddr_timing->noc_timing.b.RdToWr = ((cl+2-cwl)&0x1F);
        p_ddr_timing->pctl_timing.tal = al;
        p_ddr_timing->pctl_timing.tcl = cl;
        p_ddr_timing->pctl_timing.tcwl = cwl;
        /*
         * tRAS, 37.5ns(400MHz)     37.5ns(533MHz)
         */
        p_ddr_timing->pctl_timing.tras = (((DDR3_tRAS*nMHz+(nMHz>>1)+999)/1000)&0x3F);
        p_ddr_timing->phy_timing.dtpr0.b.tRAS = ((DDR3_tRAS*nMHz+(nMHz>>1)+999)/1000)&0x1F;
        /*
         * tRCD=CL
         */
        p_ddr_timing->pctl_timing.trcd = cl;
        p_ddr_timing->phy_timing.dtpr0.b.tRCD = cl;
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
        p_ddr_timing->pctl_timing.trrd = (tmp&0xF);
        p_ddr_timing->phy_timing.dtpr0.b.tRRD = tmp&0xF;
        /*
         * tRTP, max(4 tCK,7.5ns)
         */
        tmp = ((DDR3_tRTP*nMHz+(nMHz>>1)+999)/1000);
        if(tmp < 4)
        {
            tmp = 4;
        }
        p_ddr_timing->pctl_timing.trtp = tmp&0xF;
        p_ddr_timing->phy_timing.dtpr0.b.tRTP = tmp;
        /*
         * RdToMiss=tRTP+tRP + tRCD - (BL/2 * tCK)
         */
        p_ddr_timing->noc_timing.b.RdToMiss = ((tmp+cl+cl-(bl>>1))&0x3F);
        /*
         * tWR, 15ns
         */
        tmp = ((DDR3_tWR*nMHz+999)/1000);
        p_ddr_timing->pctl_timing.twr = tmp&0x1F;
        if(tmp<9)
            tmp = tmp - 4;
        else
            tmp = tmp>>1;
        p_ddr_timing->phy_timing.mr[0] = DDR3_BL8 | DDR3_CL(cl) | DDR3_WR(tmp);

        /*
         * tWTR, max(4 tCK,7.5ns)
         */
        tmp = ((DDR3_tWTR*nMHz+(nMHz>>1)+999)/1000);
        if(tmp < 4)
        {
            tmp = 4;
        }
        p_ddr_timing->pctl_timing.twtr = tmp&0xF;
        p_ddr_timing->phy_timing.dtpr0.b.tWTR = tmp&0x7;
        p_ddr_timing->noc_timing.b.WrToRd = ((tmp+cwl)&0x1F);
        /*
         * tXP, max(3 tCK, 7.5ns)(<933MHz)
         */
        tmp = ((DDR3_tXP*nMHz+(nMHz>>1)+999)/1000);
        if(tmp < 3)
        {
            tmp = 3;
        }
        p_ddr_timing->pctl_timing.txp = tmp&0x7;
        p_ddr_timing->phy_timing.dtpr2.b.tXP = tmp&0x1F;
        /*
         * tXPDLL, max(10 tCK,24ns)
         */
        tmp = ((DDR3_tXPDLL*nMHz+999)/1000);
        if(tmp < 10)
        {
            tmp = 10;
        }
        p_ddr_timing->pctl_timing.txpdll = tmp & 0x3F;
        /*
         * tZQCS, max(64 tCK, 80ns)
         */
        tmp = ((DDR3_tZQCS*nMHz+999)/1000);
        if(tmp < 64)
        {
            tmp = 64;
        }
        p_ddr_timing->pctl_timing.tzqcs = tmp&0x7F;
        /*
         * tZQCSI,
         */
        p_ddr_timing->pctl_timing.tzqcsi = DDR3_tZQCSI;
        /*
         * tDQS,
         */
        p_ddr_timing->pctl_timing.tdqs = DDR3_tDQS;
        /*
         * tCKSRE, max(5 tCK, 10ns)
         */
        tmp = ((DDR3_tCKSRE*nMHz+999)/1000);
        if(tmp < 5)
        {
            tmp = 5;
        }
        p_ddr_timing->pctl_timing.tcksre = tmp & 0x1F;
        /*
         * tCKSRX, max(5 tCK, 10ns)
         */
        p_ddr_timing->pctl_timing.tcksrx = tmp & 0x1F;
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
        p_ddr_timing->pctl_timing.tcke = tmp & 0x7;
        p_ddr_timing->phy_timing.dtpr2.b.tCKE = tmp;
        /*
         * tCKESR, =tCKE + 1tCK
         */
        p_ddr_timing->pctl_timing.tckesr = (tmp+1)&0xF;
        /*
         * tMOD, max(12 tCK,15ns)
         */
        tmp = ((DDR3_tMOD*nMHz+999)/1000);
        if(tmp < 12)
        {
            tmp = 12;
        }
        p_ddr_timing->pctl_timing.tmod = tmp&0x1F;
        p_ddr_timing->phy_timing.dtpr1.b.tMOD = tmp;
        /*
         * tRSTL, 100ns
         */
        p_ddr_timing->pctl_timing.trstl = ((DDR3_tRSTL*nMHz+999)/1000)&0x7F;
        /*
         * tZQCL, max(256 tCK, 320ns)
         */
        tmp = ((DDR3_tZQCL*nMHz+999)/1000);
        if(tmp < 256)
        {
            tmp = 256;
        }
        p_ddr_timing->pctl_timing.tzqcl = tmp&0x3FF;
        /*
         * tMRR, 0 tCK
         */
        p_ddr_timing->pctl_timing.tmrr = 0;
        /*
         * tDPD, 0
         */
        p_ddr_timing->pctl_timing.tdpd = 0;

        /**************************************************
         * PHY Timing
         **************************************************/
        /*
         * tCCD, BL/2 for DDR2 and 4 for DDR3
         */
        p_ddr_timing->phy_timing.dtpr0.b.tCCD = 0;
        /*
         * tDQSCKmax,5.5ns
         */
        p_ddr_timing->phy_timing.dtpr1.b.tDQSCKmax = 0;
        /*
         * tRTODT, 0:ODT may be turned on immediately after read post-amble
         *         1:ODT may not be turned on until one clock after the read post-amble
         */
        p_ddr_timing->phy_timing.dtpr1.b.tRTODT = 1;
        /*
         * tFAW,40ns(400MHz 1KB page) 37.5ns(533MHz 1KB page) 50ns(400MHz 2KB page)   50ns(533MHz 2KB page)
         */
        p_ddr_timing->phy_timing.dtpr1.b.tFAW = (((ddr3_tRC_tFAW[ddr_type]&0x0ff)*nMHz+999)/1000)&0x7F;
        /*
         * tAOND_tAOFD
         */
        p_ddr_timing->phy_timing.dtpr1.b.tAOND = 0;
        /*
         * tDLLK,512 tCK
         */
        p_ddr_timing->phy_timing.dtpr2.b.tDLLK = DDR3_tDLLK;
        /**************************************************
         * NOC Timing
         **************************************************/
        p_ddr_timing->noc_timing.b.BurstLen = ((bl>>1)&0x7);
    }
    else if(mem_type == LPDDR2)
    {
        #define LPDDR2_tREFI_3_9_us    (38)
        #define LPDDR2_tREFI_7_8_us    (78)
        #define LPDDR2_tMRD            (5)  //tCK
        #define LPDDR2_tRFC_8Gb        (210)  //ns
        #define LPDDR2_tRFC_4Gb        (130)  //ns
        #define LPDDR2_tRP_4_BANK               (24)  //ns
        #define LPDDR2_tRPab_SUB_tRPpb_4_BANK   (0)
        #define LPDDR2_tRP_8_BANK               (27)  //ns
        #define LPDDR2_tRPab_SUB_tRPpb_8_BANK   (3)
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
        #define LPDDR2_tZQCSI        (10000)
        #define LPDDR2_tDQS          (1)
        #define LPDDR2_tCKSRE        (1)  //tCK
        #define LPDDR2_tCKSRX        (2)  //tCK
        #define LPDDR2_tCKE          (3)  //tCK
        #define LPDDR2_tMOD          (0)
        #define LPDDR2_tRSTL         (0)
        #define LPDDR2_tZQCL         (360)  //ns
        #define LPDDR2_tMRR          (2)    //tCK
        #define LPDDR2_tCKESR        (15)   //ns
        #define LPDDR2_tDPD_US       (500)
        #define LPDDR2_tFAW_GREAT_200MHz    (50)  //ns
        #define LPDDR2_tFAW_LITTLE_200MHz   (60)  //ns
        #define LPDDR2_tDLLK         (2)  //tCK
        #define LPDDR2_tDQSCK_MAX    (5)  //ns
        #define LPDDR2_tDQSCK_MIN    (2)  //ns
        #define LPDDR2_tDQSS         (1)  //tCK

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
            p_ddr_timing->phy_timing.mr[2] = LPDDR2_RL3_WL1;
        }
        else if(nMHz<=266)
        {
            cl = 4;
            cwl = 2;
            p_ddr_timing->phy_timing.mr[2] = LPDDR2_RL4_WL2;
        }
        else if(nMHz<=333)
        {
            cl = 5;
            cwl = 2;
            p_ddr_timing->phy_timing.mr[2] = LPDDR2_RL5_WL2;
        }
        else if(nMHz<=400)
        {
            cl = 6;
            cwl = 3;
            p_ddr_timing->phy_timing.mr[2] = LPDDR2_RL6_WL3;
        }
        else if(nMHz<=466)
        {
            cl = 7;
            cwl = 4;
            p_ddr_timing->phy_timing.mr[2] = LPDDR2_RL7_WL4;
        }
        else //(nMHz<=1066)
        {
            cl = 8;
            cwl = 4;
            p_ddr_timing->phy_timing.mr[2] = LPDDR2_RL8_WL4;
        }
        p_ddr_timing->phy_timing.mr[3] = LPDDR2_DS_48;
        p_ddr_timing->phy_timing.mr[0] = 0;
        /**************************************************
         * PCTL Timing
         **************************************************/
        /*
         * tREFI, average periodic refresh interval, 15.6us(<256Mb) 7.8us(256Mb-1Gb) 3.9us(2Gb-8Gb)
         */
        if(capability >= 0x10000000)   // 2Gb
        {
            p_ddr_timing->pctl_timing.trefi = LPDDR2_tREFI_3_9_us;
        }
        else
        {
            p_ddr_timing->pctl_timing.trefi = LPDDR2_tREFI_7_8_us;
        }

        /*
         * tMRD, (=tMRW), 5 tCK
         */
        p_ddr_timing->pctl_timing.tmrd = LPDDR2_tMRD & 0x7;
        p_ddr_timing->phy_timing.dtpr0.b.tMRD = 3;
        /*
         * tRFC, 90ns(<=512Mb) 130ns(1Gb-4Gb) 210ns(8Gb)
         */
        if(capability >= 0x40000000)   // 8Gb
        {
            p_ddr_timing->pctl_timing.trfc = (LPDDR2_tRFC_8Gb*nMHz+999)/1000;
            /*
             * tXSR, tRFC+10ns
             */
            p_ddr_timing->pctl_timing.texsr = (((LPDDR2_tRFC_8Gb+10)*nMHz+999)/1000)&0x3FF;
            p_ddr_timing->phy_timing.dtpr1.b.tRFC = ((LPDDR2_tRFC_8Gb*nMHz+999)/1000)&0xFF;
            p_ddr_timing->phy_timing.dtpr2.b.tXS = (((LPDDR2_tRFC_8Gb+10)*nMHz+999)/1000)&0x3FF;
        }
        else
        {
            p_ddr_timing->pctl_timing.trfc = (LPDDR2_tRFC_4Gb*nMHz+999)/1000;
            p_ddr_timing->pctl_timing.texsr = (((LPDDR2_tRFC_4Gb+10)*nMHz+999)/1000)&0x3FF;
            p_ddr_timing->phy_timing.dtpr1.b.tRFC = ((LPDDR2_tRFC_4Gb*nMHz+999)/1000)&0xFF;
            p_ddr_timing->phy_timing.dtpr2.b.tXS = (((LPDDR2_tRFC_4Gb+10)*nMHz+999)/1000)&0x3FF;
        }

        /*
         * tRP, 4-bank:15ns(Fast) 18ns(Typ) 24ns(Slow), 8-bank:18ns(Fast) 21ns(Typ) 27ns(Slow)
         */
        if(pPHY_Reg->DCR.b.DDR8BNK)
        {
            p_ddr_timing->pctl_timing.trp = ((((LPDDR2_tRPab_SUB_tRPpb_8_BANK*nMHz+999)/1000) & 0x3)<<16) | (((LPDDR2_tRP_8_BANK*nMHz+999)/1000)&0xF);
            p_ddr_timing->phy_timing.dtpr0.b.tRP = ((LPDDR2_tRP_8_BANK*nMHz+999)/1000);
            /*
             * WrToMiss=WL*tCK + tDQSS + tWR + tRP + tRCD
             */
            p_ddr_timing->noc_timing.b.WrToMiss = ((cwl+LPDDR2_tDQSS+(((LPDDR2_tWR+LPDDR2_tRP_8_BANK+LPDDR2_tRCD)*nMHz+999)/1000))&0x3F);
            /*
             * RdToMiss=tRTP + tRP + tRCD - (BL/2 * tCK)
             */
            p_ddr_timing->noc_timing.b.RdToMiss = (((((LPDDR2_tRTP+LPDDR2_tRP_8_BANK+LPDDR2_tRCD)*nMHz+(nMHz>>1)+999)/1000)-(bl>>1))&0x3F);
            /*
             * tRC=tRAS+tRP
             */
            p_ddr_timing->pctl_timing.trc = ((((LPDDR2_tRP_8_BANK+LPDDR2_tRAS)*nMHz+999)/1000)&0x3F);
            p_ddr_timing->noc_timing.b.ActToAct = ((((LPDDR2_tRP_8_BANK+LPDDR2_tRAS)*nMHz+999)/1000)&0x3F);
            p_ddr_timing->phy_timing.dtpr0.b.tRC = (((LPDDR2_tRP_8_BANK+LPDDR2_tRAS)*nMHz+999)/1000)&0xF;
        }
        else
        {
            p_ddr_timing->pctl_timing.trp = (LPDDR2_tRPab_SUB_tRPpb_4_BANK<<16) | (((LPDDR2_tRP_4_BANK*nMHz+999)/1000)&0xF);
            p_ddr_timing->phy_timing.dtpr0.b.tRP = ((LPDDR2_tRP_4_BANK*nMHz+999)/1000);
            p_ddr_timing->noc_timing.b.WrToMiss = ((cwl+LPDDR2_tDQSS+(((LPDDR2_tWR+LPDDR2_tRP_4_BANK+LPDDR2_tRCD)*nMHz+999)/1000))&0x3F);
            p_ddr_timing->noc_timing.b.RdToMiss = (((((LPDDR2_tRTP+LPDDR2_tRP_4_BANK+LPDDR2_tRCD)*nMHz+(nMHz>>1)+999)/1000)-(bl>>1))&0x3F);
            p_ddr_timing->pctl_timing.trc = ((((LPDDR2_tRP_4_BANK+LPDDR2_tRAS)*nMHz+999)/1000)&0x3F);
            p_ddr_timing->noc_timing.b.ActToAct = ((((LPDDR2_tRP_4_BANK+LPDDR2_tRAS)*nMHz+999)/1000)&0x3F);
            p_ddr_timing->phy_timing.dtpr0.b.tRC = (((LPDDR2_tRP_4_BANK+LPDDR2_tRAS)*nMHz+999)/1000)&0xF;
        }

        p_ddr_timing->pctl_timing.trtw = (cl+((LPDDR2_tDQSCK_MIN*nMHz+(nMHz>>1)+999)/1000)-cwl);//LPDDR2_tRTW;
        p_ddr_timing->phy_timing.dtpr1.b.tRTW = 0;
        /*
         * RdToWr=RL+tDQSCK-WL
         */
        p_ddr_timing->noc_timing.b.RdToWr = ((cl+((LPDDR2_tDQSCK_MIN*nMHz+(nMHz>>1)+999)/1000)-cwl)&0x1F);
        p_ddr_timing->pctl_timing.tal = al;
        p_ddr_timing->pctl_timing.tcl = cl;
        p_ddr_timing->pctl_timing.tcwl = cwl;
        /*
         * tRAS, 42ns
         */
        p_ddr_timing->pctl_timing.tras = (((LPDDR2_tRAS*nMHz+999)/1000)&0x3F);
        p_ddr_timing->phy_timing.dtpr0.b.tRAS = ((LPDDR2_tRAS*nMHz+999)/1000)&0x1F;
        /*
         * tRCD, 15ns(Fast) 18ns(Typ) 24ns(Slow)
         */
        p_ddr_timing->pctl_timing.trcd = (((LPDDR2_tRCD*nMHz+999)/1000)&0xF);
        p_ddr_timing->phy_timing.dtpr0.b.tRCD = ((LPDDR2_tRCD*nMHz+999)/1000)&0xF;
        /*
         * tRRD, 10ns
         */
        p_ddr_timing->pctl_timing.trrd = (((LPDDR2_tRRD*nMHz+999)/1000)&0xF);
        p_ddr_timing->phy_timing.dtpr0.b.tRRD = ((LPDDR2_tRRD*nMHz+999)/1000)&0xF;
        /*
         * tRTP, 7.5ns
         */
        tmp = ((LPDDR2_tRTP*nMHz+(nMHz>>1)+999)/1000);
        p_ddr_timing->pctl_timing.trtp = tmp&0xF;
        p_ddr_timing->phy_timing.dtpr0.b.tRTP = (tmp<2) ? 2 : tmp;
        /*
         * tWR, 15ns
         */
        p_ddr_timing->pctl_timing.twr = ((LPDDR2_tWR*nMHz+999)/1000)&0x1F;
        p_ddr_timing->phy_timing.mr[1] = LPDDR2_BL8 | LPDDR2_nWR(((LPDDR2_tWR*nMHz+999)/1000));
        /*
         * tWTR, 7.5ns(533-266MHz)  10ns(200-166MHz)
         */
        if(nMHz > 200)
        {
            p_ddr_timing->pctl_timing.twtr = ((LPDDR2_tWTR_GREAT_200MHz*nMHz+(nMHz>>1)+999)/1000)&0xF;
            p_ddr_timing->phy_timing.dtpr0.b.tWTR = ((LPDDR2_tWTR_GREAT_200MHz*nMHz+(nMHz>>1)+999)/1000)&0x7;
            /*
             * WrToRd=WL+tDQSS+tWTR
             */
            p_ddr_timing->noc_timing.b.WrToRd = ((LPDDR2_tDQSS+((LPDDR2_tWTR_GREAT_200MHz*nMHz+(nMHz>>1)+999)/1000)+cwl)&0x1F);
        }
        else
        {
            p_ddr_timing->pctl_timing.twtr = ((LPDDR2_tWTR_LITTLE_200MHz*nMHz+999)/1000)&0xF;
            p_ddr_timing->phy_timing.dtpr0.b.tWTR = ((LPDDR2_tWTR_LITTLE_200MHz*nMHz+999)/1000)&0x7;
            p_ddr_timing->noc_timing.b.WrToRd = ((LPDDR2_tDQSS+((LPDDR2_tWTR_LITTLE_200MHz*nMHz+999)/1000)+cwl)&0x1F);
        }
        /*
         * tXP, 7.5ns
         */
        p_ddr_timing->pctl_timing.txp = ((LPDDR2_tXP*nMHz+(nMHz>>1)+999)/1000)&0x7;
        p_ddr_timing->phy_timing.dtpr2.b.tXP = ((LPDDR2_tXP*nMHz+(nMHz>>1)+999)/1000)&0x1F;
        /*
         * tXPDLL, 0ns
         */
        p_ddr_timing->pctl_timing.txpdll = LPDDR2_tXPDLL;
        /*
         * tZQCS, 90ns
         */
        p_ddr_timing->pctl_timing.tzqcs = ((LPDDR2_tZQCS*nMHz+999)/1000)&0x7F;
        /*
         * tZQCSI,
         */
        if(pDDR_Reg->MCFG &= lpddr2_s4)
        {
            p_ddr_timing->pctl_timing.tzqcsi = LPDDR2_tZQCSI;
        }
        else
        {
            p_ddr_timing->pctl_timing.tzqcsi = 0;
        }
        /*
         * tDQS,
         */
        p_ddr_timing->pctl_timing.tdqs = LPDDR2_tDQS;
        /*
         * tCKSRE, 1 tCK
         */
        p_ddr_timing->pctl_timing.tcksre = LPDDR2_tCKSRE;
        /*
         * tCKSRX, 2 tCK
         */
        p_ddr_timing->pctl_timing.tcksrx = LPDDR2_tCKSRX;
        /*
         * tCKE, 3 tCK
         */
        p_ddr_timing->pctl_timing.tcke = LPDDR2_tCKE;
        p_ddr_timing->phy_timing.dtpr2.b.tCKE = LPDDR2_tCKE;
        /*
         * tMOD, 0 tCK
         */
        p_ddr_timing->pctl_timing.tmod = LPDDR2_tMOD;
        p_ddr_timing->phy_timing.dtpr1.b.tMOD = LPDDR2_tMOD;
        /*
         * tRSTL, 0 tCK
         */
        p_ddr_timing->pctl_timing.trstl = LPDDR2_tRSTL;
        /*
         * tZQCL, 360ns
         */
        p_ddr_timing->pctl_timing.tzqcl = ((LPDDR2_tZQCL*nMHz+999)/1000)&0x3FF;
        /*
         * tMRR, 2 tCK
         */
        p_ddr_timing->pctl_timing.tmrr = LPDDR2_tMRR;
        /*
         * tCKESR, 15ns
         */
        p_ddr_timing->pctl_timing.tckesr = ((LPDDR2_tCKESR*nMHz+999)/1000)&0xF;
        /*
         * tDPD, 500us
         */
        p_ddr_timing->pctl_timing.tdpd = LPDDR2_tDPD_US;

        /**************************************************
         * PHY Timing
         **************************************************/
        /*
         * tCCD, BL/2 for DDR2 and 4 for DDR3
         */
        p_ddr_timing->phy_timing.dtpr0.b.tCCD = 0;
        /*
         * tDQSCKmax,5.5ns
         */
        tmp = ((LPDDR2_tDQSCK_MAX*nMHz+(nMHz>>1)+999)/1000);      
        p_ddr_timing->phy_timing.dtpr1.b.tDQSCKmax = 3;
        /*
         * tDQSCKmin,2.5ns
         */
        tmp = ((LPDDR2_tDQSCK_MAX*nMHz+(nMHz>>1))/1000);
        p_ddr_timing->phy_timing.dtpr1.b.tDQSCK = 2;
        /*
         * tRTODT, 0:ODT may be turned on immediately after read post-amble
         *         1:ODT may not be turned on until one clock after the read post-amble
         */
        p_ddr_timing->phy_timing.dtpr1.b.tRTODT = 1;
        /*
         * tFAW,50ns(200-533MHz)  60ns(166MHz)
         */
        if(nMHz>=200)
        {
            p_ddr_timing->phy_timing.dtpr1.b.tFAW = ((LPDDR2_tFAW_GREAT_200MHz*nMHz+999)/1000)&0x7F;
        }
        else
        {
            p_ddr_timing->phy_timing.dtpr1.b.tFAW = ((LPDDR2_tFAW_LITTLE_200MHz*nMHz+999)/1000)&0x7F;
        }
        /*
         * tAOND_tAOFD
         */
        p_ddr_timing->phy_timing.dtpr1.b.tAOND = 0;
        /*
         * tDLLK,0
         */
        p_ddr_timing->phy_timing.dtpr2.b.tDLLK = LPDDR2_tDLLK;
        /**************************************************
         * NOC Timing
         **************************************************/
        p_ddr_timing->noc_timing.b.BurstLen = ((bl>>1)&0x7);
    }
    else if(mem_type == DDR2)
    {
        #define DDR2_tREFI_7_8_us     (78)
        #define DDR2_tMRD             (2)
        #define DDR2_tRFC_256Mb       (75)
        #define DDR2_tRFC_512Mb       (105)
        #define DDR2_tRFC_1Gb         (128)
        #define DDR2_tRFC_2Gb         (195)
        #define DDR2_tRFC_4Gb         (328)
        #define DDR2_tRAS             (45)
        #define DDR2_tRTW             (2)  //register min valid value
        #define DDR2_tRRD             (10)
        #define DDR2_tRTP             (7)
        #define DDR2_tWR              (15)
        #define DDR2_tWTR_LITTLE_200MHz   (10)
        #define DDR2_tWTR_GREAT_200MHz    (7)
        #define DDR2_tDQS             (1)
        #define DDR2_tCKSRE           (1)
        #define DDR2_tCKSRX           (1)
        #define DDR2_tCKE             (3)
        #define DDR2_tCKESR           DDR2_tCKE
        #define DDR2_tMOD             (12)
        #define DDR2_tFAW_333MHz      (50)
        #define DDR2_tFAW_400MHz      (45)
        #define DDR2_tDLLK            (200)

        al = 0;
        bl = 4;
        if(nMHz <= 266)
        {
            cl =  4;
        }
        else if((nMHz > 266) && (nMHz <= 333))
        {
            cl =  5;
        }
        else if((nMHz > 333) && (nMHz <= 400))
        {
            cl =  6;
        }
        else // > 400MHz
        {
            cl =  7;
        }
        cwl = cl -1;
        p_ddr_timing->phy_timing.mr[1] = DDR2_STR_REDUCE | DDR2_Rtt_Nom_75;
        p_ddr_timing->phy_timing.mr[2] = 0;
        p_ddr_timing->phy_timing.mr[3] = 0;
        /**************************************************
         * PCTL Timing
         **************************************************/
        /*
         * tREFI, average periodic refresh interval, 7.8us
         */
        p_ddr_timing->pctl_timing.trefi = DDR2_tREFI_7_8_us;
        /*
         * tMRD, 2 tCK
         */
        p_ddr_timing->pctl_timing.tmrd = DDR2_tMRD & 0x7;
        p_ddr_timing->phy_timing.dtpr0.b.tMRD = DDR2_tMRD;
        /*
         * tRFC, 75ns(256Mb) 105ns(512Mb) 127.5ns(1Gb) 195ns(2Gb) 327.5ns(4Gb)
         */
        if(capability <= 0x2000000)  // 256Mb
        {
            tmp = DDR2_tRFC_256Mb;
        }
        else if(capability <= 0x4000000) // 512Mb
        {
            tmp = DDR2_tRFC_512Mb;
        }
        else if(capability <= 0x8000000)  // 1Gb
        {
            tmp = DDR2_tRFC_1Gb;
        }
        else if(capability <= 0x10000000)  // 2Gb
        {
            tmp = DDR2_tRFC_2Gb;
        }
        else  // 4Gb
        {
            tmp = DDR2_tRFC_4Gb;
        }
        p_ddr_timing->pctl_timing.trfc = (tmp*nMHz+999)/1000;
        p_ddr_timing->phy_timing.dtpr1.b.tRFC = ((tmp*nMHz+999)/1000)&0xFF;
        /*
         * tXSR, max(tRFC+10,200 tCK)
         */
        tmp = (((tmp+10)*nMHz+999)/1000);
        if(tmp<200)
        {
            tmp = 200;
        }
        p_ddr_timing->pctl_timing.texsr = tmp&0x3FF;
        p_ddr_timing->phy_timing.dtpr2.b.tXS = tmp&0x3FF;
        /*
         * tRP=CL
         */
        if(pPHY_Reg->DCR.b.DDR8BNK)
        {
            p_ddr_timing->pctl_timing.trp = (1<<16) | cl;
        }
        else
        {
            p_ddr_timing->pctl_timing.trp = cl;
        }
        p_ddr_timing->phy_timing.dtpr0.b.tRP = cl;
        /*
         * WrToMiss=WL*tCK + tWR + tRP + tRCD
         */
        p_ddr_timing->noc_timing.b.WrToMiss = ((cwl+((DDR2_tWR*nMHz+999)/1000)+cl+cl)&0x3F);
        /*
         * tRAS, 45ns
         */
        tmp=((DDR2_tRAS*nMHz+999)/1000);
        p_ddr_timing->pctl_timing.tras = (tmp&0x3F);
        p_ddr_timing->phy_timing.dtpr0.b.tRAS = tmp&0x1F;
        /*
         * tRC=tRAS+tRP
         */
        p_ddr_timing->pctl_timing.trc = ((tmp+cl)&0x3F);
        p_ddr_timing->noc_timing.b.ActToAct = ((tmp+cl)&0x3F);
        p_ddr_timing->phy_timing.dtpr0.b.tRC = (tmp+cl)&0xF;

        p_ddr_timing->pctl_timing.trtw = (cl+2-cwl);//DDR2_tRTW;
        p_ddr_timing->phy_timing.dtpr1.b.tRTW = 0;
        p_ddr_timing->noc_timing.b.RdToWr = ((cl+2-cwl)&0x1F);
        p_ddr_timing->pctl_timing.tal = al;
        p_ddr_timing->pctl_timing.tcl = cl;
        p_ddr_timing->pctl_timing.tcwl = cwl;
        /*
         * tRCD=CL
         */
        p_ddr_timing->pctl_timing.trcd = cl;
        p_ddr_timing->phy_timing.dtpr0.b.tRCD = cl;
        /*
         * tRRD = 10ns(2KB page)
         *
         */
        p_ddr_timing->pctl_timing.trrd = (((DDR2_tRRD*nMHz+999)/1000)&0xF);
        p_ddr_timing->phy_timing.dtpr0.b.tRRD = ((DDR2_tRRD*nMHz+999)/1000)&0xF;
        /*
         * tRTP, 7.5ns
         */
        tmp = ((DDR2_tRTP*nMHz+(nMHz>>1)+999)/1000);
        p_ddr_timing->pctl_timing.trtp = tmp&0xF;
        p_ddr_timing->phy_timing.dtpr0.b.tRTP = tmp;
        /*
         * RdToMiss=tRTP+tRP + tRCD - (BL/2 * tCK)
         */
        p_ddr_timing->noc_timing.b.RdToMiss = ((tmp+cl+cl-(bl>>1))&0x3F);
        /*
         * tWR, 15ns
         */
        tmp = ((DDR2_tWR*nMHz+999)/1000);
        p_ddr_timing->pctl_timing.twr = tmp&0x1F;
        p_ddr_timing->phy_timing.mr[0] = DDR2_BL4 | DDR2_CL(cl) | DDR2_WR(tmp);
        /*
         * tWTR, 10ns(200MHz) 7.5ns(>200MHz)
         */
        if(nMHz<=200)
        {
            tmp = ((DDR2_tWTR_LITTLE_200MHz*nMHz+999)/1000);
        }
        else
        {
            tmp = ((DDR2_tWTR_GREAT_200MHz*nMHz+(nMHz>>1)+999)/1000);
        }
        p_ddr_timing->pctl_timing.twtr = tmp&0xF;
        p_ddr_timing->phy_timing.dtpr0.b.tWTR = tmp&0x7;
        p_ddr_timing->noc_timing.b.WrToRd = ((tmp+cwl)&0x1F);
        /*
         * tXP, 6-AL(200MHz)         6-AL(266MHz)         7-AL(333MHz)         8-AL(400MHz)        10-AL(533MHz)
         */
        if(nMHz<=266)
        {
            tmp = 6-al;
        }
        else if(nMHz<=333)
        {
            tmp = 7-al;
        }
        else if(nMHz<=400)
        {
            tmp = 8-al;
        }
        else
        {
            tmp = 10-al;
        }
        p_ddr_timing->pctl_timing.txp = tmp&0x7;
        p_ddr_timing->phy_timing.dtpr2.b.tXP = tmp&0x1F;
        /*
         * tXPDLL, =tXP
         */
        p_ddr_timing->pctl_timing.txpdll = tmp & 0x3F;
        /*
         * tZQCS, 0
         */
        p_ddr_timing->pctl_timing.tzqcs = 0;
        /*
         * tZQCSI,
         */
        p_ddr_timing->pctl_timing.tzqcsi = 0;
        /*
         * tDQS,
         */
        p_ddr_timing->pctl_timing.tdqs = DDR2_tDQS;
        /*
         * tCKSRE, 1 tCK
         */
        p_ddr_timing->pctl_timing.tcksre = DDR2_tCKSRE & 0x1F;
        /*
         * tCKSRX, no such timing
         */
        p_ddr_timing->pctl_timing.tcksrx = DDR2_tCKSRX & 0x1F;
        /*
         * tCKE, 3 tCK
         */
        p_ddr_timing->pctl_timing.tcke = DDR2_tCKE & 0x7;
        p_ddr_timing->phy_timing.dtpr2.b.tCKE = DDR2_tCKE;
        /*
         * tCKESR, =tCKE
         */
        p_ddr_timing->pctl_timing.tckesr = DDR2_tCKESR&0xF;
        /*
         * tMOD, 12ns
         */
        p_ddr_timing->pctl_timing.tmod = ((DDR2_tMOD*nMHz+999)/1000)&0x1F;
        p_ddr_timing->phy_timing.dtpr1.b.tMOD = ((DDR2_tMOD*nMHz+999)/1000);
        /*
         * tRSTL, 0
         */
        p_ddr_timing->pctl_timing.trstl = 0;
        /*
         * tZQCL, 0
         */
        p_ddr_timing->pctl_timing.tzqcl = 0;
        /*
         * tMRR, 0 tCK
         */
        p_ddr_timing->pctl_timing.tmrr = 0;
        /*
         * tDPD, 0
         */
        p_ddr_timing->pctl_timing.tdpd = 0;

        /**************************************************
         * PHY Timing
         **************************************************/
        /*
         * tCCD, BL/2 for DDR2 and 4 for DDR3
         */
        p_ddr_timing->phy_timing.dtpr0.b.tCCD = 0;
        /*
         * tDQSCKmax,5.5ns
         */
        p_ddr_timing->phy_timing.dtpr1.b.tDQSCKmax = 0;
        /*
         * tRTODT, 0:ODT may be turned on immediately after read post-amble
         *         1:ODT may not be turned on until one clock after the read post-amble
         */
        p_ddr_timing->phy_timing.dtpr1.b.tRTODT = 1;
        /*
         * tFAW,50ns(<=333MHz 2KB page) 45ns(400MHz 2KB page) 45ns(533MHz 2KB page)
         */
        if(nMHz<=333)
        {
            tmp = DDR2_tFAW_333MHz;
        }
        else
        {
            tmp = DDR2_tFAW_400MHz;
        }
        p_ddr_timing->phy_timing.dtpr1.b.tFAW = ((tmp*nMHz+999)/1000)&0x7F;
        /*
         * tAOND_tAOFD
         */
        p_ddr_timing->phy_timing.dtpr1.b.tAOND = 0;
        /*
         * tDLLK,=tXSRD=200 tCK
         */
        p_ddr_timing->phy_timing.dtpr2.b.tDLLK = DDR2_tDLLK;
        /**************************************************
         * NOC Timing
         **************************************************/
        p_ddr_timing->noc_timing.b.BurstLen = ((bl>>1)&0x7);
    }
    else //if(mem_type == LPDDR)
    {
        #define mDDR_tREFI_7_8_us   (78)
        #define mDDR_tMRD           (2)
        #define mDDR_tRFC_256Mb     (80)
        #define mDDR_tRFC_512Mb     (110)
        #define mDDR_tRFC_1Gb       (140)
        #define mDDR_tXSR           (200)
        #define mDDR_tRAS_100MHz    (50)
        #define mDDR_tRAS_133MHz    (45)
        #define mDDR_tRAS_185MHz    (42)
        #define mDDR_tRAS_200MHz    (40)
        #define mDDR_tRTW           (3)  //register min valid value
        #define mDDR_tRRD_133MHz    (15)
        #define mDDR_tRRD_166MHz    (12)
        #define mDDR_tRRD_185MHz    (11)
        #define mDDR_tRRD_200MHz    (10)
        #define mDDR_tRTP           (0)
        #define mDDR_tWR            (15)
        #define mDDR_tWTR_133MHz    (1)
        #define mDDR_tWTR_200MHz    (2)
        #define mDDR_tXP            (25)
        #define mDDR_tDQS           (1)
        #define mDDR_tCKSRE         (1)
        #define mDDR_tCKSRX         (1)
        #define mDDR_tCKE           (2)

        al = 0;
        bl = 4;
        /*
         * mobile DDR timing USE 3-3-3, CL always = 3
         */
        cl = 3;
        cwl = 1;
        p_ddr_timing->phy_timing.mr[0] = mDDR_BL4 | mDDR_CL(cl);
        p_ddr_timing->phy_timing.mr[2] = mDDR_DS_3_4;  //mr[2] is mDDR MR1
        p_ddr_timing->phy_timing.mr[1] = 0;
        p_ddr_timing->phy_timing.mr[3] = 0;
        /**************************************************
         * PCTL Timing
         **************************************************/
        /*
         * tREFI, average periodic refresh interval, 7.8us
         */
        p_ddr_timing->pctl_timing.trefi = mDDR_tREFI_7_8_us;
        /*
         * tMRD, 2 tCK
         */
        p_ddr_timing->pctl_timing.tmrd = mDDR_tMRD & 0x7;
        p_ddr_timing->phy_timing.dtpr0.b.tMRD = mDDR_tMRD;
        /*
         * tRFC, 80ns(128Mb,256Mb) 110ns(512Mb) 140ns(1Gb,2Gb)
         */
        if(capability <= 0x2000000)  // 256Mb
        {
            tmp = mDDR_tRFC_256Mb;
        }
        else if(capability <= 0x4000000) // 512Mb
        {
            tmp = mDDR_tRFC_512Mb;
        }
        else  // 1Gb,2Gb
        {
            tmp = mDDR_tRFC_1Gb;
        }
        p_ddr_timing->pctl_timing.trfc = (tmp*nMHz+999)/1000;
        p_ddr_timing->phy_timing.dtpr1.b.tRFC = ((tmp*nMHz+999)/1000)&0xFF;
        /*
         * tCKESR, =tRFC
         */
        p_ddr_timing->pctl_timing.tckesr = tmp&0xF;
        /*
         * tXSR, 200ns
         */
        p_ddr_timing->pctl_timing.texsr = ((mDDR_tXSR*nMHz+999)/1000)&0x3FF;
        p_ddr_timing->phy_timing.dtpr2.b.tXS = ((mDDR_tXSR*nMHz+999)/1000)&0x3FF;
        /*
         * tRP=CL
         */
        p_ddr_timing->pctl_timing.trp = cl;
        p_ddr_timing->phy_timing.dtpr0.b.tRP = cl;
        /*
         * WrToMiss=WL*tCK + tWR + tRP + tRCD
         */
        p_ddr_timing->noc_timing.b.WrToMiss = ((cwl+((mDDR_tWR*nMHz+999)/1000)+cl+cl)&0x3F);
        /*
         * tRAS, 50ns(100MHz) 45ns(133MHz) 42ns(166MHz) 42ns(185MHz) 40ns(200MHz)
         */
        if(nMHz<=100)
        {
            tmp = mDDR_tRAS_100MHz;
        }
        else if(nMHz<=133)
        {
            tmp = mDDR_tRAS_133MHz;
        }
        else if(nMHz<=185)
        {
            tmp =mDDR_tRAS_185MHz;
        }
        else
        {
            tmp = mDDR_tRAS_200MHz;
        }
        tmp = ((tmp*nMHz+999)/1000);
        p_ddr_timing->pctl_timing.tras = (tmp&0x3F);
        p_ddr_timing->phy_timing.dtpr0.b.tRAS = tmp&0x1F;
        /*
         * tRC=tRAS+tRP
         */
        p_ddr_timing->pctl_timing.trc = ((tmp+cl)&0x3F);
        p_ddr_timing->noc_timing.b.ActToAct = ((tmp+cl)&0x3F);
        p_ddr_timing->phy_timing.dtpr0.b.tRC = (tmp+cl)&0xF;
        p_ddr_timing->pctl_timing.trtw = (cl+2-cwl);//mDDR_tRTW;
        p_ddr_timing->phy_timing.dtpr1.b.tRTW = 0;
        p_ddr_timing->noc_timing.b.RdToWr = ((cl+2-cwl)&0x1F);
        p_ddr_timing->pctl_timing.tal = al;
        p_ddr_timing->pctl_timing.tcl = cl;
        p_ddr_timing->pctl_timing.tcwl = cwl;
        /*
         * tRCD=CL
         */
        p_ddr_timing->pctl_timing.trcd = cl;
        p_ddr_timing->phy_timing.dtpr0.b.tRCD = cl;
        /*
         * tRRD,15ns(100MHz) 15ns(133MHz) 12ns(166MHz) 10.8ns(185MHz) 10ns(200MHz)
         *
         */
        if(nMHz<=133)
        {
            tmp = mDDR_tRRD_133MHz;
        }
        else if(nMHz<=166)
        {
            tmp = mDDR_tRRD_166MHz;
        }
        else if(nMHz<=185)
        {
            tmp = mDDR_tRRD_185MHz;
        }
        else
        {
            tmp = mDDR_tRRD_200MHz;
        }
        p_ddr_timing->pctl_timing.trrd = (((tmp*nMHz+999)/1000)&0xF);
        p_ddr_timing->phy_timing.dtpr0.b.tRRD = ((tmp*nMHz+999)/1000)&0xF;
        /*
         * tRTP, 0
         */
        tmp = ((mDDR_tRTP*nMHz+999)/1000);
        p_ddr_timing->pctl_timing.trtp = tmp&0xF;
        p_ddr_timing->phy_timing.dtpr0.b.tRTP = tmp;
        /*
         * RdToMiss=tRTP+tRP + tRCD - (BL/2 * tCK)
         */
        p_ddr_timing->noc_timing.b.RdToMiss = ((tmp+cl+cl-(bl>>1))&0x3F);
        /*
         * tWR, 15ns
         */
        p_ddr_timing->pctl_timing.twr = ((mDDR_tWR*nMHz+999)/1000)&0x1F;
        /*
         * tWTR, 1 tCK(100MHz,133MHz) 2 tCK(166MHz,185MHz,200MHz)
         */
        if(nMHz <= 133)
        {
            tmp = mDDR_tWTR_133MHz;
        }
        else
        {
            tmp = mDDR_tWTR_200MHz;
        }
        p_ddr_timing->pctl_timing.twtr = tmp&0xF;
        p_ddr_timing->phy_timing.dtpr0.b.tWTR = tmp&0x7;
        p_ddr_timing->noc_timing.b.WrToRd = ((tmp+cwl)&0x1F);
        /*
         * tXP, 25ns
         */

        p_ddr_timing->pctl_timing.txp = ((mDDR_tXP*nMHz+999)/1000)&0x7;
        p_ddr_timing->phy_timing.dtpr2.b.tXP = ((mDDR_tXP*nMHz+999)/1000)&0x1F;
        /*
         * tXPDLL, 0
         */
        p_ddr_timing->pctl_timing.txpdll = 0;
        /*
         * tZQCS, 0
         */
        p_ddr_timing->pctl_timing.tzqcs = 0;
        /*
         * tZQCSI,
         */
        p_ddr_timing->pctl_timing.tzqcsi = 0;
        /*
         * tDQS,
         */
        p_ddr_timing->pctl_timing.tdqs = mDDR_tDQS;
        /*
         * tCKSRE, 1 tCK
         */
        p_ddr_timing->pctl_timing.tcksre = mDDR_tCKSRE & 0x1F;
        /*
         * tCKSRX, no such timing
         */
        p_ddr_timing->pctl_timing.tcksrx = mDDR_tCKSRX & 0x1F;
        /*
         * tCKE, 2 tCK
         */
        p_ddr_timing->pctl_timing.tcke = mDDR_tCKE & 0x7;
        p_ddr_timing->phy_timing.dtpr2.b.tCKE = mDDR_tCKE;
        /*
         * tMOD, 0
         */
        p_ddr_timing->pctl_timing.tmod = 0;
        p_ddr_timing->phy_timing.dtpr1.b.tMOD = 0;
        /*
         * tRSTL, 0
         */
        p_ddr_timing->pctl_timing.trstl = 0;
        /*
         * tZQCL, 0
         */
        p_ddr_timing->pctl_timing.tzqcl = 0;
        /*
         * tMRR, 0 tCK
         */
        p_ddr_timing->pctl_timing.tmrr = 0;
        /*
         * tDPD, 0
         */
        p_ddr_timing->pctl_timing.tdpd = 0;

        /**************************************************
         * PHY Timing
         **************************************************/
        /*
         * tCCD, BL/2 for DDR2 and 4 for DDR3
         */
        p_ddr_timing->phy_timing.dtpr0.b.tCCD = 0;
        /*
         * tDQSCKmax,5.5ns
         */
        p_ddr_timing->phy_timing.dtpr1.b.tDQSCKmax = 0;
        /*
         * tRTODT, 0:ODT may be turned on immediately after read post-amble
         *         1:ODT may not be turned on until one clock after the read post-amble
         */
        p_ddr_timing->phy_timing.dtpr1.b.tRTODT = 1;
        /*
         * tFAW,0
         */
        p_ddr_timing->phy_timing.dtpr1.b.tFAW = 0;
        /*
         * tAOND_tAOFD
         */
        p_ddr_timing->phy_timing.dtpr1.b.tAOND = 0;
        /*
         * tDLLK,0
         */
        p_ddr_timing->phy_timing.dtpr2.b.tDLLK = 0;
        /**************************************************
         * NOC Timing
         **************************************************/
        p_ddr_timing->noc_timing.b.BurstLen = ((bl>>1)&0x7);
    }
out:
    return ret;
}

static uint32_t __sramlocalfunc ddr_update_timing(void)
{
    uint32_t i;
    DDR_TIMING_T  *p_ddr_timing=&ddr_timing;

    copy((uint32_t *)&(pDDR_Reg->TOGCNT1U), (uint32_t*)&(p_ddr_timing->pctl_timing.togcnt1u), 34);
    copy((uint32_t *)&(pPHY_Reg->DTPR[0]), (uint32_t*)&(p_ddr_timing->phy_timing.dtpr0), 3);
    *(volatile uint32_t *)SysSrv_DdrTiming = p_ddr_timing->noc_timing.d32;
    // Update PCTL BL
    if(mem_type == DDR3)
    {
        pDDR_Reg->MCFG = (pDDR_Reg->MCFG & (~(0x1|(0x3<<18)|(0x1<<17)|(0x1<<16)))) | ddr2_ddr3_bl_8 | tfaw_cfg(5)|pd_exit_slow|pd_type(1);
        pDDR_Reg->DFITRDDATAEN   = pDDR_Reg->TCL-2;
        pDDR_Reg->DFITPHYWRLAT   = pDDR_Reg->TCWL-1;
    }
    if(mem_type == LPDDR2)
    {
        if(ddr_freq>=200)
        {
            pDDR_Reg->MCFG = (pDDR_Reg->MCFG & (~((0x3<<20)|(0x3<<18)|(0x1<<17)|(0x1<<16)))) | mddr_lpddr2_bl_8 | tfaw_cfg(5)|pd_exit_fast|pd_type(1);
        }
        else
        {
            pDDR_Reg->MCFG = (pDDR_Reg->MCFG & (~((0x3<<20)|(0x3<<18)|(0x1<<17)|(0x1<<16)))) | mddr_lpddr2_bl_8 | tfaw_cfg(6)|pd_exit_fast|pd_type(1);
        }
        i = ((pPHY_Reg->DTPR[1] >> 27) & 0x7) - ((pPHY_Reg->DTPR[1] >> 24) & 0x7);
        pPHY_Reg->DSGCR = (pPHY_Reg->DSGCR & (~(0x3F<<5))) | (i<<5) | (i<<8);  //tDQSCKmax-tDQSCK
        pDDR_Reg->DFITRDDATAEN   = pDDR_Reg->TCL;
        pDDR_Reg->DFITPHYWRLAT   = pDDR_Reg->TCWL;
    }
    else if(mem_type == DDR2)
    {
        pDDR_Reg->MCFG = (pDDR_Reg->MCFG & (~(0x1|(0x3<<18)|(0x1<<17)|(0x1<<16)))) | ddr2_ddr3_bl_8 | tfaw_cfg(5)|pd_exit_fast|pd_type(1);
    }
    else// if(mem_type == LPDDR)
    {
        pDDR_Reg->MCFG = (pDDR_Reg->MCFG & (~((0x3<<20)|(0x3<<18)|(0x1<<17)|(0x1<<16)))) | mddr_lpddr2_bl_4 | tfaw_cfg(5)|pd_exit_fast|pd_type(1);
    }
    return 0;
}

static uint32_t __sramlocalfunc ddr_update_mr(void)
{
    DDR_TIMING_T  *p_ddr_timing=&ddr_timing;

    copy((uint32_t *)&(pPHY_Reg->MR[0]), (uint32_t*)&(p_ddr_timing->phy_timing.mr[0]), 4);
    if(mem_type == DDR3)
    {
        send_command(3, MRS_cmd, bank_addr(0x0) | cmd_addr(((uint8_t)(p_ddr_timing->phy_timing.mr[0]))|(0X1<<8)));
        delayus(100);
        send_command(3, MRS_cmd, bank_addr(0x0) | cmd_addr((uint8_t)(p_ddr_timing->phy_timing.mr[0])));
        send_command(3, MRS_cmd, bank_addr(0x1) | cmd_addr((uint8_t)(p_ddr_timing->phy_timing.mr[1])));
        send_command(3, MRS_cmd, bank_addr(0x2) | cmd_addr((uint8_t)(p_ddr_timing->phy_timing.mr[2])));
    }
    else if(mem_type == LPDDR2)
    {
        send_command(3, MRS_cmd, lpddr2_ma(0x1) | lpddr2_op((uint8_t)(p_ddr_timing->phy_timing.mr[1])));
        send_command(3, MRS_cmd, lpddr2_ma(0x2) | lpddr2_op((uint8_t)(p_ddr_timing->phy_timing.mr[2])));
        send_command(3, MRS_cmd, lpddr2_ma(0x3) | lpddr2_op((uint8_t)(p_ddr_timing->phy_timing.mr[3])));
    }
    else if(mem_type == DDR2)
    {
        send_command(3, MRS_cmd, bank_addr(0x0) | cmd_addr((uint8_t)(p_ddr_timing->phy_timing.mr[0])));
        send_command(3, MRS_cmd, bank_addr(0x1) | cmd_addr((uint8_t)(p_ddr_timing->phy_timing.mr[1])));
    }
    else //if(mem_type == LPDDR)
    {
        send_command(3, MRS_cmd, bank_addr(0x0) | cmd_addr((uint8_t)(p_ddr_timing->phy_timing.mr[0])));
        send_command(3, MRS_cmd, bank_addr(0x1) | cmd_addr((uint8_t)(p_ddr_timing->phy_timing.mr[2]))); //mr[2] is mDDR MR1
    }
    
    return 0;
}

static void __sramlocalfunc ddr_selfrefresh_enter(void)
{
    idle_port();
    move_to_Lowpower_state();
    
    //pPHY_Reg->ACDLLCR &= ~0x40000000;
    //pPHY_Reg->DATX8[0].DXDLLCR &= ~0x40000000;
    //pPHY_Reg->DATX8[1].DXDLLCR &= ~0x40000000;
    //pPHY_Reg->DATX8[2].DXDLLCR &= ~0x40000000;
    //pPHY_Reg->DATX8[3].DXDLLCR &= ~0x40000000;    //reset DLL
    //delayus(1);
    //pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1<<2)<<16)|(0x1<<2);  //close DDR PHY clock
    //phy_dll_bypass_set(0);  //dll bypass
}

static void __sramlocalfunc ddr_selfrefresh_exit(void)
{
    uint32 n;
    //pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1<<2)<<16)|(0x0<<2);  //open DDR PHY clock
    //delayus(1000);
    //pPHY_Reg->ACDLLCR |= 0x40000000;
    //pPHY_Reg->DATX8[0].DXDLLCR |= 0x40000000;
    //pPHY_Reg->DATX8[1].DXDLLCR |= 0x40000000;
    //pPHY_Reg->DATX8[2].DXDLLCR |= 0x40000000;
    //pPHY_Reg->DATX8[3].DXDLLCR |= 0x40000000;  //de-reset DLL
    //delayus(10);
    //pPHY_Reg->PIR = INIT | ITMSRST | LOCKBYP | ZCALBYP | CLRSR;  //reset ITM
    
    move_to_Config_state();
    ddr_update_timing();
    ddr_update_mr();
    n = data_training();
    move_to_Access_state();
    deIdle_port();
    if(n!=0)
    {
        ddr_print("DTT failed!\n");
    }
}

uint32_t __sramfunc ddr_change_freq(uint32_t nMHz)
{
    uint32_t ret;
    u32 i;
    volatile u32 n;	
    unsigned long flags;
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;
    unsigned long save_sp;
    
    ret=ddr_set_pll(nMHz,0);
    ddr_get_parameter(ret);

    /** 1. Make sure there is no host access */
    local_irq_save(flags);
    flush_cache_all();
	outer_flush_all();
	flush_tlb_all();
	DDR_SAVE_SP(save_sp);
	for(i=0;i<16;i++)
	{
	    n=temp[1024*i];
        barrier();
	}
    n= pDDR_Reg->SCFG.d32;
    n= pPHY_Reg->RIDR;
    n= pCRU_Reg->CRU_PLL_CON[0][0];
    n= pPMU_Reg->PMU_WAKEUP_CFG[0];
    n= *(volatile uint32_t *)SysSrv_DdrConf;
    n= pGRF_Reg->GRF_SOC_STATUS0;
    dsb();

    /** 2. ddr enter self-refresh mode or precharge power-down mode */
    ddr_selfrefresh_enter();

    phy_dll_bypass_set(0);  //dll bypass
    dsb();
    pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1<<2)<<16) | (1<<2);  //disable DDR PHY clock
    delayus(1);
    
    /** 3. change frequence  */
    ddr_set_pll(ret,1);
    ddr_freq = ret;
    
    pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1<<2)<<16) | (0<<2);  //enable DDR PHY clock
    dsb();
    delayus(1000);   //wait pll lock
    phy_dll_bypass_set(ret);
    dsb();
    
    reset_dll();
    dsb(); 
    /** 5. Issues a Mode Exit command   */
    ddr_selfrefresh_exit();
    dsb();     
    DDR_RESTORE_SP(save_sp);
    local_irq_restore(flags);
    clk_set_rate(clk_get(NULL, "ddr_pll"), 0);
    return ret;
}

EXPORT_SYMBOL(ddr_change_freq);

void __sramfunc ddr_suspend(void)
{
    u32 i;
    volatile u32 n;	
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;
    
    /** 1. Make sure there is no host access */
    flush_cache_all();
	outer_flush_all();
	flush_tlb_all();

	for(i=0;i<16;i++)
	{
	    n=temp[1024*i];
        barrier();
	}
    n= pDDR_Reg->SCFG.d32;
    n= pPHY_Reg->RIDR;
    n= pCRU_Reg->CRU_PLL_CON[0][0];
    n= pPMU_Reg->PMU_WAKEUP_CFG[0];
    n= *(volatile uint32_t *)SysSrv_DdrConf;
    dsb();
    
    //idle_port();
    move_to_Lowpower_state();

    phy_dll_bypass_set(0);  //dll bypass
    pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1<<2)<<16) | (1<<2);  //disable DDR PHY clock
    dsb();
    delayus(1);
    pCRU_Reg->CRU_MODE_CON = (0x3<<((1*4) +  16)) | (0x0<<(1*4));            //PLL slow-mode
    dsb();
    delayus(1);    

    pPHY_Reg->DSGCR = pPHY_Reg->DSGCR&(~((0x1<<28)|(0x1<<29)));
}
EXPORT_SYMBOL(ddr_suspend);

void __sramfunc ddr_resume(void)
{
    pPHY_Reg->DSGCR = pPHY_Reg->DSGCR|((0x1<<28)|(0x1<<29));
    dsb();
    
    pCRU_Reg->CRU_MODE_CON = (0x3<<((1*4) +  16))  | (0x1<<(1*4));            //PLL normal
    dsb();
    pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1<<2)<<16) | (0<<2);  //enable DDR PHY clock
    dsb();
    delayus(10);   //wait pll lock
    phy_dll_bypass_set(ddr_freq);    
    reset_dll();
    move_to_Access_state();
    //deIdle_port();
}
EXPORT_SYMBOL(ddr_resume);

//获取容量，返回字节数
uint32 ddr_get_cap(void)
{
    uint32 i;
    uint32 row;
    uint32 cs;

    i = *(volatile uint32*)SysSrv_DdrConf;
    switch((pPHY_Reg->PGCR>>18) & 0xF)
    {
        case 0xF:
            cs = 4;
        case 7:
            cs = 3;
            break;
        case 3:
            cs = 2;
            break;
        default:
            cs = 1;
            break;
    }
    row = ddr_cfg_2_rbc[i].row;
    if(pGRF_Reg->GRF_SOC_CON[2] &  (1<<1))
    {
        row += 1;
    }

    return (1 << (row+(ddr_cfg_2_rbc[i].col)+(ddr_cfg_2_rbc[i].bank)+2))*cs;
}
EXPORT_SYMBOL(ddr_get_cap);

int ddr_init(uint32_t dram_type, uint32_t freq)
{
    volatile uint32_t value = 0;
    uint32_t cs;
    uint32_t gsr,dqstr;

    ddr_print("version 1.00 20120424 \n");

    mem_type = pPHY_Reg->DCR.b.DDRMD;
    ddr_type = dram_type;
    ddr_freq = freq;    
    switch(mem_type)
    {
        case DDR3:
            ddr_print("DDR3 Device\n");
            break;
        case LPDDR2:
            ddr_print("LPDDR2 Device\n");
            break;
        case DDR2:
            ddr_print("DDR2 Device\n");
            break;
        case DDR:
            ddr_print("DDR Device\n");
            break;
        default:
            ddr_print("LPDDR Device\n");
            break;
    }
    switch((pPHY_Reg->PGCR>>18) & 0xF)
    {
        case 0xF:
            cs = 4;
        case 7:
            cs = 3;
            break;
        case 3:
            cs = 2;
            break;
        default:
            cs = 1;
            break;
    }
    //get capability per chip, not total size, used for calculate tRFC
    capability = ddr_get_cap()/cs;
    ddr_print("%d CS, ROW=%d, Bank=%d, COL=%d, Total Capability=%dMB\n", 
                                                                    cs, \
                                                                    get_row(), \
                                                                    (0x1<<(get_bank())), \
                                                                    get_col(), \
                                                                    (ddr_get_cap()>>20));
    ddr_adjust_config(mem_type);

    value=ddr_change_freq(freq);
    ddr_print("init success!!! freq=%dMHz\n", value);

    for(value=0;value<4;value++)
    {
        gsr = pPHY_Reg->DATX8[value].DXGSR[0];
        dqstr = pPHY_Reg->DATX8[value].DXDQSTR;
        ddr_print("DTONE=0x%x, DTERR=0x%x, DTIERR=0x%x, DTPASS=0x%x, DGSL=%d extra clock, DGPS=%d\n", \
                   (gsr&0xF), ((gsr>>4)&0xF), ((gsr>>8)&0xF), ((gsr>>13)&0xFFF), (dqstr&0x7), (((dqstr>>12)&0x3)*90));
    }
    ddr_print("ZERR=%x, ZDONE=%x, ZPD=0x%x, ZPU=0x%x, OPD=0x%x, OPU=0x%x\n", \
                                                (pPHY_Reg->ZQ0SR[0]>>30)&0x1, \
                                                (pPHY_Reg->ZQ0SR[0]>>31)&0x1, \
                                                pPHY_Reg->ZQ0SR[1]&0x3,\
                                                (pPHY_Reg->ZQ0SR[1]>>2)&0x3,\
                                                (pPHY_Reg->ZQ0SR[1]>>4)&0x3,\
                                                (pPHY_Reg->ZQ0SR[1]>>6)&0x3);
    ddr_print("DRV Pull-Up=0x%x, DRV Pull-Dwn=0x%x\n", pPHY_Reg->ZQ0SR[0]&0x1F, (pPHY_Reg->ZQ0SR[0]>>5)&0x1F);
    ddr_print("ODT Pull-Up=0x%x, ODT Pull-Dwn=0x%x\n", (pPHY_Reg->ZQ0SR[0]>>10)&0x1F, (pPHY_Reg->ZQ0SR[0]>>15)&0x1F);

    return 0;
}
EXPORT_SYMBOL(ddr_init);

