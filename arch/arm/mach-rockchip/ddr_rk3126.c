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
#include <linux/cpu.h>
#include <dt-bindings/clock/ddr.h>
#include <linux/rockchip/cru.h>
#include <linux/rk_fb.h>
#include "cpu_axi.h"

typedef uint32_t uint32;

#define DDR3_DDR2_DLL_DISABLE_FREQ    (300)	/* 颗粒dll disable的频率*/
#define DDR3_DDR2_ODT_DISABLE_FREQ    (333)	/*颗粒odt disable的频率*/
#define SR_IDLE                       (0x1)	/*unit:32*DDR clk cycle, and 0 for disable auto self-refresh*/
#define PD_IDLE                       (0x40)	/*unit:DDR clk cycle, and 0 for disable auto power-down*/
#define PHY_ODT_DISABLE_FREQ          (333)	/*定义主控端odt disable的频率*/
#define PHY_DLL_DISABLE_FREQ          (266)	/*定义主控端dll bypass的频率*/

#define ddr_print(x...) printk("DDR DEBUG: " x)

#define SRAM_CODE_OFFSET        rockchip_sram_virt
#define SRAM_SIZE               rockchip_sram_size

#ifdef CONFIG_FB_ROCKCHIP
#define DDR_CHANGE_FREQ_IN_LCDC_VSYNC
#endif

/*#define PHY_RX_PHASE_CAL*/
#define PHY_DE_SKEW_STEP  (20)
/***********************************
 * DDR3 define
 ***********************************/
/*mr0 for ddr3*/
#define DDR3_BL8          (0)
#define DDR3_BC4_8        (1)
#define DDR3_BC4          (2)
#define DDR3_CL(n)        (((((n)-4)&0x7)<<4)|((((n)-4)&0x8)>>1))
#define DDR3_WR(n)        (((n)&0x7)<<9)
#define DDR3_DLL_RESET    (1<<8)
#define DDR3_DLL_DeRESET  (0<<8)

/*mr1 for ddr3*/
#define DDR3_DLL_ENABLE    (0)
#define DDR3_DLL_DISABLE   (1)
#define DDR3_MR1_AL(n)  (((n)&0x7)<<3)

#define DDR3_DS_40            (0)
#define DDR3_DS_34            (1<<1)
#define DDR3_Rtt_Nom_DIS      (0)
#define DDR3_Rtt_Nom_60       (1<<2)
#define DDR3_Rtt_Nom_120      (1<<6)
#define DDR3_Rtt_Nom_40       ((1<<2)|(1<<6))

/*mr2 for ddr3*/
#define DDR3_MR2_CWL(n) ((((n)-5)&0x7)<<3)
#define DDR3_Rtt_WR_DIS       (0)
#define DDR3_Rtt_WR_60        (1<<9)
#define DDR3_Rtt_WR_120       (2<<9)

#define DDR_PLL_REFDIV  (1)
#define FBDIV(n)        ((0xFFF<<16) | (n&0xfff))
#define REFDIV(n)       ((0x3F<<16) | (n&0x3f))
#define POSTDIV1(n)     ((0x7<<(12+16)) | ((n&0x7)<<12))
#define POSTDIV2(n)     ((0x7<<(6+16)) | ((n&0x7)<<6))

#define PLL_LOCK_STATUS  (0x1<<10)
 /*CRU Registers updated*/
typedef volatile struct tagCRU_STRUCT {
	uint32 CRU_PLL_CON[4][4];
	uint32 CRU_MODE_CON;
	uint32 CRU_CLKSEL_CON[35];
	uint32 CRU_CLKGATE_CON[11];	/*0xd0*/
	uint32 reserved1;	/*0xfc*/
	uint32 CRU_GLB_SRST_FST_VALUE;	/*0x100*/
	uint32 CRU_GLB_SRST_SND_VALUE;
	uint32 reserved2[2];
	uint32 CRU_SOFTRST_CON[9];	/*0x110*/
	uint32 CRU_MISC_CON;	/*0x134*/
	uint32 reserved3[2];
	uint32 CRU_GLB_CNT_TH;	/*0x140*/
	uint32 reserved4[3];
	uint32 CRU_GLB_RST_ST;	/*0x150*/
	uint32 reserved5[(0x1c0 - 0x154) / 4];
	uint32 CRU_SDMMC_CON[2];	/*0x1c0*/
	uint32 CRU_SDIO_CON[2];
	uint32 reserved6[2];
	uint32 CRU_EMMC_CON[2];	/*0x1d8*/
	uint32 reserved7[(0x1f0 - 0x1e0) / 4];
	uint32 CRU_PLL_PRG_EN;
} CRU_REG, *pCRU_REG;

typedef struct tagGPIO_LH {
	uint32 GPIOL;
	uint32 GPIOH;
} GPIO_LH_T;

typedef struct tagGPIO_IOMUX {
	uint32 GPIOA_IOMUX;
	uint32 GPIOB_IOMUX;
	uint32 GPIOC_IOMUX;
	uint32 GPIOD_IOMUX;
} GPIO_IOMUX_T;

/********************************
*GRF 寄存器中GRF_OS_REG1 存ddr rank，type等信息
*GRF_SOC_CON2寄存器中控制c_sysreq信号向pctl发送进入low power 请求
*GRF_DDRC_STAT 可查询pctl是否接受请求 进入low power
********************************/
/*REG FILE registers*/
/*GRF_SOC_CON0*/
#define DDR_MONITOR_EN  ((1<<(16+6))+(1<<6))
#define DDR_MONITOR_DISB  ((1<<(16+6))+(0<<6))

/*GRF_SOC_STATUS0*/
#define sys_pwr_idle     (1<<27)
#define gpu_pwr_idle     (1<<26)
#define vpu_pwr_idle     (1<<25)
#define vio_pwr_idle     (1<<24)
#define peri_pwr_idle    (1<<23)
#define core_pwr_idle     (1<<22)
/*GRF_SOC_CON2*/
#define core_pwr_idlereq    (13)
#define peri_pwr_idlereq    (12)
#define vio_pwr_idlereq     (11)
#define vpu_pwr_idlereq     (10)
#define gpu_pwr_idlereq     (9)
#define sys_pwr_idlereq     (8)
#define GRF_DDR_LP_EN     (0x1<<(2+16))
#define GRF_DDR_LP_DISB     ((0x1<<(2+16))|(0x1<<2))

/*grf updated*/
typedef volatile struct tagREG_FILE {
	uint32 reserved0[(0xa8 - 0x0) / 4];
	GPIO_IOMUX_T GRF_GPIO_IOMUX[4];	/*0x00a8*/
	uint32 GRF_GPIO2C_IOMUX2;	/*0xe8*/
	uint32 GRF_CIF_IOMUX[2];
	uint32 reserved1[(0x100 - 0xf4) / 4];
	uint32 GRF_GPIO_DS;	/*0x100*/
	uint32 reserved2[(0x118 - 0x104) / 4];
	GPIO_LH_T GRF_GPIO_PULL[4];	/*0x118*/
	uint32 reserved3[1];
	uint32 GRF_ACODEC_CON;	/*0x13c*/
	uint32 GRF_SOC_CON[3];	/*0x140*/
	uint32 GRF_SOC_STATUS0;
	uint32 GRF_LVDS_CON0;	/*0x150*/
	uint32 reserved4[(0x15c - 0x154) / 4];
	uint32 GRF_DMAC_CON[3];	/*0x15c*/
	uint32 GRF_MAC_CON[2];
	uint32 GRF_TVE_CON;	/*0x170*/
	uint32 reserved5[(0x17c - 0x174) / 4];
	uint32 GRF_UOC0_CON0;	/*0x17c*/
	uint32 reserved6;
	uint32 GRF_UOC1_CON[5];	/*0x184*/
	uint32 reserved7;
	uint32 GRF_DDRC_STAT;	/*0x19c*/
	uint32 reserved8;
	uint32 GRF_SOC_STATUS1;	/*0x1a4*/
	uint32 GRF_CPU_CON[4];
	uint32 reserved9[(0x1c0 - 0x1b8) / 4];
	uint32 GRF_CPU_STATUS[2];	/*0x1c0*/
	uint32 GRF_OS_REG[8];
	uint32 reserved10[(0x200 - 0x1e8) / 4];
	uint32 GRF_PVTM_CON[4];	/*0x200*/
	uint32 GRF_PVTM_STATUS[4];
	/*uint32 reserved10[(0x220-0x214)/4];*/
	uint32 GRF_DFI_WRNUM;	/*0X220*/
	uint32 GRF_DFI_RDNUM;
	uint32 GRF_DFI_ACTNUM;
	uint32 GRF_DFI_TIMERVAL;
	uint32 GRF_NIF_FIFO[4];
	uint32 reserved11[(0x280 - 0x240) / 4];
	uint32 GRF_USBPHY0_CON[8];	/*0x280*/
	uint32 GRF_USBPHY1_CON[8];
	uint32 GRF_UOC_STATUS0;	/*0x2c0*/
	uint32 reserved12[(0x300 - 0x2c4) / 4];
	uint32 GRF_CHIP_TAG;
	uint32 GRF_SDMMC_DET_CNT;
	uint32 reserved13[(0x37c - 0x308) / 4];
	uint32 GRF_EFUSE_PRG_EN;
} REG_FILE, *pREG_FILE;

/*SCTL*/
#define INIT_STATE                     (0)
#define CFG_STATE                      (1)
#define GO_STATE                       (2)
#define SLEEP_STATE                    (3)
#define WAKEUP_STATE                   (4)

/*STAT*/
#define Init_mem                       (0)
#define Config                         (1)
#define Config_req                     (2)
#define Access                         (3)
#define Access_req                     (4)
#define Low_power                      (5)
#define Low_power_entry_req            (6)
#define Low_power_exit_req             (7)

/*MCFG*/
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

/*POWCTL*/
#define power_up_start                 (1<<0)

/*POWSTAT*/
#define power_up_done                  (1<<0)

/*DFISTSTAT0*/
#define dfi_init_complete              (1<<0)

/*CMDTSTAT*/
#define cmd_tstat                      (1<<0)

/*CMDTSTATEN*/
#define cmd_tstat_en                   (1<<1)

/*MCMD*/
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

typedef union STAT_Tag {
	uint32 d32;
	struct {
		unsigned ctl_stat:3;
		unsigned reserved3:1;
		unsigned lp_trig:3;
		unsigned reserved7_31:25;
	} b;
} STAT_T;

typedef union SCFG_Tag {
	uint32 d32;
	struct {
		unsigned hw_low_power_en:1;
		unsigned reserved1_5:5;
		unsigned nfifo_nif1_dis:1;
		unsigned reserved7:1;
		unsigned bbflags_timing:4;
		unsigned reserved12_31:20;
	} b;
} SCFG_T;

/* DDR Controller register struct */
typedef volatile struct DDR_REG_Tag {
	/*Operational State, Control, and Status Registers*/
	SCFG_T SCFG;		/*State Configuration Register*/
	volatile uint32 SCTL;	/*State Control Register*/
	STAT_T STAT;		/*State Status Register*/
	volatile uint32 INTRSTAT;	/*Interrupt Status Register*/
	uint32 reserved0[(0x40 - 0x10) / 4];
	/*Initailization Control and Status Registers*/
	volatile uint32 MCMD;	/*Memory Command Register*/
	volatile uint32 POWCTL;	/*Power Up Control Registers*/
	volatile uint32 POWSTAT;	/*Power Up Status Register*/
	volatile uint32 CMDTSTAT;	/*Command Timing Status Register*/
	volatile uint32 CMDTSTATEN;	/*Command Timing Status Enable Register*/
	uint32 reserved1[(0x60 - 0x54) / 4];
	volatile uint32 MRRCFG0;	/*MRR Configuration 0 Register*/
	volatile uint32 MRRSTAT0;	/*MRR Status 0 Register*/
	volatile uint32 MRRSTAT1;	/*MRR Status 1 Register*/
	uint32 reserved2[(0x7c - 0x6c) / 4];
	/*Memory Control and Status Registers*/
	volatile uint32 MCFG1;	/*Memory Configuration 1 Register*/
	volatile uint32 MCFG;	/*Memory Configuration Register*/
	volatile uint32 PPCFG;	/*Partially Populated Memories Configuration Register*/
	volatile uint32 MSTAT;	/*Memory Status Register*/
	volatile uint32 LPDDR2ZQCFG;	/*LPDDR2 ZQ Configuration Register*/
	uint32 reserved3;
	/*DTU Control and Status Registers*/
	volatile uint32 DTUPDES;	/*DTU Status Register*/
	volatile uint32 DTUNA;	/*DTU Number of Random Addresses Created Register*/
	volatile uint32 DTUNE;	/*DTU Number of Errors Register*/
	volatile uint32 DTUPRD0;	/*DTU Parallel Read 0*/
	volatile uint32 DTUPRD1;	/*DTU Parallel Read 1*/
	volatile uint32 DTUPRD2;	/*DTU Parallel Read 2*/
	volatile uint32 DTUPRD3;	/*DTU Parallel Read 3*/
	volatile uint32 DTUAWDT;	/*DTU Address Width*/
	uint32 reserved4[(0xc0 - 0xb4) / 4];
	/*Memory Timing Registers*/
	volatile uint32 TOGCNT1U;	/*Toggle Counter 1U Register*/
	volatile uint32 TINIT;	/*t_init Timing Register*/
	volatile uint32 TRSTH;	/*Reset High Time Register*/
	volatile uint32 TOGCNT100N;	/*Toggle Counter 100N Register*/
	volatile uint32 TREFI;	/*t_refi Timing Register*/
	volatile uint32 TMRD;	/*t_mrd Timing Register*/
	volatile uint32 TRFC;	/*t_rfc Timing Register*/
	volatile uint32 TRP;	/*t_rp Timing Register*/
	volatile uint32 TRTW;	/*t_rtw Timing Register*/
	volatile uint32 TAL;	/*AL Latency Register*/
	volatile uint32 TCL;	/*CL Timing Register*/
	volatile uint32 TCWL;	/*CWL Register*/
	volatile uint32 TRAS;	/*t_ras Timing Register*/
	volatile uint32 TRC;	/*t_rc Timing Register*/
	volatile uint32 TRCD;	/*t_rcd Timing Register*/
	volatile uint32 TRRD;	/*t_rrd Timing Register*/
	volatile uint32 TRTP;	/*t_rtp Timing Register*/
	volatile uint32 TWR;	/*t_wr Timing Register*/
	volatile uint32 TWTR;	/*t_wtr Timing Register*/
	volatile uint32 TEXSR;	/*t_exsr Timing Register*/
	volatile uint32 TXP;	/*t_xp Timing Register*/
	volatile uint32 TXPDLL;	/*t_xpdll Timing Register*/
	volatile uint32 TZQCS;	/*t_zqcs Timing Register*/
	volatile uint32 TZQCSI;	/*t_zqcsi Timing Register*/
	volatile uint32 TDQS;	/*t_dqs Timing Register*/
	volatile uint32 TCKSRE;	/*t_cksre Timing Register*/
	volatile uint32 TCKSRX;	/*t_cksrx Timing Register*/
	volatile uint32 TCKE;	/*t_cke Timing Register*/
	volatile uint32 TMOD;	/*t_mod Timing Register*/
	volatile uint32 TRSTL;	/*Reset Low Timing Register*/
	volatile uint32 TZQCL;	/*t_zqcl Timing Register*/
	volatile uint32 TMRR;	/*t_mrr Timing Register*/
	volatile uint32 TCKESR;	/*t_ckesr Timing Register*/
	volatile uint32 TDPD;	/*t_dpd Timing Register*/
	uint32 reserved5[(0x180 - 0x148) / 4];
	/*ECC Configuration, Control, and Status Registers*/
	volatile uint32 ECCCFG;	/*ECC Configuration Register*/
	volatile uint32 ECCTST;	/*ECC Test Register*/
	volatile uint32 ECCCLR;	/*ECC Clear Register*/
	volatile uint32 ECCLOG;	/*ECC Log Register*/
	uint32 reserved6[(0x200 - 0x190) / 4];
	/*DTU Control and Status Registers*/
	volatile uint32 DTUWACTL;	/*DTU Write Address Control Register*/
	volatile uint32 DTURACTL;	/*DTU Read Address Control Register*/
	volatile uint32 DTUCFG;	/*DTU Configuration Control Register*/
	volatile uint32 DTUECTL;	/*DTU Execute Control Register*/
	volatile uint32 DTUWD0;	/*DTU Write Data 0*/
	volatile uint32 DTUWD1;	/*DTU Write Data 1*/
	volatile uint32 DTUWD2;	/*DTU Write Data 2*/
	volatile uint32 DTUWD3;	/*DTU Write Data 3*/
	volatile uint32 DTUWDM;	/*DTU Write Data Mask*/
	volatile uint32 DTURD0;	/*DTU Read Data 0*/
	volatile uint32 DTURD1;	/*DTU Read Data 1*/
	volatile uint32 DTURD2;	/*DTU Read Data 2*/
	volatile uint32 DTURD3;	/*DTU Read Data 3*/
	volatile uint32 DTULFSRWD;	/*DTU LFSR Seed for Write Data Generation*/
	volatile uint32 DTULFSRRD;	/*DTU LFSR Seed for Read Data Generation*/
	volatile uint32 DTUEAF;	/*DTU Error Address FIFO*/
	/*DFI Control Registers*/
	volatile uint32 DFITCTRLDELAY;	/*DFI tctrl_delay Register*/
	volatile uint32 DFIODTCFG;	/*DFI ODT Configuration Register*/
	volatile uint32 DFIODTCFG1;	/*DFI ODT Configuration 1 Register*/
	volatile uint32 DFIODTRANKMAP;	/*DFI ODT Rank Mapping Register*/
	/*DFI Write Data Registers*/
	volatile uint32 DFITPHYWRDATA;	/*DFI tphy_wrdata Register*/
	volatile uint32 DFITPHYWRLAT;	/*DFI tphy_wrlat Register*/
	uint32 reserved7[(0x260 - 0x258) / 4];
	volatile uint32 DFITRDDATAEN;	/*DFI trddata_en Register*/
	volatile uint32 DFITPHYRDLAT;	/*DFI tphy_rddata Register*/
	uint32 reserved8[(0x270 - 0x268) / 4];
	/*DFI Update Registers*/
	volatile uint32 DFITPHYUPDTYPE0;	/*DFI tphyupd_type0 Register*/
	volatile uint32 DFITPHYUPDTYPE1;	/*DFI tphyupd_type1 Register*/
	volatile uint32 DFITPHYUPDTYPE2;	/*DFI tphyupd_type2 Register*/
	volatile uint32 DFITPHYUPDTYPE3;	/*DFI tphyupd_type3 Register*/
	volatile uint32 DFITCTRLUPDMIN;	/*DFI tctrlupd_min Register*/
	volatile uint32 DFITCTRLUPDMAX;	/*DFI tctrlupd_max Register*/
	volatile uint32 DFITCTRLUPDDLY;	/*DFI tctrlupd_dly Register*/
	uint32 reserved9;
	volatile uint32 DFIUPDCFG;	/*DFI Update Configuration Register*/
	volatile uint32 DFITREFMSKI;	/*DFI Masked Refresh Interval Register*/
	volatile uint32 DFITCTRLUPDI;	/*DFI tctrlupd_interval Register*/
	uint32 reserved10[(0x2ac - 0x29c) / 4];
	volatile uint32 DFITRCFG0;	/*DFI Training Configuration 0 Register*/
	volatile uint32 DFITRSTAT0;	/*DFI Training Status 0 Register*/
	volatile uint32 DFITRWRLVLEN;	/*DFI Training dfi_wrlvl_en Register*/
	volatile uint32 DFITRRDLVLEN;	/*DFI Training dfi_rdlvl_en Register*/
	volatile uint32 DFITRRDLVLGATEEN;	/*DFI Training dfi_rdlvl_gate_en Register*/
	/*DFI Status Registers*/
	volatile uint32 DFISTSTAT0;	/*DFI Status Status 0 Register*/
	volatile uint32 DFISTCFG0;	/*DFI Status Configuration 0 Register*/
	volatile uint32 DFISTCFG1;	/*DFI Status configuration 1 Register*/
	uint32 reserved11;
	volatile uint32 DFITDRAMCLKEN;	/*DFI tdram_clk_enalbe Register*/
	volatile uint32 DFITDRAMCLKDIS;	/*DFI tdram_clk_disalbe Register*/
	volatile uint32 DFISTCFG2;	/*DFI Status configuration 2 Register*/
	volatile uint32 DFISTPARCLR;	/*DFI Status Parity Clear Register*/
	volatile uint32 DFISTPARLOG;	/*DFI Status Parity Log Register*/
	uint32 reserved12[(0x2f0 - 0x2e4) / 4];
	/*DFI Low Power Registers*/
	volatile uint32 DFILPCFG0;	/*DFI Low Power Configuration 0 Register*/
	uint32 reserved13[(0x300 - 0x2f4) / 4];
	/*DFI Training 2 Registers*/
	volatile uint32 DFITRWRLVLRESP0;	/*DFI Training dif_wrlvl_resp Status 0 Register*/
	volatile uint32 DFITRWRLVLRESP1;	/*DFI Training dif_wrlvl_resp Status 1 Register*/
	volatile uint32 DFITRWRLVLRESP2;	/*DFI Training dif_wrlvl_resp Status 2 Register*/
	volatile uint32 DFITRRDLVLRESP0;	/*DFI Training dif_rdlvl_resp Status 0 Register*/
	volatile uint32 DFITRRDLVLRESP1;	/*DFI Training dif_rdlvl_resp Status 1 Register*/
	volatile uint32 DFITRRDLVLRESP2;	/*DFI Training dif_rdlvl_resp Status 2 Register*/
	volatile uint32 DFITRWRLVLDELAY0;	/*DFI Training dif_wrlvl_delay Configuration 0 Register*/
	volatile uint32 DFITRWRLVLDELAY1;	/*DFI Training dif_wrlvl_delay Configuration 1 Register*/
	volatile uint32 DFITRWRLVLDELAY2;	/*DFI Training dif_wrlvl_delay Configuration 2 Register*/
	volatile uint32 DFITRRDLVLDELAY0;	/*DFI Training dif_rdlvl_delay Configuration 0 Register*/
	volatile uint32 DFITRRDLVLDELAY1;	/*DFI Training dif_rdlvl_delay Configuration 1 Register*/
	volatile uint32 DFITRRDLVLDELAY2;	/*DFI Training dif_rdlvl_delay Configuration 2 Register*/
	volatile uint32 DFITRRDLVLGATEDELAY0;	/*DFI Training dif_rdlvl_gate_delay Configuration 0 Register*/
	volatile uint32 DFITRRDLVLGATEDELAY1;	/*DFI Training dif_rdlvl_gate_delay Configuration 1 Register*/
	volatile uint32 DFITRRDLVLGATEDELAY2;	/*DFI Training dif_rdlvl_gate_delay Configuration 2 Register*/
	volatile uint32 DFITRCMD;	/*DFI Training Command Register*/
	uint32 reserved14[(0x3f8 - 0x340) / 4];
	/*IP Status Registers*/
	volatile uint32 IPVR;	/*IP Version Register*/
	volatile uint32 IPTR;	/*IP Type Register*/
} DDR_REG_T, *pDDR_REG_T;

/*PHY_REG2*/
#define PHY_AUTO_CALIBRATION (1<<0)
#define PHY_SW_CALIBRATION   (1<<1)
/*PHY_REG1*/
#define PHY_DDR2             (1)
#define PHY_DDR3             (0)
#define PHY_LPDDR2           (2)
#define PHY_Burst8           (1<<2)

#define PHY_RON_DISABLE     (0)
#define PHY_RON_309ohm      (1)
#define PHY_RON_155ohm      (2)
#define PHY_RON_103ohm      (3)
#define PHY_RON_77ohm       (4)
#define PHY_RON_63ohm       (5)
#define PHY_RON_52ohm       (6)
#define PHY_RON_45ohm       (7)
/*#define PHY_RON_77ohm       (8)*/
#define PHY_RON_62ohm       (9)
/*#define PHY_RON_52ohm       (10)*/
#define PHY_RON_44ohm       (11)
#define PHY_RON_39ohm       (12)
#define PHY_RON_34ohm       (13)
#define PHY_RON_31ohm       (14)
#define PHY_RON_28ohm       (15)

#define PHY_RTT_DISABLE     (0)
#define PHY_RTT_816ohm      (1)
#define PHY_RTT_431ohm      (2)
#define PHY_RTT_287ohm      (3)
#define PHY_RTT_216ohm      (4)
#define PHY_RTT_172ohm      (5)
#define PHY_RTT_145ohm      (6)
#define PHY_RTT_124ohm      (7)
#define PHY_RTT_215ohm      (8)
/*#define PHY_RTT_172ohm      (9)*/
#define PHY_RTT_144ohm      (10)
#define PHY_RTT_123ohm      (11)
#define PHY_RTT_108ohm      (12)
#define PHY_RTT_96ohm       (13)
#define PHY_RTT_86ohm       (14)
#define PHY_RTT_78ohm       (15)

#define PHY_DRV_ODT_SET(n) ((n<<4)|n)

/* DDR PHY register struct  updated */
typedef volatile struct DDRPHY_REG_Tag {
	volatile uint32 PHY_REG0;	/*PHY soft reset Register*/
	volatile uint32 PHY_REG1;	/*phy working mode, burst length*/
	volatile uint32 PHY_REG2;	/*PHY DQS squelch calibration Register*/
	volatile uint32 PHY_REG3;	/*channel A read odt delay*/
	volatile uint32 PHY_REG4;	/*channel B read odt dleay*/
	uint32 reserved0[(0x2c - 0x14) / 4];
	volatile uint32 PHY_REGb;	/*cl,al*/
	volatile uint32 PHY_REGc;	/*CWL set register*/
	uint32 reserved1[(0x44 - 0x34) / 4];
	volatile uint32 PHY_REG11;	/*cmd drv*/
	volatile uint32 PHY_REG12;	/*cmd weak pull up*/
	volatile uint32 PHY_REG13;	/*cmd dll delay*/
	volatile uint32 PHY_REG14;	/*CK dll delay*/
	uint32 reserved2;	/*0x54*/
	volatile uint32 PHY_REG16;	/*/CK drv*/
	uint32 reserved3[(0x80 - 0x5c) / 4];
	volatile uint32 PHY_REG20;	/*left channel a drv*/
	volatile uint32 PHY_REG21;	/*left channel a odt*/
	uint32 reserved4[(0x98 - 0x88) / 4];
	volatile uint32 PHY_REG26;	/*left channel a dq write dll*/
	volatile uint32 PHY_REG27;	/*left channel a dqs write dll*/
	volatile uint32 PHY_REG28;	/*left channel a dqs read dll*/
	uint32 reserved5[(0xc0 - 0xa4) / 4];
	volatile uint32 PHY_REG30;	/*right channel a drv*/
	volatile uint32 PHY_REG31;	/*right channel a odt*/
	uint32 reserved6[(0xd8 - 0xc8) / 4];
	volatile uint32 PHY_REG36;	/*right channel a dq write dll*/
	volatile uint32 PHY_REG37;	/*right channel a dqs write dll*/
	volatile uint32 PHY_REG38;	/*right channel a dqs read dll*/
	uint32 reserved7[(0x100 - 0xe4) / 4];
	volatile uint32 PHY_REG40;	/*left channel b drv*/
	volatile uint32 PHY_REG41;	/*left channel b odt*/
	uint32 reserved8[(0x118 - 0x108) / 4];
	volatile uint32 PHY_REG46;	/*left channel b dq write dll*/
	volatile uint32 PHY_REG47;	/*left channel b dqs write dll*/
	volatile uint32 PHY_REG48;	/*left channel b dqs read dll*/
	uint32 reserved9[(0x140 - 0x124) / 4];
	volatile uint32 PHY_REG50;	/*right channel b drv*/
	volatile uint32 PHY_REG51;	/*right channel b odt*/
	uint32 reserved10[(0x158 - 0x148) / 4];
	volatile uint32 PHY_REG56;	/*right channel b dq write dll*/
	volatile uint32 PHY_REG57;	/*right channel b dqs write dll*/
	volatile uint32 PHY_REG58;	/*right channel b dqs read dll*/
	uint32 reserved11[(0x290 - 0x164) / 4];
	volatile uint32 PHY_REGDLL;	/*dll bypass switch reg*/
	uint32 reserved12[(0x2c0 - 0x294) / 4];
	volatile uint32 PHY_REG_skew[(0x3b0 - 0x2c0) / 4];	/*de-skew*/
	uint32 reserved13[(0x3e8 - 0x3b0) / 4];
	volatile uint32 PHY_REGfa;	/*idqs*/
	volatile uint32 PHY_REGfb;	/* left channel a calibration result*/
	volatile uint32 PHY_REGfc;	/* right channel a calibration result*/
	volatile uint32 PHY_REGfd;	/*left channel b calibration result*/
	volatile uint32 PHY_REGfe;	/* right channel b calibration result*/
	volatile uint32 PHY_REGff;	/*calibrationg done*/
} DDRPHY_REG_T, *pDDRPHY_REG_T;

#define pCRU_Reg               ((pCRU_REG)RK_CRU_VIRT)
#define pGRF_Reg               ((pREG_FILE)RK_GRF_VIRT)
#define pDDR_Reg               ((pDDR_REG_T)RK_DDR_VIRT)
#define pPHY_Reg               ((pDDRPHY_REG_T)(RK_DDR_VIRT+RK3036_DDR_PCTL_SIZE))
#define SysSrv_DdrTiming       (RK_CPU_AXI_BUS_VIRT+0xc)
#define PMU_PWEDN_ST		(RK_PMU_VIRT + 0x8)
#define READ_CS_INFO()   ((((pGRF_Reg->GRF_OS_REG[1])>>11)&0x1)+1)
#define READ_COL_INFO()  (9+(((pGRF_Reg->GRF_OS_REG[1])>>9)&0x3))
#define READ_BK_INFO()   (3-(((pGRF_Reg->GRF_OS_REG[1])>>8)&0x1))
#define READ_CS0_ROW_INFO()  (13+(((pGRF_Reg->GRF_OS_REG[1])>>6)&0x3))
#define READ_CS1_ROW_INFO()  (13+(((pGRF_Reg->GRF_OS_REG[1])>>4)&0x3))
#define READ_BW_INFO()   (2>>(((pGRF_Reg->GRF_OS_REG[1])&0xc)>>2))	/*代码中 0->8bit 1->16bit 2->32bit  与grf中定义相反*/
#define READ_DIE_BW_INFO()   (2>>((pGRF_Reg->GRF_OS_REG[1])&0x3))

/***********************************
 * LPDDR2 define
 ***********************************/
/*MR0 (Device Information)*/
#define  LPDDR2_DAI    (0x1)	/* 0:DAI complete, 1:DAI still in progress*/
#define  LPDDR2_DI     (0x1<<1)	/* 0:S2 or S4 SDRAM, 1:NVM*/
#define  LPDDR2_DNVI   (0x1<<2)	/* 0:DNV not supported, 1:DNV supported*/
#define  LPDDR2_RZQI   (0x3<<3)	/*00:RZQ self test not supported, 01:ZQ-pin may connect to VDDCA or float*/
				    /*10:ZQ-pin may short to GND.     11:ZQ-pin self test completed, no error condition detected.*/

/*MR1 (Device Feature)*/
#define LPDDR2_BL4     (0x2)
#define LPDDR2_BL8     (0x3)
#define LPDDR2_BL16    (0x4)
#define LPDDR2_nWR(n)  (((n)-2)<<5)

/*MR2 (Device Feature 2)*/
#define LPDDR2_RL3_WL1  (0x1)
#define LPDDR2_RL4_WL2  (0x2)
#define LPDDR2_RL5_WL2  (0x3)
#define LPDDR2_RL6_WL3  (0x4)
#define LPDDR2_RL7_WL4  (0x5)
#define LPDDR2_RL8_WL4  (0x6)

/*MR3 (IO Configuration 1)*/
#define LPDDR2_DS_34    (0x1)
#define LPDDR2_DS_40    (0x2)
#define LPDDR2_DS_48    (0x3)
#define LPDDR2_DS_60    (0x4)
#define LPDDR2_DS_80    (0x6)
#define LPDDR2_DS_120   (0x7)	/*optional*/

/*MR4 (Device Temperature)*/
#define LPDDR2_tREF_MASK (0x7)
#define LPDDR2_4_tREF    (0x1)
#define LPDDR2_2_tREF    (0x2)
#define LPDDR2_1_tREF    (0x3)
#define LPDDR2_025_tREF  (0x5)
#define LPDDR2_025_tREF_DERATE    (0x6)

#define LPDDR2_TUF       (0x1<<7)

/*MR8 (Basic configuration 4)*/
#define LPDDR2_S4        (0x0)
#define LPDDR2_S2        (0x1)
#define LPDDR2_N         (0x2)
#define LPDDR2_Density(mr8)  (8<<(((mr8)>>2)&0xf))	/*Unit:MB*/
#define LPDDR2_IO_Width(mr8) (32>>(((mr8)>>6)&0x3))

/*MR10 (Calibration)*/
#define LPDDR2_ZQINIT   (0xFF)
#define LPDDR2_ZQCL     (0xAB)
#define LPDDR2_ZQCS     (0x56)
#define LPDDR2_ZQRESET  (0xC3)

/*MR16 (PASR Bank Mask)*/
/*S2 SDRAM Only*/
#define LPDDR2_PASR_Full (0x0)
#define LPDDR2_PASR_1_2  (0x1)
#define LPDDR2_PASR_1_4  (0x2)
#define LPDDR2_PASR_1_8  (0x3)

typedef enum PLL_ID_Tag {
	APLL = 0,
	DPLL,
	CPLL,
	GPLL,
	PLL_MAX
} PLL_ID;

typedef enum DRAM_TYPE_Tag {
	LPDDR = 0,
	DDR,
	DDR2,
	DDR3,
	LPDDR2S2,
	LPDDR2,

	DRAM_MAX
} DRAM_TYPE;

struct ddr_freq_t {
	unsigned long screen_ft_us;
	unsigned long long t0;
	unsigned long long t1;
	unsigned long t2;
};

typedef struct PCTRL_TIMING_Tag {
	uint32 ddrFreq;
	/*Memory Timing Registers*/
	uint32 togcnt1u;	/*Toggle Counter 1U Register*/
	uint32 tinit;		/*t_init Timing Register*/
	uint32 trsth;		/*Reset High Time Register*/
	uint32 togcnt100n;	/*Toggle Counter 100N Register*/
	uint32 trefi;		/*t_refi Timing Register*/
	uint32 tmrd;		/*t_mrd Timing Register*/
	uint32 trfc;		/*t_rfc Timing Register*/
	uint32 trp;		    /*t_rp Timing Register*/
	uint32 trtw;		/*t_rtw Timing Register*/
	uint32 tal;		    /*AL Latency Register*/
	uint32 tcl;		    /*CL Timing Register*/
	uint32 tcwl;		/*CWL Register*/
	uint32 tras;		/*t_ras Timing Register*/
	uint32 trc;		    /*t_rc Timing Register*/
	uint32 trcd;		/*t_rcd Timing Register*/
	uint32 trrd;		/*t_rrd Timing Register*/
	uint32 trtp;		/*t_rtp Timing Register*/
	uint32 twr;		    /*t_wr Timing Register*/
	uint32 twtr;		/*t_wtr Timing Register*/
	uint32 texsr;		/*t_exsr Timing Register*/
	uint32 txp;		    /*t_xp Timing Register*/
	uint32 txpdll;		/*t_xpdll Timing Register*/
	uint32 tzqcs;		/*t_zqcs Timing Register*/
	uint32 tzqcsi;		/*t_zqcsi Timing Register*/
	uint32 tdqs;		/*t_dqs Timing Register*/
	uint32 tcksre;		/*t_cksre Timing Register*/
	uint32 tcksrx;		/*t_cksrx Timing Register*/
	uint32 tcke;		/*t_cke Timing Register*/
	uint32 tmod;		/*t_mod Timing Register*/
	uint32 trstl;		/*Reset Low Timing Register*/
	uint32 tzqcl;		/*t_zqcl Timing Register*/
	uint32 tmrr;		/*t_mrr Timing Register*/
	uint32 tckesr;		/*t_ckesr Timing Register*/
	uint32 tdpd;		/*t_dpd Timing Register*/
} PCTL_TIMING_T;

struct ddr_change_freq_sram_param {
	uint32 freq;
	uint32 freq_slew;
};

typedef union NOC_TIMING_Tag {
	uint32 d32;
	struct {
		unsigned ActToAct:6;
		unsigned RdToMiss:6;
		unsigned WrToMiss:6;
		unsigned BurstLen:3;
		unsigned RdToWr:5;
		unsigned WrToRd:5;
		unsigned BwRatio:1;
	} b;
} NOC_TIMING_T;

typedef struct BACKUP_REG_Tag {
	PCTL_TIMING_T pctl_timing;
	NOC_TIMING_T noc_timing;
	uint32 ddrMR[4];
	uint32 mem_type;
	uint32 ddr_speed_bin;
	uint32 ddr_capability_per_die;
} BACKUP_REG_T;

BACKUP_REG_T DEFINE_PIE_DATA(ddr_reg);
static BACKUP_REG_T *p_ddr_reg;

uint32 DEFINE_PIE_DATA(ddr_freq);
static uint32 *p_ddr_freq;
uint32 DEFINE_PIE_DATA(ddr_sr_idle);
uint32 DEFINE_PIE_DATA(ddr_dll_status);	/* 记录ddr dll的状态，在selfrefresh exit时选择是否进行dll reset*/

uint32_t ddr3_cl_cwl[22][4] = {
/*   0~330           330~400         400~533        speed
* tCK  >3             2.5~3          1.875~2.5     1.875~1.5
*    cl<<16, cwl    cl<<16, cwl     cl<<16, cwl              */
	{((5 << 16) | 5), ((5 << 16) | 5), 0, 0},	/*DDR3_800D*/
	{((5 << 16) | 5), ((6 << 16) | 5), 0, 0},	/*DDR3_800E*/

	{((5 << 16) | 5), ((5 << 16) | 5), ((6 << 16) | 6), 0},	/*DDR3_1066E*/
	{((5 << 16) | 5), ((6 << 16) | 5), ((7 << 16) | 6), 0},	/*DDR3_1066F*/
	{((5 << 16) | 5), ((6 << 16) | 5), ((8 << 16) | 6), 0},	/*DDR3_1066G*/

	{((5 << 16) | 5), ((5 << 16) | 5), ((6 << 16) | 6), ((7 << 16) | 7)},	/*DDR3_1333F*/
	{((5 << 16) | 5), ((5 << 16) | 5), ((7 << 16) | 6), ((8 << 16) | 7)},	/*DDR3_1333G*/
	{((5 << 16) | 5), ((6 << 16) | 5), ((7 << 16) | 6), ((9 << 16) | 7)},	/*DDR3_1333H*/
	{((5 << 16) | 5), ((6 << 16) | 5), ((8 << 16) | 6), ((10 << 16) | 7)},	/*DDR3_1333J*/

	{((5 << 16) | 5), ((5 << 16) | 5), ((6 << 16) | 6), ((7 << 16) | 7)},	/*DDR3_1600G*/
	{((5 << 16) | 5), ((5 << 16) | 5), ((6 << 16) | 6), ((8 << 16) | 7)},	/*DDR3_1600H*/
	{((5 << 16) | 5), ((5 << 16) | 5), ((7 << 16) | 6), ((9 << 16) | 7)},	/*DDR3_1600J*/
	{((5 << 16) | 5), ((6 << 16) | 5), ((7 << 16) | 6), ((10 << 16) | 7)},	/*DDR3_1600K*/

	{((5 << 16) | 5), ((5 << 16) | 5), ((6 << 16) | 6), ((8 << 16) | 7)},	/*DDR3_1866J*/
	{((5 << 16) | 5), ((5 << 16) | 5), ((7 << 16) | 6), ((8 << 16) | 7)},	/*DDR3_1866K*/
	{((6 << 16) | 5), ((6 << 16) | 5), ((7 << 16) | 6), ((9 << 16) | 7)},	/*DDR3_1866L*/
	{((6 << 16) | 5), ((6 << 16) | 5), ((8 << 16) | 6), ((10 << 16) | 7)},	/*DDR3_1866M*/

	{((5 << 16) | 5), ((5 << 16) | 5), ((6 << 16) | 6), ((7 << 16) | 7)},	/*DDR3_2133K*/
	{((5 << 16) | 5), ((5 << 16) | 5), ((6 << 16) | 6), ((8 << 16) | 7)},	/*DDR3_2133L*/
	{((5 << 16) | 5), ((5 << 16) | 5), ((7 << 16) | 6), ((9 << 16) | 7)},	/*DDR3_2133M*/
	{((6 << 16) | 5), ((6 << 16) | 5), ((7 << 16) | 6), ((9 << 16) | 7)},	/*DDR3_2133N*/

	{((6 << 16) | 5), ((6 << 16) | 5), ((8 << 16) | 6), ((10 << 16) | 7)}	/*DDR3_DEFAULT*/
};

uint32_t ddr3_tRC_tFAW[22] = {
/**    tRC    tFAW   */
	((50 << 16) | 50),	/*DDR3_800D*/
	((53 << 16) | 50),	/*DDR3_800E*/

	((49 << 16) | 50),	/*DDR3_1066E*/
	((51 << 16) | 50),	/*DDR3_1066F*/
	((53 << 16) | 50),	/*DDR3_1066G*/

	((47 << 16) | 45),	/*DDR3_1333F*/
	((48 << 16) | 45),	/*DDR3_1333G*/
	((50 << 16) | 45),	/*DDR3_1333H*/
	((51 << 16) | 45),	/*DDR3_1333J*/

	((45 << 16) | 40),	/*DDR3_1600G*/
	((47 << 16) | 40),	/*DDR3_1600H*/
	((48 << 16) | 40),	/*DDR3_1600J*/
	((49 << 16) | 40),	/*DDR3_1600K*/

	((45 << 16) | 35),	/*DDR3_1866J*/
	((46 << 16) | 35),	/*DDR3_1866K*/
	((47 << 16) | 35),	/*DDR3_1866L*/
	((48 << 16) | 35),	/*DDR3_1866M*/

	((44 << 16) | 35),	/*DDR3_2133K*/
	((45 << 16) | 35),	/*DDR3_2133L*/
	((46 << 16) | 35),	/*DDR3_2133M*/
	((47 << 16) | 35),	/*DDR3_2133N*/

	((53 << 16) | 50)	/*DDR3_DEFAULT*/
};

/****************************************************************************
*Internal sram us delay function
*Cpu highest frequency is 1.6 GHz
*1 cycle = 1/1.6 ns
*1 us = 1000 ns = 1000 * 1.6 cycles = 1600 cycles
******************************************************************************/
volatile uint32 DEFINE_PIE_DATA(loops_per_us);
#define LPJ_100MHZ  999456UL

/*----------------------------------------------------------------------
*Name	: void __sramlocalfunc ddr_delayus(uint32_t us)
*Desc	: ddr 延时函数
*Params  : uint32_t us  --延时时间
*Return  : void
*Notes   : loops_per_us 为全局变量 需要根据arm freq而定
*----------------------------------------------------------------------*/
static void __sramfunc ddr_delayus(uint32 us)
{
	do {
		volatile unsigned int i = (DATA(loops_per_us) * us);
		if (i < 7)
			i = 7;
		barrier();
		asm volatile (".align 4; 1: subs %0, %0, #1; bne 1b;":"+r" (i));
	} while (0);
}

/*----------------------------------------------------------------------
*Name	: __sramfunc void ddr_copy(uint32 *pDest, uint32 *pSrc, uint32 words)
*Desc	: ddr 拷贝寄存器函数
*Params  : pDest ->目标寄存器首地址
*          pSrc  ->源标寄存器首地址
*          words ->拷贝长度
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/

static __sramfunc void ddr_copy(uint32 *pDest, uint32 *pSrc, uint32 words)
{
	uint32 i;

	for (i = 0; i < words; i++) {
		pDest[i] = pSrc[i];
	}
}

/*----------------------------------------------------------------------
*Name	: __sramfunc void ddr_move_to_Lowpower_state(void)
*Desc	: pctl 进入 lowpower state
*Params  : void
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/
static __sramfunc void ddr_move_to_Lowpower_state(void)
{
	volatile uint32 value;

	while (1) {
		value = pDDR_Reg->STAT.b.ctl_stat;
		if (value == Low_power) {
			break;
		}
		switch (value) {
		case Init_mem:
			pDDR_Reg->SCTL = CFG_STATE;
			dsb();
			while ((pDDR_Reg->STAT.b.ctl_stat) != Config)
			;
		case Config:
			pDDR_Reg->SCTL = GO_STATE;
			dsb();
			while ((pDDR_Reg->STAT.b.ctl_stat) != Access)
			;
		case Access:
			pDDR_Reg->SCTL = SLEEP_STATE;
			dsb();
			while ((pDDR_Reg->STAT.b.ctl_stat) != Low_power)
			;
			break;
		default:	/*Transitional state*/
			break;
		}
	}
}

/*----------------------------------------------------------------------
*Name	: __sramfunc void ddr_move_to_Access_state(void)
*Desc	: pctl 进入 Access state
*Params  : void
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/
static __sramfunc void ddr_move_to_Access_state(void)
{
	volatile uint32 value;

	/*set auto self-refresh idle*/
	pDDR_Reg->MCFG1 =
	    (pDDR_Reg->MCFG1 & 0xffffff00) | DATA(ddr_sr_idle) | (1 << 31);
	pDDR_Reg->MCFG = (pDDR_Reg->MCFG & 0xffff00ff) | (PD_IDLE << 8);
	while (1) {
		value = pDDR_Reg->STAT.b.ctl_stat;
		if ((value == Access)
		    || ((pDDR_Reg->STAT.b.lp_trig == 1)
			&& ((pDDR_Reg->STAT.b.ctl_stat) == Low_power))) {
			break;
		}
		switch (value) {
		case Low_power:
			pDDR_Reg->SCTL = WAKEUP_STATE;
			dsb();
			while ((pDDR_Reg->STAT.b.ctl_stat) != Access)
			;
			break;
		case Init_mem:
			pDDR_Reg->SCTL = CFG_STATE;
			dsb();
			while ((pDDR_Reg->STAT.b.ctl_stat) != Config)
			;
		case Config:
			pDDR_Reg->SCTL = GO_STATE;
			dsb();
			while (!(((pDDR_Reg->STAT.b.ctl_stat) == Access)
				 || ((pDDR_Reg->STAT.b.lp_trig == 1)
				     && ((pDDR_Reg->STAT.b.ctl_stat) ==
					 Low_power))))
		    ;
			break;
		default:	/*Transitional state*/
			break;
		}
	}
	pGRF_Reg->GRF_SOC_CON[2] = (1 << 16 | 0);	/*de_hw_wakeup :enable auto sr if sr_idle != 0*/
}

/*----------------------------------------------------------------------
*Name	: __sramfunc void ddr_move_to_Config_state(void)
*Desc	: pctl 进入 config state
*Params  : void
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/
static __sramfunc void ddr_move_to_Config_state(void)
{
	volatile uint32 value;
	pGRF_Reg->GRF_SOC_CON[2] = (1 << 16 | 1);	/*hw_wakeup :disable auto sr*/
	while (1) {
		value = pDDR_Reg->STAT.b.ctl_stat;
		if (value == Config) {
			break;
		}
		switch (value) {
		case Low_power:
			pDDR_Reg->SCTL = WAKEUP_STATE;
			dsb();
		case Access:
		case Init_mem:
			pDDR_Reg->SCTL = CFG_STATE;
			dsb();
			break;
		default:	/*Transitional state*/
			break;
		}
	}
}

/*----------------------------------------------------------------------
*Name	: void __sramlocalfunc ddr_send_command(uint32 rank, uint32 cmd, uint32 arg)
*Desc	: 通过写 pctl MCMD寄存器向ddr发送命令
*Params  : rank ->ddr rank 数
*          cmd  ->发送命令类型
*          arg  ->发送的数据
*Return  : void
*Notes   : arg包括bank_addr和cmd_addr
*----------------------------------------------------------------------*/
static void __sramfunc ddr_send_command(uint32 rank, uint32 cmd, uint32 arg)
{
	pDDR_Reg->MCMD = (start_cmd | (rank << 20) | arg | cmd);
	dsb();
	while (pDDR_Reg->MCMD & start_cmd)
	;
}

__sramdata uint32 copy_data[8] = {
     0xffffffff, 0x00000000, 0x55555555, 0xAAAAAAAA,
	0xEEEEEEEE, 0x11111111, 0x22222222, 0xDDDDDDDD
};

 EXPORT_PIE_SYMBOL(copy_data[8]);
uint32 *p_copy_data;

/*----------------------------------------------------------------------
Name	: uint32_t __sramlocalfunc ddr_data_training(void)
Desc	: 对ddr做data training
Params  : void
Return  : void
Notes   : 没有做data training校验
----------------------------------------------------------------------*/
static uint32_t __sramfunc ddr_data_training(void)
{
	uint32 value, dram_bw;
	value = pDDR_Reg->TREFI;
	pDDR_Reg->TREFI = 0;
	dram_bw = (pPHY_Reg->PHY_REG0 >> 4) & 0xf;
	pPHY_Reg->PHY_REG2 |= PHY_AUTO_CALIBRATION;
	dsb();
	/*wait echo byte DTDONE*/
	ddr_delayus(1);
	/*stop DTT*/
	while ((pPHY_Reg->PHY_REGff & 0xf) != dram_bw)
	;
	pPHY_Reg->PHY_REG2 = (pPHY_Reg->PHY_REG2 & (~0x1));
	/*send some auto refresh to complement the lost while DTT*/
	ddr_send_command(3, PREA_cmd, 0);
	ddr_send_command(3, REF_cmd, 0);
	ddr_send_command(3, REF_cmd, 0);

	/*resume auto refresh*/
	pDDR_Reg->TREFI = value;
	return 0;
}

/*----------------------------------------------------------------------
Name    : void __sramlocalfunc ddr_set_dll_bypass(uint32 freq)
Desc    : 设置PHY dll 工作模式
Params  : freq -> ddr工作频率
Return  : void
Notes   :
----------------------------------------------------------------------*/
static void __sramfunc ddr_set_dll_bypass(uint32 freq)
{
#if defined (PHY_RX_PHASE_CAL)
	uint32 phase_90, dll_set, de_skew;

	phase_90 = 1000000 / freq / 4;
	dll_set = (phase_90 - 300 + (0x7*PHY_DE_SKEW_STEP)) / (phase_90 / 4);
	de_skew = (phase_90 - 300 + (0x7*PHY_DE_SKEW_STEP) - ((phase_90 / 4) * dll_set));
	if (de_skew > PHY_DE_SKEW_STEP * 15) {
		if (dll_set == 3) {
			de_skew = 15;
		} else {
			dll_set += 1;
			de_skew = 0;
		}
	} else {
		de_skew = de_skew / PHY_DE_SKEW_STEP;
	}

	pPHY_Reg->PHY_REG28 = dll_set;/*rx dll 45°delay*/
	pPHY_Reg->PHY_REG38 = dll_set;/*rx dll 45°delay*/
	pPHY_Reg->PHY_REG48 = dll_set;/*rx dll 45°delay*/
	pPHY_Reg->PHY_REG58 = dll_set;/*rx dll 45°delay*/
	pPHY_Reg->PHY_REG_skew[(0x324-0x2c0)/4] = 0x7 | (de_skew << 4);
	pPHY_Reg->PHY_REG_skew[(0x350-0x2c0)/4] = 0x7 | (de_skew << 4);
	pPHY_Reg->PHY_REG_skew[(0x37c-0x2c0)/4] = 0x7 | (de_skew << 4);
	pPHY_Reg->PHY_REG_skew[(0x3a8-0x2c0)/4] = 0x7 | (de_skew << 4);
#else
	uint32 phase;
	if (freq < 350) {
		phase = 3;
	} else if (freq < 600) {
		phase = 2;
	} else
		phase = 1;
	pPHY_Reg->PHY_REG28 = phase;	/*rx dll 45°delay*/
	pPHY_Reg->PHY_REG38 = phase;	/*rx dll 45°delay*/
	pPHY_Reg->PHY_REG48 = phase;	/*rx dll 45°delay*/
	pPHY_Reg->PHY_REG58 = phase;	/*rx dll 45°delay*/
#endif
	if (freq <= PHY_DLL_DISABLE_FREQ) {
		pPHY_Reg->PHY_REGDLL |= 0x1F;	/*TX DLL bypass */
	} else {
		pPHY_Reg->PHY_REGDLL &= ~0x1F;	/* TX DLL bypass*/
	}

	dsb();
}

static noinline uint32 ddr_get_pll_freq(PLL_ID pll_id)	/*APLL-1;CPLL-2;DPLL-3;GPLL-4*/
{
	uint32 ret = 0;

	if (((pCRU_Reg->CRU_MODE_CON >> (pll_id * 4)) & 1) == 1)	/* DPLL Normal mode*/
		ret = 24 * ((pCRU_Reg->CRU_PLL_CON[pll_id][0] & 0xfff))	/* NF = 2*(CLKF+1)*/
		    / ((pCRU_Reg->CRU_PLL_CON[pll_id][1] & 0x3f)
		       * ((pCRU_Reg->CRU_PLL_CON[pll_id][0] >> 12) & 0x7) * ((pCRU_Reg->CRU_PLL_CON[pll_id][1] >> 6) & 0x7));	/* OD = 2^CLKOD*/
	else
		ret = 24;

	return ret;
}

static __sramdata uint32 clkFbDiv;
static __sramdata uint32 clkPostDiv1;
static __sramdata uint32 clkPostDiv2;

/*****************************************
*REFDIV   FBDIV     POSTDIV1/POSTDIV2      FOUTPOSTDIV           freq Step        FOUTPOSRDIV            finally use
*==================================================================================================================
*1        17 - 66   4                      100MHz - 400MHz          6MHz          200MHz  <= 300MHz             <= 150MHz
*1        17 - 66   3                      133MHz - 533MHz          8MHz
*1        17 - 66   2                      200MHz - 800MHz          12MHz         300MHz  <= 600MHz      150MHz <= 300MHz
*1        17 - 66   1                      400MHz - 1600MHz         24MHz         600MHz  <= 1200MHz     300MHz <= 600MHz
*******************************************/
/*for minimum jitter operation, the highest VCO and FREF frequencies should be used.*/
/*----------------------------------------------------------------------
*Name    : uint32_t __sramlocalfunc ddr_set_pll(uint32_t nMHz, uint32_t set)
*Desc    : 设置ddr pll
*Params  : nMHZ -> ddr工作频率
*          set  ->0获取设置的频率信息
*                 1设置ddr pll
*Return  : 设置的频率值
*Notes   : 在变频时需要先set=0调用一次ddr_set_pll，再set=1 调用ddr_set_pll
-*---------------------------------------------------------------------*/
static uint32 __sramfunc ddr_set_pll(uint32 nMHz, uint32 set)
{
	uint32 ret = 0;
	int delay = 1000;
	uint32 pll_id = 1;	/*DPLL*/

	if (nMHz == 24) {
		ret = 24;
		goto out;
	}
	if (!set) {
		if (nMHz <= 150) {	/*实际输出频率<300*/
			clkPostDiv1 = 6;
		} else if (nMHz <= 200) {
			clkPostDiv1 = 4;
		} else if (nMHz <= 300) {
			clkPostDiv1 = 3;
		} else if (nMHz <= 450) {
			clkPostDiv1 = 2;
		} else {
			clkPostDiv1 = 1;
		}
		clkPostDiv2 = 1;
		clkFbDiv = (nMHz * 2 * DDR_PLL_REFDIV * clkPostDiv1 * clkPostDiv2) / 24;	/*最后送入ddr的是再经过2分频*/
		ret =
		    (24 * clkFbDiv) / (2 * DDR_PLL_REFDIV * clkPostDiv1 *
				       clkPostDiv2);
	} else {
		pCRU_Reg->CRU_MODE_CON = (0x1 << ((pll_id * 4) + 16)) | (0x0 << (pll_id * 4));	/*PLL slow-mode*/

		pCRU_Reg->CRU_PLL_CON[pll_id][0] =
		    FBDIV(clkFbDiv) | POSTDIV1(clkPostDiv1);
		pCRU_Reg->CRU_PLL_CON[pll_id][1] = REFDIV(DDR_PLL_REFDIV) | POSTDIV2(clkPostDiv2) | (0x10001 << 12);	/*interger mode*/

		ddr_delayus(1);

		while (delay > 0) {
			ddr_delayus(1);
			if (pCRU_Reg->CRU_PLL_CON[pll_id][1] & (PLL_LOCK_STATUS))	/*wait for pll locked*/
				break;
			delay--;
		}

		pCRU_Reg->CRU_CLKSEL_CON[26] = ((0x3 << 16) | 0x0);	/*clk_ddr_src:clk_ddrphy = 1:1*/
		pCRU_Reg->CRU_MODE_CON = (0x1 << ((pll_id * 4) + 16)) | (0x1 << (pll_id * 4));	/*PLL normal*/
	}
out:
	return ret;
}

uint32 PIE_FUNC(ddr_set_pll)(uint32 nMHz, uint32 set)
{
	return ddr_set_pll(nMHz, set);
}

EXPORT_PIE_SYMBOL(FUNC(ddr_set_pll));
static uint32(*p_ddr_set_pll) (uint32 nMHz, uint32 set);

/*----------------------------------------------------------------------
*Name    : uint32_t ddr_get_parameter(uint32_t nMHz)
*Desc    : 获取配置参数
*Params  : nMHZ -> ddr工作频率
*Return  : 0 成功
*          -1 失败
*          -4 频率值超过颗粒最大频率
*Notes   :
*----------------------------------------------------------------------*/
static uint32 ddr_get_parameter(uint32 nMHz)
{
	uint32_t tmp;
	uint32_t ret = 0;
	uint32_t al;
	uint32_t bl;
	uint32_t cl;
	uint32_t cwl;
	uint32_t bl_tmp;
	PCTL_TIMING_T *p_pctl_timing = &(p_ddr_reg->pctl_timing);
	NOC_TIMING_T *p_noc_timing = &(p_ddr_reg->noc_timing);

	p_pctl_timing->togcnt1u = nMHz;
	p_pctl_timing->togcnt100n = nMHz / 10;
	p_pctl_timing->tinit = 200;

	if (p_ddr_reg->mem_type == DDR3) {
		if (p_ddr_reg->ddr_speed_bin > DDR3_DEFAULT) {
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
#define DDR3_tRTW            (2)	/*register min valid value*/
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
		p_pctl_timing->trsth = 500;
		al = 0;
		bl = 8;
		if (nMHz <= 330) {
			tmp = 0;
		} else if (nMHz <= 400) {
			tmp = 1;
		} else if (nMHz <= 533) {
			tmp = 2;
		} else {	/*666MHz*/
			tmp = 3;
		}
		if (nMHz <= DDR3_DDR2_DLL_DISABLE_FREQ) {	/*when dll bypss cl = cwl = 6*/
			cl = 6;
			cwl = 6;
		} else {
			cl = ddr3_cl_cwl[p_ddr_reg->ddr_speed_bin][tmp] >> 16;
			cwl =
			    ddr3_cl_cwl[p_ddr_reg->ddr_speed_bin][tmp] & 0x0ff;
		}
		if (cl == 0) {
			ret = -4;	/*超过颗粒的最大频率*/
		}
		if (nMHz <= DDR3_DDR2_ODT_DISABLE_FREQ) {
			p_ddr_reg->ddrMR[1] = DDR3_DS_40 | DDR3_Rtt_Nom_DIS;
		} else {
			p_ddr_reg->ddrMR[1] = DDR3_DS_40 | DDR3_Rtt_Nom_120;
		}
		p_ddr_reg->ddrMR[2] = DDR3_MR2_CWL(cwl) /* | DDR3_Rtt_WR_60 */ ;
		p_ddr_reg->ddrMR[3] = 0;
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
		if (p_ddr_reg->ddr_capability_per_die <= 0x4000000) {	/*512Mb 90ns*/
			tmp = DDR3_tRFC_512Mb;
		} else if (p_ddr_reg->ddr_capability_per_die <= 0x8000000) {	/*1Gb 110ns*/
			tmp = DDR3_tRFC_1Gb;
		} else if (p_ddr_reg->ddr_capability_per_die <= 0x10000000) {	/*2Gb 160ns*/
			tmp = DDR3_tRFC_2Gb;
		} else if (p_ddr_reg->ddr_capability_per_die <= 0x20000000) {/*4Gb 300ns*/
			tmp = DDR3_tRFC_4Gb;
		} else{		/*8Gb  350ns*/
			tmp = DDR3_tRFC_8Gb;
		}
		p_pctl_timing->trfc = (tmp * nMHz + 999) / 1000;
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
		p_noc_timing->b.WrToMiss =
		    ((cwl + ((DDR3_tWR * nMHz + 999) / 1000) + cl + cl) & 0x3F);
		/*
		 * tRC=tRAS+tRP
		 */
		p_pctl_timing->trc =
		    ((((ddr3_tRC_tFAW[p_ddr_reg->ddr_speed_bin] >> 16) * nMHz +
		       999) / 1000) & 0x3F);
		p_noc_timing->b.ActToAct =
		    ((((ddr3_tRC_tFAW[p_ddr_reg->ddr_speed_bin] >> 16) * nMHz +
		       999) / 1000) & 0x3F);

		p_pctl_timing->trtw = (cl + 2 - cwl);	/*DDR3_tRTW*/
		p_noc_timing->b.RdToWr = ((cl + 2 - cwl) & 0x1F);
		p_pctl_timing->tal = al;
		p_pctl_timing->tcl = cl;
		p_pctl_timing->tcwl = cwl;
		/*
		 * tRAS, 37.5ns(400MHz)     37.5ns(533MHz)
		 */
		p_pctl_timing->tras =
		    (((DDR3_tRAS * nMHz + (nMHz >> 1) + 999) / 1000) & 0x3F);
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
		tmp = ((DDR3_tRRD * nMHz + 999) / 1000);
		if (tmp < 4) {
			tmp = 4;
		}
		p_pctl_timing->trrd = (tmp & 0xF);
		/*
		 * tRTP, max(4 tCK,7.5ns)
		 */
		tmp = ((DDR3_tRTP * nMHz + (nMHz >> 1) + 999) / 1000);
		if (tmp < 4) {
			tmp = 4;
		}
		p_pctl_timing->trtp = tmp & 0xF;
		/*
		 * RdToMiss=tRTP+tRP + tRCD - (BL/2 * tCK)
		 */
		p_noc_timing->b.RdToMiss = ((tmp + cl + cl - (bl >> 1)) & 0x3F);
		/*
		 * tWR, 15ns
		 */
		tmp = ((DDR3_tWR * nMHz + 999) / 1000);
		p_pctl_timing->twr = tmp & 0x1F;
		if (tmp < 9)
			tmp = tmp - 4;
		else
			tmp = tmp >> 1;
		p_ddr_reg->ddrMR[0] = DDR3_BL8 | DDR3_CL(cl) | DDR3_WR(tmp);

		/*
		 * tWTR, max(4 tCK,7.5ns)
		 */
		tmp = ((DDR3_tWTR * nMHz + (nMHz >> 1) + 999) / 1000);
		if (tmp < 4) {
			tmp = 4;
		}
		p_pctl_timing->twtr = tmp & 0xF;
		p_noc_timing->b.WrToRd = ((tmp + cwl) & 0x1F);
		/*
		 * tXP, max(3 tCK, 7.5ns)(<933MHz)
		 */
		tmp = ((DDR3_tXP * nMHz + (nMHz >> 1) + 999) / 1000);
		if (tmp < 3) {
			tmp = 3;
		}
		p_pctl_timing->txp = tmp & 0x7;
		/*
		 * tXPDLL, max(10 tCK,24ns)
		 */
		tmp = ((DDR3_tXPDLL * nMHz + 999) / 1000);
		if (tmp < 10) {
			tmp = 10;
		}
		p_pctl_timing->txpdll = tmp & 0x3F;
		/*
		 * tZQCS, max(64 tCK, 80ns)
		 */
		tmp = ((DDR3_tZQCS * nMHz + 999) / 1000);
		if (tmp < 64) {
			tmp = 64;
		}
		p_pctl_timing->tzqcs = tmp & 0x7F;
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
		tmp = ((DDR3_tCKSRE * nMHz + 999) / 1000);
		if (tmp < 5) {
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
		if (nMHz >= 533) {
			tmp = ((DDR3_tCKE_533MHz * nMHz + 999) / 1000);
		} else {
			tmp =
			    ((DDR3_tCKE_400MHz * nMHz + (nMHz >> 1) +
			      999) / 1000);
		}
		if (tmp < 3) {
			tmp = 3;
		}
		p_pctl_timing->tcke = tmp & 0x7;
		/*
		 * tCKESR, =tCKE + 1tCK
		 */
		p_pctl_timing->tckesr = (tmp + 1) & 0xF;
		/*
		 * tMOD, max(12 tCK,15ns)
		 */
		tmp = ((DDR3_tMOD * nMHz + 999) / 1000);
		if (tmp < 12) {
			tmp = 12;
		}
		p_pctl_timing->tmod = tmp & 0x1F;
		/*
		 * tRSTL, 100ns
		 */
		p_pctl_timing->trstl =
		    ((DDR3_tRSTL * nMHz + 999) / 1000) & 0x7F;
		/*
		 * tZQCL, max(256 tCK, 320ns)
		 */
		tmp = ((DDR3_tZQCL * nMHz + 999) / 1000);
		if (tmp < 256) {
			tmp = 256;
		}
		p_pctl_timing->tzqcl = tmp & 0x3FF;
		/*
		 * tMRR, 0 tCK
		 */
		p_pctl_timing->tmrr = 0;
		/*
		 * tDPD, 0
		 */
		p_pctl_timing->tdpd = 0;

		/**************************************************
		*NOC Timing
		**************************************************/
		p_noc_timing->b.BurstLen = ((bl >> 1) & 0x7);
	} else if (p_ddr_reg->mem_type == LPDDR2) {
#define LPDDR2_tREFI_3_9_us    (38)	/*unit 100ns*/
#define LPDDR2_tREFI_7_8_us    (78)	/*unit 100ns*/
#define LPDDR2_tMRD            (5)	/*tCK*/
#define LPDDR2_tRFC_8Gb        (210)	/*ns*/
#define LPDDR2_tRFC_4Gb        (130)	/*ns*/
#define LPDDR2_tRP_4_BANK               (24)	/*ns*/
#define LPDDR2_tRPab_SUB_tRPpb_4_BANK   (0)
#define LPDDR2_tRP_8_BANK               (27)	/*ns*/
#define LPDDR2_tRPab_SUB_tRPpb_8_BANK   (3)
#define LPDDR2_tRTW          (1)	/*tCK register min valid value*/
#define LPDDR2_tRAS          (42)	/*ns*/
#define LPDDR2_tRCD          (24)	/*ns*/
#define LPDDR2_tRRD          (10)	/*ns*/
#define LPDDR2_tRTP          (7)	/*ns*/
#define LPDDR2_tWR           (15)	/*ns*/
#define LPDDR2_tWTR_GREAT_200MHz         (7)	/*ns*/
#define LPDDR2_tWTR_LITTLE_200MHz        (10)	/*ns*/
#define LPDDR2_tXP           (7)	/*ns*/
#define LPDDR2_tXPDLL        (0)
#define LPDDR2_tZQCS         (90)	/*ns*/
#define LPDDR2_tZQCSI        (0)
#define LPDDR2_tDQS          (1)
#define LPDDR2_tCKSRE        (1)	/*tCK*/
#define LPDDR2_tCKSRX        (2)	/*tCK*/
#define LPDDR2_tCKE          (3)	/*tCK*/
#define LPDDR2_tMOD          (0)
#define LPDDR2_tRSTL         (0)
#define LPDDR2_tZQCL         (360)	/*ns*/
#define LPDDR2_tMRR          (2)	/*tCK*/
#define LPDDR2_tCKESR        (15)	/*ns*/
#define LPDDR2_tDPD_US       (500)
#define LPDDR2_tFAW_GREAT_200MHz    (50)	/*ns*/
#define LPDDR2_tFAW_LITTLE_200MHz   (60)	/*ns*/
#define LPDDR2_tDLLK         (2)	/*tCK*/
#define LPDDR2_tDQSCK_MAX    (3)	/*tCK*/
#define LPDDR2_tDQSCK_MIN    (0)	/*tCK*/
#define LPDDR2_tDQSS         (1)	/*tCK*/

		uint32 trp_tmp;
		uint32 trcd_tmp;
		uint32 tras_tmp;
		uint32 trtp_tmp;
		uint32 twr_tmp;

		al = 0;
		if (nMHz >= 200) {
			bl = 4;	/*you can change burst here*/
		} else {
			bl = 8;	/* freq < 200MHz, BL fixed 8*/
		}
		/*     1066 933 800 667 533 400 333
		 * RL,   8   7   6   5   4   3   3
		 * WL,   4   4   3   2   2   1   1
		 */
		if (nMHz <= 200) {
			cl = 3;
			cwl = 1;
			p_ddr_reg->ddrMR[2] = LPDDR2_RL3_WL1;
		} else if (nMHz <= 266) {
			cl = 4;
			cwl = 2;
			p_ddr_reg->ddrMR[2] = LPDDR2_RL4_WL2;
		} else if (nMHz <= 333) {
			cl = 5;
			cwl = 2;
			p_ddr_reg->ddrMR[2] = LPDDR2_RL5_WL2;
		} else if (nMHz <= 400) {
			cl = 6;
			cwl = 3;
			p_ddr_reg->ddrMR[2] = LPDDR2_RL6_WL3;
		} else if (nMHz <= 466) {
			cl = 7;
			cwl = 4;
			p_ddr_reg->ddrMR[2] = LPDDR2_RL7_WL4;
		} else {		/*(nMHz<=1066)*/
			cl = 8;
			cwl = 4;
			p_ddr_reg->ddrMR[2] = LPDDR2_RL8_WL4;
		}
		p_ddr_reg->ddrMR[3] = LPDDR2_DS_34;
		p_ddr_reg->ddrMR[0] = 0;
		/**************************************************
		* PCTL Timing
		**************************************************/
		/*
		 * tREFI, average periodic refresh interval, 15.6us(<256Mb) 7.8us(256Mb-1Gb) 3.9us(2Gb-8Gb)
		 */
		if (p_ddr_reg->ddr_capability_per_die >= 0x10000000) {	/*2Gb*/
			p_pctl_timing->trefi = LPDDR2_tREFI_3_9_us;
		} else {
			p_pctl_timing->trefi = LPDDR2_tREFI_7_8_us;
		}

		/*
		 * tMRD, (=tMRW), 5 tCK
		 */
		p_pctl_timing->tmrd = LPDDR2_tMRD & 0x7;
		/*
		 * tRFC, 90ns(<=512Mb) 130ns(1Gb-4Gb) 210ns(8Gb)
		 */
		if (p_ddr_reg->ddr_capability_per_die >= 0x40000000) {	/*8Gb*/
			p_pctl_timing->trfc =
			    (LPDDR2_tRFC_8Gb * nMHz + 999) / 1000;
			/*
			 * tXSR, max(2tCK,tRFC+10ns)
			 */
			tmp = (((LPDDR2_tRFC_8Gb + 10) * nMHz + 999) / 1000);
		} else {
			p_pctl_timing->trfc =
			    (LPDDR2_tRFC_4Gb * nMHz + 999) / 1000;
			tmp = (((LPDDR2_tRFC_4Gb + 10) * nMHz + 999) / 1000);
		}
		if (tmp < 2) {
			tmp = 2;
		}
		p_pctl_timing->texsr = tmp & 0x3FF;
		/*
		 * tRP, max(3tCK, 4-bank:15ns(Fast) 18ns(Typ) 24ns(Slow), 8-bank:18ns(Fast) 21ns(Typ) 27ns(Slow))
		 */
		trp_tmp = ((LPDDR2_tRP_8_BANK * nMHz + 999) / 1000);
		if (trp_tmp < 3) {
			trp_tmp = 3;
		}
		p_pctl_timing->trp =
		    ((((LPDDR2_tRPab_SUB_tRPpb_8_BANK * nMHz +
			999) / 1000) & 0x3) << 16) | (trp_tmp & 0xF);

		/*
		 * tRAS, max(3tCK,42ns)
		 */
		tras_tmp = ((LPDDR2_tRAS * nMHz + 999) / 1000);
		if (tras_tmp < 3) {
			tras_tmp = 3;
		}
		p_pctl_timing->tras = (tras_tmp & 0x3F);

		/*
		 * tRCD, max(3tCK, 15ns(Fast) 18ns(Typ) 24ns(Slow))
		 */
		trcd_tmp = ((LPDDR2_tRCD * nMHz + 999) / 1000);
		if (trcd_tmp < 3) {
			trcd_tmp = 3;
		}
		p_pctl_timing->trcd = (trcd_tmp & 0xF);
		/*
		 * tRTP, max(2tCK, 7.5ns)
		 */
		trtp_tmp = ((LPDDR2_tRTP * nMHz + (nMHz >> 1) + 999) / 1000);
		if (trtp_tmp < 2) {
			trtp_tmp = 2;
		}
		p_pctl_timing->trtp = trtp_tmp & 0xF;
		/*
		 * tWR, max(3tCK,15ns)
		 */
		twr_tmp = ((LPDDR2_tWR * nMHz + 999) / 1000);
		if (twr_tmp < 3) {
			twr_tmp = 3;
		}
		p_pctl_timing->twr = twr_tmp & 0x1F;
		bl_tmp =
		    (bl ==
		     16) ? LPDDR2_BL16 : ((bl == 8) ? LPDDR2_BL8 : LPDDR2_BL4);
		p_ddr_reg->ddrMR[1] = bl_tmp | LPDDR2_nWR(twr_tmp);

		/*
		 * WrToMiss=WL*tCK + tDQSS + tWR + tRP + tRCD
		 */
		p_noc_timing->b.WrToMiss =
		    ((cwl + LPDDR2_tDQSS + twr_tmp + trp_tmp +
		      trcd_tmp) & 0x3F);
		/*
		 * RdToMiss=tRTP + tRP + tRCD - (BL/2 * tCK)
		 */
		p_noc_timing->b.RdToMiss =
		    ((trtp_tmp + trp_tmp + trcd_tmp - (bl >> 1)) & 0x3F);
		/*
		 * tRC=tRAS+tRP
		 */
		p_pctl_timing->trc = ((tras_tmp + trp_tmp) & 0x3F);
		p_noc_timing->b.ActToAct = ((tras_tmp + trp_tmp) & 0x3F);
		/*
		 * RdToWr=RL+tDQSCK-WL
		 */
		p_pctl_timing->trtw = (cl + LPDDR2_tDQSCK_MAX + (bl / 2) + 1 - cwl);	/*LPDDR2_tRTW*/
		p_noc_timing->b.RdToWr =
		    ((cl + LPDDR2_tDQSCK_MAX + 1 - cwl) & 0x1F);
		p_pctl_timing->tal = al;
		p_pctl_timing->tcl = cl;
		p_pctl_timing->tcwl = cwl;
		/*
		 * tRRD, max(2tCK,10ns)
		 */
		tmp = ((LPDDR2_tRRD * nMHz + 999) / 1000);
		if (tmp < 2) {
			tmp = 2;
		}
		p_pctl_timing->trrd = (tmp & 0xF);
		/*
		 * tWTR, max(2tCK, 7.5ns(533-266MHz)  10ns(200-166MHz))
		 */
		if (nMHz > 200) {
			tmp =
			    ((LPDDR2_tWTR_GREAT_200MHz * nMHz + (nMHz >> 1) +
			      999) / 1000);
		} else {
			tmp = ((LPDDR2_tWTR_LITTLE_200MHz * nMHz + 999) / 1000);
		}
		if (tmp < 2) {
			tmp = 2;
		}
		p_pctl_timing->twtr = tmp & 0xF;
		/*
		 * WrToRd=WL+tDQSS+tWTR
		 */
		p_noc_timing->b.WrToRd = ((cwl + LPDDR2_tDQSS + tmp) & 0x1F);
		/*
		 * tXP, max(2tCK,7.5ns)
		 */
		tmp = ((LPDDR2_tXP * nMHz + (nMHz >> 1) + 999) / 1000);
		if (tmp < 2) {
			tmp = 2;
		}
		p_pctl_timing->txp = tmp & 0x7;
		/*
		 * tXPDLL, 0ns
		 */
		p_pctl_timing->txpdll = LPDDR2_tXPDLL;
		/*
		 * tZQCS, 90ns
		 */
		p_pctl_timing->tzqcs =
		    ((LPDDR2_tZQCS * nMHz + 999) / 1000) & 0x7F;
		/*
		 * tZQCSI,
		 */
		/*if (pDDR_Reg->MCFG &= lpddr2_s4) {*/
		if (1) {
			p_pctl_timing->tzqcsi = LPDDR2_tZQCSI;
		} else {
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
		/*
		 * tMOD, 0 tCK
		 */
		p_pctl_timing->tmod = LPDDR2_tMOD;
		/*
		 * tRSTL, 0 tCK
		 */
		p_pctl_timing->trstl = LPDDR2_tRSTL;
		/*
		 * tZQCL, 360ns
		 */
		p_pctl_timing->tzqcl =
		    ((LPDDR2_tZQCL * nMHz + 999) / 1000) & 0x3FF;
		/*
		 * tMRR, 2 tCK
		 */
		p_pctl_timing->tmrr = LPDDR2_tMRR;
		/*
		 * tCKESR, max(3tCK,15ns)
		 */
		tmp = ((LPDDR2_tCKESR * nMHz + 999) / 1000);
		if (tmp < 3) {
			tmp = 3;
		}
		p_pctl_timing->tckesr = tmp & 0xF;
		/*
		 * tDPD, 500us
		 */
		p_pctl_timing->tdpd = LPDDR2_tDPD_US;

		/*************************************************
		* NOC Timing
		**************************************************/
		p_noc_timing->b.BurstLen = ((bl >> 1) & 0x7);
	}

out:
	return ret;
}

/*----------------------------------------------------------------------
*Name    : uint32_t __sramlocalfunc ddr_update_timing(void)
*Desc    : 更新pctl phy 相关timing寄存器
*Params  : void
*Return  : 0 成功
*Notes   :
*----------------------------------------------------------------------*/
static uint32 __sramfunc ddr_update_timing(void)
{
	uint32_t bl_tmp;
	uint32_t ret = 0;

	PCTL_TIMING_T *p_pctl_timing = &(DATA(ddr_reg).pctl_timing);
	NOC_TIMING_T *p_noc_timing = &(DATA(ddr_reg).noc_timing);

	ddr_copy((uint32 *)&(pDDR_Reg->TOGCNT1U),
		 (uint32 *)&(p_pctl_timing->togcnt1u), 34);
/*    pPHY_Reg->PHY_REG1 |= PHY_Burst8;*/    /*ddr3 burst length固定为8*/
	pPHY_Reg->PHY_REGb = ((p_pctl_timing->tcl << 4) | (p_pctl_timing->tal));
	pPHY_Reg->PHY_REGc = p_pctl_timing->tcwl;
	*(volatile uint32 *)SysSrv_DdrTiming = p_noc_timing->d32;
	/*Update PCTL BL*/
	if (DATA(ddr_reg).mem_type == DDR3) {
		pDDR_Reg->MCFG =
		    (pDDR_Reg->MCFG &
		     (~(0x1 | (0x3 << 18) | (0x1 << 17) | (0x1 << 16))))
		    | ddr2_ddr3_bl_8 | tfaw_cfg(5) | pd_exit_slow | pd_type(1);
		pDDR_Reg->DFITRDDATAEN = (pDDR_Reg->TAL + pDDR_Reg->TCL) - 3;	/*trdata_en = rl-3*/
		pDDR_Reg->DFITPHYWRLAT = pDDR_Reg->TCWL - 1;
	} else if (DATA(ddr_reg).mem_type == LPDDR2) {
		if ((DATA(ddr_reg).ddrMR[1] & 0x7) == LPDDR2_BL8) {
			bl_tmp = mddr_lpddr2_bl_8;
			pPHY_Reg->PHY_REG1 |= PHY_Burst8;
		} else if ((DATA(ddr_reg).ddrMR[1] & 0x7) == LPDDR2_BL4) {
			bl_tmp = mddr_lpddr2_bl_4;
			pPHY_Reg->PHY_REG1 &= (~PHY_Burst8);
		} else{		/*if((DATA(ddr_reg).ddrMR[1] & 0x7) == LPDDR2_BL16)*/
			bl_tmp = mddr_lpddr2_bl_16;
			ret = -1;
		}
		if (DATA(ddr_freq) >= 200) {
			pDDR_Reg->MCFG =
			    (pDDR_Reg->MCFG &
			     (~
			      ((0x3 << 20) | (0x3 << 18) | (0x1 << 17) |
			       (0x1 << 16)))) | bl_tmp | tfaw_cfg(5) |
			    pd_exit_fast | pd_type(1);
		} else {
			pDDR_Reg->MCFG =
			    (pDDR_Reg->MCFG &
			     (~
			      ((0x3 << 20) | (0x3 << 18) | (0x1 << 17) |
			       (0x1 << 16)))) | mddr_lpddr2_bl_8 | tfaw_cfg(6) |
			    pd_exit_fast | pd_type(1);
		}
		pDDR_Reg->DFITRDDATAEN = pDDR_Reg->TCL - 3;
		pDDR_Reg->DFITPHYWRLAT = pDDR_Reg->TCWL;

	}
	return ret;
}

/*----------------------------------------------------------------------
*Name    : uint32_t __sramlocalfunc ddr_update_mr(void)
*Desc    : 更新颗粒MR寄存器
*Params  : void
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/
static uint32 __sramfunc ddr_update_mr(void)
{
	/*uint32 cs;
	cs = READ_CS_INFO();
	cs = (1 << cs) - 1;	*/
	if (DATA(ddr_reg).mem_type == DDR3) {
		if (DATA(ddr_freq) > DDR3_DDR2_DLL_DISABLE_FREQ) {
			if (DATA(ddr_dll_status) == DDR3_DLL_DISABLE) {	/*off -> on*/
				ddr_send_command(3, MRS_cmd, bank_addr(0x1) | cmd_addr((DATA(ddr_reg).ddrMR[1])));	/*DLL enable*/
				ddr_send_command(3, MRS_cmd, bank_addr(0x0) | cmd_addr(((DATA(ddr_reg).ddrMR[0])) | DDR3_DLL_RESET));	/*DLL reset*/
				ddr_delayus(2);	/*at least 200 DDR cycle*/
				ddr_send_command(3, MRS_cmd,
						 bank_addr(0x0) |
						 cmd_addr((DATA(ddr_reg).ddrMR
							   [0])));
				DATA(ddr_dll_status) = DDR3_DLL_ENABLE;
			} else{  	/*on -> on*/
				ddr_send_command(3, MRS_cmd,
						 bank_addr(0x1) |
						 cmd_addr((DATA(ddr_reg).ddrMR
							   [1])));
				ddr_send_command(3, MRS_cmd,
						 bank_addr(0x0) |
						 cmd_addr((DATA(ddr_reg).ddrMR
							   [0])));
			}
		} else {
			ddr_send_command(3, MRS_cmd, bank_addr(0x1) | cmd_addr(((DATA(ddr_reg).ddrMR[1])) | DDR3_DLL_DISABLE));	/*DLL disable*/
			ddr_send_command(3, MRS_cmd,
					 bank_addr(0x0) |
					 cmd_addr((DATA(ddr_reg).ddrMR[0])));
			DATA(ddr_dll_status) = DDR3_DLL_DISABLE;
		}
		ddr_send_command(3, MRS_cmd,
				 bank_addr(0x2) |
				 cmd_addr((DATA(ddr_reg).ddrMR[2])));
	} else if (DATA(ddr_reg).mem_type == LPDDR2) {
		ddr_send_command(3, MRS_cmd,
				 lpddr2_ma(0x1) |
				 lpddr2_op(DATA(ddr_reg).ddrMR[1]));
		ddr_send_command(3, MRS_cmd,
				 lpddr2_ma(0x2) |
				 lpddr2_op(DATA(ddr_reg).ddrMR[2]));
		ddr_send_command(3, MRS_cmd,
				 lpddr2_ma(0x3) |
				 lpddr2_op(DATA(ddr_reg).ddrMR[3]));
	}
	return 0;
}

/*----------------------------------------------------------------------
*Name    : void __sramlocalfunc ddr_update_odt(void)
*Desc    : update PHY odt & PHY driver impedance
*Params  : void
*Return  : void
*Notes   :-------------------------------------------------*/
static void __sramfunc ddr_update_odt(void)
{
	/*adjust DRV and ODT*/
	uint32 phy_odt;
	if (DATA(ddr_reg).mem_type == DDR3) {
		if (DATA(ddr_freq) <= PHY_ODT_DISABLE_FREQ) {
			phy_odt = PHY_RTT_DISABLE;
		} else {
			phy_odt = PHY_RTT_216ohm;
		}
	} else {
		phy_odt = PHY_RTT_DISABLE;
	}

	pPHY_Reg->PHY_REG21 = PHY_DRV_ODT_SET(phy_odt);	/*DQS0 odt*/
	pPHY_Reg->PHY_REG31 = PHY_DRV_ODT_SET(phy_odt);	/*DQS1 odt*/
	pPHY_Reg->PHY_REG41 = PHY_DRV_ODT_SET(phy_odt);	/*DQS2 odt*/
	pPHY_Reg->PHY_REG51 = PHY_DRV_ODT_SET(phy_odt);	/*DQS3 odt*/

	pPHY_Reg->PHY_REG11 = PHY_DRV_ODT_SET(PHY_RON_44ohm);	/*cmd drv*/
	pPHY_Reg->PHY_REG16 = PHY_DRV_ODT_SET(PHY_RON_44ohm);	/*clk drv*/

	pPHY_Reg->PHY_REG20 = PHY_DRV_ODT_SET(PHY_RON_44ohm);	/*DQS0 drv*/
	pPHY_Reg->PHY_REG30 = PHY_DRV_ODT_SET(PHY_RON_44ohm);	/*DQS1 drv*/
	pPHY_Reg->PHY_REG40 = PHY_DRV_ODT_SET(PHY_RON_44ohm);	/*DQS2 drv*/
	pPHY_Reg->PHY_REG50 = PHY_DRV_ODT_SET(PHY_RON_44ohm);	/*DQS3 drv*/

	dsb();
}

#if 0
void PIE_FUNC(ddr_adjust_config)(void)
{
	/*enter config state*/
	ddr_move_to_Config_state();

	/*set auto power down idle*/
	pDDR_Reg->MCFG = (pDDR_Reg->MCFG & 0xffff00ff) | (PD_IDLE << 8);
	/*enable the hardware low-power interface*/
	pDDR_Reg->SCFG.b.hw_low_power_en = 1;
	ddr_update_odt();
	/*enter access state*/
	ddr_move_to_Access_state();
}

EXPORT_PIE_SYMBOL(FUNC(ddr_adjust_config));

/*----------------------------------------------------------------------
Name    : __sramfunc void ddr_adjust_config(uint32_t dram_type)
Desc    :
Params  : dram_type ->颗粒类型
Return  : void
Notes   :
----------------------------------------------------------------------*/

static void ddr_adjust_config(uint32_t dram_type)
{
	unsigned long save_sp;
	uint32 i;
	volatile uint32 n;
	volatile unsigned int *temp = (volatile unsigned int *)SRAM_CODE_OFFSET;

    /** 1. Make sure there is no host access */
	flush_cache_all();
	outer_flush_all();
	flush_tlb_all();
	for (i = 0; i < 2; i++) {
		n = temp[1024 * i];
		barrier();
	}
	n = pDDR_Reg->SCFG.d32;
	n = pPHY_Reg->PHY_REG1;
	n = pCRU_Reg->CRU_PLL_CON[0][0];
	n = *(volatile uint32_t *)SysSrv_DdrTiming;
	dsb();

	call_with_stack(fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_adjust_config)),
			(void *)0, rockchip_sram_stack);

}
#endif

static void __sramfunc idle_port(void)
{
	int i;
	uint32 clk_gate[11];
	uint32 pd_status;
	uint32 idle_req, idle_stus;
	/*bit0:core bit1:gpu bit2:video bit3:vio 1:power off*/
	pd_status = *(volatile uint32 *)PMU_PWEDN_ST;
	/*save clock gate status*/
	for (i = 0; i < 11; i++) {
		clk_gate[i] = pCRU_Reg->CRU_CLKGATE_CON[i];
	}
	/*enable all clock gate for request idle*/
	for (i = 0; i < 11; i++) {
		pCRU_Reg->CRU_CLKGATE_CON[i] = 0xffff0000;
	}

	idle_req = (1 << peri_pwr_idlereq); /*peri*/
	idle_stus = peri_pwr_idle;
	if ((pd_status & (0x1<<3)) == 0) {/*pwr_vio*/
		idle_req |= (1 << vio_pwr_idlereq);
		idle_stus |= vio_pwr_idle;
	}
	if ((pd_status & (0x1<<2)) == 0) {/*pwr_vpu*/
		idle_req |= (1 << vpu_pwr_idlereq);
		idle_stus |= vpu_pwr_idle;
	}
	if ((pd_status & (0x1<<1)) == 0) {/*pwr_gpu*/
		idle_req |= (1 << gpu_pwr_idlereq);
		idle_stus |= gpu_pwr_idle;
	}

	pGRF_Reg->GRF_SOC_CON[2] = (idle_req << 16) | idle_req;
	dsb();
	while ((pGRF_Reg->GRF_SOC_STATUS0 & idle_stus) != idle_stus)
	;

	/*resume clock gate status*/
	for (i = 0; i < 10; i++)
		pCRU_Reg->CRU_CLKGATE_CON[i] = (clk_gate[i] | 0xffff0000);
}

static void __sramfunc deidle_port(void)
{
	int i;
	uint32 clk_gate[11];
	uint32 pd_status;
	uint32 idle_req, idle_stus;
	/*bit0:core bit1:gpu bit2:video bit3:vio 1:power off*/
	pd_status = *(volatile uint32 *)PMU_PWEDN_ST;

	/*save clock gate status*/
	for (i = 0; i < 11; i++) {
		clk_gate[i] = pCRU_Reg->CRU_CLKGATE_CON[i];
	}
	/*enable all clock gate for request idle*/
	for (i = 0; i < 11; i++) {
		pCRU_Reg->CRU_CLKGATE_CON[i] = 0xffff0000;
	}

	idle_req = (1 << peri_pwr_idlereq); /*peri*/
	idle_stus = peri_pwr_idle;
	if ((pd_status & (0x1<<3)) == 0) {/*pwr_vio*/
		idle_req |= (1 << vio_pwr_idlereq);
		idle_stus |= vio_pwr_idle;
	}
	if ((pd_status & (0x1<<2)) == 0) {/*pwr_vpu*/
		idle_req |= (1 << vpu_pwr_idlereq);
		idle_stus |= vpu_pwr_idle;
	}
	if ((pd_status & (0x1<<1)) == 0) {/*pwr_gpu*/
		idle_req |= (1 << gpu_pwr_idlereq);
		idle_stus |= gpu_pwr_idle;
	}

	pGRF_Reg->GRF_SOC_CON[2] = (idle_req << 16) | 0 ;
	dsb();
	while ((pGRF_Reg->GRF_SOC_STATUS0 & idle_stus) != 0)
	;

	/*resume clock gate status*/
	for (i = 0; i < 10; i++)
		pCRU_Reg->CRU_CLKGATE_CON[i] = (clk_gate[i] | 0xffff0000);

}

/*----------------------------------------------------------------------
*Name    : void __sramlocalfunc ddr_selfrefresh_enter(uint32 nMHz)
*Desc    : 进入自刷新
*Params  : nMHz ->ddr频率
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/
#if 1
static void __sramfunc ddr_selfrefresh_enter(uint32 nMHz)
{
	ddr_move_to_Config_state();
	ddr_move_to_Lowpower_state();
	pGRF_Reg->GRF_SOC_CON[2] = GRF_DDR_LP_EN;
	pPHY_Reg->PHY_REG0 = (pPHY_Reg->PHY_REG0 & (~(0x3 << 2)));	/*phy soft reset*/
	dsb();
	pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1 << 2) << 16) | (1 << 2);	/*disable DDR PHY clock*/
	ddr_delayus(1);
}

/*EXPORT_SYMBOL(ddr_selfrefresh_enter);*/
#endif

uint32 dtt_buffer[8];

/*----------------------------------------------------------------------
*Name    : void ddr_dtt_check(void)
*Desc    : data training check
*Params  : void
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/
void ddr_dtt_check(void)
{
	uint32 i;
	for (i = 0; i < 8; i++) {
		dtt_buffer[i] = p_copy_data[i];
	}
	dsb();
	flush_cache_all();
	outer_flush_all();
	for (i = 0; i < 8; i++) {
		if (dtt_buffer[i] != p_copy_data[i]) {
/*            sram_printascii("DTT failed!\n");*/
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
#if 1
static void __sramfunc ddr_selfrefresh_exit(void)
{
	pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1 << 2) << 16) | (0 << 2);	/*enable DDR PHY clock*/
	dsb();
	ddr_delayus(1);
	pPHY_Reg->PHY_REG0 = (pPHY_Reg->PHY_REG0 | (0x3 << 2));	/*phy soft de-reset*/
	pGRF_Reg->GRF_SOC_CON[2] = GRF_DDR_LP_DISB;
	/*pPHY_Reg->PHY_REG264 |= (1<<1);*/
	dsb();
	ddr_move_to_Config_state();
	ddr_data_training();
	ddr_move_to_Access_state();
/*    ddr_dtt_check();*/
}

#endif
/*----------------------------------------------------------------------
*Name    : void __sramlocalfunc ddr_change_freq_in(uint32 freq_slew)
*Desc    : 设置ddr pll前的timing及mr参数调整
*Params  : freq_slew :变频斜率 1升平  0降频
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/
void __sramlocalfunc ddr_change_freq_in(uint32 freq_slew)
{
	uint32 value_100n, value_1u;

	if (freq_slew == 1) {
		value_100n = DATA(ddr_reg).pctl_timing.togcnt100n;
		value_1u = DATA(ddr_reg).pctl_timing.togcnt1u;
		DATA(ddr_reg).pctl_timing.togcnt1u = pDDR_Reg->TOGCNT1U;
		DATA(ddr_reg).pctl_timing.togcnt100n = pDDR_Reg->TOGCNT100N;
		ddr_update_timing();
		ddr_update_mr();
		DATA(ddr_reg).pctl_timing.togcnt100n = value_100n;
		DATA(ddr_reg).pctl_timing.togcnt1u = value_1u;
	} else {
		pDDR_Reg->TOGCNT100N = DATA(ddr_reg).pctl_timing.togcnt100n;
		pDDR_Reg->TOGCNT1U = DATA(ddr_reg).pctl_timing.togcnt1u;
	}

	pDDR_Reg->TZQCSI = 0;

}

/*----------------------------------------------------------------------
*Name    : void __sramlocalfunc ddr_change_freq_out(uint32 freq_slew)
*Desc    : 设置ddr pll后的timing及mr参数调整
*Params  : freq_slew :变频斜率 1升平  0降频
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/
void __sramlocalfunc ddr_change_freq_out(uint32 freq_slew)
{
	if (freq_slew == 1) {
		pDDR_Reg->TOGCNT100N = DATA(ddr_reg).pctl_timing.togcnt100n;
		pDDR_Reg->TOGCNT1U = DATA(ddr_reg).pctl_timing.togcnt1u;
		pDDR_Reg->TZQCSI = DATA(ddr_reg).pctl_timing.tzqcsi;
	} else {
		ddr_update_timing();
		ddr_update_mr();
	}
	ddr_data_training();
}

static void __sramfunc ddr_SRE_2_SRX(uint32 freq, uint32 freq_slew)
{
	idle_port();

	ddr_move_to_Config_state();
	DATA(ddr_freq) = freq;
	ddr_change_freq_in(freq_slew);
	ddr_move_to_Lowpower_state();
	pGRF_Reg->GRF_SOC_CON[2] = GRF_DDR_LP_EN;
	pPHY_Reg->PHY_REG0 = (pPHY_Reg->PHY_REG0 & (~(0x3 << 2)));	/*phy soft reset*/
	dsb();
    /* 3. change frequence  */
	FUNC(ddr_set_pll) (freq, 1);
	ddr_set_dll_bypass(freq);	/*set phy dll mode;*/
	/*pPHY_Reg->PHY_REG0 = (pPHY_Reg->PHY_REG0 | (0x3 << 2)); */      /*phy soft de-reset */
	pPHY_Reg->PHY_REG0 |= (1 << 2); /*soft de-reset analogue(dll)*/
	ddr_delayus(5);
	pPHY_Reg->PHY_REG0 |= (1 << 3);/*soft de-reset digital*/
	pGRF_Reg->GRF_SOC_CON[2] = GRF_DDR_LP_DISB;
	dsb();
	ddr_update_odt();
	ddr_move_to_Config_state();
	ddr_change_freq_out(freq_slew);
	ddr_move_to_Access_state();

	deidle_port();
}

void PIE_FUNC(ddr_change_freq_sram)(void *arg)
{
	struct ddr_change_freq_sram_param *param = arg;
	/* Make sure ddr_SRE_2_SRX paramter less than 4 */
	ddr_SRE_2_SRX(param->freq, param->freq_slew);
}

EXPORT_PIE_SYMBOL(FUNC(ddr_change_freq_sram));

typedef struct freq_tag {
	uint32_t nMHz;
	struct ddr_freq_t *p_ddr_freq_t;
} freq_t;

/*----------------------------------------------------------------------
*Name    : uint32_t __sramfunc ddr_change_freq(uint32_t nMHz)
*Desc    : ddr变频
*Params  : nMHz -> 变频的频率值
*Return  : 频率值
*Notes   :
*----------------------------------------------------------------------*/
static uint32 ddr_change_freq_sram(void *arg)
{
	uint32 ret;
	uint32 i;
	volatile uint32 n;
	unsigned long flags;
	volatile unsigned int *temp = (volatile unsigned int *)SRAM_CODE_OFFSET;
	freq_t *p_freq_t = (freq_t *) arg;
	uint32 nMHz = p_freq_t->nMHz;
#if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
	struct ddr_freq_t *p_ddr_freq_t = p_freq_t->p_ddr_freq_t;
#endif

	struct ddr_change_freq_sram_param param;
	/*uint32 freq;*/
	uint32 freq_slew;
	uint32 arm_freq;
	arm_freq = ddr_get_pll_freq(APLL);
	*kern_to_pie(rockchip_pie_chunk, &DATA(loops_per_us)) =
	    LPJ_100MHZ * arm_freq / 1000000;
	ret = p_ddr_set_pll(nMHz, 0);
	if (ret == *p_ddr_freq) {
		goto out;
	} else {
		freq_slew = (ret > *p_ddr_freq) ? 1 : -1;
	}
	ddr_get_parameter(ret);
	/*kern_to_pie(rockchip_pie_chunk, &DATA(ddr_freq))= ret;*/
    /** 1. Make sure there is no host access */
	local_irq_save(flags);
	local_fiq_disable();
	flush_cache_all();
	outer_flush_all();
	flush_tlb_all();

#if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
	if (p_ddr_freq_t->screen_ft_us > 0) {
		p_ddr_freq_t->t1 = cpu_clock(0);
		p_ddr_freq_t->t2 = (uint32)(p_ddr_freq_t->t1 - p_ddr_freq_t->t0);   /*ns*/

		if ((p_ddr_freq_t->t2 > p_ddr_freq_t->screen_ft_us*1000) && (p_ddr_freq_t->screen_ft_us != 0xfefefefe)) {
			ret = 0;
			goto end;
		} else {
			rk_fb_poll_wait_frame_complete();
		}
	}
#endif
    /*8KB SRAM*/
	for (i = 0; i < 2; i++) {
		n = temp[1024 * i];
		barrier();
	}
	n = pDDR_Reg->SCFG.d32;
	n = pPHY_Reg->PHY_REG1;
	n = pCRU_Reg->CRU_PLL_CON[0][0];
	n = *(volatile uint32_t *)SysSrv_DdrTiming;
	n = pGRF_Reg->GRF_SOC_STATUS0;
	dsb();
	param.freq = ret;
	param.freq_slew = freq_slew;
	call_with_stack(fn_to_pie
			(rockchip_pie_chunk, &FUNC(ddr_change_freq_sram)),
			&param,
			rockchip_sram_stack - (NR_CPUS -
					       1) * PAUSE_CPU_STACK_SIZE);
    /** 5. Issues a Mode Exit command   */
	ddr_dtt_check();
#if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
end:
#endif
	local_fiq_enable();
	local_irq_restore(flags);
/*    clk_set_rate(clk_get(NULL, "ddr_pll"), 0);    */
out:
	return ret;
}

bool DEFINE_PIE_DATA(cpu_pause[NR_CPUS]);
volatile bool *DATA(p_cpu_pause);
static inline bool is_cpu0_paused(unsigned int cpu)
{
	smp_rmb();
	return DATA(cpu_pause)[0];
}

static inline void set_cpuX_paused(unsigned int cpu, bool pause)
{
	DATA(cpu_pause)[cpu] = pause;
	smp_wmb();
}

static inline bool is_cpuX_paused(unsigned int cpu)
{
	smp_rmb();
	return DATA(p_cpu_pause)[cpu];
}

static inline void set_cpu0_paused(bool pause)
{
	DATA(p_cpu_pause)[0] = pause;
	smp_wmb();
}

#define MAX_TIMEOUT (16000000UL << 6)	/*>0.64s*/

/* Do not use stack, safe on SMP */
void PIE_FUNC(_pause_cpu)(void *arg)
{
	unsigned int cpu = (unsigned int)arg;

	set_cpuX_paused(cpu, true);
	while (is_cpu0_paused(cpu))
    ;
	set_cpuX_paused(cpu, false);
}

static void pause_cpu(void *info)
{
	unsigned int cpu = raw_smp_processor_id();

	call_with_stack(fn_to_pie(rockchip_pie_chunk, &FUNC(_pause_cpu)),
			(void *)cpu,
			rockchip_sram_stack - (cpu - 1) * PAUSE_CPU_STACK_SIZE);
}

static void wait_cpu(void *info)
{
}

static int call_with_single_cpu(u32(*fn) (void *arg), void *arg)
{
	u32 timeout = MAX_TIMEOUT;
	unsigned int cpu;
	unsigned int this_cpu = smp_processor_id();	/*获取当前cpu*/
	int ret = 0;
	cpu_maps_update_begin();
	local_bh_disable();	/*disable swi*/
	set_cpu0_paused(true);
	smp_call_function((smp_call_func_t) pause_cpu, NULL, 0);

	for_each_online_cpu(cpu) {
		if (cpu == this_cpu)
			continue;
		while (!is_cpuX_paused(cpu) && --timeout)
		    ;
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

static int __ddr_change_freq(uint32_t nMHz, struct ddr_freq_t ddr_freq_t)
{
	freq_t freq;
	int ret = 0;

	freq.nMHz = nMHz;
	freq.p_ddr_freq_t = &ddr_freq_t;
	ret = call_with_single_cpu(&ddr_change_freq_sram, (void *)&freq);
	/*ret = ddr_change_freq_sram((void*)&freq);*/
	return ret;
}

static int _ddr_change_freq(uint32 nMHz)
{
	struct ddr_freq_t ddr_freq_t;
#if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
	unsigned long remain_t, vblank_t, pass_t;
	static unsigned long reserve_t = 800;	/*us*/
	unsigned long long tmp;
	int test_count = 0;
#endif
	int ret;
	/*ddr_print("ddr change freq to:%d\n",nMHz);*/
	memset(&ddr_freq_t, 0x00, sizeof(ddr_freq_t));

#if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
	do {
		ddr_freq_t.screen_ft_us = rk_fb_get_prmry_screen_ft();
		ddr_freq_t.t0 = rk_fb_get_prmry_screen_framedone_t();
		if (!ddr_freq_t.screen_ft_us)
			return __ddr_change_freq(nMHz, ddr_freq_t);

		tmp = cpu_clock(0) - ddr_freq_t.t0;
		do_div(tmp, 1000);
		pass_t = tmp;
		/*lost frame interrupt*/
		while (pass_t > ddr_freq_t.screen_ft_us) {
			int n = pass_t / ddr_freq_t.screen_ft_us;

			/*printk("lost frame int, pass_t:%lu\n", pass_t);*/
			pass_t -= n * ddr_freq_t.screen_ft_us;
			ddr_freq_t.t0 += n * ddr_freq_t.screen_ft_us * 1000;
		}

		remain_t = ddr_freq_t.screen_ft_us - pass_t;
		if (remain_t < reserve_t) {
			/*printk("remain_t(%lu) < reserve_t(%lu)\n", remain_t, reserve_t);*/
			vblank_t = rk_fb_get_prmry_screen_vbt();
			usleep_range(remain_t + vblank_t, remain_t + vblank_t);
			continue;
		}
		/*test 10 times*/
		test_count++;
		if (test_count > 10) {
			ddr_freq_t.screen_ft_us = 0xfefefefe;
		}
		/*printk("ft:%lu, pass_t:%lu, remaint_t:%lu, reservet_t:%lu\n",
		 *     ddr_freq_t.screen_ft_us, (unsigned long)pass_t, remain_t, reserve_t);*/
		usleep_range(remain_t - reserve_t, remain_t - reserve_t);
		flush_tlb_all();

		ret = __ddr_change_freq(nMHz, ddr_freq_t);
		if (ret) {
			reserve_t = 800;
			return ret;
		} else {
			if (reserve_t < 3000)
				reserve_t += 200;
		}
	} while (1);
#else
	ret = __ddr_change_freq(nMHz, ddr_freq_t);
#endif

	return ret;
}

EXPORT_SYMBOL(_ddr_change_freq);

/*----------------------------------------------------------------------
*Name    : void ddr_set_auto_self_refresh(bool en)
*Desc    : 设置进入 selfrefesh 的周期数
*Params  : en -> 使能auto selfrefresh
*Return  : 频率值
*Notes   : 周期数为1*32 cycle
*----------------------------------------------------------------------*/
void _ddr_set_auto_self_refresh(bool en)
{
	/*set auto self-refresh idle    */
	*kern_to_pie(rockchip_pie_chunk, &DATA(ddr_sr_idle)) = en ? SR_IDLE : 0;
}

EXPORT_SYMBOL(_ddr_set_auto_self_refresh);

/*----------------------------------------------------------------------
*Name    : void __sramfunc ddr_suspend(void)
*Desc    : 进入ddr suspend
*Params  : void
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/
#if 1
void PIE_FUNC(ddr_suspend)(void)
{
	ddr_selfrefresh_enter(0);
	pCRU_Reg->CRU_MODE_CON = (0x1 << ((1 * 4) + 16)) | (0x0 << (1 * 4));	/*PLL slow-mode*/
	dsb();
	ddr_delayus(1);
	pCRU_Reg->CRU_PLL_CON[1][1] = ((0x1 << 13) << 16) | (0x1 << 13);	/*PLL power-down*/
	dsb();
	ddr_delayus(1);

}

EXPORT_PIE_SYMBOL(FUNC(ddr_suspend));

void ddr_suspend(void)
{
	uint32 i;
	volatile uint32 n;
	volatile unsigned int *temp = (volatile unsigned int *)SRAM_CODE_OFFSET;
    /** 1. Make sure there is no host access */
	flush_cache_all();
	outer_flush_all();
    /*flush_tlb_all();*/

    /*sram size = 8KB*/
	for (i = 0; i < 2; i++) {
		n = temp[1024 * i];
		barrier();
	}
	n = pDDR_Reg->SCFG.d32;
	n = pPHY_Reg->PHY_REG1;
	n = pCRU_Reg->CRU_PLL_CON[0][0];
	n = *(volatile uint32_t *)SysSrv_DdrTiming;
	n = pGRF_Reg->GRF_SOC_STATUS0;
	dsb();

	fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_suspend)) ();
}

EXPORT_SYMBOL(ddr_suspend);
#endif

#if 1
/*----------------------------------------------------------------------
*Name    : void __sramfunc ddr_resume(void)
*Desc    : ddr resume
*Params  : void
*Return  : void
*Notes   :
*----------------------------------------------------------------------*/
void PIE_FUNC(ddr_resume)(void)
{
	uint32 delay = 1000;

	pCRU_Reg->CRU_PLL_CON[1][1] = ((0x1 << 13) << 16) | (0x0 << 13);	/*PLL no power-down*/
	dsb();
	while (delay > 0) {
		ddr_delayus(1);
		if (pCRU_Reg->CRU_PLL_CON[1][1] & (0x1 << 10))
			break;
		delay--;
	}

	pCRU_Reg->CRU_MODE_CON = (0x1 << ((1 * 4) + 16)) | (0x1 << (1 * 4));	/*PLL normal*/
	dsb();

	ddr_selfrefresh_exit();
}

EXPORT_PIE_SYMBOL(FUNC(ddr_resume));

void ddr_resume(void)
{
	fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_resume)) ();
}

EXPORT_SYMBOL(ddr_resume);
#endif

/*----------------------------------------------------------------------
*Name    : uint32 ddr_get_cap(void)
*Desc    : 获取容量，返回字节数
*Params  : void
*Return  : 颗粒容量
*Notes   :
*----------------------------------------------------------------------*/
uint32 ddr_get_cap(void)
{
	uint32 cs, bank, row, col, row1, bw;

	bank = READ_BK_INFO();
	row = READ_CS0_ROW_INFO();
	col = READ_COL_INFO();
	cs = READ_CS_INFO();
	bw = READ_BW_INFO();
	if (cs > 1) {
		row1 = READ_CS1_ROW_INFO();
		return ((1 << (row + col + bank + bw)) +
			(1 << (row1 + col + bank + bw)));
	} else {
		return (1 << (row + col + bank + bw));
	}
}

EXPORT_SYMBOL(ddr_get_cap);

static long _ddr_round_rate(uint32 nMHz)
{
	return p_ddr_set_pll(nMHz, 0);
}

enum ddr_bandwidth_id{
	ddrbw_wr_num = 0,
	ddrbw_rd_num,
	ddrbw_act_num,
	ddrbw_time_num,
	ddrbw_id_end
};

static void ddr_dfi_monitor_strat(void)
{
	pGRF_Reg->GRF_SOC_CON[0] = DDR_MONITOR_EN;
}
static void ddr_dfi_monitor_stop(void)
{
	pGRF_Reg->GRF_SOC_CON[0] = DDR_MONITOR_DISB;
}

void _ddr_bandwidth_get(struct ddr_bw_info *ddr_bw_ch0, struct ddr_bw_info *ddr_bw_ch1)
{
	uint32 ddr_bw_val[ddrbw_id_end], ddr_freq;
	u64 temp64;
	uint32 i;
	uint32 ddr_bw;

	ddr_bw = READ_BW_INFO();
	ddr_dfi_monitor_stop();
	for (i = 0; i < ddrbw_id_end; i++) {
		ddr_bw_val[i] = *(uint32 *)(&(pGRF_Reg->GRF_DFI_WRNUM) + i);
	}
	if (!ddr_bw_val[ddrbw_time_num])
		goto end;

	ddr_freq = pDDR_Reg->TOGCNT1U;
	temp64 = ((u64)ddr_bw_val[ddrbw_wr_num] + (u64)ddr_bw_val[ddrbw_rd_num])*4*100;
	do_div(temp64, ddr_bw_val[ddrbw_time_num]);

	ddr_bw_ch0->ddr_percent = (uint32)temp64;
	ddr_bw_ch0->ddr_time = ddr_bw_val[ddrbw_time_num]/(ddr_freq*1000); /*ms*/
	ddr_bw_ch0->ddr_wr = (ddr_bw_val[ddrbw_wr_num]*8*ddr_bw*2)*ddr_freq/ddr_bw_val[ddrbw_time_num];/*Byte/us,MB/s*/
	ddr_bw_ch0->ddr_rd = (ddr_bw_val[ddrbw_rd_num]*8*ddr_bw*2)*ddr_freq/ddr_bw_val[ddrbw_time_num];
	ddr_bw_ch0->ddr_act = ddr_bw_val[ddrbw_act_num];
	ddr_bw_ch0->ddr_total = ddr_freq*2*ddr_bw*2;
end:
	ddr_dfi_monitor_strat();
}
EXPORT_SYMBOL(_ddr_bandwidth_get);

/*----------------------------------------------------------------------
*Name    : int ddr_init(uint32_t dram_speed_bin, uint32_t freq)
*Desc    : ddr  初始化函数
*Params  : dram_speed_bin ->ddr颗粒类型
*          freq ->频率值
*Return  : 0 成功
*Notes   :
*----------------------------------------------------------------------*/
int ddr_init(uint32_t dram_speed_bin, uint32 freq)
{
	uint32_t value = 0;
	uint32_t cs, die = 1;
	/*uint32_t calStatusLeft, calStatusRight*/
	struct clk *clk;

	ddr_print("version 1.02 20140828\n");
	cs = READ_CS_INFO();	/*case 1:1rank ; case 2:2rank*/

	p_ddr_reg = kern_to_pie(rockchip_pie_chunk, &DATA(ddr_reg));
	p_ddr_freq = kern_to_pie(rockchip_pie_chunk, &DATA(ddr_freq));
	p_ddr_set_pll = fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_set_pll));
	DATA(p_cpu_pause) =
	    kern_to_pie(rockchip_pie_chunk, &DATA(cpu_pause[0]));
	p_ddr_reg->mem_type = ((pGRF_Reg->GRF_OS_REG[1] >> 13) & 0x7);
	p_ddr_reg->ddr_speed_bin = dram_speed_bin;
	*p_ddr_freq = 0;
	*kern_to_pie(rockchip_pie_chunk, &DATA(ddr_sr_idle)) = 0;
	*kern_to_pie(rockchip_pie_chunk, &DATA(ddr_dll_status)) =
	    DDR3_DLL_DISABLE;
	p_copy_data = kern_to_pie(rockchip_pie_chunk, &copy_data[0]);

	switch (p_ddr_reg->mem_type) {
	case DDR3:
		ddr_print("DRAM Type:DDR3\n");
		break;
	case LPDDR2:
		ddr_print("DRAM Type:LPDDR2\n");
		break;
	default:
		ddr_print("ddr type error type=%d\n", (p_ddr_reg->mem_type));
	}
	die = 1 << (READ_BW_INFO() - READ_DIE_BW_INFO());
	p_ddr_reg->ddr_capability_per_die = ddr_get_cap() / (cs * die);
	ddr_print("%d CS, ROW=%d, Bank=%d, COL=%d, Total Capability=%dMB\n",
						cs, READ_CS0_ROW_INFO(),
						(0x1 << (READ_BK_INFO())),
						READ_COL_INFO(),
						(ddr_get_cap() >> 20));

	clk = clk_get(NULL, "clk_ddr");
	if (IS_ERR(clk)) {
		ddr_print("failed to get ddr clk\n");
		clk = NULL;
	}
	if (freq != 0)
		value = clk_set_rate(clk, 1000 * 1000 * freq);
	else
		value = clk_set_rate(clk, clk_get_rate(clk));
	ddr_print("init success!!! freq=%luMHz\n",
		  clk ? clk_get_rate(clk) / 1000000 : freq);

	return 0;
}

EXPORT_SYMBOL(ddr_init);
