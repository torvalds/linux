/*
 * Prototypes, etc. for the Freescale MPC52xx embedded cpu chips
 * May need to be cleaned as the port goes on ...
 *
 * Copyright (C) 2004-2005 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 MontaVista, Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_POWERPC_MPC52xx_H__
#define __ASM_POWERPC_MPC52xx_H__

#ifndef __ASSEMBLER__
#include <asm/types.h>
#include <asm/mpc5xxx.h>
#endif /* __ASSEMBLER__ */

#include <linux/suspend.h>

/* Variants of the 5200(B) */
#define MPC5200_SVR		0x80110010
#define MPC5200_SVR_MASK	0xfffffff0
#define MPC5200B_SVR		0x80110020
#define MPC5200B_SVR_MASK	0xfffffff0

/* ======================================================================== */
/* Structures mapping of some unit register set                             */
/* ======================================================================== */

#ifndef __ASSEMBLER__

/* Memory Mapping Control */
struct mpc52xx_mmap_ctl {
	u32 mbar;		/* MMAP_CTRL + 0x00 */

	u32 cs0_start;		/* MMAP_CTRL + 0x04 */
	u32 cs0_stop;		/* MMAP_CTRL + 0x08 */
	u32 cs1_start;		/* MMAP_CTRL + 0x0c */
	u32 cs1_stop;		/* MMAP_CTRL + 0x10 */
	u32 cs2_start;		/* MMAP_CTRL + 0x14 */
	u32 cs2_stop;		/* MMAP_CTRL + 0x18 */
	u32 cs3_start;		/* MMAP_CTRL + 0x1c */
	u32 cs3_stop;		/* MMAP_CTRL + 0x20 */
	u32 cs4_start;		/* MMAP_CTRL + 0x24 */
	u32 cs4_stop;		/* MMAP_CTRL + 0x28 */
	u32 cs5_start;		/* MMAP_CTRL + 0x2c */
	u32 cs5_stop;		/* MMAP_CTRL + 0x30 */

	u32 sdram0;		/* MMAP_CTRL + 0x34 */
	u32 sdram1;		/* MMAP_CTRL + 0X38 */

	u32 reserved[4];	/* MMAP_CTRL + 0x3c .. 0x48 */

	u32 boot_start;		/* MMAP_CTRL + 0x4c */
	u32 boot_stop;		/* MMAP_CTRL + 0x50 */

	u32 ipbi_ws_ctrl;	/* MMAP_CTRL + 0x54 */

	u32 cs6_start;		/* MMAP_CTRL + 0x58 */
	u32 cs6_stop;		/* MMAP_CTRL + 0x5c */
	u32 cs7_start;		/* MMAP_CTRL + 0x60 */
	u32 cs7_stop;		/* MMAP_CTRL + 0x64 */
};

/* SDRAM control */
struct mpc52xx_sdram {
	u32 mode;		/* SDRAM + 0x00 */
	u32 ctrl;		/* SDRAM + 0x04 */
	u32 config1;		/* SDRAM + 0x08 */
	u32 config2;		/* SDRAM + 0x0c */
};

/* SDMA */
struct mpc52xx_sdma {
	u32 taskBar;		/* SDMA + 0x00 */
	u32 currentPointer;	/* SDMA + 0x04 */
	u32 endPointer;		/* SDMA + 0x08 */
	u32 variablePointer;	/* SDMA + 0x0c */

	u8 IntVect1;		/* SDMA + 0x10 */
	u8 IntVect2;		/* SDMA + 0x11 */
	u16 PtdCntrl;		/* SDMA + 0x12 */

	u32 IntPend;		/* SDMA + 0x14 */
	u32 IntMask;		/* SDMA + 0x18 */

	u16 tcr[16];		/* SDMA + 0x1c .. 0x3a */

	u8 ipr[32];		/* SDMA + 0x3c .. 0x5b */

	u32 cReqSelect;		/* SDMA + 0x5c */
	u32 task_size0;		/* SDMA + 0x60 */
	u32 task_size1;		/* SDMA + 0x64 */
	u32 MDEDebug;		/* SDMA + 0x68 */
	u32 ADSDebug;		/* SDMA + 0x6c */
	u32 Value1;		/* SDMA + 0x70 */
	u32 Value2;		/* SDMA + 0x74 */
	u32 Control;		/* SDMA + 0x78 */
	u32 Status;		/* SDMA + 0x7c */
	u32 PTDDebug;		/* SDMA + 0x80 */
};

/* GPT */
struct mpc52xx_gpt {
	u32 mode;		/* GPTx + 0x00 */
	u32 count;		/* GPTx + 0x04 */
	u32 pwm;		/* GPTx + 0x08 */
	u32 status;		/* GPTx + 0X0c */
};

/* GPIO */
struct mpc52xx_gpio {
	u32 port_config;	/* GPIO + 0x00 */
	u32 simple_gpioe;	/* GPIO + 0x04 */
	u32 simple_ode;		/* GPIO + 0x08 */
	u32 simple_ddr;		/* GPIO + 0x0c */
	u32 simple_dvo;		/* GPIO + 0x10 */
	u32 simple_ival;	/* GPIO + 0x14 */
	u8 outo_gpioe;		/* GPIO + 0x18 */
	u8 reserved1[3];	/* GPIO + 0x19 */
	u8 outo_dvo;		/* GPIO + 0x1c */
	u8 reserved2[3];	/* GPIO + 0x1d */
	u8 sint_gpioe;		/* GPIO + 0x20 */
	u8 reserved3[3];	/* GPIO + 0x21 */
	u8 sint_ode;		/* GPIO + 0x24 */
	u8 reserved4[3];	/* GPIO + 0x25 */
	u8 sint_ddr;		/* GPIO + 0x28 */
	u8 reserved5[3];	/* GPIO + 0x29 */
	u8 sint_dvo;		/* GPIO + 0x2c */
	u8 reserved6[3];	/* GPIO + 0x2d */
	u8 sint_inten;		/* GPIO + 0x30 */
	u8 reserved7[3];	/* GPIO + 0x31 */
	u16 sint_itype;		/* GPIO + 0x34 */
	u16 reserved8;		/* GPIO + 0x36 */
	u8 gpio_control;	/* GPIO + 0x38 */
	u8 reserved9[3];	/* GPIO + 0x39 */
	u8 sint_istat;		/* GPIO + 0x3c */
	u8 sint_ival;		/* GPIO + 0x3d */
	u8 bus_errs;		/* GPIO + 0x3e */
	u8 reserved10;		/* GPIO + 0x3f */
};

#define MPC52xx_GPIO_PSC_CONFIG_UART_WITHOUT_CD	4
#define MPC52xx_GPIO_PSC_CONFIG_UART_WITH_CD	5
#define MPC52xx_GPIO_PCI_DIS			(1<<15)

/* GPIO with WakeUp*/
struct mpc52xx_gpio_wkup {
	u8 wkup_gpioe;		/* GPIO_WKUP + 0x00 */
	u8 reserved1[3];	/* GPIO_WKUP + 0x03 */
	u8 wkup_ode;		/* GPIO_WKUP + 0x04 */
	u8 reserved2[3];	/* GPIO_WKUP + 0x05 */
	u8 wkup_ddr;		/* GPIO_WKUP + 0x08 */
	u8 reserved3[3];	/* GPIO_WKUP + 0x09 */
	u8 wkup_dvo;		/* GPIO_WKUP + 0x0C */
	u8 reserved4[3];	/* GPIO_WKUP + 0x0D */
	u8 wkup_inten;		/* GPIO_WKUP + 0x10 */
	u8 reserved5[3];	/* GPIO_WKUP + 0x11 */
	u8 wkup_iinten;		/* GPIO_WKUP + 0x14 */
	u8 reserved6[3];	/* GPIO_WKUP + 0x15 */
	u16 wkup_itype;		/* GPIO_WKUP + 0x18 */
	u8 reserved7[2];	/* GPIO_WKUP + 0x1A */
	u8 wkup_maste;		/* GPIO_WKUP + 0x1C */
	u8 reserved8[3];	/* GPIO_WKUP + 0x1D */
	u8 wkup_ival;		/* GPIO_WKUP + 0x20 */
	u8 reserved9[3];	/* GPIO_WKUP + 0x21 */
	u8 wkup_istat;		/* GPIO_WKUP + 0x24 */
	u8 reserved10[3];	/* GPIO_WKUP + 0x25 */
};

/* XLB Bus control */
struct mpc52xx_xlb {
	u8 reserved[0x40];
	u32 config;		/* XLB + 0x40 */
	u32 version;		/* XLB + 0x44 */
	u32 status;		/* XLB + 0x48 */
	u32 int_enable;		/* XLB + 0x4c */
	u32 addr_capture;	/* XLB + 0x50 */
	u32 bus_sig_capture;	/* XLB + 0x54 */
	u32 addr_timeout;	/* XLB + 0x58 */
	u32 data_timeout;	/* XLB + 0x5c */
	u32 bus_act_timeout;	/* XLB + 0x60 */
	u32 master_pri_enable;	/* XLB + 0x64 */
	u32 master_priority;	/* XLB + 0x68 */
	u32 base_address;	/* XLB + 0x6c */
	u32 snoop_window;	/* XLB + 0x70 */
};

#define MPC52xx_XLB_CFG_PLDIS		(1 << 31)
#define MPC52xx_XLB_CFG_SNOOP		(1 << 15)

/* Clock Distribution control */
struct mpc52xx_cdm {
	u32 jtag_id;		/* CDM + 0x00  reg0 read only */
	u32 rstcfg;		/* CDM + 0x04  reg1 read only */
	u32 breadcrumb;		/* CDM + 0x08  reg2 */

	u8 mem_clk_sel;		/* CDM + 0x0c  reg3 byte0 */
	u8 xlb_clk_sel;		/* CDM + 0x0d  reg3 byte1 read only */
	u8 ipb_clk_sel;		/* CDM + 0x0e  reg3 byte2 */
	u8 pci_clk_sel;		/* CDM + 0x0f  reg3 byte3 */

	u8 ext_48mhz_en;	/* CDM + 0x10  reg4 byte0 */
	u8 fd_enable;		/* CDM + 0x11  reg4 byte1 */
	u16 fd_counters;	/* CDM + 0x12  reg4 byte2,3 */

	u32 clk_enables;	/* CDM + 0x14  reg5 */

	u8 osc_disable;		/* CDM + 0x18  reg6 byte0 */
	u8 reserved0[3];	/* CDM + 0x19  reg6 byte1,2,3 */

	u8 ccs_sleep_enable;	/* CDM + 0x1c  reg7 byte0 */
	u8 osc_sleep_enable;	/* CDM + 0x1d  reg7 byte1 */
	u8 reserved1;		/* CDM + 0x1e  reg7 byte2 */
	u8 ccs_qreq_test;	/* CDM + 0x1f  reg7 byte3 */

	u8 soft_reset;		/* CDM + 0x20  u8 byte0 */
	u8 no_ckstp;		/* CDM + 0x21  u8 byte0 */
	u8 reserved2[2];	/* CDM + 0x22  u8 byte1,2,3 */

	u8 pll_lock;		/* CDM + 0x24  reg9 byte0 */
	u8 pll_looselock;	/* CDM + 0x25  reg9 byte1 */
	u8 pll_sm_lockwin;	/* CDM + 0x26  reg9 byte2 */
	u8 reserved3;		/* CDM + 0x27  reg9 byte3 */

	u16 reserved4;		/* CDM + 0x28  reg10 byte0,1 */
	u16 mclken_div_psc1;	/* CDM + 0x2a  reg10 byte2,3 */

	u16 reserved5;		/* CDM + 0x2c  reg11 byte0,1 */
	u16 mclken_div_psc2;	/* CDM + 0x2e  reg11 byte2,3 */

	u16 reserved6;		/* CDM + 0x30  reg12 byte0,1 */
	u16 mclken_div_psc3;	/* CDM + 0x32  reg12 byte2,3 */

	u16 reserved7;		/* CDM + 0x34  reg13 byte0,1 */
	u16 mclken_div_psc6;	/* CDM + 0x36  reg13 byte2,3 */
};

/* Interrupt controller Register set */
struct mpc52xx_intr {
	u32 per_mask;		/* INTR + 0x00 */
	u32 per_pri1;		/* INTR + 0x04 */
	u32 per_pri2;		/* INTR + 0x08 */
	u32 per_pri3;		/* INTR + 0x0c */
	u32 ctrl;		/* INTR + 0x10 */
	u32 main_mask;		/* INTR + 0x14 */
	u32 main_pri1;		/* INTR + 0x18 */
	u32 main_pri2;		/* INTR + 0x1c */
	u32 reserved1;		/* INTR + 0x20 */
	u32 enc_status;		/* INTR + 0x24 */
	u32 crit_status;	/* INTR + 0x28 */
	u32 main_status;	/* INTR + 0x2c */
	u32 per_status;		/* INTR + 0x30 */
	u32 reserved2;		/* INTR + 0x34 */
	u32 per_error;		/* INTR + 0x38 */
};

#endif /* __ASSEMBLER__ */


/* ========================================================================= */
/* Prototypes for MPC52xx sysdev                                             */
/* ========================================================================= */

#ifndef __ASSEMBLER__

struct device_node;

/* mpc52xx_common.c */
extern void mpc5200_setup_xlb_arbiter(void);
extern void mpc52xx_declare_of_platform_devices(void);
extern int mpc5200_psc_ac97_gpio_reset(int psc_number);
extern void mpc52xx_map_common_devices(void);
extern int mpc52xx_set_psc_clkdiv(int psc_id, int clkdiv);
extern void __noreturn mpc52xx_restart(char *cmd);

/* mpc52xx_gpt.c */
struct mpc52xx_gpt_priv;
extern struct mpc52xx_gpt_priv *mpc52xx_gpt_from_irq(int irq);
extern int mpc52xx_gpt_start_timer(struct mpc52xx_gpt_priv *gpt, u64 period,
                            int continuous);
extern u64 mpc52xx_gpt_timer_period(struct mpc52xx_gpt_priv *gpt);
extern int mpc52xx_gpt_stop_timer(struct mpc52xx_gpt_priv *gpt);

/* mpc52xx_pic.c */
extern void mpc52xx_init_irq(void);
extern unsigned int mpc52xx_get_irq(void);

/* mpc52xx_pci.c */
#ifdef CONFIG_PCI
extern int __init mpc52xx_add_bridge(struct device_node *node);
extern void __init mpc52xx_setup_pci(void);
#else
static inline void mpc52xx_setup_pci(void) { }
#endif

#endif /* __ASSEMBLER__ */

#ifdef CONFIG_PM
struct mpc52xx_suspend {
	void (*board_suspend_prepare)(void __iomem *mbar);
	void (*board_resume_finish)(void __iomem *mbar);
};

extern struct mpc52xx_suspend mpc52xx_suspend;
extern int __init mpc52xx_pm_init(void);
extern int mpc52xx_set_wakeup_gpio(u8 pin, u8 level);

/* lite5200 calls mpc5200 suspend functions, so here they are */
extern int mpc52xx_pm_prepare(void);
extern int mpc52xx_pm_enter(suspend_state_t);
extern void mpc52xx_pm_finish(void);
extern char saved_sram[0x4000]; /* reuse buffer from mpc52xx suspend */

#ifdef CONFIG_PPC_LITE5200
int __init lite5200_pm_init(void);
#endif
#endif /* CONFIG_PM */

#endif /* __ASM_POWERPC_MPC52xx_H__ */

