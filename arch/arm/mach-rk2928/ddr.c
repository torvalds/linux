/*
 * arch/arm/mach-rk2928/ddr.c-- for ddr3&ddr2
 *
 * Function Driver for DDR controller
 *
 * Copyright (C) 2012 Fuzhou Rockchip Electronics Co.,Ltd
 * Author: 
 * hcy@rock-chips.com
 * yk@rock-chips.com
 * typ@rock-chips.com
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

typedef uint32_t uint32 ;


#define DDR3_DDR2_DLL_DISABLE_FREQ    (125)   //lvddr3 频率太低时dll不能正常工作
#define DDR3_DDR2_ODT_DISABLE_FREQ    (333)
#define SR_IDLE                       (0x1)   //unit:32*DDR clk cycle, and 0 for disable auto self-refresh
#define PD_IDLE                       (0x40)  //unit:DDR clk cycle, and 0 for disable auto power-down
#define PHY_ODT_DISABLE_FREQ          (333)  //定义odt disable的频率
#define PHY_DLL_DISABLE_FREQ          (666)  //定义dll bypass的频率

//#define PMU_BASE_ADDR           RK30_PMU_BASE //??RK 2928 PMU在哪里
#define SDRAMC_BASE_ADDR        RK2928_DDR_PCTL_BASE
#define DDR_PHY_BASE            RK2928_DDR_PHY_BASE
#define CRU_BASE_ADDR           RK2928_CRU_BASE
#define REG_FILE_BASE_ADDR      RK2928_GRF_BASE
#define SysSrv_DdrConf          (RK2928_CPU_AXI_BUS_BASE+0x08)
#define SysSrv_DdrTiming        (RK2928_CPU_AXI_BUS_BASE+0x0c)
#define SysSrv_DdrMode          (RK2928_CPU_AXI_BUS_BASE+0x10)
#define SysSrv_ReadLatency      (RK2928_CPU_AXI_BUS_BASE+0x14)

#define ddr_print(x...) printk( "DDR DEBUG: " x )


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


#define DDR_PLL_REFDIV  (1)
#define FBDIV(n)        ((0xFFF<<16) | (n&0xfff))
#define REFDIV(n)       ((0x3F<<16) | (n&0x3f))
#define POSTDIV1(n)     ((0x7<<(12+16)) | ((n&0x7)<<12))
#define POSTDIV2(n)     ((0x7<<(6+16)) | ((n&0x7)<<6))

#define PLL_LOCK_STATUS  (0x1<<10)
 //CRU Registers
typedef volatile struct tagCRU_STRUCT
{
    uint32 CRU_PLL_CON[4][4];           //cru_pll_con[][4] reserved
    uint32 CRU_MODE_CON;
    uint32 CRU_CLKSEL_CON[35];
    uint32 CRU_CLKGATE_CON[10];
    uint32 reserved2[(0x100-0xf8)/4];
    uint32 CRU_GLB_SRST_FST_VALUE;
    uint32 CRU_GLB_SRST_SND_VALUE;
    uint32 reserved3[(0x110-0x108)/4];
    uint32 CRU_SOFTRST_CON[9];
    uint32 CRU_MISC_CON;
    uint32 reserved4[(0x140-0x138)/4];
    uint32 CRU_GLB_CNT_TH;
} CRU_REG, *pCRU_REG;

#define pCRU_Reg ((pCRU_REG)CRU_BASE_ADDR)

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

//GRF_OS_REG1   ddr message
#define DDR_RANK_COUNT   (11)
#define DDR_COL_COUNT    (9)
#define DDR_BANK_COUNT   (8)
#define DDR_ROW_COUNT    (6)
/********************************
GRF 寄存器中GRF_OS_REG1 存ddr rank，type等信息
GRF_SOC_CON2寄存器中控制c_sysreq信号向pctl发送进入low power 请求
GRF_DDRC_STAT 可查询pctl是否接受请求 进入low power 
********************************/
//REG FILE registers    
typedef volatile struct tagREG_FILE
{
    uint32 reserved1[(0xa8-0x0)/4];     //42
    uint32 GRF_GPIO_IOMUX[16];      //其中 12，13reserved
    uint32 reserved2[(0x118-0xe8)/4];   //12
    GPIO_LH_T GRF_GPIO_PULL[4];
    uint32 reserved3[(0x140-0x138)/4];  // 2
    uint32 GRF_SOC_CON[3];
    uint32 GRF_SOC_STATUS0;
    uint32 GRF_LCDS_CON0;
    uint32 reserved4[(0x15c-0x154)/4];  // 2
    uint32 GRF_DMAC1_CON[3];
    uint32 reserved5[(0x16c-0x168)/4];  // 1
    uint32 GRF_UOC0_CON[5];
    uint32 GRF_UOC1_CON[6];
    uint32 reserved6[(0x19c-0x198)/4];  // 1
    uint32 GRF_DDRC_STAT;
    uint32 reserved7[(0x1c8-0x1a0)/4];  //10
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

//PHY_REG2
#define PHY_AUTO_CALIBRATION (1<<0)
#define PHY_SW_CALIBRATION   (1<<1)
#define PHY_MEM_TYPE         (6)

//PHY_REG22,25,26,27,28
#define PHY_RON_DISABLE     (0)
#define PHY_RON_138O        (1)
//#define PHY_RON_69O         (2)
//#define PHY_RON_46O         (3)
#define PHY_RON_69O         (4)
#define PHY_RON_46O         (5)
#define PHY_RON_34O         (6)
#define PHY_RON_28O         (7)

#define PHY_RTT_DISABLE     (0)
#define PHY_RTT_212O        (1)
#define PHY_RTT_106O        (4)
#define PHY_RTT_71O         (5)
#define PHY_RTT_53O         (6)
#define PHY_RTT_42O         (7)



/* DDR PHY register struct */
typedef volatile struct DDRPHY_REG_Tag
{
    volatile uint32 PHY_REG1;               //PHY soft reset Register
    volatile uint32 PHY_REG3;               //Burst type select Register
    volatile uint32 PHY_REG2;               //PHY DQS squelch calibration Register
    uint32 reserved1[(0x38-0x0a)/4];
    volatile uint32 PHY_REG4a;              //CL,AL set register
    volatile uint32 PHY_REG4b;              //dqs gata delay select bypass mode register
    uint32 reserved2[(0x54-0x40)/4];
    volatile uint32 PHY_REG16;              //
    uint32 reserved3[(0x5c-0x58)/4];
    volatile uint32 PHY_REG18;              //0x5c
    volatile uint32 PHY_REG19;
    uint32 reserved4[(0x68-0x64)/4];
    volatile uint32 PHY_REG21;              //0x68
    uint32 reserved5[(0x70-0x6c)/4];     
    volatile uint32 PHY_REG22;              //0x70
    uint32 reserved6[(0x80-0x74)/4];
    volatile uint32 PHY_REG25;              //0x80
    volatile uint32 PHY_REG26;
    volatile uint32 PHY_REG27;
    volatile uint32 PHY_REG28;
    uint32 reserved7[(0xd4-0x90)/4];
    volatile uint32 PHY_REG6;               //0xd4
    volatile uint32 PHY_REG7;
    uint32 reserved8[(0xe0-0xdc)/4];
    volatile uint32 PHY_REG8;               //0xe0
    volatile uint32 PHY_REG0e4;             //use for DQS ODT off
    uint32 reserved9[(0x114-0xe8)/4];
    volatile uint32 PHY_REG9;               //0x114
    volatile uint32 PHY_REG10;
    uint32 reserved10[(0x120-0x11c)/4];
    volatile uint32 PHY_REG11;              //0x120
    volatile uint32 PHY_REG124;             //use for DQS ODT off
    uint32 reserved11[(0x1c0-0x128)/4];
    volatile uint32 PHY_REG29;              //0x1c0
    uint32 reserved12[(0x264-0x1c4)/4];
	volatile uint32 PHY_REG264;             //use for phy soft reset
	uint32 reserved13[(0x2b0-0x268)/4];
    volatile uint32 PHY_REG2a;              //0x2b0
    uint32 reserved14[(0x2c4-0x2b4)/4];
//    volatile uint32 PHY_TX_DeSkew[24];        //0x2c4-0x320
    volatile uint32 PHY_REG30;
    volatile uint32 PHY_REG31;
    volatile uint32 PHY_REG32;
    volatile uint32 PHY_REG33;
    volatile uint32 PHY_REG34;
    volatile uint32 PHY_REG35;
    volatile uint32 PHY_REG36;
    volatile uint32 PHY_REG37;
    volatile uint32 PHY_REG38;
    volatile uint32 PHY_REG39;
    volatile uint32 PHY_REG40;
    volatile uint32 PHY_REG41;
    volatile uint32 PHY_REG42;
    volatile uint32 PHY_REG43;
    volatile uint32 PHY_REG44;
    volatile uint32 PHY_REG45;
    volatile uint32 PHY_REG46;
    volatile uint32 PHY_REG47;
    volatile uint32 PHY_REG48;
    volatile uint32 PHY_REG49;
    volatile uint32 PHY_REG50;
    volatile uint32 PHY_REG51;
    volatile uint32 PHY_REG52;
    volatile uint32 PHY_REG53;
    uint32 reserved15[(0x328-0x324)/4];
//    volatile uint32 PHY_RX_DeSkew[11];      //0x328-0x350
    volatile uint32 PHY_REG54;
    volatile uint32 PHY_REG55;
    volatile uint32 PHY_REG56;
    volatile uint32 PHY_REG57;
    volatile uint32 PHY_REG58;
    volatile uint32 PHY_REG59;
    volatile uint32 PHY_REG5a;
    volatile uint32 PHY_REG5b;
    volatile uint32 PHY_REG5c;
    volatile uint32 PHY_REG5d;
    volatile uint32 PHY_REG5e;    
    uint32 reserved16[(0x3c4-0x354)/4];
    volatile uint32 PHY_REG5f;              //0x3c4
    uint32 reserved17[(0x3e0-0x3c8)/4];
    volatile uint32 PHY_REG60;
    volatile uint32 PHY_REG61;
    volatile uint32 PHY_REG62;            
}DDRPHY_REG_T, *pDDRPHY_REG_T;

#define pPHY_Reg ((pDDRPHY_REG_T)DDR_PHY_BASE)

typedef enum DRAM_TYPE_Tag
{
    LPDDR = 0,
    DDR,
    DDR2,
    DDR3,
    LPDDR2_S2,
    LPDDR2_S4,

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

typedef struct BACKUP_REG_Tag
{
    PCTL_REG_T pctl;
    uint32 DdrConf;
    NOC_TIMING_T noc_timing;
    uint32 DdrMode;
    uint32 ReadLatency;
    uint32 ddrMR[4];
}BACKUP_REG_T;

__sramdata BACKUP_REG_T ddr_reg;


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

__sramdata uint32_t mem_type;    //0:DDR3  1:DDR2  ;与Inno PHY 的PHY_REG2 里设置的值相一致
static __sramdata uint32_t ddr_speed_bin;    // used for ddr3 only
static __sramdata uint32_t ddr_capability_per_die;  // one chip cs capability
static __sramdata uint32_t ddr_freq;
static __sramdata uint32_t ddr_sr_idle;
static __sramdata uint32_t ddr_dll_status;  // 记录ddr dll的状态，在selfrefresh exit时选择是否进行dll reset

/****************************************************************************
Internal sram us delay function
Cpu highest frequency is 1.6 GHz
1 cycle = 1/1.6 ns
1 us = 1000 ns = 1000 * 1.6 cycles = 1600 cycles
*****************************************************************************/
static __sramdata uint32_t loops_per_us;

#define LPJ_100MHZ  999456UL

/*----------------------------------------------------------------------
Name	: void __sramlocalfunc ddr_delayus(uint32_t us)
Desc	: ddr 延时函数
Params  : uint32_t us  --延时时间
Return  : void
Notes   : loops_per_us 为全局变量 需要根据arm freq而定
----------------------------------------------------------------------*/

/*static*/ void __sramlocalfunc ddr_delayus(uint32_t us)
{   
    uint32_t count;
     
    count = loops_per_us*us;
    while(count--)  // 3 cycles
        barrier();
}

/*----------------------------------------------------------------------
Name	: __sramfunc void ddr_copy(uint32 *pDest, uint32 *pSrc, uint32 words)
Desc	: ddr 拷贝寄存器函数
Params  : pDest ->目标寄存器首地址
          pSrc  ->源标寄存器首地址
          words ->拷贝长度
Return  : void
Notes   : 
----------------------------------------------------------------------*/

__sramfunc void ddr_copy(uint32 *pDest, uint32 *pSrc, uint32 words)
{
    uint32 i;

    for(i=0; i<words; i++)
    {
        pDest[i] = pSrc[i];
    }
}

/*----------------------------------------------------------------------
Name	: uint32 ddr_get_row(void)
Desc	: 获取ddr row 信息
Params  : void
Return  : row 数
Notes   : 
----------------------------------------------------------------------*/
uint32 ddr_get_row(void)
{
    return (13+((pGRF_Reg->GRF_OS_REG[1] >> DDR_ROW_COUNT) & 0x3));
}

/*----------------------------------------------------------------------
Name	: uint32 ddr_get_bank(void)
Desc	: 获取ddr bank 信息
Params  : void
Return  : bank 数
Notes   : 
----------------------------------------------------------------------*/
uint32 ddr_get_bank(void)
{
     return (((pGRF_Reg->GRF_OS_REG[1] >> DDR_BANK_COUNT) & 0x1)? 2:3);
}

/*----------------------------------------------------------------------
Name	: uint32 ddr_get_col(void)
Desc	: 获取ddr col 信息
Params  : void
Return  : col 数
Notes   : 
----------------------------------------------------------------------*/
uint32 ddr_get_col(void)
{
    return (9 + ((pGRF_Reg->GRF_OS_REG[1]>>DDR_COL_COUNT)&0x3));
}

/*----------------------------------------------------------------------
Name	: __sramfunc void ddr_move_to_Lowpower_state(void)
Desc	: pctl 进入 lowpower state
Params  : void
Return  : void
Notes   : 
----------------------------------------------------------------------*/
__sramfunc void ddr_move_to_Lowpower_state(void)
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

/*----------------------------------------------------------------------
Name	: __sramfunc void ddr_move_to_Access_state(void)
Desc	: pctl 进入 Access state
Params  : void
Return  : void
Notes   : 
----------------------------------------------------------------------*/
__sramfunc void ddr_move_to_Access_state(void)
{
    volatile uint32 value;

    //set auto self-refresh idle
    pDDR_Reg->MCFG1=(pDDR_Reg->MCFG1&0xffffff00)|ddr_sr_idle | (1<<31);

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
    pGRF_Reg->GRF_SOC_CON[2] = (1<<16 | 0);//de_hw_wakeup :enable auto sr if sr_idle != 0
}

/*----------------------------------------------------------------------
Name	: __sramfunc void ddr_move_to_Config_state(void)
Desc	: pctl 进入 config state
Params  : void
Return  : void
Notes   : 
----------------------------------------------------------------------*/
__sramfunc void ddr_move_to_Config_state(void)
{
    volatile uint32 value;
    pGRF_Reg->GRF_SOC_CON[2] = (1<<16 | 1); //hw_wakeup :disable auto sr
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
            case Access:
            case Init_mem:
                pDDR_Reg->SCTL = CFG_STATE;
                dsb();
                break;
            default:  //Transitional state
                break;
        }
    }
}

/*----------------------------------------------------------------------
Name	: void __sramlocalfunc ddr_send_command(uint32 rank, uint32 cmd, uint32 arg)
Desc	: 通过写 pctl MCMD寄存器向ddr发送命令
Params  : rank ->ddr rank 数
          cmd  ->发送命令类型
          arg  ->发送的数据
Return  : void 
Notes   : arg包括bank_addr和cmd_addr
----------------------------------------------------------------------*/
void __sramlocalfunc ddr_send_command(uint32 rank, uint32 cmd, uint32 arg)
{
    pDDR_Reg->MCMD = (start_cmd | (rank<<20) | arg | cmd);
    dsb();
    while(pDDR_Reg->MCMD & start_cmd);
}

__sramdata uint32 copy_data[8]={0xffffffff,0x00000000,0x55555555,0xAAAAAAAA,
        			0xEEEEEEEE,0x11111111,0x22222222,0xDDDDDDDD};/**/

/*----------------------------------------------------------------------
Name	: uint32_t __sramlocalfunc ddr_data_training(void)
Desc	: 对ddr做data training
Params  : void
Return  : void 
Notes   : 没有做data training校验
----------------------------------------------------------------------*/
uint32_t __sramlocalfunc ddr_data_training(void)
{
    uint32 value, cs;
    value = pDDR_Reg->TREFI;
    pDDR_Reg->TREFI = 0;
    cs = (pGRF_Reg->GRF_OS_REG[1] >> DDR_RANK_COUNT) & 0x1;
    cs = cs + (1 << cs);                               //case 0:1rank cs=1; case 1:2rank cs =3;
    // trigger DTT
    pPHY_Reg->PHY_REG2 = ((pPHY_Reg->PHY_REG2 & (~0x1)) | PHY_AUTO_CALIBRATION);
    // wait echo byte DTDONE
    ddr_delayus(6);
    // stop DTT
    while((pPHY_Reg->PHY_REG62 & 0x3)!=0x3);
    pPHY_Reg->PHY_REG2 = (pPHY_Reg->PHY_REG2 & (~0x1));
    // send some auto refresh to complement the lost while DTT
    ddr_send_command(cs, REF_cmd, 0);    
    ddr_send_command(cs, REF_cmd, 0);
    ddr_send_command(cs, REF_cmd, 0);    
    ddr_send_command(cs, REF_cmd, 0);

    // resume auto refresh
    pDDR_Reg->TREFI = value;

    return(0);
}

/*----------------------------------------------------------------------
Name    : void __sramlocalfunc ddr_set_dll_bypass(uint32 freq)
Desc    : 设置PHY dll 工作模式
Params  : freq -> ddr工作频率
Return  : void 
Notes   : 
----------------------------------------------------------------------*/
void __sramlocalfunc ddr_set_dll_bypass(uint32 freq)
{
    if(freq <= PHY_DLL_DISABLE_FREQ)
    {
        pPHY_Reg->PHY_REG2a = 0x1F;         //set cmd,left right dll bypass
        pPHY_Reg->PHY_REG19 = 0x08;         //cmd slave dll
        pPHY_Reg->PHY_REG6 = 0x18;          //left TX DQ DLL
        pPHY_Reg->PHY_REG7 = 0x00;          //left TX DQS DLL
        pPHY_Reg->PHY_REG9 = 0x18;          //right TX DQ DLL
        pPHY_Reg->PHY_REG10 = 0x00;         //right TX DQS DLL
        
    }
    else 
    {
        pPHY_Reg->PHY_REG2a = 0x03;         //set cmd,left right dll bypass
        pPHY_Reg->PHY_REG19 = 0x08;         //cmd slave dll
        pPHY_Reg->PHY_REG6 = 0x0c;          //left TX DQ DLL
        pPHY_Reg->PHY_REG7 = 0x00;          //left TX DQS DLL
        pPHY_Reg->PHY_REG9 = 0x0c;          //right TX DQ DLL
        pPHY_Reg->PHY_REG10 = 0x00;         //right TX DQS DLL                
    }
    dsb();
    //其他与dll相关的寄存器有:REG8(RX DQS),REG11(RX DQS),REG18(CMD),REG21(CK) 保持默认值
}


static __sramdata uint32_t clkFbDiv;
static __sramdata uint32_t clkPostDiv1;
static __sramdata uint32_t clkPostDiv2;

/*****************************************
REFDIV   FBDIV     POSTDIV1/POSTDIV2      FOUTPOSTDIV           freq Step        FOUTPOSRDIV            finally use
==================================================================================================================
1        17 - 66   4                      100MHz - 400MHz          6MHz          200MHz  <= 300MHz             <= 150MHz
1        17 - 66   3                      133MHz - 533MHz          8MHz             
1        17 - 66   2                      200MHz - 800MHz          12MHz         300MHz  <= 600MHz      150MHz <= 300MHz
1        17 - 66   1                      400MHz - 1600MHz         24MHz         600MHz  <= 1200MHz     300MHz <= 600MHz
******************************************/
//for minimum jitter operation, the highest VCO and FREF frequencies should be used.
/*----------------------------------------------------------------------
Name    : uint32_t __sramlocalfunc ddr_set_pll(uint32_t nMHz, uint32_t set)
Desc    : 设置ddr pll
Params  : nMHZ -> ddr工作频率
          set  ->0获取设置的频率信息
                 1设置ddr pll
Return  : 设置的频率值 
Notes   : 在变频时需要先set=0调用一次ddr_set_pll，再set=1 调用ddr_set_pll
----------------------------------------------------------------------*/
uint32_t __sramlocalfunc ddr_set_pll(uint32_t nMHz, uint32_t set)
{
    uint32_t ret = 0;
    int delay = 1000;
    uint32_t pll_id=1;  //DPLL     
    
    if(nMHz == 24)
    {
        ret = 24;
        goto out;
    }
    
    if(!set)
    {
        if(nMHz <= 150)
        {
            clkPostDiv1 = 4;
        }
        else if(nMHz <= 300)
        {
            clkPostDiv1 = 2;
        }
        else
        {
            clkPostDiv1 = 1;
        }
        clkPostDiv2 = 1;
        clkFbDiv = (nMHz * 2 * DDR_PLL_REFDIV * clkPostDiv1 * clkPostDiv2)/24;//最后送入ddr的是再经过2分频
        ret = (24 * clkFbDiv)/(2 * DDR_PLL_REFDIV * clkPostDiv1 * clkPostDiv2);
    }
    else
    {
        pCRU_Reg->CRU_MODE_CON = (0x1<<((pll_id*4) +  16)) | (0x0<<(pll_id*4));            //PLL slow-mode
    
        pCRU_Reg->CRU_PLL_CON[pll_id][0] = FBDIV(clkFbDiv) | POSTDIV1(clkPostDiv1);
        pCRU_Reg->CRU_PLL_CON[pll_id][1] = REFDIV(DDR_PLL_REFDIV) | POSTDIV2(clkPostDiv2) | (0x10001<<12);//interger mode

        ddr_delayus(1);

        while (delay > 0) 
        {
    	    ddr_delayus(1);
    		if (pCRU_Reg->CRU_PLL_CON[pll_id][1] & (PLL_LOCK_STATUS))        // wait for pll locked
    			break;
    		delay--;
    	}
        
        pCRU_Reg->CRU_CLKSEL_CON[26] = ((0x3<<16) | 0x0);           //clk_ddr_src:clk_ddrphy = 1:1       
        pCRU_Reg->CRU_MODE_CON = (0x1<<((pll_id*4) +  16))  | (0x1<<(pll_id*4));            //PLL normal
    }
out:
    return ret;
}

/*----------------------------------------------------------------------
Name    : uint32_t ddr_get_parameter(uint32_t nMHz)
Desc    : 获取配置参数
Params  : nMHZ -> ddr工作频率     
Return  : 0 成功
          -1 失败
          -4 频率值超过颗粒最大频率
Notes   : 
----------------------------------------------------------------------*/
uint32_t ddr_get_parameter(uint32_t nMHz)
{
    uint32_t tmp;
    uint32_t ret = 0;
    uint32_t al;
    uint32_t bl;
    uint32_t cl;
    uint32_t cwl;    
    PCTL_TIMING_T *p_pctl_timing=&(ddr_reg.pctl.pctl_timing);
    NOC_TIMING_T  *p_noc_timing=&(ddr_reg.noc_timing);

    p_pctl_timing->togcnt1u = nMHz;
    p_pctl_timing->togcnt100n = nMHz/10;
    p_pctl_timing->tinit = 200;
    p_pctl_timing->trsth = 500;

    if(mem_type == DDR3)
    {
        if(ddr_speed_bin > DDR3_DEFAULT)
        {
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
        cl = ddr3_cl_cwl[ddr_speed_bin][tmp] >> 16;
        cwl = ddr3_cl_cwl[ddr_speed_bin][tmp] & 0x0ff;
        if(cl == 0)
        {
            ret = -4; //超过颗粒的最大频率
        }
        if(nMHz <= DDR3_DDR2_ODT_DISABLE_FREQ)     
        {
            ddr_reg.ddrMR[1] = DDR3_DS_40 | DDR3_Rtt_Nom_DIS;
        }
        else
        {
            ddr_reg.ddrMR[1] = DDR3_DS_40 | DDR3_Rtt_Nom_120;
        }
        ddr_reg.ddrMR[2] = DDR3_MR2_CWL(cwl) /* | DDR3_Rtt_WR_60 */;
        ddr_reg.ddrMR[3] = 0;
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
        /*
         * tXSR, =tDLLK=512 tCK
         */
        p_pctl_timing->texsr = DDR3_tDLLK;
        /*
         * tRP=CL
         */
        p_pctl_timing->trp = cl;
        /*
         * WrToMiss=WL*tCK + tWR + tRP + tRCD
         */
        p_noc_timing->b.WrToMiss = ((cwl+((DDR3_tWR*nMHz+999)/1000)+cl+cl)&0x3F);
        /*
         * tRC=tRAS+tRP
         */
        p_pctl_timing->trc = ((((ddr3_tRC_tFAW[ddr_speed_bin]>>16)*nMHz+999)/1000)&0x3F);
        p_noc_timing->b.ActToAct = ((((ddr3_tRC_tFAW[ddr_speed_bin]>>16)*nMHz+999)/1000)&0x3F);

        p_pctl_timing->trtw = (cl+2-cwl);//DDR3_tRTW;
        p_noc_timing->b.RdToWr = ((cl+2-cwl)&0x1F);
        p_pctl_timing->tal = al;
        p_pctl_timing->tcl = cl;
        p_pctl_timing->tcwl = cwl;
        /*
         * tRAS, 37.5ns(400MHz)     37.5ns(533MHz)
         */
        p_pctl_timing->tras = (((DDR3_tRAS*nMHz+(nMHz>>1)+999)/1000)&0x3F);
        /*
         * tRCD=CL
         */
        p_pctl_timing->trcd = cl;
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
        /*
         * tRTP, max(4 tCK,7.5ns)
         */
        tmp = ((DDR3_tRTP*nMHz+(nMHz>>1)+999)/1000);
        if(tmp < 4)
        {
            tmp = 4;
        }
        p_pctl_timing->trtp = tmp&0xF;
        /*
         * RdToMiss=tRTP+tRP + tRCD - (BL/2 * tCK)
         */
        p_noc_timing->b.RdToMiss = ((tmp+cl+cl-(bl>>1))&0x3F);
        /*
         * tWR, 15ns
         */
        tmp = ((DDR3_tWR*nMHz+999)/1000);
        p_pctl_timing->twr = tmp&0x1F;
        if(tmp<9)
            tmp = tmp - 4;
        else
            tmp = tmp>>1;
        ddr_reg.ddrMR[0] = DDR3_BL8 | DDR3_CL(cl) | DDR3_WR(tmp);

        /*
         * tWTR, max(4 tCK,7.5ns)
         */
        tmp = ((DDR3_tWTR*nMHz+(nMHz>>1)+999)/1000);
        if(tmp < 4)
        {
            tmp = 4;
        }
        p_pctl_timing->twtr = tmp&0xF;
        p_noc_timing->b.WrToRd = ((tmp+cwl)&0x1F);
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
        /*
         * tMOD, max(12 tCK,15ns)
         */
        tmp = ((DDR3_tMOD*nMHz+999)/1000);
        if(tmp < 12)
        {
            tmp = 12;
        }
        p_pctl_timing->tmod = tmp&0x1F;
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
         * NOC Timing
         **************************************************/
        p_noc_timing->b.BurstLen = ((bl>>1)&0x7);
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
        if(nMHz <= DDR3_DDR2_ODT_DISABLE_FREQ)
        {
            ddr_reg.ddrMR[1] = DDR2_STR_REDUCE | DDR2_Rtt_Nom_DIS;
        }
        else
        {
            ddr_reg.ddrMR[1] = DDR2_STR_REDUCE | DDR2_Rtt_Nom_75;
        }
        ddr_reg.ddrMR[2] = 0;
        ddr_reg.ddrMR[3] = 0;
        /**************************************************
         * PCTL Timing
         **************************************************/
        /*
         * tREFI, average periodic refresh interval, 7.8us
         */
        p_pctl_timing->trefi = DDR2_tREFI_7_8_us;
        /*
         * tMRD, 2 tCK
         */
        p_pctl_timing->tmrd = DDR2_tMRD & 0x7;
        /*
         * tRFC, 75ns(256Mb) 105ns(512Mb) 127.5ns(1Gb) 195ns(2Gb) 327.5ns(4Gb)
         */
        if(ddr_capability_per_die <= 0x2000000)  // 256Mb
        {
            tmp = DDR2_tRFC_256Mb;
        }
        else if(ddr_capability_per_die <= 0x4000000) // 512Mb
        {
            tmp = DDR2_tRFC_512Mb;
        }
        else if(ddr_capability_per_die <= 0x8000000)  // 1Gb
        {
            tmp = DDR2_tRFC_1Gb;
        }
        else if(ddr_capability_per_die <= 0x10000000)  // 2Gb
        {
            tmp = DDR2_tRFC_2Gb;
        }
        else  // 4Gb
        {
            tmp = DDR2_tRFC_4Gb;
        }
        p_pctl_timing->trfc = (tmp*nMHz+999)/1000;
        /*
         * tXSR, max(tRFC+10,200 tCK)
         */
        tmp = (((tmp+10)*nMHz+999)/1000);
        if(tmp<200)
        {
            tmp = 200;
        }
        p_pctl_timing->texsr = tmp&0x3FF;
        /*
         * tRP=CL  DDR2 8bank need to add 1 additional cycles for PREA
         */
        if(ddr_get_bank() == 8)
        {
            p_pctl_timing->trp = (1<<16) | cl;
        }
        else
        {
            p_pctl_timing->trp = cl;
        }
        /*
         * WrToMiss=WL*tCK + tWR + tRP + tRCD
         */
        p_noc_timing->b.WrToMiss = ((cwl+((DDR2_tWR*nMHz+999)/1000)+cl+cl)&0x3F);
        /*
         * tRAS, 45ns
         */
        tmp=((DDR2_tRAS*nMHz+999)/1000);
        p_pctl_timing->tras = (tmp&0x3F);
        /*
         * tRC=tRAS+tRP
         */
        p_pctl_timing->trc = ((tmp+cl)&0x3F);
        p_noc_timing->b.ActToAct = ((tmp+cl)&0x3F);

        p_pctl_timing->trtw = (cl+2-cwl);//DDR2_tRTW;
        p_noc_timing->b.RdToWr = ((cl+2-cwl)&0x1F);
        p_pctl_timing->tal = al;
        p_pctl_timing->tcl = cl;
        p_pctl_timing->tcwl = cwl;
        /*
         * tRCD=CL
         */
        p_pctl_timing->trcd = cl;
        /*
         * tRRD = 10ns(2KB page)
         *
         */
        p_pctl_timing->trrd = (((DDR2_tRRD*nMHz+999)/1000)&0xF);
        /*
         * tRTP, 7.5ns
         */
        tmp = ((DDR2_tRTP*nMHz+(nMHz>>1)+999)/1000);
        p_pctl_timing->trtp = tmp&0xF;
        /*
         * RdToMiss=tRTP+tRP + tRCD - (BL/2 * tCK)
         */
        p_noc_timing->b.RdToMiss = ((tmp+cl+cl-(bl>>1))&0x3F);
        /*
         * tWR, 15ns
         */
        tmp = ((DDR2_tWR*nMHz+999)/1000);
        p_pctl_timing->twr = tmp&0x1F;
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
        p_pctl_timing->twtr = tmp&0xF;
        p_noc_timing->b.WrToRd = ((tmp+cwl)&0x1F);
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
        p_pctl_timing->txp = tmp&0x7;
        /*
         * tXPDLL, =tXP
         */
        p_pctl_timing->txpdll = tmp & 0x3F;
        /*
         * tZQCS, 0
         */
        p_pctl_timing->tzqcs = 0;
        /*
         * tZQCSI,
         */
        p_pctl_timing->tzqcsi = 0;
        /*
         * tDQS,
         */
        p_pctl_timing->tdqs = DDR2_tDQS;
        /*
         * tCKSRE, 1 tCK
         */
        p_pctl_timing->tcksre = DDR2_tCKSRE & 0x1F;
        /*
         * tCKSRX, no such timing
         */
        p_pctl_timing->tcksrx = DDR2_tCKSRX & 0x1F;
        /*
         * tCKE, 3 tCK
         */
        p_pctl_timing->tcke = DDR2_tCKE & 0x7;
        /*
         * tCKESR, =tCKE
         */
        p_pctl_timing->tckesr = DDR2_tCKESR&0xF;
        /*
         * tMOD, 12ns
         */
        p_pctl_timing->tmod = ((DDR2_tMOD*nMHz+999)/1000)&0x1F;
        /*
         * tRSTL, 0
         */
        p_pctl_timing->trstl = 0;
        /*
         * tZQCL, 0
         */
        p_pctl_timing->tzqcl = 0;
        /*
         * tMRR, 0 tCK
         */
        p_pctl_timing->tmrr = 0;
        /*
         * tDPD, 0
         */
        p_pctl_timing->tdpd = 0;

        /**************************************************
         * NOC Timing
         **************************************************/
        p_noc_timing->b.BurstLen = ((bl>>1)&0x7);
    }
    
out:
    return ret;
}

/*----------------------------------------------------------------------
Name    : uint32_t __sramlocalfunc ddr_update_timing(void)
Desc    : 更新pctl phy 相关timing寄存器
Params  : void  
Return  : 0 成功
Notes   : 
----------------------------------------------------------------------*/
uint32_t __sramlocalfunc ddr_update_timing(void)
{
    PCTL_TIMING_T *p_pctl_timing = &(ddr_reg.pctl.pctl_timing);
    NOC_TIMING_T  *p_noc_timing = &(ddr_reg.noc_timing);

    ddr_copy((uint32_t *)&(pDDR_Reg->TOGCNT1U), (uint32_t*)&(p_pctl_timing->togcnt1u), 34);
    pPHY_Reg->PHY_REG3 = (0x12 << 1) | (ddr2_ddr3_bl_8);   //0x12为保留位的默认值，以默认值回写
    pPHY_Reg->PHY_REG4a = ((p_pctl_timing->tcl << 4) | (p_pctl_timing->tal));
    *(volatile uint32_t *)SysSrv_DdrTiming = p_noc_timing->d32;
    // Update PCTL BL
    if(mem_type == DDR3)
    {
        pDDR_Reg->MCFG = (pDDR_Reg->MCFG & (~(0x1|(0x3<<18)|(0x1<<17)|(0x1<<16)))) | ddr2_ddr3_bl_8 | tfaw_cfg(5)|pd_exit_slow|pd_type(1);
        pDDR_Reg->DFITRDDATAEN   = (pDDR_Reg->TAL + pDDR_Reg->TCL)-3;  //trdata_en = rl-3
        pDDR_Reg->DFITPHYWRLAT   = pDDR_Reg->TCWL-1;
    }
    else if(mem_type == DDR2)
    {
        pDDR_Reg->MCFG = (pDDR_Reg->MCFG & (~(0x1|(0x3<<18)|(0x1<<17)|(0x1<<16)))) | ddr2_ddr3_bl_8 | tfaw_cfg(5)|pd_exit_fast|pd_type(1);
    }
    return 0;
}

/*----------------------------------------------------------------------
Name    : uint32_t __sramlocalfunc ddr_update_mr(void)
Desc    : 更新颗粒MR寄存器
Params  : void  
Return  : void
Notes   : 
----------------------------------------------------------------------*/
uint32_t __sramlocalfunc ddr_update_mr(void)
{
    uint32_t cs;

    cs = (pGRF_Reg->GRF_OS_REG[1] >> DDR_RANK_COUNT) & 0x1;
    cs = cs + (1 << cs);                               //case 0:1rank cs=1; case 1:2rank cs =3;
    if(ddr_freq > DDR3_DDR2_DLL_DISABLE_FREQ)
    {
        if(ddr_dll_status == DDR3_DLL_DISABLE)  // off -> on
        {
            ddr_send_command(cs, MRS_cmd, bank_addr(0x1) | cmd_addr((ddr_reg.ddrMR[1])));  //DLL enable
            ddr_send_command(cs, MRS_cmd, bank_addr(0x0) | cmd_addr(((ddr_reg.ddrMR[0]))| DDR3_DLL_RESET));  //DLL reset
            ddr_delayus(2);  //at least 200 DDR cycle
            ddr_send_command(cs, MRS_cmd, bank_addr(0x0) | cmd_addr((ddr_reg.ddrMR[0])));
            ddr_dll_status = DDR3_DLL_ENABLE;
        }
        else // on -> on
        {
            ddr_send_command(cs, MRS_cmd, bank_addr(0x1) | cmd_addr((ddr_reg.ddrMR[1])));
            ddr_send_command(cs, MRS_cmd, bank_addr(0x0) | cmd_addr((ddr_reg.ddrMR[0])));
        }
    }
    else
    {
        ddr_send_command(cs, MRS_cmd, bank_addr(0x1) | cmd_addr(((ddr_reg.ddrMR[1])) | DDR3_DLL_DISABLE));  //DLL disable
        ddr_send_command(cs, MRS_cmd, bank_addr(0x0) | cmd_addr((ddr_reg.ddrMR[0])));
        ddr_dll_status = DDR3_DLL_DISABLE;
    }
    ddr_send_command(cs, MRS_cmd, bank_addr(0x2) | cmd_addr((ddr_reg.ddrMR[2])));

    return 0;
}

/*----------------------------------------------------------------------
Name    : void __sramlocalfunc ddr_update_odt(void)
Desc    : update PHY odt & PHY driver impedance
Params  : void  
Return  : void
Notes   : 
----------------------------------------------------------------------*/
void __sramlocalfunc ddr_update_odt(void)
{
    uint32_t tmp;
    
    //adjust DRV and ODT
    if(ddr_freq <= PHY_ODT_DISABLE_FREQ)
    {
        pPHY_Reg->PHY_REG27 = PHY_RTT_DISABLE;  //dynamic RTT disable, Left 8bit ODT
        pPHY_Reg->PHY_REG28 = PHY_RTT_DISABLE;  //Right 8bit ODT
        pPHY_Reg->PHY_REG0e4 = (0x0E & 0xc)|0x1;//off DQS ODT  bit[1:0]=2'b01 
        pPHY_Reg->PHY_REG124 = (0x0E & 0xc)|0x1;//off DQS ODT  bit[1:0]=2'b01 
    }
    else
    {
        pPHY_Reg->PHY_REG27 = ((PHY_RTT_212O<<3) | PHY_RTT_212O);       //0x5 ODT =  71ohm
        pPHY_Reg->PHY_REG28 = ((PHY_RTT_212O<<3) | PHY_RTT_212O);    
        pPHY_Reg->PHY_REG0e4 = 0x0E;           //on DQS ODT default:0x0E
        pPHY_Reg->PHY_REG124 = 0x0E;           //on DQS ODT default:0x0E
    }
    
    tmp = ((PHY_RON_46O<<3) | PHY_RON_46O);     //0x5 = 46ohm
    pPHY_Reg->PHY_REG16 = tmp;  //CMD driver strength
    pPHY_Reg->PHY_REG22 = tmp;  //CK driver strength    
    pPHY_Reg->PHY_REG25 = tmp;  //Left 8bit DQ driver strength
    pPHY_Reg->PHY_REG26 = tmp;  //Right 8bit DQ driver strength
    dsb();
}

/*----------------------------------------------------------------------
Name    : __sramfunc void ddr_adjust_config(uint32_t dram_type)
Desc    : 
Params  : dram_type ->颗粒类型
Return  : void
Notes   : 
----------------------------------------------------------------------*/
__sramfunc void ddr_adjust_config(uint32_t dram_type)
{
//    uint32 value;
    unsigned long save_sp;
    uint32 i;
    volatile uint32 n; 
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;

    //get data training address before idle port
//    value = ddr_get_datatraing_addr();    //Inno PHY 不需要training address

    /** 1. Make sure there is no host access */
    flush_cache_all();
    outer_flush_all();
    flush_tlb_all();
    DDR_SAVE_SP(save_sp);

    for(i=0;i<2;i++)        //8KB SRAM
    {
        n=temp[1024*i];
        barrier();
    }
    n= pDDR_Reg->SCFG.d32;
    n= pPHY_Reg->PHY_REG1;
    n= pCRU_Reg->CRU_PLL_CON[0][0];
    n= *(volatile uint32_t *)SysSrv_DdrConf;
    dsb();
    
    //enter config state
    ddr_move_to_Config_state();
    pDDR_Reg->DFIODTCFG = ((1<<3) | (1<<11));  //loader中漏了初始化
    //set auto power down idle
    pDDR_Reg->MCFG=(pDDR_Reg->MCFG&0xffff00ff)|(PD_IDLE<<8);

    //enable the hardware low-power interface
    pDDR_Reg->SCFG.b.hw_low_power_en = 1;

    ddr_update_odt();

    //enter access state
    ddr_move_to_Access_state();

    DDR_RESTORE_SP(save_sp);
}

/*----------------------------------------------------------------------
Name    : void __sramlocalfunc ddr_selfrefresh_enter(uint32 nMHz)
Desc    : 进入自刷新
Params  : nMHz ->ddr频率
Return  : void
Notes   : 
----------------------------------------------------------------------*/
void __sramlocalfunc ddr_selfrefresh_enter(uint32 nMHz)
{    
    ddr_move_to_Config_state();
    ddr_move_to_Lowpower_state();
	pPHY_Reg->PHY_REG264 &= ~(1<<1);
    pPHY_Reg->PHY_REG1 = (pPHY_Reg->PHY_REG1 & (~(0x3<<2)));     //phy soft reset
    dsb();
    pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1<<2)<<16) | (1<<2);  //disable DDR PHY clock
    ddr_delayus(1);
}

uint32 dtt_buffer[8];

/*----------------------------------------------------------------------
Name    : void ddr_dtt_check(void)
Desc    : data training check
Params  : void
Return  : void
Notes   : 
----------------------------------------------------------------------*/
void ddr_dtt_check(void)
{
    uint32 i;
    for(i=0;i<8;i++)
    {
        dtt_buffer[i] = copy_data[i];
    }
    dsb();
    flush_cache_all();
    outer_flush_all();
    for(i=0;i<8;i++)
    {
        if(dtt_buffer[i] != copy_data[i])
        {
            sram_printascii("DTT failed!\n");
            break;
        }
        dtt_buffer[i] = 0;
    }

}

/*----------------------------------------------------------------------
Name    : void __sramlocalfunc ddr_selfrefresh_exit(void)
Desc    : 退出自刷新
Params  : void
Return  : void
Notes   : 
----------------------------------------------------------------------*/
void __sramlocalfunc ddr_selfrefresh_exit(void)
{
    pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1<<2)<<16) | (0<<2);  //enable DDR PHY clock
    dsb();
    ddr_delayus(1);
	pPHY_Reg->PHY_REG1 = (pPHY_Reg->PHY_REG1 | (0x3 << 2)); //phy soft de-reset
	pPHY_Reg->PHY_REG264 |= (1<<1);
	dsb();
    ddr_move_to_Config_state();    
    ddr_data_training(); 
    ddr_move_to_Access_state();
    ddr_dtt_check();
}

/*----------------------------------------------------------------------
Name    : void __sramlocalfunc ddr_change_freq_in(uint32 freq_slew)
Desc    : 设置ddr pll前的timing及mr参数调整
Params  : freq_slew :变频斜率 1升平  0降频
Return  : void
Notes   : 
----------------------------------------------------------------------*/
void __sramlocalfunc ddr_change_freq_in(uint32 freq_slew)
{
    uint32 value_100n, value_1u;
    
    if(freq_slew == 1)
    {
        value_100n = ddr_reg.pctl.pctl_timing.togcnt100n;
        value_1u = ddr_reg.pctl.pctl_timing.togcnt1u;
        ddr_reg.pctl.pctl_timing.togcnt1u = pDDR_Reg->TOGCNT1U;
        ddr_reg.pctl.pctl_timing.togcnt100n = pDDR_Reg->TOGCNT100N;
        ddr_update_timing();                
        ddr_update_mr();
        ddr_reg.pctl.pctl_timing.togcnt100n = value_100n;
        ddr_reg.pctl.pctl_timing.togcnt1u = value_1u;
    }
    else
    {
        pDDR_Reg->TOGCNT100N = ddr_reg.pctl.pctl_timing.togcnt100n;
        pDDR_Reg->TOGCNT1U = ddr_reg.pctl.pctl_timing.togcnt1u;
    }

    pDDR_Reg->TZQCSI = 0;    

}

/*----------------------------------------------------------------------
Name    : void __sramlocalfunc ddr_change_freq_out(uint32 freq_slew)
Desc    : 设置ddr pll后的timing及mr参数调整
Params  : freq_slew :变频斜率 1升平  0降频
Return  : void
Notes   : 
----------------------------------------------------------------------*/
void __sramlocalfunc ddr_change_freq_out(uint32 freq_slew)
{
    if(freq_slew == 1)
    {
        pDDR_Reg->TOGCNT100N = ddr_reg.pctl.pctl_timing.togcnt100n;
        pDDR_Reg->TOGCNT1U = ddr_reg.pctl.pctl_timing.togcnt1u;
        pDDR_Reg->TZQCSI = ddr_reg.pctl.pctl_timing.tzqcsi;
    }
    else
    {
        ddr_update_timing();
        ddr_update_mr();
    }
    ddr_data_training();
}

static uint32 save_sp;
/*----------------------------------------------------------------------
Name    : uint32_t __sramfunc ddr_change_freq(uint32_t nMHz)
Desc    : ddr变频
Params  : nMHz -> 变频的频率值
Return  : 频率值
Notes   :
----------------------------------------------------------------------*/
uint32_t __sramfunc ddr_change_freq(uint32_t nMHz)
{
    uint32_t ret;
    u32 i;
    volatile u32 n;	
    unsigned long flags;
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;
    uint32_t regvalue0 = pCRU_Reg->CRU_PLL_CON[0][0];
    uint32_t regvalue1 = pCRU_Reg->CRU_PLL_CON[0][1];
    uint32_t freq;
	uint32 freq_slew;
	
     // freq = (fin*fbdiv/(refdiv * postdiv1 * postdiv2))
     if((pCRU_Reg->CRU_MODE_CON & 1) == 1)             // CPLL Normal mode
     {
         freq = 24*(regvalue0&0xfff)     
                /((regvalue1&0x3f)*((regvalue0>>12)&0x7)*((regvalue1>>6)&0x7));                  
     }  
     else
     {
        freq = 24;
     }     
    loops_per_us = LPJ_100MHZ*freq / 1000000;
    
    ret=ddr_set_pll(nMHz,0);
    if(ret == ddr_freq)
    {
        goto out;
    }
    else 
    {
        freq_slew = (ret>ddr_freq)? 1 : -1;
    }    
    ddr_get_parameter(ret);
    /** 1. Make sure there is no host access */
    local_irq_save(flags);
	local_fiq_disable();
    flush_cache_all();
	outer_flush_all();
	flush_tlb_all();
	DDR_SAVE_SP(save_sp);
	for(i=0;i<2;i++)    //8KB SRAM
	{
	    n=temp[1024*i];
        barrier();
	}
    n= pDDR_Reg->SCFG.d32;
    n= pPHY_Reg->PHY_REG1;
    n= pCRU_Reg->CRU_PLL_CON[0][0];
    n= *(volatile uint32_t *)SysSrv_DdrConf;
    n= pGRF_Reg->GRF_SOC_STATUS0;
    dsb();
    ddr_move_to_Config_state();    
    ddr_freq = ret;
    ddr_change_freq_in(freq_slew);
    ddr_move_to_Lowpower_state();
    pPHY_Reg->PHY_REG264 &= ~(1<<1);
    pPHY_Reg->PHY_REG1 = (pPHY_Reg->PHY_REG1 & (~(0x3<<2)));     //phy soft reset
    dsb();    
    /** 3. change frequence  */
    ddr_set_pll(ret,1);
    ddr_set_dll_bypass(ddr_freq);    //set phy dll mode;
	pPHY_Reg->PHY_REG1 = (pPHY_Reg->PHY_REG1 | (0x3 << 2)); //phy soft de-reset
	pPHY_Reg->PHY_REG264 |= (1<<1);
	dsb();
	ddr_update_odt();
    ddr_move_to_Config_state();
    ddr_change_freq_out(freq_slew);
    ddr_move_to_Access_state();
    ddr_dtt_check();
    /** 5. Issues a Mode Exit command   */
    DDR_RESTORE_SP(save_sp);
    local_fiq_enable();
    local_irq_restore(flags);
//    clk_set_rate(clk_get(NULL, "ddr_pll"), 0);    
out:
    return ret;
}


EXPORT_SYMBOL(ddr_change_freq);

/*----------------------------------------------------------------------
Name    : void ddr_set_auto_self_refresh(bool en)
Desc    : 设置进入 selfrefesh 的周期数
Params  : en -> 使能auto selfrefresh
Return  : 频率值
Notes   : 周期数为1*32 cycle  
----------------------------------------------------------------------*/
void ddr_set_auto_self_refresh(bool en)
{   
    //set auto self-refresh idle    
    ddr_sr_idle = en ? SR_IDLE : 0;
}

EXPORT_SYMBOL(ddr_set_auto_self_refresh);

/*----------------------------------------------------------------------
Name    : void __sramfunc ddr_suspend(void)
Desc    : 进入ddr suspend
Params  : void
Return  : void
Notes   :  
----------------------------------------------------------------------*/
void __sramfunc ddr_suspend(void)
{
    u32 i;
    volatile u32 n;	
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;  
    /** 1. Make sure there is no host access */
    flush_cache_all();
    outer_flush_all();
//    flush_tlb_all();

    for(i=0;i<2;i++)  //sram size = 8KB
    {
        n=temp[1024*i];
        barrier();
    }
    n= pDDR_Reg->SCFG.d32;
    n= pPHY_Reg->PHY_REG1;
    n= pCRU_Reg->CRU_PLL_CON[0][0];
    n= *(volatile uint32_t *)SysSrv_DdrConf;
    n= pGRF_Reg->GRF_SOC_STATUS0;
    dsb();
    ddr_selfrefresh_enter(0);
    pCRU_Reg->CRU_MODE_CON = (0x1<<((1*4) +  16)) | (0x0<<(1*4));   //PLL slow-mode
    dsb();
    ddr_delayus(1);    
    pCRU_Reg->CRU_PLL_CON[1][1] = ((0x1<<13)<<16) | (0x1<<13);         //PLL power-down
    dsb();
    ddr_delayus(1);    
    
}
EXPORT_SYMBOL(ddr_suspend);

/*----------------------------------------------------------------------
Name    : void __sramfunc ddr_resume(void)
Desc    : ddr resume
Params  : void
Return  : void
Notes   : 
----------------------------------------------------------------------*/
void __sramfunc ddr_resume(void)
{
    int delay=1000;
    
    pCRU_Reg->CRU_PLL_CON[1][1] = ((0x1<<13)<<16) | (0x0<<13);         //PLL no power-down
    dsb();
    while (delay > 0) 
    {
	    ddr_delayus(1);
		if (pCRU_Reg->CRU_PLL_CON[1][1] & (0x1<<10))
			break;
		delay--;
	}
    
    pCRU_Reg->CRU_MODE_CON = (0x1<<((1*4) +  16))  | (0x1<<(1*4));   //PLL normal
    dsb();

    ddr_selfrefresh_exit();
}
EXPORT_SYMBOL(ddr_resume);

/*----------------------------------------------------------------------
Name    : uint32 ddr_get_cap(void)
Desc    : 获取容量，返回字节数
Params  : void
Return  : 颗粒容量
Notes   :  
----------------------------------------------------------------------*/
uint32 ddr_get_cap(void)
{
    uint32 value;
    uint32 cs, bank, row, col;
    value = pGRF_Reg->GRF_OS_REG[1];
    bank = (((value >> DDR_BANK_COUNT) & 0x1)? 2:3);
    row = (13+((value >> DDR_ROW_COUNT) & 0x3));
    col = (9 + ((value >> DDR_COL_COUNT)&0x3));
    cs = (1 + ((value >> DDR_RANK_COUNT)&0x1)); 
    return ((1 << (row + col + bank + 1))*cs);
}
EXPORT_SYMBOL(ddr_get_cap);

/*----------------------------------------------------------------------
Name    : void ddr_reg_save(void)
Desc    : 保存控制器寄存器值
Params  : void
Return  : void
Notes   :  
----------------------------------------------------------------------*/
void ddr_reg_save(void)
{
    //PCTLR
    ddr_reg.pctl.SCFG = pDDR_Reg->SCFG.d32;
    ddr_reg.pctl.CMDTSTATEN = pDDR_Reg->CMDTSTATEN;
    ddr_reg.pctl.MCFG1 = pDDR_Reg->MCFG1;
    ddr_reg.pctl.MCFG = pDDR_Reg->MCFG;
    ddr_reg.pctl.pctl_timing.ddrFreq = ddr_freq;
    ddr_reg.pctl.DFITCTRLDELAY = pDDR_Reg->DFITCTRLDELAY;
    ddr_reg.pctl.DFIODTCFG = pDDR_Reg->DFIODTCFG;
    ddr_reg.pctl.DFIODTCFG1 = pDDR_Reg->DFIODTCFG1;
    ddr_reg.pctl.DFIODTRANKMAP = pDDR_Reg->DFIODTRANKMAP;
    ddr_reg.pctl.DFITPHYWRDATA = pDDR_Reg->DFITPHYWRDATA;
    ddr_reg.pctl.DFITPHYWRLAT = pDDR_Reg->DFITPHYWRLAT;
    ddr_reg.pctl.DFITRDDATAEN = pDDR_Reg->DFITRDDATAEN;
    ddr_reg.pctl.DFITPHYRDLAT = pDDR_Reg->DFITPHYRDLAT;
    ddr_reg.pctl.DFITPHYUPDTYPE0 = pDDR_Reg->DFITPHYUPDTYPE0;
    ddr_reg.pctl.DFITPHYUPDTYPE1 = pDDR_Reg->DFITPHYUPDTYPE1;
    ddr_reg.pctl.DFITPHYUPDTYPE2 = pDDR_Reg->DFITPHYUPDTYPE2;
    ddr_reg.pctl.DFITPHYUPDTYPE3 = pDDR_Reg->DFITPHYUPDTYPE3;
    ddr_reg.pctl.DFITCTRLUPDMIN = pDDR_Reg->DFITCTRLUPDMIN;
    ddr_reg.pctl.DFITCTRLUPDMAX = pDDR_Reg->DFITCTRLUPDMAX;
    ddr_reg.pctl.DFITCTRLUPDDLY = pDDR_Reg->DFITCTRLUPDDLY;
    
    ddr_reg.pctl.DFIUPDCFG = pDDR_Reg->DFIUPDCFG;
    ddr_reg.pctl.DFITREFMSKI = pDDR_Reg->DFITREFMSKI;
    ddr_reg.pctl.DFITCTRLUPDI = pDDR_Reg->DFITCTRLUPDI;
    ddr_reg.pctl.DFISTCFG0 = pDDR_Reg->DFISTCFG0;
    ddr_reg.pctl.DFISTCFG1 = pDDR_Reg->DFISTCFG1;
    ddr_reg.pctl.DFITDRAMCLKEN = pDDR_Reg->DFITDRAMCLKEN;
    ddr_reg.pctl.DFITDRAMCLKDIS = pDDR_Reg->DFITDRAMCLKDIS;
    ddr_reg.pctl.DFISTCFG2 = pDDR_Reg->DFISTCFG2;
    ddr_reg.pctl.DFILPCFG0 = pDDR_Reg->DFILPCFG0;

    //NOC
    ddr_reg.DdrConf = *(volatile uint32_t *)SysSrv_DdrConf;
    ddr_reg.DdrMode = *(volatile uint32_t *)SysSrv_DdrMode;
    ddr_reg.ReadLatency = *(volatile uint32_t *)SysSrv_ReadLatency;
}
EXPORT_SYMBOL(ddr_reg_save);
/*
__attribute__((aligned(4))) __sramdata uint32 ddr_reg_resume[] = 
{
#include "ddr_reg_resume.inc"
};
*/

/*----------------------------------------------------------------------
Name    : int ddr_init(uint32_t dram_speed_bin, uint32_t freq)
Desc    : ddr  初始化函数
Params  : dram_speed_bin ->ddr颗粒类型
          freq ->频率值
Return  : 0 成功
Notes   :  
----------------------------------------------------------------------*/
int ddr_init(uint32_t dram_speed_bin, uint32_t freq)
{
    volatile uint32_t value = 0;
    uint32_t cs,die=1;
    uint32_t calStatusLeft, calStatusRight;

    ddr_print("version 1.00 20120925 \n");
    cs = (1 << (((pGRF_Reg->GRF_OS_REG[1]) >> DDR_RANK_COUNT)&0x1));    //case 0:1rank ; case 1:2rank ;                            
    mem_type = ((pGRF_Reg->GRF_OS_REG[1] >> 13) &0x7);
    ddr_speed_bin = dram_speed_bin;
    ddr_freq = freq;
    ddr_sr_idle = 0;
    ddr_dll_status = DDR3_DLL_DISABLE;

    switch(mem_type)
    {
        case DDR3:
            die = 1;
            break;
		case DDR2:
			die = 1;
			break;
        default:
            ddr_print("ddr type error type=%d\n",mem_type);
            break;
    }
	
    
    //get capability per chip, not total size, used for calculate tRFC
    ddr_capability_per_die = ddr_get_cap()/(cs * die);
    ddr_print("%d CS, ROW=%d, Bank=%d, COL=%d, Total Capability=%dMB\n", 
                                                                    cs, \
                                                                    ddr_get_row(), \
                                                                    (0x1<<(ddr_get_bank())), \
                                                                    ddr_get_col(), \
                                                                    (ddr_get_cap()>>20));
    ddr_adjust_config(mem_type);
    value=ddr_change_freq(freq);
    clk_set_rate(clk_get(NULL, "ddr_pll"), 0);
    ddr_print("init success!!! freq=%dMHz\n", value);

    calStatusLeft = pPHY_Reg->PHY_REG60;
    calStatusRight = pPHY_Reg->PHY_REG61;


    ddr_print("left channel:Dllsel=%x, Ophsel=%x, Cycsel=%x\n",\
                                    (calStatusRight >> 5) & 0x07,\
                                    (calStatusRight >> 3) & 0x03,\
                                    calStatusRight  & 0x07);
    ddr_print("right channel:Dllsel=%x, Ophsel=%x, Cycsel=%x\n",\
                                    (calStatusLeft >> 5) & 0x07,\
                                    (calStatusLeft >> 3) & 0x03,\
                                    calStatusLeft  & 0x07);                                                        
    
    ddr_print("DRV Pull-Up=0x%x, DRV Pull-Dwn=0x%x\n", (pPHY_Reg->PHY_REG25>>3)&0x7, pPHY_Reg->PHY_REG25&0x7);
    ddr_print("ODT Pull-Up=0x%x, ODT Pull-Dwn=0x%x\n", (pPHY_Reg->PHY_REG27>>3)&0x7, pPHY_Reg->PHY_REG27&0x7);		
    return 0;
}

EXPORT_SYMBOL(ddr_init);

