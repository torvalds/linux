/*
 * SiliconBackplane Chipcommon core hardware definitions.
 *
 * The chipcommon core provides chip identification, SB control,
 * JTAG, 0/1/2 UARTs, clock frequency control, a watchdog interrupt timer,
 * GPIO interface, extbus, and support for serial and parallel flashes.
 *
 * $Id: sbchipc.h 474281 2014-04-30 18:24:55Z $
 *
 * $Copyright Open Broadcom Corporation$
 */

#ifndef	_SBCHIPC_H
#define	_SBCHIPC_H

#if !defined(_LANGUAGE_ASSEMBLY) && !defined(__ASSEMBLY__)

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif	/* PAD */

/**
 * In chipcommon rev 49 the pmu registers have been moved from chipc to the pmu core if the
 * 'AOBPresent' bit of 'CoreCapabilitiesExt' is set. If this field is set, the traditional chipc to
 * [pmu|gci|sreng] register interface is deprecated and removed. These register blocks would instead
 * be assigned their respective chipc-specific address space and connected to the Always On
 * Backplane via the APB interface.
 */
typedef volatile struct {
	uint32  PAD[384];
	uint32	pmucontrol;		/* 0x600 */
	uint32	pmucapabilities;
	uint32	pmustatus;
	uint32	res_state;
	uint32	res_pending;
	uint32	pmutimer;
	uint32	min_res_mask;
	uint32	max_res_mask;
	uint32	res_table_sel;
	uint32	res_dep_mask;
	uint32	res_updn_timer;
	uint32	res_timer;
	uint32	clkstretch;
	uint32	pmuwatchdog;
	uint32	gpiosel;		/* 0x638, rev >= 1 */
	uint32	gpioenable;		/* 0x63c, rev >= 1 */
	uint32	res_req_timer_sel;
	uint32	res_req_timer;
	uint32	res_req_mask;
	uint32	PAD;
	uint32	chipcontrol_addr;	/* 0x650 */
	uint32	chipcontrol_data;	/* 0x654 */
	uint32	regcontrol_addr;
	uint32	regcontrol_data;
	uint32	pllcontrol_addr;
	uint32	pllcontrol_data;
	uint32	pmustrapopt;		/* 0x668, corerev >= 28 */
	uint32	pmu_xtalfreq;		/* 0x66C, pmurev >= 10 */
	uint32  retention_ctl;		/* 0x670 */
	uint32  PAD[3];
	uint32  retention_grpidx;	/* 0x680 */
	uint32  retention_grpctl;	/* 0x684 */
	uint32  PAD[20];
	uint32	pmucontrol_ext;		/* 0x6d8 */
	uint32	slowclkperiod;		/* 0x6dc */
	uint32	PAD[8];
	uint32	pmuintmask0;		/* 0x700 */
	uint32	pmuintmask1;		/* 0x704 */
	uint32  PAD[14];
	uint32  pmuintstatus;		/* 0x740 */
} pmuregs_t;

typedef struct eci_prerev35 {
	uint32	eci_output;
	uint32	eci_control;
	uint32	eci_inputlo;
	uint32	eci_inputmi;
	uint32	eci_inputhi;
	uint32	eci_inputintpolaritylo;
	uint32	eci_inputintpolaritymi;
	uint32	eci_inputintpolarityhi;
	uint32	eci_intmasklo;
	uint32	eci_intmaskmi;
	uint32	eci_intmaskhi;
	uint32	eci_eventlo;
	uint32	eci_eventmi;
	uint32	eci_eventhi;
	uint32	eci_eventmasklo;
	uint32	eci_eventmaskmi;
	uint32	eci_eventmaskhi;
	uint32	PAD[3];
} eci_prerev35_t;

typedef struct eci_rev35 {
	uint32	eci_outputlo;
	uint32	eci_outputhi;
	uint32	eci_controllo;
	uint32	eci_controlhi;
	uint32	eci_inputlo;
	uint32	eci_inputhi;
	uint32	eci_inputintpolaritylo;
	uint32	eci_inputintpolarityhi;
	uint32	eci_intmasklo;
	uint32	eci_intmaskhi;
	uint32	eci_eventlo;
	uint32	eci_eventhi;
	uint32	eci_eventmasklo;
	uint32	eci_eventmaskhi;
	uint32	eci_auxtx;
	uint32	eci_auxrx;
	uint32	eci_datatag;
	uint32	eci_uartescvalue;
	uint32	eci_autobaudctr;
	uint32	eci_uartfifolevel;
} eci_rev35_t;

typedef struct flash_config {
	uint32	PAD[19];
	/* Flash struct configuration registers (0x18c) for BCM4706 (corerev = 31) */
	uint32 flashstrconfig;
} flash_config_t;

typedef volatile struct {
	uint32	chipid;			/* 0x0 */
	uint32	capabilities;
	uint32	corecontrol;		/* corerev >= 1 */
	uint32	bist;

	/* OTP */
	uint32	otpstatus;		/* 0x10, corerev >= 10 */
	uint32	otpcontrol;
	uint32	otpprog;
	uint32	otplayout;		/* corerev >= 23 */

	/* Interrupt control */
	uint32	intstatus;		/* 0x20 */
	uint32	intmask;

	/* Chip specific regs */
	uint32	chipcontrol;		/* 0x28, rev >= 11 */
	uint32	chipstatus;		/* 0x2c, rev >= 11 */

	/* Jtag Master */
	uint32	jtagcmd;		/* 0x30, rev >= 10 */
	uint32	jtagir;
	uint32	jtagdr;
	uint32	jtagctrl;

	/* serial flash interface registers */
	uint32	flashcontrol;		/* 0x40 */
	uint32	flashaddress;
	uint32	flashdata;
	uint32	otplayoutextension;	/* rev >= 35 */

	/* Silicon backplane configuration broadcast control */
	uint32	broadcastaddress;	/* 0x50 */
	uint32	broadcastdata;

	/* gpio - cleared only by power-on-reset */
	uint32	gpiopullup;		/* 0x58, corerev >= 20 */
	uint32	gpiopulldown;		/* 0x5c, corerev >= 20 */
	uint32	gpioin;			/* 0x60 */
	uint32	gpioout;		/* 0x64 */
	uint32	gpioouten;		/* 0x68 */
	uint32	gpiocontrol;		/* 0x6C */
	uint32	gpiointpolarity;	/* 0x70 */
	uint32	gpiointmask;		/* 0x74 */

	/* GPIO events corerev >= 11 */
	uint32	gpioevent;
	uint32	gpioeventintmask;

	/* Watchdog timer */
	uint32	watchdog;		/* 0x80 */

	/* GPIO events corerev >= 11 */
	uint32	gpioeventintpolarity;

	/* GPIO based LED powersave registers corerev >= 16 */
	uint32  gpiotimerval;		/* 0x88 */
	uint32  gpiotimeroutmask;

	/* clock control */
	uint32	clockcontrol_n;		/* 0x90 */
	uint32	clockcontrol_sb;	/* aka m0 */
	uint32	clockcontrol_pci;	/* aka m1 */
	uint32	clockcontrol_m2;	/* mii/uart/mipsref */
	uint32	clockcontrol_m3;	/* cpu */
	uint32	clkdiv;			/* corerev >= 3 */
	uint32	gpiodebugsel;		/* corerev >= 28 */
	uint32	capabilities_ext;               	/* 0xac  */

	/* pll delay registers (corerev >= 4) */
	uint32	pll_on_delay;		/* 0xb0 */
	uint32	fref_sel_delay;
	uint32	slow_clk_ctl;		/* 5 < corerev < 10 */
	uint32	PAD;

	/* Instaclock registers (corerev >= 10) */
	uint32	system_clk_ctl;		/* 0xc0 */
	uint32	clkstatestretch;
	uint32	PAD[2];

	/* Indirect backplane access (corerev >= 22) */
	uint32	bp_addrlow;		/* 0xd0 */
	uint32	bp_addrhigh;
	uint32	bp_data;
	uint32	PAD;
	uint32	bp_indaccess;
	/* SPI registers, corerev >= 37 */
	uint32	gsioctrl;
	uint32	gsioaddress;
	uint32	gsiodata;

	/* More clock dividers (corerev >= 32) */
	uint32	clkdiv2;
	/* FAB ID (corerev >= 40) */
	uint32	otpcontrol1;
	uint32	fabid;			/* 0xf8 */

	/* In AI chips, pointer to erom */
	uint32	eromptr;		/* 0xfc */

	/* ExtBus control registers (corerev >= 3) */
	uint32	pcmcia_config;		/* 0x100 */
	uint32	pcmcia_memwait;
	uint32	pcmcia_attrwait;
	uint32	pcmcia_iowait;
	uint32	ide_config;
	uint32	ide_memwait;
	uint32	ide_attrwait;
	uint32	ide_iowait;
	uint32	prog_config;
	uint32	prog_waitcount;
	uint32	flash_config;
	uint32	flash_waitcount;
	uint32  SECI_config;		/* 0x130 SECI configuration */
	uint32	SECI_status;
	uint32	SECI_statusmask;
	uint32	SECI_rxnibchanged;

	uint32	PAD[20];

	/* SROM interface (corerev >= 32) */
	uint32	sromcontrol;		/* 0x190 */
	uint32	sromaddress;
	uint32	sromdata;
	uint32	PAD[1];				/* 0x19C */
	/* NAND flash registers for BCM4706 (corerev = 31) */
	uint32  nflashctrl;         /* 0x1a0 */
	uint32  nflashconf;
	uint32  nflashcoladdr;
	uint32  nflashrowaddr;
	uint32  nflashdata;
	uint32  nflashwaitcnt0;		/* 0x1b4 */
	uint32  PAD[2];

	uint32  seci_uart_data;		/* 0x1C0 */
	uint32  seci_uart_bauddiv;
	uint32  seci_uart_fcr;
	uint32  seci_uart_lcr;
	uint32  seci_uart_mcr;
	uint32  seci_uart_lsr;
	uint32  seci_uart_msr;
	uint32  seci_uart_baudadj;
	/* Clock control and hardware workarounds (corerev >= 20) */
	uint32	clk_ctl_st;		/* 0x1e0 */
	uint32	hw_war;
	uint32	PAD[70];

	/* UARTs */
	uint8	uart0data;		/* 0x300 */
	uint8	uart0imr;
	uint8	uart0fcr;
	uint8	uart0lcr;
	uint8	uart0mcr;
	uint8	uart0lsr;
	uint8	uart0msr;
	uint8	uart0scratch;
	uint8	PAD[248];		/* corerev >= 1 */

	uint8	uart1data;		/* 0x400 */
	uint8	uart1imr;
	uint8	uart1fcr;
	uint8	uart1lcr;
	uint8	uart1mcr;
	uint8	uart1lsr;
	uint8	uart1msr;
	uint8	uart1scratch;		/* 0x407 */
	uint32	PAD[62];

	/* save/restore, corerev >= 48 */
	uint32	sr_capability;		/* 0x500 */
	uint32	sr_control0;		/* 0x504 */
	uint32	sr_control1;		/* 0x508 */
	uint32  gpio_control;		/* 0x50C */
	uint32	PAD[60];

	/* PMU registers (corerev >= 20) */
	/* Note: all timers driven by ILP clock are updated asynchronously to HT/ALP.
	 * The CPU must read them twice, compare, and retry if different.
	 */
	uint32	pmucontrol;		/* 0x600 */
	uint32	pmucapabilities;
	uint32	pmustatus;
	uint32	res_state;
	uint32	res_pending;
	uint32	pmutimer;
	uint32	min_res_mask;
	uint32	max_res_mask;
	uint32	res_table_sel;
	uint32	res_dep_mask;
	uint32	res_updn_timer;
	uint32	res_timer;
	uint32	clkstretch;
	uint32	pmuwatchdog;
	uint32	gpiosel;		/* 0x638, rev >= 1 */
	uint32	gpioenable;		/* 0x63c, rev >= 1 */
	uint32	res_req_timer_sel;
	uint32	res_req_timer;
	uint32	res_req_mask;
	uint32	PAD;
	uint32	chipcontrol_addr;	/* 0x650 */
	uint32	chipcontrol_data;	/* 0x654 */
	uint32	regcontrol_addr;
	uint32	regcontrol_data;
	uint32	pllcontrol_addr;
	uint32	pllcontrol_data;
	uint32	pmustrapopt;		/* 0x668, corerev >= 28 */
	uint32	pmu_xtalfreq;		/* 0x66C, pmurev >= 10 */
	uint32  retention_ctl;		/* 0x670 */
	uint32  PAD[3];
	uint32  retention_grpidx;	/* 0x680 */
	uint32  retention_grpctl;	/* 0x684 */
	uint32  PAD[20];
	uint32	pmucontrol_ext;		/* 0x6d8 */
	uint32	slowclkperiod;		/* 0x6dc */
	uint32	PAD[8];
	uint32	pmuintmask0;		/* 0x700 */
	uint32	pmuintmask1;		/* 0x704 */
	uint32  PAD[14];
	uint32  pmuintstatus;		/* 0x740 */
	uint32	PAD[47];
	uint16	sromotp[512];		/* 0x800 */
#ifdef NFLASH_SUPPORT
	/* Nand flash MLC controller registers (corerev >= 38) */
	uint32	nand_revision;		/* 0xC00 */
	uint32	nand_cmd_start;
	uint32	nand_cmd_addr_x;
	uint32	nand_cmd_addr;
	uint32	nand_cmd_end_addr;
	uint32	nand_cs_nand_select;
	uint32	nand_cs_nand_xor;
	uint32	PAD;
	uint32	nand_spare_rd0;
	uint32	nand_spare_rd4;
	uint32	nand_spare_rd8;
	uint32	nand_spare_rd12;
	uint32	nand_spare_wr0;
	uint32	nand_spare_wr4;
	uint32	nand_spare_wr8;
	uint32	nand_spare_wr12;
	uint32	nand_acc_control;
	uint32	PAD;
	uint32	nand_config;
	uint32	PAD;
	uint32	nand_timing_1;
	uint32	nand_timing_2;
	uint32	nand_semaphore;
	uint32	PAD;
	uint32	nand_devid;
	uint32	nand_devid_x;
	uint32	nand_block_lock_status;
	uint32	nand_intfc_status;
	uint32	nand_ecc_corr_addr_x;
	uint32	nand_ecc_corr_addr;
	uint32	nand_ecc_unc_addr_x;
	uint32	nand_ecc_unc_addr;
	uint32	nand_read_error_count;
	uint32	nand_corr_stat_threshold;
	uint32	PAD[2];
	uint32	nand_read_addr_x;
	uint32	nand_read_addr;
	uint32	nand_page_program_addr_x;
	uint32	nand_page_program_addr;
	uint32	nand_copy_back_addr_x;
	uint32	nand_copy_back_addr;
	uint32	nand_block_erase_addr_x;
	uint32	nand_block_erase_addr;
	uint32	nand_inv_read_addr_x;
	uint32	nand_inv_read_addr;
	uint32	PAD[2];
	uint32	nand_blk_wr_protect;
	uint32	PAD[3];
	uint32	nand_acc_control_cs1;
	uint32	nand_config_cs1;
	uint32	nand_timing_1_cs1;
	uint32	nand_timing_2_cs1;
	uint32	PAD[20];
	uint32	nand_spare_rd16;
	uint32	nand_spare_rd20;
	uint32	nand_spare_rd24;
	uint32	nand_spare_rd28;
	uint32	nand_cache_addr;
	uint32	nand_cache_data;
	uint32	nand_ctrl_config;
	uint32	nand_ctrl_status;
#endif /* NFLASH_SUPPORT */
	uint32  gci_corecaps0; /* GCI starting at 0xC00 */
	uint32  gci_corecaps1;
	uint32  gci_corecaps2;
	uint32  gci_corectrl;
	uint32  gci_corestat; /* 0xC10 */
	uint32  gci_intstat; /* 0xC14 */
	uint32  gci_intmask; /* 0xC18 */
	uint32  gci_wakemask; /* 0xC1C */
	uint32  gci_levelintstat; /* 0xC20 */
	uint32  gci_eventintstat; /* 0xC24 */
	uint32  PAD[6];
	uint32  gci_indirect_addr; /* 0xC40 */
	uint32  gci_gpioctl; /* 0xC44 */
	uint32	gci_gpiostatus;
	uint32  gci_gpiomask; /* 0xC4C */
	uint32  PAD;
	uint32  gci_miscctl; /* 0xC54 */
	uint32	gci_gpiointmask;
	uint32	gci_gpiowakemask;
	uint32  gci_input[32]; /* C60 */
	uint32  gci_event[32]; /* CE0 */
	uint32  gci_output[4]; /* D60 */
	uint32  gci_control_0; /* 0xD70 */
	uint32  gci_control_1; /* 0xD74 */
	uint32  gci_intpolreg; /* 0xD78 */
	uint32  gci_levelintmask; /* 0xD7C */
	uint32  gci_eventintmask; /* 0xD80 */
	uint32  PAD[3];
	uint32  gci_inbandlevelintmask; /* 0xD90 */
	uint32  gci_inbandeventintmask; /* 0xD94 */
	uint32  PAD[2];
	uint32  gci_seciauxtx; /* 0xDA0 */
	uint32  gci_seciauxrx; /* 0xDA4 */
	uint32  gci_secitx_datatag; /* 0xDA8 */
	uint32  gci_secirx_datatag; /* 0xDAC */
	uint32  gci_secitx_datamask; /* 0xDB0 */
	uint32  gci_seciusef0tx_reg; /* 0xDB4 */
	uint32  gci_secif0tx_offset; /* 0xDB8 */
	uint32  gci_secif0rx_offset; /* 0xDBC */
	uint32  gci_secif1tx_offset; /* 0xDC0 */
	uint32	gci_rxfifo_common_ctrl; /* 0xDC4 */
	uint32	gci_rxfifoctrl; /* 0xDC8 */
	uint32	gci_uartreadid; /* DCC */
	uint32  gci_uartescval; /* DD0 */
	uint32	PAD;
	uint32	gci_secififolevel; /* DD8 */
	uint32	gci_seciuartdata; /* DDC */
	uint32  gci_secibauddiv; /* DE0 */
	uint32  gci_secifcr; /* DE4 */
	uint32  gci_secilcr; /* DE8 */
	uint32  gci_secimcr; /* DEC */
	uint32	gci_secilsr; /* DF0 */
	uint32	gci_secimsr; /* DF4 */
	uint32  gci_baudadj; /* DF8 */
	uint32  PAD;
	uint32  gci_chipctrl; /* 0xE00 */
	uint32  gci_chipsts; /* 0xE04 */
	uint32	gci_gpioout; /* 0xE08 */
	uint32	gci_gpioout_read; /* 0xE0C */
	uint32	gci_mpwaketx; /* 0xE10 */
	uint32	gci_mpwakedetect; /* 0xE14 */
	uint32	gci_seciin_ctrl; /* 0xE18 */
	uint32	gci_seciout_ctrl; /* 0xE1C */
	uint32	gci_seciin_auxfifo_en; /* 0xE20 */
	uint32	gci_seciout_txen_txbr; /* 0xE24 */
	uint32	gci_seciin_rxbrstatus; /* 0xE28 */
	uint32	gci_seciin_rxerrstatus; /* 0xE2C */
	uint32	gci_seciin_fcstatus; /* 0xE30 */
	uint32	gci_seciout_txstatus; /* 0xE34 */
	uint32	gci_seciout_txbrstatus; /* 0xE38 */
} chipcregs_t;

#endif /* !_LANGUAGE_ASSEMBLY && !__ASSEMBLY__ */


#define	CC_CHIPID		0
#define	CC_CAPABILITIES		4
#define	CC_CHIPST		0x2c
#define	CC_EROMPTR		0xfc

#define	CC_OTPST		0x10
#define	CC_INTSTATUS		0x20
#define	CC_INTMASK		0x24
#define	CC_JTAGCMD		0x30
#define	CC_JTAGIR		0x34
#define	CC_JTAGDR		0x38
#define	CC_JTAGCTRL		0x3c
#define	CC_GPIOPU		0x58
#define	CC_GPIOPD		0x5c
#define	CC_GPIOIN		0x60
#define	CC_GPIOOUT		0x64
#define	CC_GPIOOUTEN		0x68
#define	CC_GPIOCTRL		0x6c
#define	CC_GPIOPOL		0x70
#define	CC_GPIOINTM		0x74
#define	CC_GPIOEVENT		0x78
#define	CC_GPIOEVENTMASK	0x7c
#define	CC_WATCHDOG		0x80
#define	CC_GPIOEVENTPOL		0x84
#define	CC_CLKC_N		0x90
#define	CC_CLKC_M0		0x94
#define	CC_CLKC_M1		0x98
#define	CC_CLKC_M2		0x9c
#define	CC_CLKC_M3		0xa0
#define	CC_CLKDIV		0xa4
#define	CC_SYS_CLK_CTL		0xc0
#define	CC_CLK_CTL_ST		SI_CLK_CTL_ST
#define	PMU_CTL			0x600
#define	PMU_CAP			0x604
#define	PMU_ST			0x608
#define PMU_RES_STATE		0x60c
#define PMU_RES_PENDING		0x610
#define PMU_TIMER		0x614
#define	PMU_MIN_RES_MASK	0x618
#define	PMU_MAX_RES_MASK	0x61c
#define CC_CHIPCTL_ADDR         0x650
#define CC_CHIPCTL_DATA         0x654
#define PMU_REG_CONTROL_ADDR	0x658
#define PMU_REG_CONTROL_DATA	0x65C
#define PMU_PLL_CONTROL_ADDR 	0x660
#define PMU_PLL_CONTROL_DATA 	0x664
#define CC_SROM_CTRL		0x190
#define	CC_SROM_OTP		0x800		/* SROM/OTP address space */
#define CC_GCI_INDIRECT_ADDR_REG	0xC40
#define CC_GCI_CHIP_CTRL_REG	0xE00
#define CC_GCI_CC_OFFSET_2	2
#define CC_GCI_CC_OFFSET_5	5
#define CC_SWD_CTRL		0x380
#define CC_SWD_REQACK		0x384
#define CC_SWD_DATA		0x388


#define CHIPCTRLREG0 0x0
#define CHIPCTRLREG1 0x1
#define CHIPCTRLREG2 0x2
#define CHIPCTRLREG3 0x3
#define CHIPCTRLREG4 0x4
#define CHIPCTRLREG5 0x5
#define CHIPCTRLREG6 0x6
#define REGCTRLREG4 0x4
#define REGCTRLREG5 0x5
#define REGCTRLREG6 0x6
#define MINRESMASKREG 0x618
#define MAXRESMASKREG 0x61c
#define CHIPCTRLADDR 0x650
#define CHIPCTRLDATA 0x654
#define RSRCTABLEADDR 0x620
#define PMU_RES_DEP_MASK 0x624
#define RSRCUPDWNTIME 0x628
#define PMUREG_RESREQ_MASK 0x68c
#define EXT_LPO_AVAIL 0x100
#define LPO_SEL					(1 << 0)
#define CC_EXT_LPO_PU 0x200000
#define GC_EXT_LPO_PU 0x2
#define CC_INT_LPO_PU 0x100000
#define GC_INT_LPO_PU 0x1
#define EXT_LPO_SEL 0x8
#define INT_LPO_SEL 0x4
#define ENABLE_FINE_CBUCK_CTRL 			(1 << 30)
#define REGCTRL5_PWM_AUTO_CTRL_MASK 		0x007e0000
#define REGCTRL5_PWM_AUTO_CTRL_SHIFT		17
#define REGCTRL6_PWM_AUTO_CTRL_MASK 		0x3fff0000
#define REGCTRL6_PWM_AUTO_CTRL_SHIFT		16

#ifdef SR_DEBUG
#define SUBCORE_POWER_ON 0x0001
#define PHY_POWER_ON 0x0010
#define VDDM_POWER_ON 0x0100
#define MEMLPLDO_POWER_ON 0x1000
#define SUBCORE_POWER_ON_CHK 0x00040000
#define PHY_POWER_ON_CHK 0x00080000
#define VDDM_POWER_ON_CHK 0x00100000
#define MEMLPLDO_POWER_ON_CHK 0x00200000
#endif /* SR_DEBUG */

#ifdef NFLASH_SUPPORT
/* NAND flash support */
#define CC_NAND_REVISION	0xC00
#define CC_NAND_CMD_START	0xC04
#define CC_NAND_CMD_ADDR	0xC0C
#define CC_NAND_SPARE_RD_0	0xC20
#define CC_NAND_SPARE_RD_4	0xC24
#define CC_NAND_SPARE_RD_8	0xC28
#define CC_NAND_SPARE_RD_C	0xC2C
#define CC_NAND_CONFIG		0xC48
#define CC_NAND_DEVID		0xC60
#define CC_NAND_DEVID_EXT	0xC64
#define CC_NAND_INTFC_STATUS	0xC6C
#endif /* NFLASH_SUPPORT */

/* chipid */
#define	CID_ID_MASK		0x0000ffff	/* Chip Id mask */
#define	CID_REV_MASK		0x000f0000	/* Chip Revision mask */
#define	CID_REV_SHIFT		16		/* Chip Revision shift */
#define	CID_PKG_MASK		0x00f00000	/* Package Option mask */
#define	CID_PKG_SHIFT		20		/* Package Option shift */
#define	CID_CC_MASK		0x0f000000	/* CoreCount (corerev >= 4) */
#define CID_CC_SHIFT		24
#define	CID_TYPE_MASK		0xf0000000	/* Chip Type */
#define CID_TYPE_SHIFT		28

/* capabilities */
#define	CC_CAP_UARTS_MASK	0x00000003	/* Number of UARTs */
#define CC_CAP_MIPSEB		0x00000004	/* MIPS is in big-endian mode */
#define CC_CAP_UCLKSEL		0x00000018	/* UARTs clock select */
#define CC_CAP_UINTCLK		0x00000008	/* UARTs are driven by internal divided clock */
#define CC_CAP_UARTGPIO		0x00000020	/* UARTs own GPIOs 15:12 */
#define CC_CAP_EXTBUS_MASK	0x000000c0	/* External bus mask */
#define CC_CAP_EXTBUS_NONE	0x00000000	/* No ExtBus present */
#define CC_CAP_EXTBUS_FULL	0x00000040	/* ExtBus: PCMCIA, IDE & Prog */
#define CC_CAP_EXTBUS_PROG	0x00000080	/* ExtBus: ProgIf only */
#define	CC_CAP_FLASH_MASK	0x00000700	/* Type of flash */
#define	CC_CAP_PLL_MASK		0x00038000	/* Type of PLL */
#define CC_CAP_PWR_CTL		0x00040000	/* Power control */
#define CC_CAP_OTPSIZE		0x00380000	/* OTP Size (0 = none) */
#define CC_CAP_OTPSIZE_SHIFT	19		/* OTP Size shift */
#define CC_CAP_OTPSIZE_BASE	5		/* OTP Size base */
#define CC_CAP_JTAGP		0x00400000	/* JTAG Master Present */
#define CC_CAP_ROM		0x00800000	/* Internal boot rom active */
#define CC_CAP_BKPLN64		0x08000000	/* 64-bit backplane */
#define	CC_CAP_PMU		0x10000000	/* PMU Present, rev >= 20 */
#define	CC_CAP_ECI		0x20000000	/* ECI Present, rev >= 21 */
#define	CC_CAP_SROM		0x40000000	/* Srom Present, rev >= 32 */
#define	CC_CAP_NFLASH		0x80000000	/* Nand flash present, rev >= 35 */

#define	CC_CAP2_SECI		0x00000001	/* SECI Present, rev >= 36 */
#define	CC_CAP2_GSIO		0x00000002	/* GSIO (spi/i2c) present, rev >= 37 */

/* capabilities extension */
#define CC_CAP_EXT_SECI_PRESENT	0x00000001    /* SECI present */
#define CC_CAP_EXT_GSIO_PRESENT	0x00000002    /* GSIO present */
#define CC_CAP_EXT_GCI_PRESENT  0x00000004    /* GCI present */
#define CC_CAP_EXT_AOB_PRESENT  0x00000040    /* AOB present */

/* WL Channel Info to BT via GCI - bits 40 - 47 */
#define GCI_WL_CHN_INFO_MASK 	(0xFF00)
/* PLL type */
#define PLL_NONE		0x00000000
#define PLL_TYPE1		0x00010000	/* 48MHz base, 3 dividers */
#define PLL_TYPE2		0x00020000	/* 48MHz, 4 dividers */
#define PLL_TYPE3		0x00030000	/* 25MHz, 2 dividers */
#define PLL_TYPE4		0x00008000	/* 48MHz, 4 dividers */
#define PLL_TYPE5		0x00018000	/* 25MHz, 4 dividers */
#define PLL_TYPE6		0x00028000	/* 100/200 or 120/240 only */
#define PLL_TYPE7		0x00038000	/* 25MHz, 4 dividers */

/* ILP clock */
#define	ILP_CLOCK		32000

/* ALP clock on pre-PMU chips */
#define	ALP_CLOCK		20000000

#ifdef CFG_SIM
#define NS_ALP_CLOCK		84922
#define NS_SLOW_ALP_CLOCK	84922
#define NS_CPU_CLOCK		534500
#define NS_SLOW_CPU_CLOCK	534500
#define NS_SI_CLOCK		271750
#define NS_SLOW_SI_CLOCK	271750
#define NS_FAST_MEM_CLOCK	271750
#define NS_MEM_CLOCK		271750
#define NS_SLOW_MEM_CLOCK	271750
#else
#define NS_ALP_CLOCK		125000000
#define NS_SLOW_ALP_CLOCK	100000000
#define NS_CPU_CLOCK		1000000000
#define NS_SLOW_CPU_CLOCK	800000000
#define NS_SI_CLOCK		250000000
#define NS_SLOW_SI_CLOCK	200000000
#define NS_FAST_MEM_CLOCK	800000000
#define NS_MEM_CLOCK		533000000
#define NS_SLOW_MEM_CLOCK	400000000
#endif /* CFG_SIM */

/* HT clock */
#define	HT_CLOCK		80000000

/* corecontrol */
#define CC_UARTCLKO		0x00000001	/* Drive UART with internal clock */
#define	CC_SE			0x00000002	/* sync clk out enable (corerev >= 3) */
#define CC_ASYNCGPIO	0x00000004	/* 1=generate GPIO interrupt without backplane clock */
#define CC_UARTCLKEN		0x00000008	/* enable UART Clock (corerev > = 21 */

/* 4321 chipcontrol */
#define CHIPCTRL_4321A0_DEFAULT	0x3a4
#define CHIPCTRL_4321A1_DEFAULT	0x0a4
#define CHIPCTRL_4321_PLL_DOWN	0x800000	/* serdes PLL down override */

/* Fields in the otpstatus register in rev >= 21 */
#define OTPS_OL_MASK		0x000000ff
#define OTPS_OL_MFG		0x00000001	/* manuf row is locked */
#define OTPS_OL_OR1		0x00000002	/* otp redundancy row 1 is locked */
#define OTPS_OL_OR2		0x00000004	/* otp redundancy row 2 is locked */
#define OTPS_OL_GU		0x00000008	/* general use region is locked */
#define OTPS_GUP_MASK		0x00000f00
#define OTPS_GUP_SHIFT		8
#define OTPS_GUP_HW		0x00000100	/* h/w subregion is programmed */
#define OTPS_GUP_SW		0x00000200	/* s/w subregion is programmed */
#define OTPS_GUP_CI		0x00000400	/* chipid/pkgopt subregion is programmed */
#define OTPS_GUP_FUSE		0x00000800	/* fuse subregion is programmed */
#define OTPS_READY		0x00001000
#define OTPS_RV(x)		(1 << (16 + (x)))	/* redundancy entry valid */
#define OTPS_RV_MASK		0x0fff0000
#define OTPS_PROGOK     0x40000000

/* Fields in the otpcontrol register in rev >= 21 */
#define OTPC_PROGSEL		0x00000001
#define OTPC_PCOUNT_MASK	0x0000000e
#define OTPC_PCOUNT_SHIFT	1
#define OTPC_VSEL_MASK		0x000000f0
#define OTPC_VSEL_SHIFT		4
#define OTPC_TMM_MASK		0x00000700
#define OTPC_TMM_SHIFT		8
#define OTPC_ODM		0x00000800
#define OTPC_PROGEN		0x80000000

/* Fields in the 40nm otpcontrol register in rev >= 40 */
#define OTPC_40NM_PROGSEL_SHIFT	0
#define OTPC_40NM_PCOUNT_SHIFT	1
#define OTPC_40NM_PCOUNT_WR	0xA
#define OTPC_40NM_PCOUNT_V1X	0xB
#define OTPC_40NM_REGCSEL_SHIFT	5
#define OTPC_40NM_REGCSEL_DEF	0x4
#define OTPC_40NM_PROGIN_SHIFT	8
#define OTPC_40NM_R2X_SHIFT	10
#define OTPC_40NM_ODM_SHIFT	11
#define OTPC_40NM_DF_SHIFT	15
#define OTPC_40NM_VSEL_SHIFT	16
#define OTPC_40NM_VSEL_WR	0xA
#define OTPC_40NM_VSEL_V1X	0xA
#define OTPC_40NM_VSEL_R1X	0x5
#define OTPC_40NM_COFAIL_SHIFT	30

#define OTPC1_CPCSEL_SHIFT	0
#define OTPC1_CPCSEL_DEF	6
#define OTPC1_TM_SHIFT		8
#define OTPC1_TM_WR		0x84
#define OTPC1_TM_V1X		0x84
#define OTPC1_TM_R1X		0x4
#define OTPC1_CLK_EN_MASK	0x00020000
#define OTPC1_CLK_DIV_MASK	0x00FC0000

/* Fields in otpprog in rev >= 21 and HND OTP */
#define OTPP_COL_MASK		0x000000ff
#define OTPP_COL_SHIFT		0
#define OTPP_ROW_MASK		0x0000ff00
#define OTPP_ROW_MASK9		0x0001ff00		/* for ccrev >= 49 */
#define OTPP_ROW_SHIFT		8
#define OTPP_OC_MASK		0x0f000000
#define OTPP_OC_SHIFT		24
#define OTPP_READERR		0x10000000
#define OTPP_VALUE_MASK		0x20000000
#define OTPP_VALUE_SHIFT	29
#define OTPP_START_BUSY		0x80000000
#define	OTPP_READ		0x40000000	/* HND OTP */

/* Fields in otplayout register */
#define OTPL_HWRGN_OFF_MASK	0x00000FFF
#define OTPL_HWRGN_OFF_SHIFT	0
#define OTPL_WRAP_REVID_MASK	0x00F80000
#define OTPL_WRAP_REVID_SHIFT	19
#define OTPL_WRAP_TYPE_MASK	0x00070000
#define OTPL_WRAP_TYPE_SHIFT	16
#define OTPL_WRAP_TYPE_65NM	0
#define OTPL_WRAP_TYPE_40NM	1
#define OTPL_ROW_SIZE_MASK	0x0000F000
#define OTPL_ROW_SIZE_SHIFT	12

/* otplayout reg corerev >= 36 */
#define OTP_CISFORMAT_NEW	0x80000000

/* Opcodes for OTPP_OC field */
#define OTPPOC_READ		0
#define OTPPOC_BIT_PROG		1
#define OTPPOC_VERIFY		3
#define OTPPOC_INIT		4
#define OTPPOC_SET		5
#define OTPPOC_RESET		6
#define OTPPOC_OCST		7
#define OTPPOC_ROW_LOCK		8
#define OTPPOC_PRESCN_TEST	9

/* Opcodes for OTPP_OC field (40NM) */
#define OTPPOC_READ_40NM	0
#define OTPPOC_PROG_ENABLE_40NM 1
#define OTPPOC_PROG_DISABLE_40NM	2
#define OTPPOC_VERIFY_40NM	3
#define OTPPOC_WORD_VERIFY_1_40NM	4
#define OTPPOC_ROW_LOCK_40NM	5
#define OTPPOC_STBY_40NM	6
#define OTPPOC_WAKEUP_40NM	7
#define OTPPOC_WORD_VERIFY_0_40NM	8
#define OTPPOC_PRESCN_TEST_40NM 9
#define OTPPOC_BIT_PROG_40NM	10
#define OTPPOC_WORDPROG_40NM	11
#define OTPPOC_BURNIN_40NM	12
#define OTPPOC_AUTORELOAD_40NM	13
#define OTPPOC_OVST_READ_40NM	14
#define OTPPOC_OVST_PROG_40NM	15

/* Fields in otplayoutextension */
#define OTPLAYOUTEXT_FUSE_MASK	0x3FF


/* Jtagm characteristics that appeared at a given corerev */
#define	JTAGM_CREV_OLD		10	/* Old command set, 16bit max IR */
#define	JTAGM_CREV_IRP		22	/* Able to do pause-ir */
#define	JTAGM_CREV_RTI		28	/* Able to do return-to-idle */

/* jtagcmd */
#define JCMD_START		0x80000000
#define JCMD_BUSY		0x80000000
#define JCMD_STATE_MASK		0x60000000
#define JCMD_STATE_TLR		0x00000000	/* Test-logic-reset */
#define JCMD_STATE_PIR		0x20000000	/* Pause IR */
#define JCMD_STATE_PDR		0x40000000	/* Pause DR */
#define JCMD_STATE_RTI		0x60000000	/* Run-test-idle */
#define JCMD0_ACC_MASK		0x0000f000
#define JCMD0_ACC_IRDR		0x00000000
#define JCMD0_ACC_DR		0x00001000
#define JCMD0_ACC_IR		0x00002000
#define JCMD0_ACC_RESET		0x00003000
#define JCMD0_ACC_IRPDR		0x00004000
#define JCMD0_ACC_PDR		0x00005000
#define JCMD0_IRW_MASK		0x00000f00
#define JCMD_ACC_MASK		0x000f0000	/* Changes for corerev 11 */
#define JCMD_ACC_IRDR		0x00000000
#define JCMD_ACC_DR		0x00010000
#define JCMD_ACC_IR		0x00020000
#define JCMD_ACC_RESET		0x00030000
#define JCMD_ACC_IRPDR		0x00040000
#define JCMD_ACC_PDR		0x00050000
#define JCMD_ACC_PIR		0x00060000
#define JCMD_ACC_IRDR_I		0x00070000	/* rev 28: return to run-test-idle */
#define JCMD_ACC_DR_I		0x00080000	/* rev 28: return to run-test-idle */
#define JCMD_IRW_MASK		0x00001f00
#define JCMD_IRW_SHIFT		8
#define JCMD_DRW_MASK		0x0000003f

/* jtagctrl */
#define JCTRL_FORCE_CLK		4		/* Force clock */
#define JCTRL_EXT_EN		2		/* Enable external targets */
#define JCTRL_EN		1		/* Enable Jtag master */

#define JCTRL_TAPSEL_BIT	0x00000008	/* JtagMasterCtrl tap_sel bit */

/* Fields in clkdiv */
#define	CLKD_SFLASH		0x0f000000
#define	CLKD_SFLASH_SHIFT	24
#define	CLKD_OTP		0x000f0000
#define	CLKD_OTP_SHIFT		16
#define	CLKD_JTAG		0x00000f00
#define	CLKD_JTAG_SHIFT		8
#define	CLKD_UART		0x000000ff

#define	CLKD2_SROM		0x00000003

/* intstatus/intmask */
#define	CI_GPIO			0x00000001	/* gpio intr */
#define	CI_EI			0x00000002	/* extif intr (corerev >= 3) */
#define	CI_TEMP			0x00000004	/* temp. ctrl intr (corerev >= 15) */
#define	CI_SIRQ			0x00000008	/* serial IRQ intr (corerev >= 15) */
#define	CI_ECI			0x00000010	/* eci intr (corerev >= 21) */
#define	CI_PMU			0x00000020	/* pmu intr (corerev >= 21) */
#define	CI_UART			0x00000040	/* uart intr (corerev >= 21) */
#define	CI_WDRESET		0x80000000	/* watchdog reset occurred */

/* slow_clk_ctl */
#define SCC_SS_MASK		0x00000007	/* slow clock source mask */
#define	SCC_SS_LPO		0x00000000	/* source of slow clock is LPO */
#define	SCC_SS_XTAL		0x00000001	/* source of slow clock is crystal */
#define	SCC_SS_PCI		0x00000002	/* source of slow clock is PCI */
#define SCC_LF			0x00000200	/* LPOFreqSel, 1: 160Khz, 0: 32KHz */
#define SCC_LP			0x00000400	/* LPOPowerDown, 1: LPO is disabled,
						 * 0: LPO is enabled
						 */
#define SCC_FS			0x00000800	/* ForceSlowClk, 1: sb/cores running on slow clock,
						 * 0: power logic control
						 */
#define SCC_IP			0x00001000	/* IgnorePllOffReq, 1/0: power logic ignores/honors
						 * PLL clock disable requests from core
						 */
#define SCC_XC			0x00002000	/* XtalControlEn, 1/0: power logic does/doesn't
						 * disable crystal when appropriate
						 */
#define SCC_XP			0x00004000	/* XtalPU (RO), 1/0: crystal running/disabled */
#define SCC_CD_MASK		0xffff0000	/* ClockDivider (SlowClk = 1/(4+divisor)) */
#define SCC_CD_SHIFT		16

/* system_clk_ctl */
#define	SYCC_IE			0x00000001	/* ILPen: Enable Idle Low Power */
#define	SYCC_AE			0x00000002	/* ALPen: Enable Active Low Power */
#define	SYCC_FP			0x00000004	/* ForcePLLOn */
#define	SYCC_AR			0x00000008	/* Force ALP (or HT if ALPen is not set */
#define	SYCC_HR			0x00000010	/* Force HT */
#define SYCC_CD_MASK		0xffff0000	/* ClkDiv  (ILP = 1/(4 * (divisor + 1)) */
#define SYCC_CD_SHIFT		16

/* Indirect backplane access */
#define	BPIA_BYTEEN		0x0000000f
#define	BPIA_SZ1		0x00000001
#define	BPIA_SZ2		0x00000003
#define	BPIA_SZ4		0x00000007
#define	BPIA_SZ8		0x0000000f
#define	BPIA_WRITE		0x00000100
#define	BPIA_START		0x00000200
#define	BPIA_BUSY		0x00000200
#define	BPIA_ERROR		0x00000400

/* pcmcia/prog/flash_config */
#define	CF_EN			0x00000001	/* enable */
#define	CF_EM_MASK		0x0000000e	/* mode */
#define	CF_EM_SHIFT		1
#define	CF_EM_FLASH		0		/* flash/asynchronous mode */
#define	CF_EM_SYNC		2		/* synchronous mode */
#define	CF_EM_PCMCIA		4		/* pcmcia mode */
#define	CF_DS			0x00000010	/* destsize:  0=8bit, 1=16bit */
#define	CF_BS			0x00000020	/* byteswap */
#define	CF_CD_MASK		0x000000c0	/* clock divider */
#define	CF_CD_SHIFT		6
#define	CF_CD_DIV2		0x00000000	/* backplane/2 */
#define	CF_CD_DIV3		0x00000040	/* backplane/3 */
#define	CF_CD_DIV4		0x00000080	/* backplane/4 */
#define	CF_CE			0x00000100	/* clock enable */
#define	CF_SB			0x00000200	/* size/bytestrobe (synch only) */

/* pcmcia_memwait */
#define	PM_W0_MASK		0x0000003f	/* waitcount0 */
#define	PM_W1_MASK		0x00001f00	/* waitcount1 */
#define	PM_W1_SHIFT		8
#define	PM_W2_MASK		0x001f0000	/* waitcount2 */
#define	PM_W2_SHIFT		16
#define	PM_W3_MASK		0x1f000000	/* waitcount3 */
#define	PM_W3_SHIFT		24

/* pcmcia_attrwait */
#define	PA_W0_MASK		0x0000003f	/* waitcount0 */
#define	PA_W1_MASK		0x00001f00	/* waitcount1 */
#define	PA_W1_SHIFT		8
#define	PA_W2_MASK		0x001f0000	/* waitcount2 */
#define	PA_W2_SHIFT		16
#define	PA_W3_MASK		0x1f000000	/* waitcount3 */
#define	PA_W3_SHIFT		24

/* pcmcia_iowait */
#define	PI_W0_MASK		0x0000003f	/* waitcount0 */
#define	PI_W1_MASK		0x00001f00	/* waitcount1 */
#define	PI_W1_SHIFT		8
#define	PI_W2_MASK		0x001f0000	/* waitcount2 */
#define	PI_W2_SHIFT		16
#define	PI_W3_MASK		0x1f000000	/* waitcount3 */
#define	PI_W3_SHIFT		24

/* prog_waitcount */
#define	PW_W0_MASK		0x0000001f	/* waitcount0 */
#define	PW_W1_MASK		0x00001f00	/* waitcount1 */
#define	PW_W1_SHIFT		8
#define	PW_W2_MASK		0x001f0000	/* waitcount2 */
#define	PW_W2_SHIFT		16
#define	PW_W3_MASK		0x1f000000	/* waitcount3 */
#define	PW_W3_SHIFT		24

#define PW_W0       		0x0000000c
#define PW_W1       		0x00000a00
#define PW_W2       		0x00020000
#define PW_W3       		0x01000000

/* flash_waitcount */
#define	FW_W0_MASK		0x0000003f	/* waitcount0 */
#define	FW_W1_MASK		0x00001f00	/* waitcount1 */
#define	FW_W1_SHIFT		8
#define	FW_W2_MASK		0x001f0000	/* waitcount2 */
#define	FW_W2_SHIFT		16
#define	FW_W3_MASK		0x1f000000	/* waitcount3 */
#define	FW_W3_SHIFT		24

/* When Srom support present, fields in sromcontrol */
#define	SRC_START		0x80000000
#define	SRC_BUSY		0x80000000
#define	SRC_OPCODE		0x60000000
#define	SRC_OP_READ		0x00000000
#define	SRC_OP_WRITE		0x20000000
#define	SRC_OP_WRDIS		0x40000000
#define	SRC_OP_WREN		0x60000000
#define	SRC_OTPSEL		0x00000010
#define SRC_OTPPRESENT		0x00000020
#define	SRC_LOCK		0x00000008
#define	SRC_SIZE_MASK		0x00000006
#define	SRC_SIZE_1K		0x00000000
#define	SRC_SIZE_4K		0x00000002
#define	SRC_SIZE_16K		0x00000004
#define	SRC_SIZE_SHIFT		1
#define	SRC_PRESENT		0x00000001

/* Fields in pmucontrol */
#define	PCTL_ILP_DIV_MASK	0xffff0000
#define	PCTL_ILP_DIV_SHIFT	16
#define PCTL_LQ_REQ_EN		0x00008000
#define PCTL_PLL_PLLCTL_UPD	0x00000400	/* rev 2 */
#define PCTL_NOILP_ON_WAIT	0x00000200	/* rev 1 */
#define	PCTL_HT_REQ_EN		0x00000100
#define	PCTL_ALP_REQ_EN		0x00000080
#define	PCTL_XTALFREQ_MASK	0x0000007c
#define	PCTL_XTALFREQ_SHIFT	2
#define	PCTL_ILP_DIV_EN		0x00000002
#define	PCTL_LPO_SEL		0x00000001

/*  Retention Control */
#define PMU_RCTL_CLK_DIV_SHIFT		0
#define PMU_RCTL_CHAIN_LEN_SHIFT	12
#define PMU_RCTL_MACPHY_DISABLE_SHIFT	26
#define PMU_RCTL_MACPHY_DISABLE_MASK	(1 << 26)
#define PMU_RCTL_LOGIC_DISABLE_SHIFT	27
#define PMU_RCTL_LOGIC_DISABLE_MASK	(1 << 27)
#define PMU_RCTL_MEMSLP_LOG_SHIFT	28
#define PMU_RCTL_MEMSLP_LOG_MASK	(1 << 28)
#define PMU_RCTL_MEMRETSLP_LOG_SHIFT	29
#define PMU_RCTL_MEMRETSLP_LOG_MASK	(1 << 29)

/*  Retention Group Control */
#define PMU_RCTLGRP_CHAIN_LEN_SHIFT	0
#define PMU_RCTLGRP_RMODE_ENABLE_SHIFT	14
#define PMU_RCTLGRP_RMODE_ENABLE_MASK	(1 << 14)
#define PMU_RCTLGRP_DFT_ENABLE_SHIFT	15
#define PMU_RCTLGRP_DFT_ENABLE_MASK	(1 << 15)
#define PMU_RCTLGRP_NSRST_DISABLE_SHIFT	16
#define PMU_RCTLGRP_NSRST_DISABLE_MASK	(1 << 16)
/*  Retention Group Control special for 4334 */
#define PMU4334_RCTLGRP_CHAIN_LEN_GRP0	338
#define PMU4334_RCTLGRP_CHAIN_LEN_GRP1	315
/*  Retention Group Control special for 43341 */
#define PMU43341_RCTLGRP_CHAIN_LEN_GRP0	366
#define PMU43341_RCTLGRP_CHAIN_LEN_GRP1	330

/* Fields in clkstretch */
#define CSTRETCH_HT		0xffff0000
#define CSTRETCH_ALP		0x0000ffff

/* gpiotimerval */
#define GPIO_ONTIME_SHIFT	16

/* clockcontrol_n */
#define	CN_N1_MASK		0x3f		/* n1 control */
#define	CN_N2_MASK		0x3f00		/* n2 control */
#define	CN_N2_SHIFT		8
#define	CN_PLLC_MASK		0xf0000		/* pll control */
#define	CN_PLLC_SHIFT		16

/* clockcontrol_sb/pci/uart */
#define	CC_M1_MASK		0x3f		/* m1 control */
#define	CC_M2_MASK		0x3f00		/* m2 control */
#define	CC_M2_SHIFT		8
#define	CC_M3_MASK		0x3f0000	/* m3 control */
#define	CC_M3_SHIFT		16
#define	CC_MC_MASK		0x1f000000	/* mux control */
#define	CC_MC_SHIFT		24

/* N3M Clock control magic field values */
#define	CC_F6_2			0x02		/* A factor of 2 in */
#define	CC_F6_3			0x03		/* 6-bit fields like */
#define	CC_F6_4			0x05		/* N1, M1 or M3 */
#define	CC_F6_5			0x09
#define	CC_F6_6			0x11
#define	CC_F6_7			0x21

#define	CC_F5_BIAS		5		/* 5-bit fields get this added */

#define	CC_MC_BYPASS		0x08
#define	CC_MC_M1		0x04
#define	CC_MC_M1M2		0x02
#define	CC_MC_M1M2M3		0x01
#define	CC_MC_M1M3		0x11

/* Type 2 Clock control magic field values */
#define	CC_T2_BIAS		2		/* n1, n2, m1 & m3 bias */
#define	CC_T2M2_BIAS		3		/* m2 bias */

#define	CC_T2MC_M1BYP		1
#define	CC_T2MC_M2BYP		2
#define	CC_T2MC_M3BYP		4

/* Type 6 Clock control magic field values */
#define	CC_T6_MMASK		1		/* bits of interest in m */
#define	CC_T6_M0		120000000	/* sb clock for m = 0 */
#define	CC_T6_M1		100000000	/* sb clock for m = 1 */
#define	SB2MIPS_T6(sb)		(2 * (sb))

/* Common clock base */
#define	CC_CLOCK_BASE1		24000000	/* Half the clock freq */
#define CC_CLOCK_BASE2		12500000	/* Alternate crystal on some PLLs */

/* Clock control values for 200MHz in 5350 */
#define	CLKC_5350_N		0x0311
#define	CLKC_5350_M		0x04020009

/* Flash types in the chipcommon capabilities register */
#define FLASH_NONE		0x000		/* No flash */
#define SFLASH_ST		0x100		/* ST serial flash */
#define SFLASH_AT		0x200		/* Atmel serial flash */
#define NFLASH			0x300
#define	PFLASH			0x700		/* Parallel flash */
#define QSPIFLASH_ST		0x800
#define QSPIFLASH_AT		0x900

/* Bits in the ExtBus config registers */
#define	CC_CFG_EN		0x0001		/* Enable */
#define	CC_CFG_EM_MASK		0x000e		/* Extif Mode */
#define	CC_CFG_EM_ASYNC		0x0000		/*   Async/Parallel flash */
#define	CC_CFG_EM_SYNC		0x0002		/*   Synchronous */
#define	CC_CFG_EM_PCMCIA	0x0004		/*   PCMCIA */
#define	CC_CFG_EM_IDE		0x0006		/*   IDE */
#define	CC_CFG_DS		0x0010		/* Data size, 0=8bit, 1=16bit */
#define	CC_CFG_CD_MASK		0x00e0		/* Sync: Clock divisor, rev >= 20 */
#define	CC_CFG_CE		0x0100		/* Sync: Clock enable, rev >= 20 */
#define	CC_CFG_SB		0x0200		/* Sync: Size/Bytestrobe, rev >= 20 */
#define	CC_CFG_IS		0x0400		/* Extif Sync Clk Select, rev >= 20 */

/* ExtBus address space */
#define	CC_EB_BASE		0x1a000000	/* Chipc ExtBus base address */
#define	CC_EB_PCMCIA_MEM	0x1a000000	/* PCMCIA 0 memory base address */
#define	CC_EB_PCMCIA_IO		0x1a200000	/* PCMCIA 0 I/O base address */
#define	CC_EB_PCMCIA_CFG	0x1a400000	/* PCMCIA 0 config base address */
#define	CC_EB_IDE		0x1a800000	/* IDE memory base */
#define	CC_EB_PCMCIA1_MEM	0x1a800000	/* PCMCIA 1 memory base address */
#define	CC_EB_PCMCIA1_IO	0x1aa00000	/* PCMCIA 1 I/O base address */
#define	CC_EB_PCMCIA1_CFG	0x1ac00000	/* PCMCIA 1 config base address */
#define	CC_EB_PROGIF		0x1b000000	/* ProgIF Async/Sync base address */


/* Start/busy bit in flashcontrol */
#define SFLASH_OPCODE		0x000000ff
#define SFLASH_ACTION		0x00000700
#define	SFLASH_CS_ACTIVE	0x00001000	/* Chip Select Active, rev >= 20 */
#define SFLASH_START		0x80000000
#define SFLASH_BUSY		SFLASH_START

/* flashcontrol action codes */
#define	SFLASH_ACT_OPONLY	0x0000		/* Issue opcode only */
#define	SFLASH_ACT_OP1D		0x0100		/* opcode + 1 data byte */
#define	SFLASH_ACT_OP3A		0x0200		/* opcode + 3 addr bytes */
#define	SFLASH_ACT_OP3A1D	0x0300		/* opcode + 3 addr & 1 data bytes */
#define	SFLASH_ACT_OP3A4D	0x0400		/* opcode + 3 addr & 4 data bytes */
#define	SFLASH_ACT_OP3A4X4D	0x0500		/* opcode + 3 addr, 4 don't care & 4 data bytes */
#define	SFLASH_ACT_OP3A1X4D	0x0700		/* opcode + 3 addr, 1 don't care & 4 data bytes */

/* flashcontrol action+opcodes for ST flashes */
#define SFLASH_ST_WREN		0x0006		/* Write Enable */
#define SFLASH_ST_WRDIS		0x0004		/* Write Disable */
#define SFLASH_ST_RDSR		0x0105		/* Read Status Register */
#define SFLASH_ST_WRSR		0x0101		/* Write Status Register */
#define SFLASH_ST_READ		0x0303		/* Read Data Bytes */
#define SFLASH_ST_PP		0x0302		/* Page Program */
#define SFLASH_ST_SE		0x02d8		/* Sector Erase */
#define SFLASH_ST_BE		0x00c7		/* Bulk Erase */
#define SFLASH_ST_DP		0x00b9		/* Deep Power-down */
#define SFLASH_ST_RES		0x03ab		/* Read Electronic Signature */
#define SFLASH_ST_CSA		0x1000		/* Keep chip select asserted */
#define SFLASH_ST_SSE		0x0220		/* Sub-sector Erase */

#define SFLASH_MXIC_RDID	0x0390		/* Read Manufacture ID */
#define SFLASH_MXIC_MFID	0xc2		/* MXIC Manufacture ID */

/* Status register bits for ST flashes */
#define SFLASH_ST_WIP		0x01		/* Write In Progress */
#define SFLASH_ST_WEL		0x02		/* Write Enable Latch */
#define SFLASH_ST_BP_MASK	0x1c		/* Block Protect */
#define SFLASH_ST_BP_SHIFT	2
#define SFLASH_ST_SRWD		0x80		/* Status Register Write Disable */

/* flashcontrol action+opcodes for Atmel flashes */
#define SFLASH_AT_READ				0x07e8
#define SFLASH_AT_PAGE_READ			0x07d2
#define SFLASH_AT_BUF1_READ
#define SFLASH_AT_BUF2_READ
#define SFLASH_AT_STATUS			0x01d7
#define SFLASH_AT_BUF1_WRITE			0x0384
#define SFLASH_AT_BUF2_WRITE			0x0387
#define SFLASH_AT_BUF1_ERASE_PROGRAM		0x0283
#define SFLASH_AT_BUF2_ERASE_PROGRAM		0x0286
#define SFLASH_AT_BUF1_PROGRAM			0x0288
#define SFLASH_AT_BUF2_PROGRAM			0x0289
#define SFLASH_AT_PAGE_ERASE			0x0281
#define SFLASH_AT_BLOCK_ERASE			0x0250
#define SFLASH_AT_BUF1_WRITE_ERASE_PROGRAM	0x0382
#define SFLASH_AT_BUF2_WRITE_ERASE_PROGRAM	0x0385
#define SFLASH_AT_BUF1_LOAD			0x0253
#define SFLASH_AT_BUF2_LOAD			0x0255
#define SFLASH_AT_BUF1_COMPARE			0x0260
#define SFLASH_AT_BUF2_COMPARE			0x0261
#define SFLASH_AT_BUF1_REPROGRAM		0x0258
#define SFLASH_AT_BUF2_REPROGRAM		0x0259

/* Status register bits for Atmel flashes */
#define SFLASH_AT_READY				0x80
#define SFLASH_AT_MISMATCH			0x40
#define SFLASH_AT_ID_MASK			0x38
#define SFLASH_AT_ID_SHIFT			3

/* SPI register bits, corerev >= 37 */
#define GSIO_START			0x80000000
#define GSIO_BUSY			GSIO_START

/*
 * These are the UART port assignments, expressed as offsets from the base
 * register.  These assignments should hold for any serial port based on
 * a 8250, 16450, or 16550(A).
 */

#define UART_RX		0	/* In:  Receive buffer (DLAB=0) */
#define UART_TX		0	/* Out: Transmit buffer (DLAB=0) */
#define UART_DLL	0	/* Out: Divisor Latch Low (DLAB=1) */
#define UART_IER	1	/* In/Out: Interrupt Enable Register (DLAB=0) */
#define UART_DLM	1	/* Out: Divisor Latch High (DLAB=1) */
#define UART_IIR	2	/* In: Interrupt Identity Register  */
#define UART_FCR	2	/* Out: FIFO Control Register */
#define UART_LCR	3	/* Out: Line Control Register */
#define UART_MCR	4	/* Out: Modem Control Register */
#define UART_LSR	5	/* In:  Line Status Register */
#define UART_MSR	6	/* In:  Modem Status Register */
#define UART_SCR	7	/* I/O: Scratch Register */
#define UART_LCR_DLAB	0x80	/* Divisor latch access bit */
#define UART_LCR_WLEN8	0x03	/* Word length: 8 bits */
#define UART_MCR_OUT2	0x08	/* MCR GPIO out 2 */
#define UART_MCR_LOOP	0x10	/* Enable loopback test mode */
#define UART_LSR_RX_FIFO 	0x80	/* Receive FIFO error */
#define UART_LSR_TDHR		0x40	/* Data-hold-register empty */
#define UART_LSR_THRE		0x20	/* Transmit-hold-register empty */
#define UART_LSR_BREAK		0x10	/* Break interrupt */
#define UART_LSR_FRAMING	0x08	/* Framing error */
#define UART_LSR_PARITY		0x04	/* Parity error */
#define UART_LSR_OVERRUN	0x02	/* Overrun error */
#define UART_LSR_RXRDY		0x01	/* Receiver ready */
#define UART_FCR_FIFO_ENABLE 1	/* FIFO control register bit controlling FIFO enable/disable */

/* Interrupt Identity Register (IIR) bits */
#define UART_IIR_FIFO_MASK	0xc0	/* IIR FIFO disable/enabled mask */
#define UART_IIR_INT_MASK	0xf	/* IIR interrupt ID source */
#define UART_IIR_MDM_CHG	0x0	/* Modem status changed */
#define UART_IIR_NOINT		0x1	/* No interrupt pending */
#define UART_IIR_THRE		0x2	/* THR empty */
#define UART_IIR_RCVD_DATA	0x4	/* Received data available */
#define UART_IIR_RCVR_STATUS 	0x6	/* Receiver status */
#define UART_IIR_CHAR_TIME 	0xc	/* Character time */

/* Interrupt Enable Register (IER) bits */
#define UART_IER_PTIME	128	/* Programmable THRE Interrupt Mode Enable */
#define UART_IER_EDSSI	8	/* enable modem status interrupt */
#define UART_IER_ELSI	4	/* enable receiver line status interrupt */
#define UART_IER_ETBEI  2	/* enable transmitter holding register empty interrupt */
#define UART_IER_ERBFI	1	/* enable data available interrupt */

/* pmustatus */
#define PST_SLOW_WR_PENDING 0x0400
#define PST_EXTLPOAVAIL	0x0100
#define PST_WDRESET	0x0080
#define	PST_INTPEND	0x0040
#define	PST_SBCLKST	0x0030
#define	PST_SBCLKST_ILP	0x0010
#define	PST_SBCLKST_ALP	0x0020
#define	PST_SBCLKST_HT	0x0030
#define	PST_ALPAVAIL	0x0008
#define	PST_HTAVAIL	0x0004
#define	PST_RESINIT	0x0003

/* pmucapabilities */
#define PCAP_REV_MASK	0x000000ff
#define PCAP_RC_MASK	0x00001f00
#define PCAP_RC_SHIFT	8
#define PCAP_TC_MASK	0x0001e000
#define PCAP_TC_SHIFT	13
#define PCAP_PC_MASK	0x001e0000
#define PCAP_PC_SHIFT	17
#define PCAP_VC_MASK	0x01e00000
#define PCAP_VC_SHIFT	21
#define PCAP_CC_MASK	0x1e000000
#define PCAP_CC_SHIFT	25
#define PCAP5_PC_MASK	0x003e0000	/* PMU corerev >= 5 */
#define PCAP5_PC_SHIFT	17
#define PCAP5_VC_MASK	0x07c00000
#define PCAP5_VC_SHIFT	22
#define PCAP5_CC_MASK	0xf8000000
#define PCAP5_CC_SHIFT	27

/* PMU Resource Request Timer registers */
/* This is based on PmuRev0 */
#define	PRRT_TIME_MASK	0x03ff
#define	PRRT_INTEN	0x0400
#define	PRRT_REQ_ACTIVE	0x0800
#define	PRRT_ALP_REQ	0x1000
#define	PRRT_HT_REQ	0x2000
#define PRRT_HQ_REQ 0x4000

/* bit 0 of the PMU interrupt vector is asserted if this mask is enabled */
#define RSRC_INTR_MASK_TIMER_INT_0 1

/* PMU resource bit position */
#define PMURES_BIT(bit)	(1 << (bit))

/* PMU resource number limit */
#define PMURES_MAX_RESNUM	30

/* PMU chip control0 register */
#define	PMU_CHIPCTL0		0
#define PMU43143_CC0_SDIO_DRSTR_OVR	(1 << 31) /* sdio drive strength override enable */

/* clock req types */
#define PMU_CC1_CLKREQ_TYPE_SHIFT	19
#define PMU_CC1_CLKREQ_TYPE_MASK	(1 << PMU_CC1_CLKREQ_TYPE_SHIFT)

#define CLKREQ_TYPE_CONFIG_OPENDRAIN		0
#define CLKREQ_TYPE_CONFIG_PUSHPULL		1

/* PMU chip control1 register */
#define	PMU_CHIPCTL1			1
#define	PMU_CC1_RXC_DLL_BYPASS		0x00010000
#define PMU_CC1_ENABLE_BBPLL_PWR_DOWN	0x00000010

#define PMU_CC1_IF_TYPE_MASK   		0x00000030
#define PMU_CC1_IF_TYPE_RMII    	0x00000000
#define PMU_CC1_IF_TYPE_MII     	0x00000010
#define PMU_CC1_IF_TYPE_RGMII   	0x00000020

#define PMU_CC1_SW_TYPE_MASK    	0x000000c0
#define PMU_CC1_SW_TYPE_EPHY    	0x00000000
#define PMU_CC1_SW_TYPE_EPHYMII 	0x00000040
#define PMU_CC1_SW_TYPE_EPHYRMII	0x00000080
#define PMU_CC1_SW_TYPE_RGMII   	0x000000c0

/* PMU chip control2 register */
#define	PMU_CHIPCTL2		2
#define PMU_CC2_FORCE_SUBCORE_PWR_SWITCH_ON   	(1 << 18)
#define PMU_CC2_FORCE_PHY_PWR_SWITCH_ON   	(1 << 19)
#define PMU_CC2_FORCE_VDDM_PWR_SWITCH_ON   	(1 << 20)
#define PMU_CC2_FORCE_MEMLPLDO_PWR_SWITCH_ON   	(1 << 21)

/* PMU chip control3 register */
#define	PMU_CHIPCTL3		3
#define PMU_CC3_ENABLE_SDIO_WAKEUP_SHIFT  19
#define PMU_CC3_ENABLE_RF_SHIFT           22
#define PMU_CC3_RF_DISABLE_IVALUE_SHIFT   23

/* PMU chip control5 register */
#define PMU_CHIPCTL5                    5

/* PMU chip control6 register */
#define PMU_CHIPCTL6                    6
#define PMU_CC6_ENABLE_CLKREQ_WAKEUP    (1 << 4)
#define PMU_CC6_ENABLE_PMU_WAKEUP_ALP   (1 << 6)

/* PMU chip control7 register */
#define PMU_CHIPCTL7				7
#define PMU_CC7_ENABLE_L2REFCLKPAD_PWRDWN	(1 << 25)
#define PMU_CC7_ENABLE_MDIO_RESET_WAR		(1 << 27)


/* PMU corerev and chip specific PLL controls.
 * PMU<rev>_PLL<num>_XX where <rev> is PMU corerev and <num> is an arbitrary number
 * to differentiate different PLLs controlled by the same PMU rev.
 */
/* pllcontrol registers */
/* PDIV, div_phy, div_arm, div_adc, dith_sel, ioff, kpd_scale, lsb_sel, mash_sel, lf_c & lf_r */
#define	PMU0_PLL0_PLLCTL0		0
#define	PMU0_PLL0_PC0_PDIV_MASK		1
#define	PMU0_PLL0_PC0_PDIV_FREQ		25000
#define PMU0_PLL0_PC0_DIV_ARM_MASK	0x00000038
#define PMU0_PLL0_PC0_DIV_ARM_SHIFT	3
#define PMU0_PLL0_PC0_DIV_ARM_BASE	8

/* PC0_DIV_ARM for PLLOUT_ARM */
#define PMU0_PLL0_PC0_DIV_ARM_110MHZ	0
#define PMU0_PLL0_PC0_DIV_ARM_97_7MHZ	1
#define PMU0_PLL0_PC0_DIV_ARM_88MHZ	2
#define PMU0_PLL0_PC0_DIV_ARM_80MHZ	3 /* Default */
#define PMU0_PLL0_PC0_DIV_ARM_73_3MHZ	4
#define PMU0_PLL0_PC0_DIV_ARM_67_7MHZ	5
#define PMU0_PLL0_PC0_DIV_ARM_62_9MHZ	6
#define PMU0_PLL0_PC0_DIV_ARM_58_6MHZ	7

/* Wildcard base, stop_mod, en_lf_tp, en_cal & lf_r2 */
#define	PMU0_PLL0_PLLCTL1		1
#define	PMU0_PLL0_PC1_WILD_INT_MASK	0xf0000000
#define	PMU0_PLL0_PC1_WILD_INT_SHIFT	28
#define	PMU0_PLL0_PC1_WILD_FRAC_MASK	0x0fffff00
#define	PMU0_PLL0_PC1_WILD_FRAC_SHIFT	8
#define	PMU0_PLL0_PC1_STOP_MOD		0x00000040

/* Wildcard base, vco_calvar, vco_swc, vco_var_selref, vso_ical & vco_sel_avdd */
#define	PMU0_PLL0_PLLCTL2		2
#define	PMU0_PLL0_PC2_WILD_INT_MASK	0xf
#define	PMU0_PLL0_PC2_WILD_INT_SHIFT	4

/* pllcontrol registers */
/* ndiv_pwrdn, pwrdn_ch<x>, refcomp_pwrdn, dly_ch<x>, p1div, p2div, _bypass_sdmod */
#define PMU1_PLL0_PLLCTL0		0
#define PMU1_PLL0_PC0_P1DIV_MASK	0x00f00000
#define PMU1_PLL0_PC0_P1DIV_SHIFT	20
#define PMU1_PLL0_PC0_P2DIV_MASK	0x0f000000
#define PMU1_PLL0_PC0_P2DIV_SHIFT	24

/* m<x>div */
#define PMU1_PLL0_PLLCTL1		1
#define PMU1_PLL0_PC1_M1DIV_MASK	0x000000ff
#define PMU1_PLL0_PC1_M1DIV_SHIFT	0
#define PMU1_PLL0_PC1_M2DIV_MASK	0x0000ff00
#define PMU1_PLL0_PC1_M2DIV_SHIFT	8
#define PMU1_PLL0_PC1_M3DIV_MASK	0x00ff0000
#define PMU1_PLL0_PC1_M3DIV_SHIFT	16
#define PMU1_PLL0_PC1_M4DIV_MASK	0xff000000
#define PMU1_PLL0_PC1_M4DIV_SHIFT	24
#define PMU1_PLL0_PC1_M4DIV_BY_9	9
#define PMU1_PLL0_PC1_M4DIV_BY_18	0x12
#define PMU1_PLL0_PC1_M4DIV_BY_36	0x24
#define PMU1_PLL0_PC1_M4DIV_BY_60	0x3C

#define DOT11MAC_880MHZ_CLK_DIVISOR_SHIFT 8
#define DOT11MAC_880MHZ_CLK_DIVISOR_MASK (0xFF << DOT11MAC_880MHZ_CLK_DIVISOR_SHIFT)
#define DOT11MAC_880MHZ_CLK_DIVISOR_VAL  (0xE << DOT11MAC_880MHZ_CLK_DIVISOR_SHIFT)

/* m<x>div, ndiv_dither_mfb, ndiv_mode, ndiv_int */
#define PMU1_PLL0_PLLCTL2		2
#define PMU1_PLL0_PC2_M5DIV_MASK	0x000000ff
#define PMU1_PLL0_PC2_M5DIV_SHIFT	0
#define PMU1_PLL0_PC2_M5DIV_BY_12	0xc
#define PMU1_PLL0_PC2_M5DIV_BY_18	0x12
#define PMU1_PLL0_PC2_M5DIV_BY_36	0x24
#define PMU1_PLL0_PC2_M6DIV_MASK	0x0000ff00
#define PMU1_PLL0_PC2_M6DIV_SHIFT	8
#define PMU1_PLL0_PC2_M6DIV_BY_18	0x12
#define PMU1_PLL0_PC2_M6DIV_BY_36	0x24
#define PMU1_PLL0_PC2_NDIV_MODE_MASK	0x000e0000
#define PMU1_PLL0_PC2_NDIV_MODE_SHIFT	17
#define PMU1_PLL0_PC2_NDIV_MODE_MASH	1
#define PMU1_PLL0_PC2_NDIV_MODE_MFB	2	/* recommended for 4319 */
#define PMU1_PLL0_PC2_NDIV_INT_MASK	0x1ff00000
#define PMU1_PLL0_PC2_NDIV_INT_SHIFT	20

/* ndiv_frac */
#define PMU1_PLL0_PLLCTL3		3
#define PMU1_PLL0_PC3_NDIV_FRAC_MASK	0x00ffffff
#define PMU1_PLL0_PC3_NDIV_FRAC_SHIFT	0

/* pll_ctrl */
#define PMU1_PLL0_PLLCTL4		4

/* pll_ctrl, vco_rng, clkdrive_ch<x> */
#define PMU1_PLL0_PLLCTL5		5
#define PMU1_PLL0_PC5_CLK_DRV_MASK 0xffffff00
#define PMU1_PLL0_PC5_CLK_DRV_SHIFT 8

#define PMU1_PLL0_PLLCTL6		6
#define PMU1_PLL0_PLLCTL7		7

#define PMU1_PLL0_PLLCTL8		8
#define PMU1_PLLCTL8_OPENLOOP_MASK	0x2

/* PMU rev 2 control words */
#define PMU2_PHY_PLL_PLLCTL		4
#define PMU2_SI_PLL_PLLCTL		10

/* PMU rev 2 */
/* pllcontrol registers */
/* ndiv_pwrdn, pwrdn_ch<x>, refcomp_pwrdn, dly_ch<x>, p1div, p2div, _bypass_sdmod */
#define PMU2_PLL_PLLCTL0		0
#define PMU2_PLL_PC0_P1DIV_MASK 	0x00f00000
#define PMU2_PLL_PC0_P1DIV_SHIFT	20
#define PMU2_PLL_PC0_P2DIV_MASK 	0x0f000000
#define PMU2_PLL_PC0_P2DIV_SHIFT	24

/* m<x>div */
#define PMU2_PLL_PLLCTL1		1
#define PMU2_PLL_PC1_M1DIV_MASK 	0x000000ff
#define PMU2_PLL_PC1_M1DIV_SHIFT	0
#define PMU2_PLL_PC1_M2DIV_MASK 	0x0000ff00
#define PMU2_PLL_PC1_M2DIV_SHIFT	8
#define PMU2_PLL_PC1_M3DIV_MASK 	0x00ff0000
#define PMU2_PLL_PC1_M3DIV_SHIFT	16
#define PMU2_PLL_PC1_M4DIV_MASK 	0xff000000
#define PMU2_PLL_PC1_M4DIV_SHIFT	24

/* m<x>div, ndiv_dither_mfb, ndiv_mode, ndiv_int */
#define PMU2_PLL_PLLCTL2		2
#define PMU2_PLL_PC2_M5DIV_MASK 	0x000000ff
#define PMU2_PLL_PC2_M5DIV_SHIFT	0
#define PMU2_PLL_PC2_M6DIV_MASK 	0x0000ff00
#define PMU2_PLL_PC2_M6DIV_SHIFT	8
#define PMU2_PLL_PC2_NDIV_MODE_MASK	0x000e0000
#define PMU2_PLL_PC2_NDIV_MODE_SHIFT	17
#define PMU2_PLL_PC2_NDIV_INT_MASK	0x1ff00000
#define PMU2_PLL_PC2_NDIV_INT_SHIFT	20

/* ndiv_frac */
#define PMU2_PLL_PLLCTL3		3
#define PMU2_PLL_PC3_NDIV_FRAC_MASK	0x00ffffff
#define PMU2_PLL_PC3_NDIV_FRAC_SHIFT	0

/* pll_ctrl */
#define PMU2_PLL_PLLCTL4		4

/* pll_ctrl, vco_rng, clkdrive_ch<x> */
#define PMU2_PLL_PLLCTL5		5
#define PMU2_PLL_PC5_CLKDRIVE_CH1_MASK	0x00000f00
#define PMU2_PLL_PC5_CLKDRIVE_CH1_SHIFT	8
#define PMU2_PLL_PC5_CLKDRIVE_CH2_MASK	0x0000f000
#define PMU2_PLL_PC5_CLKDRIVE_CH2_SHIFT	12
#define PMU2_PLL_PC5_CLKDRIVE_CH3_MASK	0x000f0000
#define PMU2_PLL_PC5_CLKDRIVE_CH3_SHIFT	16
#define PMU2_PLL_PC5_CLKDRIVE_CH4_MASK	0x00f00000
#define PMU2_PLL_PC5_CLKDRIVE_CH4_SHIFT	20
#define PMU2_PLL_PC5_CLKDRIVE_CH5_MASK	0x0f000000
#define PMU2_PLL_PC5_CLKDRIVE_CH5_SHIFT	24
#define PMU2_PLL_PC5_CLKDRIVE_CH6_MASK	0xf0000000
#define PMU2_PLL_PC5_CLKDRIVE_CH6_SHIFT	28

/* PMU rev 5 (& 6) */
#define	PMU5_PLL_P1P2_OFF		0
#define	PMU5_PLL_P1_MASK		0x0f000000
#define	PMU5_PLL_P1_SHIFT		24
#define	PMU5_PLL_P2_MASK		0x00f00000
#define	PMU5_PLL_P2_SHIFT		20
#define	PMU5_PLL_M14_OFF		1
#define	PMU5_PLL_MDIV_MASK		0x000000ff
#define	PMU5_PLL_MDIV_WIDTH		8
#define	PMU5_PLL_NM5_OFF		2
#define	PMU5_PLL_NDIV_MASK		0xfff00000
#define	PMU5_PLL_NDIV_SHIFT		20
#define	PMU5_PLL_NDIV_MODE_MASK		0x000e0000
#define	PMU5_PLL_NDIV_MODE_SHIFT	17
#define	PMU5_PLL_FMAB_OFF		3
#define	PMU5_PLL_MRAT_MASK		0xf0000000
#define	PMU5_PLL_MRAT_SHIFT		28
#define	PMU5_PLL_ABRAT_MASK		0x08000000
#define	PMU5_PLL_ABRAT_SHIFT		27
#define	PMU5_PLL_FDIV_MASK		0x07ffffff
#define	PMU5_PLL_PLLCTL_OFF		4
#define	PMU5_PLL_PCHI_OFF		5
#define	PMU5_PLL_PCHI_MASK		0x0000003f

/* pmu XtalFreqRatio */
#define	PMU_XTALFREQ_REG_ILPCTR_MASK	0x00001FFF
#define	PMU_XTALFREQ_REG_MEASURE_MASK	0x80000000
#define	PMU_XTALFREQ_REG_MEASURE_SHIFT	31

/* Divider allocation in 4716/47162/5356/5357 */
#define	PMU5_MAINPLL_CPU		1
#define	PMU5_MAINPLL_MEM		2
#define	PMU5_MAINPLL_SI			3

/* 4706 PMU */
#define PMU4706_MAINPLL_PLL0	0
#define PMU6_4706_PROCPLL_OFF	4	/* The CPU PLL */
#define PMU6_4706_PROC_P2DIV_MASK		0x000f0000
#define PMU6_4706_PROC_P2DIV_SHIFT	16
#define PMU6_4706_PROC_P1DIV_MASK		0x0000f000
#define PMU6_4706_PROC_P1DIV_SHIFT	12
#define PMU6_4706_PROC_NDIV_INT_MASK	0x00000ff8
#define PMU6_4706_PROC_NDIV_INT_SHIFT	3
#define PMU6_4706_PROC_NDIV_MODE_MASK		0x00000007
#define PMU6_4706_PROC_NDIV_MODE_SHIFT	0

#define PMU7_PLL_PLLCTL7                7
#define PMU7_PLL_CTL7_M4DIV_MASK	0xff000000
#define PMU7_PLL_CTL7_M4DIV_SHIFT 	24
#define PMU7_PLL_CTL7_M4DIV_BY_6	6
#define PMU7_PLL_CTL7_M4DIV_BY_12	0xc
#define PMU7_PLL_CTL7_M4DIV_BY_24	0x18
#define PMU7_PLL_PLLCTL8                8
#define PMU7_PLL_CTL8_M5DIV_MASK	0x000000ff
#define PMU7_PLL_CTL8_M5DIV_SHIFT	0
#define PMU7_PLL_CTL8_M5DIV_BY_8	8
#define PMU7_PLL_CTL8_M5DIV_BY_12	0xc
#define PMU7_PLL_CTL8_M5DIV_BY_24	0x18
#define PMU7_PLL_CTL8_M6DIV_MASK	0x0000ff00
#define PMU7_PLL_CTL8_M6DIV_SHIFT	8
#define PMU7_PLL_CTL8_M6DIV_BY_12	0xc
#define PMU7_PLL_CTL8_M6DIV_BY_24	0x18
#define PMU7_PLL_PLLCTL11		11
#define PMU7_PLL_PLLCTL11_MASK		0xffffff00
#define PMU7_PLL_PLLCTL11_VAL		0x22222200

/* PMU rev 15 */
#define PMU15_PLL_PLLCTL0		0
#define PMU15_PLL_PC0_CLKSEL_MASK	0x00000003
#define PMU15_PLL_PC0_CLKSEL_SHIFT	0
#define PMU15_PLL_PC0_FREQTGT_MASK	0x003FFFFC
#define PMU15_PLL_PC0_FREQTGT_SHIFT	2
#define PMU15_PLL_PC0_PRESCALE_MASK	0x00C00000
#define PMU15_PLL_PC0_PRESCALE_SHIFT	22
#define PMU15_PLL_PC0_KPCTRL_MASK	0x07000000
#define PMU15_PLL_PC0_KPCTRL_SHIFT	24
#define PMU15_PLL_PC0_FCNTCTRL_MASK	0x38000000
#define PMU15_PLL_PC0_FCNTCTRL_SHIFT	27
#define PMU15_PLL_PC0_FDCMODE_MASK	0x40000000
#define PMU15_PLL_PC0_FDCMODE_SHIFT	30
#define PMU15_PLL_PC0_CTRLBIAS_MASK	0x80000000
#define PMU15_PLL_PC0_CTRLBIAS_SHIFT	31

#define PMU15_PLL_PLLCTL1			1
#define PMU15_PLL_PC1_BIAS_CTLM_MASK		0x00000060
#define PMU15_PLL_PC1_BIAS_CTLM_SHIFT		5
#define PMU15_PLL_PC1_BIAS_CTLM_RST_MASK	0x00000040
#define PMU15_PLL_PC1_BIAS_CTLM_RST_SHIFT	6
#define PMU15_PLL_PC1_BIAS_SS_DIVR_MASK		0x0001FF80
#define PMU15_PLL_PC1_BIAS_SS_DIVR_SHIFT	7
#define PMU15_PLL_PC1_BIAS_SS_RSTVAL_MASK	0x03FE0000
#define PMU15_PLL_PC1_BIAS_SS_RSTVAL_SHIFT	17
#define PMU15_PLL_PC1_BIAS_INTG_BW_MASK		0x0C000000
#define PMU15_PLL_PC1_BIAS_INTG_BW_SHIFT	26
#define PMU15_PLL_PC1_BIAS_INTG_BYP_MASK	0x10000000
#define PMU15_PLL_PC1_BIAS_INTG_BYP_SHIFT	28
#define PMU15_PLL_PC1_OPENLP_EN_MASK		0x40000000
#define PMU15_PLL_PC1_OPENLP_EN_SHIFT		30

#define PMU15_PLL_PLLCTL2			2
#define PMU15_PLL_PC2_CTEN_MASK			0x00000001
#define PMU15_PLL_PC2_CTEN_SHIFT		0

#define PMU15_PLL_PLLCTL3			3
#define PMU15_PLL_PC3_DITHER_EN_MASK		0x00000001
#define PMU15_PLL_PC3_DITHER_EN_SHIFT		0
#define PMU15_PLL_PC3_DCOCTLSP_MASK		0xFE000000
#define PMU15_PLL_PC3_DCOCTLSP_SHIFT		25
#define PMU15_PLL_PC3_DCOCTLSP_DIV2EN_MASK	0x01
#define PMU15_PLL_PC3_DCOCTLSP_DIV2EN_SHIFT	0
#define PMU15_PLL_PC3_DCOCTLSP_CH0EN_MASK	0x02
#define PMU15_PLL_PC3_DCOCTLSP_CH0EN_SHIFT	1
#define PMU15_PLL_PC3_DCOCTLSP_CH1EN_MASK	0x04
#define PMU15_PLL_PC3_DCOCTLSP_CH1EN_SHIFT	2
#define PMU15_PLL_PC3_DCOCTLSP_CH0SEL_MASK	0x18
#define PMU15_PLL_PC3_DCOCTLSP_CH0SEL_SHIFT	3
#define PMU15_PLL_PC3_DCOCTLSP_CH1SEL_MASK	0x60
#define PMU15_PLL_PC3_DCOCTLSP_CH1SEL_SHIFT	5
#define PMU15_PLL_PC3_DCOCTLSP_CHSEL_OUTP_DIV1	0
#define PMU15_PLL_PC3_DCOCTLSP_CHSEL_OUTP_DIV2	1
#define PMU15_PLL_PC3_DCOCTLSP_CHSEL_OUTP_DIV3	2
#define PMU15_PLL_PC3_DCOCTLSP_CHSEL_OUTP_DIV5	3

#define PMU15_PLL_PLLCTL4			4
#define PMU15_PLL_PC4_FLLCLK1_DIV_MASK		0x00000007
#define PMU15_PLL_PC4_FLLCLK1_DIV_SHIFT		0
#define PMU15_PLL_PC4_FLLCLK2_DIV_MASK		0x00000038
#define PMU15_PLL_PC4_FLLCLK2_DIV_SHIFT		3
#define PMU15_PLL_PC4_FLLCLK3_DIV_MASK		0x000001C0
#define PMU15_PLL_PC4_FLLCLK3_DIV_SHIFT		6
#define PMU15_PLL_PC4_DBGMODE_MASK		0x00000E00
#define PMU15_PLL_PC4_DBGMODE_SHIFT		9
#define PMU15_PLL_PC4_FLL480_CTLSP_LK_MASK	0x00001000
#define PMU15_PLL_PC4_FLL480_CTLSP_LK_SHIFT	12
#define PMU15_PLL_PC4_FLL480_CTLSP_MASK		0x000FE000
#define PMU15_PLL_PC4_FLL480_CTLSP_SHIFT	13
#define PMU15_PLL_PC4_DINPOL_MASK		0x00100000
#define PMU15_PLL_PC4_DINPOL_SHIFT		20
#define PMU15_PLL_PC4_CLKOUT_PD_MASK		0x00200000
#define PMU15_PLL_PC4_CLKOUT_PD_SHIFT		21
#define PMU15_PLL_PC4_CLKDIV2_PD_MASK		0x00400000
#define PMU15_PLL_PC4_CLKDIV2_PD_SHIFT		22
#define PMU15_PLL_PC4_CLKDIV4_PD_MASK		0x00800000
#define PMU15_PLL_PC4_CLKDIV4_PD_SHIFT		23
#define PMU15_PLL_PC4_CLKDIV8_PD_MASK		0x01000000
#define PMU15_PLL_PC4_CLKDIV8_PD_SHIFT		24
#define PMU15_PLL_PC4_CLKDIV16_PD_MASK		0x02000000
#define PMU15_PLL_PC4_CLKDIV16_PD_SHIFT		25
#define PMU15_PLL_PC4_TEST_EN_MASK		0x04000000
#define PMU15_PLL_PC4_TEST_EN_SHIFT		26

#define PMU15_PLL_PLLCTL5			5
#define PMU15_PLL_PC5_FREQTGT_MASK		0x000FFFFF
#define PMU15_PLL_PC5_FREQTGT_SHIFT		0
#define PMU15_PLL_PC5_DCOCTLSP_MASK		0x07F00000
#define PMU15_PLL_PC5_DCOCTLSP_SHIFT		20
#define PMU15_PLL_PC5_PRESCALE_MASK		0x18000000
#define PMU15_PLL_PC5_PRESCALE_SHIFT		27

#define PMU15_PLL_PLLCTL6		6
#define PMU15_PLL_PC6_FREQTGT_MASK	0x000FFFFF
#define PMU15_PLL_PC6_FREQTGT_SHIFT	0
#define PMU15_PLL_PC6_DCOCTLSP_MASK	0x07F00000
#define PMU15_PLL_PC6_DCOCTLSP_SHIFT	20
#define PMU15_PLL_PC6_PRESCALE_MASK	0x18000000
#define PMU15_PLL_PC6_PRESCALE_SHIFT	27

#define PMU15_FREQTGT_480_DEFAULT	0x19AB1
#define PMU15_FREQTGT_492_DEFAULT	0x1A4F5
#define PMU15_ARM_96MHZ			96000000	/* 96 Mhz */
#define PMU15_ARM_98MHZ			98400000	/* 98.4 Mhz */
#define PMU15_ARM_97MHZ			97000000	/* 97 Mhz */


#define PMU17_PLLCTL2_NDIVTYPE_MASK		0x00000070
#define PMU17_PLLCTL2_NDIVTYPE_SHIFT		4

#define PMU17_PLLCTL2_NDIV_MODE_INT		0
#define PMU17_PLLCTL2_NDIV_MODE_INT1B8		1
#define PMU17_PLLCTL2_NDIV_MODE_MASH111		2
#define PMU17_PLLCTL2_NDIV_MODE_MASH111B8	3

#define PMU17_PLLCTL0_BBPLL_PWRDWN		0
#define PMU17_PLLCTL0_BBPLL_DRST		3
#define PMU17_PLLCTL0_BBPLL_DISBL_CLK		8

/* PLL usage in 4716/47162 */
#define	PMU4716_MAINPLL_PLL0		12

/* PLL usage in 4335 */
#define PMU4335_PLL0_PC2_P1DIV_MASK			0x000f0000
#define PMU4335_PLL0_PC2_P1DIV_SHIFT		16
#define PMU4335_PLL0_PC2_NDIV_INT_MASK		0xff800000
#define PMU4335_PLL0_PC2_NDIV_INT_SHIFT		23
#define PMU4335_PLL0_PC1_MDIV2_MASK			0x0000ff00
#define PMU4335_PLL0_PC1_MDIV2_SHIFT		8


/* PLL usage in 5356/5357 */
#define	PMU5356_MAINPLL_PLL0		0
#define	PMU5357_MAINPLL_PLL0		0

/* 4716/47162 resources */
#define RES4716_PROC_PLL_ON		0x00000040
#define RES4716_PROC_HT_AVAIL		0x00000080

/* 4716/4717/4718 Chip specific ChipControl register bits */
#define CCTRL_471X_I2S_PINS_ENABLE	0x0080 /* I2S pins off by default, shared w/ pflash */

/* 5357 Chip specific ChipControl register bits */
/* 2nd - 32-bit reg */
#define CCTRL_5357_I2S_PINS_ENABLE	0x00040000 /* I2S pins enable */
#define CCTRL_5357_I2CSPI_PINS_ENABLE	0x00080000 /* I2C/SPI pins enable */

/* 5354 resources */
#define RES5354_EXT_SWITCHER_PWM	0	/* 0x00001 */
#define RES5354_BB_SWITCHER_PWM		1	/* 0x00002 */
#define RES5354_BB_SWITCHER_BURST	2	/* 0x00004 */
#define RES5354_BB_EXT_SWITCHER_BURST	3	/* 0x00008 */
#define RES5354_ILP_REQUEST		4	/* 0x00010 */
#define RES5354_RADIO_SWITCHER_PWM	5	/* 0x00020 */
#define RES5354_RADIO_SWITCHER_BURST	6	/* 0x00040 */
#define RES5354_ROM_SWITCH		7	/* 0x00080 */
#define RES5354_PA_REF_LDO		8	/* 0x00100 */
#define RES5354_RADIO_LDO		9	/* 0x00200 */
#define RES5354_AFE_LDO			10	/* 0x00400 */
#define RES5354_PLL_LDO			11	/* 0x00800 */
#define RES5354_BG_FILTBYP		12	/* 0x01000 */
#define RES5354_TX_FILTBYP		13	/* 0x02000 */
#define RES5354_RX_FILTBYP		14	/* 0x04000 */
#define RES5354_XTAL_PU			15	/* 0x08000 */
#define RES5354_XTAL_EN			16	/* 0x10000 */
#define RES5354_BB_PLL_FILTBYP		17	/* 0x20000 */
#define RES5354_RF_PLL_FILTBYP		18	/* 0x40000 */
#define RES5354_BB_PLL_PU		19	/* 0x80000 */

/* 5357 Chip specific ChipControl register bits */
#define CCTRL5357_EXTPA                 (1<<14) /* extPA in ChipControl 1, bit 14 */
#define CCTRL5357_ANT_MUX_2o3		(1<<15) /* 2o3 in ChipControl 1, bit 15 */
#define CCTRL5357_NFLASH		(1<<16) /* Nandflash in ChipControl 1, bit 16 */

/* 43217 Chip specific ChipControl register bits */
#define CCTRL43217_EXTPA_C0             (1<<13) /* core0 extPA in ChipControl 1, bit 13 */
#define CCTRL43217_EXTPA_C1             (1<<8)  /* core1 extPA in ChipControl 1, bit 8 */

/* 43228 Chip specific ChipControl register bits */
#define CCTRL43228_EXTPA_C0             (1<<14) /* core1 extPA in ChipControl 1, bit 14 */
#define CCTRL43228_EXTPA_C1             (1<<9)  /* core0 extPA in ChipControl 1, bit 1 */

/* 4328 resources */
#define RES4328_EXT_SWITCHER_PWM	0	/* 0x00001 */
#define RES4328_BB_SWITCHER_PWM		1	/* 0x00002 */
#define RES4328_BB_SWITCHER_BURST	2	/* 0x00004 */
#define RES4328_BB_EXT_SWITCHER_BURST	3	/* 0x00008 */
#define RES4328_ILP_REQUEST		4	/* 0x00010 */
#define RES4328_RADIO_SWITCHER_PWM	5	/* 0x00020 */
#define RES4328_RADIO_SWITCHER_BURST	6	/* 0x00040 */
#define RES4328_ROM_SWITCH		7	/* 0x00080 */
#define RES4328_PA_REF_LDO		8	/* 0x00100 */
#define RES4328_RADIO_LDO		9	/* 0x00200 */
#define RES4328_AFE_LDO			10	/* 0x00400 */
#define RES4328_PLL_LDO			11	/* 0x00800 */
#define RES4328_BG_FILTBYP		12	/* 0x01000 */
#define RES4328_TX_FILTBYP		13	/* 0x02000 */
#define RES4328_RX_FILTBYP		14	/* 0x04000 */
#define RES4328_XTAL_PU			15	/* 0x08000 */
#define RES4328_XTAL_EN			16	/* 0x10000 */
#define RES4328_BB_PLL_FILTBYP		17	/* 0x20000 */
#define RES4328_RF_PLL_FILTBYP		18	/* 0x40000 */
#define RES4328_BB_PLL_PU		19	/* 0x80000 */

/* 4325 A0/A1 resources */
#define RES4325_BUCK_BOOST_BURST	0	/* 0x00000001 */
#define RES4325_CBUCK_BURST		1	/* 0x00000002 */
#define RES4325_CBUCK_PWM		2	/* 0x00000004 */
#define RES4325_CLDO_CBUCK_BURST	3	/* 0x00000008 */
#define RES4325_CLDO_CBUCK_PWM		4	/* 0x00000010 */
#define RES4325_BUCK_BOOST_PWM		5	/* 0x00000020 */
#define RES4325_ILP_REQUEST		6	/* 0x00000040 */
#define RES4325_ABUCK_BURST		7	/* 0x00000080 */
#define RES4325_ABUCK_PWM		8	/* 0x00000100 */
#define RES4325_LNLDO1_PU		9	/* 0x00000200 */
#define RES4325_OTP_PU			10	/* 0x00000400 */
#define RES4325_LNLDO3_PU		11	/* 0x00000800 */
#define RES4325_LNLDO4_PU		12	/* 0x00001000 */
#define RES4325_XTAL_PU			13	/* 0x00002000 */
#define RES4325_ALP_AVAIL		14	/* 0x00004000 */
#define RES4325_RX_PWRSW_PU		15	/* 0x00008000 */
#define RES4325_TX_PWRSW_PU		16	/* 0x00010000 */
#define RES4325_RFPLL_PWRSW_PU		17	/* 0x00020000 */
#define RES4325_LOGEN_PWRSW_PU		18	/* 0x00040000 */
#define RES4325_AFE_PWRSW_PU		19	/* 0x00080000 */
#define RES4325_BBPLL_PWRSW_PU		20	/* 0x00100000 */
#define RES4325_HT_AVAIL		21	/* 0x00200000 */

/* 4325 B0/C0 resources */
#define RES4325B0_CBUCK_LPOM		1	/* 0x00000002 */
#define RES4325B0_CBUCK_BURST		2	/* 0x00000004 */
#define RES4325B0_CBUCK_PWM		3	/* 0x00000008 */
#define RES4325B0_CLDO_PU		4	/* 0x00000010 */

/* 4325 C1 resources */
#define RES4325C1_LNLDO2_PU		12	/* 0x00001000 */

/* 4325 chip-specific ChipStatus register bits */
#define CST4325_SPROM_OTP_SEL_MASK	0x00000003
#define CST4325_DEFCIS_SEL		0	/* OTP is powered up, use def. CIS, no SPROM */
#define CST4325_SPROM_SEL		1	/* OTP is powered up, SPROM is present */
#define CST4325_OTP_SEL			2	/* OTP is powered up, no SPROM */
#define CST4325_OTP_PWRDN		3	/* OTP is powered down, SPROM is present */
#define CST4325_SDIO_USB_MODE_MASK	0x00000004
#define CST4325_SDIO_USB_MODE_SHIFT	2
#define CST4325_RCAL_VALID_MASK		0x00000008
#define CST4325_RCAL_VALID_SHIFT	3
#define CST4325_RCAL_VALUE_MASK		0x000001f0
#define CST4325_RCAL_VALUE_SHIFT	4
#define CST4325_PMUTOP_2B_MASK 		0x00000200	/* 1 for 2b, 0 for to 2a */
#define CST4325_PMUTOP_2B_SHIFT   	9

#define RES4329_RESERVED0		0	/* 0x00000001 */
#define RES4329_CBUCK_LPOM		1	/* 0x00000002 */
#define RES4329_CBUCK_BURST		2	/* 0x00000004 */
#define RES4329_CBUCK_PWM		3	/* 0x00000008 */
#define RES4329_CLDO_PU			4	/* 0x00000010 */
#define RES4329_PALDO_PU		5	/* 0x00000020 */
#define RES4329_ILP_REQUEST		6	/* 0x00000040 */
#define RES4329_RESERVED7		7	/* 0x00000080 */
#define RES4329_RESERVED8		8	/* 0x00000100 */
#define RES4329_LNLDO1_PU		9	/* 0x00000200 */
#define RES4329_OTP_PU			10	/* 0x00000400 */
#define RES4329_RESERVED11		11	/* 0x00000800 */
#define RES4329_LNLDO2_PU		12	/* 0x00001000 */
#define RES4329_XTAL_PU			13	/* 0x00002000 */
#define RES4329_ALP_AVAIL		14	/* 0x00004000 */
#define RES4329_RX_PWRSW_PU		15	/* 0x00008000 */
#define RES4329_TX_PWRSW_PU		16	/* 0x00010000 */
#define RES4329_RFPLL_PWRSW_PU		17	/* 0x00020000 */
#define RES4329_LOGEN_PWRSW_PU		18	/* 0x00040000 */
#define RES4329_AFE_PWRSW_PU		19	/* 0x00080000 */
#define RES4329_BBPLL_PWRSW_PU		20	/* 0x00100000 */
#define RES4329_HT_AVAIL		21	/* 0x00200000 */

#define CST4329_SPROM_OTP_SEL_MASK	0x00000003
#define CST4329_DEFCIS_SEL		0	/* OTP is powered up, use def. CIS, no SPROM */
#define CST4329_SPROM_SEL		1	/* OTP is powered up, SPROM is present */
#define CST4329_OTP_SEL			2	/* OTP is powered up, no SPROM */
#define CST4329_OTP_PWRDN		3	/* OTP is powered down, SPROM is present */
#define CST4329_SPI_SDIO_MODE_MASK	0x00000004
#define CST4329_SPI_SDIO_MODE_SHIFT	2

/* 4312 chip-specific ChipStatus register bits */
#define CST4312_SPROM_OTP_SEL_MASK	0x00000003
#define CST4312_DEFCIS_SEL		0	/* OTP is powered up, use def. CIS, no SPROM */
#define CST4312_SPROM_SEL		1	/* OTP is powered up, SPROM is present */
#define CST4312_OTP_SEL			2	/* OTP is powered up, no SPROM */
#define CST4312_OTP_BAD			3	/* OTP is broken, SPROM is present */

/* 4312 resources (all PMU chips with little memory constraint) */
#define RES4312_SWITCHER_BURST		0	/* 0x00000001 */
#define RES4312_SWITCHER_PWM    	1	/* 0x00000002 */
#define RES4312_PA_REF_LDO		2	/* 0x00000004 */
#define RES4312_CORE_LDO_BURST		3	/* 0x00000008 */
#define RES4312_CORE_LDO_PWM		4	/* 0x00000010 */
#define RES4312_RADIO_LDO		5	/* 0x00000020 */
#define RES4312_ILP_REQUEST		6	/* 0x00000040 */
#define RES4312_BG_FILTBYP		7	/* 0x00000080 */
#define RES4312_TX_FILTBYP		8	/* 0x00000100 */
#define RES4312_RX_FILTBYP		9	/* 0x00000200 */
#define RES4312_XTAL_PU			10	/* 0x00000400 */
#define RES4312_ALP_AVAIL		11	/* 0x00000800 */
#define RES4312_BB_PLL_FILTBYP		12	/* 0x00001000 */
#define RES4312_RF_PLL_FILTBYP		13	/* 0x00002000 */
#define RES4312_HT_AVAIL		14	/* 0x00004000 */

/* 4322 resources */
#define RES4322_RF_LDO			0
#define RES4322_ILP_REQUEST		1
#define RES4322_XTAL_PU			2
#define RES4322_ALP_AVAIL		3
#define RES4322_SI_PLL_ON		4
#define RES4322_HT_SI_AVAIL		5
#define RES4322_PHY_PLL_ON		6
#define RES4322_HT_PHY_AVAIL		7
#define RES4322_OTP_PU			8

/* 4322 chip-specific ChipStatus register bits */
#define CST4322_XTAL_FREQ_20_40MHZ	0x00000020
#define CST4322_SPROM_OTP_SEL_MASK	0x000000c0
#define CST4322_SPROM_OTP_SEL_SHIFT	6
#define CST4322_NO_SPROM_OTP		0	/* no OTP, no SPROM */
#define CST4322_SPROM_PRESENT		1	/* SPROM is present */
#define CST4322_OTP_PRESENT		2	/* OTP is present */
#define CST4322_PCI_OR_USB		0x00000100
#define CST4322_BOOT_MASK		0x00000600
#define CST4322_BOOT_SHIFT		9
#define CST4322_BOOT_FROM_SRAM		0	/* boot from SRAM, ARM in reset */
#define CST4322_BOOT_FROM_ROM		1	/* boot from ROM */
#define CST4322_BOOT_FROM_FLASH		2	/* boot from FLASH */
#define CST4322_BOOT_FROM_INVALID	3
#define CST4322_ILP_DIV_EN		0x00000800
#define CST4322_FLASH_TYPE_MASK		0x00001000
#define CST4322_FLASH_TYPE_SHIFT	12
#define CST4322_FLASH_TYPE_SHIFT_ST	0	/* ST serial FLASH */
#define CST4322_FLASH_TYPE_SHIFT_ATMEL	1	/* ATMEL flash */
#define CST4322_ARM_TAP_SEL		0x00002000
#define CST4322_RES_INIT_MODE_MASK	0x0000c000
#define CST4322_RES_INIT_MODE_SHIFT	14
#define CST4322_RES_INIT_MODE_ILPAVAIL	0	/* resinitmode: ILP available */
#define CST4322_RES_INIT_MODE_ILPREQ	1	/* resinitmode: ILP request */
#define CST4322_RES_INIT_MODE_ALPAVAIL	2	/* resinitmode: ALP available */
#define CST4322_RES_INIT_MODE_HTAVAIL	3	/* resinitmode: HT available */
#define CST4322_PCIPLLCLK_GATING	0x00010000
#define CST4322_CLK_SWITCH_PCI_TO_ALP	0x00020000
#define CST4322_PCI_CARDBUS_MODE	0x00040000

/* 43224 chip-specific ChipControl register bits */
#define CCTRL43224_GPIO_TOGGLE          0x8000 /* gpio[3:0] pins as btcoex or s/w gpio */
#define CCTRL_43224A0_12MA_LED_DRIVE    0x00F000F0 /* 12 mA drive strength */
#define CCTRL_43224B0_12MA_LED_DRIVE    0xF0    /* 12 mA drive strength for later 43224s */

/* 43236 resources */
#define RES43236_REGULATOR		0
#define RES43236_ILP_REQUEST		1
#define RES43236_XTAL_PU		2
#define RES43236_ALP_AVAIL		3
#define RES43236_SI_PLL_ON		4
#define RES43236_HT_SI_AVAIL		5

/* 43236 chip-specific ChipControl register bits */
#define CCTRL43236_BT_COEXIST		(1<<0)	/* 0 disable */
#define CCTRL43236_SECI			(1<<1)	/* 0 SECI is disabled (JATG functional) */
#define CCTRL43236_EXT_LNA		(1<<2)	/* 0 disable */
#define CCTRL43236_ANT_MUX_2o3          (1<<3)	/* 2o3 mux, chipcontrol bit 3 */
#define CCTRL43236_GSIO			(1<<4)	/* 0 disable */

/* 43236 Chip specific ChipStatus register bits */
#define CST43236_SFLASH_MASK		0x00000040
#define CST43236_OTP_SEL_MASK		0x00000080
#define CST43236_OTP_SEL_SHIFT		7
#define CST43236_HSIC_MASK		0x00000100	/* USB/HSIC */
#define CST43236_BP_CLK			0x00000200	/* 120/96Mbps */
#define CST43236_BOOT_MASK		0x00001800
#define CST43236_BOOT_SHIFT		11
#define CST43236_BOOT_FROM_SRAM		0	/* boot from SRAM, ARM in reset */
#define CST43236_BOOT_FROM_ROM		1	/* boot from ROM */
#define CST43236_BOOT_FROM_FLASH	2	/* boot from FLASH */
#define CST43236_BOOT_FROM_INVALID	3

/* 43237 resources */
#define RES43237_REGULATOR		0
#define RES43237_ILP_REQUEST		1
#define RES43237_XTAL_PU		2
#define RES43237_ALP_AVAIL		3
#define RES43237_SI_PLL_ON		4
#define RES43237_HT_SI_AVAIL		5

/* 43237 chip-specific ChipControl register bits */
#define CCTRL43237_BT_COEXIST		(1<<0)	/* 0 disable */
#define CCTRL43237_SECI			(1<<1)	/* 0 SECI is disabled (JATG functional) */
#define CCTRL43237_EXT_LNA		(1<<2)	/* 0 disable */
#define CCTRL43237_ANT_MUX_2o3          (1<<3)	/* 2o3 mux, chipcontrol bit 3 */
#define CCTRL43237_GSIO			(1<<4)	/* 0 disable */

/* 43237 Chip specific ChipStatus register bits */
#define CST43237_SFLASH_MASK		0x00000040
#define CST43237_OTP_SEL_MASK		0x00000080
#define CST43237_OTP_SEL_SHIFT		7
#define CST43237_HSIC_MASK		0x00000100	/* USB/HSIC */
#define CST43237_BP_CLK			0x00000200	/* 120/96Mbps */
#define CST43237_BOOT_MASK		0x00001800
#define CST43237_BOOT_SHIFT		11
#define CST43237_BOOT_FROM_SRAM		0	/* boot from SRAM, ARM in reset */
#define CST43237_BOOT_FROM_ROM		1	/* boot from ROM */
#define CST43237_BOOT_FROM_FLASH	2	/* boot from FLASH */
#define CST43237_BOOT_FROM_INVALID	3

/* 43239 resources */
#define RES43239_OTP_PU			9
#define RES43239_MACPHY_CLKAVAIL	23
#define RES43239_HT_AVAIL		24

/* 43239 Chip specific ChipStatus register bits */
#define CST43239_SPROM_MASK			0x00000002
#define CST43239_SFLASH_MASK		0x00000004
#define	CST43239_RES_INIT_MODE_SHIFT	7
#define	CST43239_RES_INIT_MODE_MASK		0x000001f0
#define CST43239_CHIPMODE_SDIOD(cs)	((cs) & (1 << 15))	/* SDIO || gSPI */
#define CST43239_CHIPMODE_USB20D(cs)	(~(cs) & (1 << 15))	/* USB || USBDA */
#define CST43239_CHIPMODE_SDIO(cs)	(((cs) & (1 << 0)) == 0)	/* SDIO */
#define CST43239_CHIPMODE_GSPI(cs)	(((cs) & (1 << 0)) == (1 << 0))	/* gSPI */

/* 4324 resources */
/* 43242 use same PMU as 4324 */
#define RES4324_LPLDO_PU			0
#define RES4324_RESET_PULLDN_DIS		1
#define RES4324_PMU_BG_PU			2
#define RES4324_HSIC_LDO_PU			3
#define RES4324_CBUCK_LPOM_PU			4
#define RES4324_CBUCK_PFM_PU			5
#define RES4324_CLDO_PU				6
#define RES4324_LPLDO2_LVM			7
#define RES4324_LNLDO1_PU			8
#define RES4324_LNLDO2_PU			9
#define RES4324_LDO3P3_PU			10
#define RES4324_OTP_PU				11
#define RES4324_XTAL_PU				12
#define RES4324_BBPLL_PU			13
#define RES4324_LQ_AVAIL			14
#define RES4324_WL_CORE_READY			17
#define RES4324_ILP_REQ				18
#define RES4324_ALP_AVAIL			19
#define RES4324_PALDO_PU			20
#define RES4324_RADIO_PU			21
#define RES4324_SR_CLK_STABLE			22
#define RES4324_SR_SAVE_RESTORE			23
#define RES4324_SR_PHY_PWRSW			24
#define RES4324_SR_PHY_PIC			25
#define RES4324_SR_SUBCORE_PWRSW		26
#define RES4324_SR_SUBCORE_PIC			27
#define RES4324_SR_MEM_PM0			28
#define RES4324_HT_AVAIL			29
#define RES4324_MACPHY_CLKAVAIL			30

/* 4324 Chip specific ChipStatus register bits */
#define CST4324_SPROM_MASK			0x00000080
#define CST4324_SFLASH_MASK			0x00400000
#define	CST4324_RES_INIT_MODE_SHIFT	10
#define	CST4324_RES_INIT_MODE_MASK	0x00000c00
#define CST4324_CHIPMODE_MASK		0x7
#define CST4324_CHIPMODE_SDIOD(cs)	((~(cs)) & (1 << 2))	/* SDIO || gSPI */
#define CST4324_CHIPMODE_USB20D(cs)	(((cs) & CST4324_CHIPMODE_MASK) == 0x6)	/* USB || USBDA */

/* 43242 Chip specific ChipStatus register bits */
#define CST43242_SFLASH_MASK                    0x00000008
#define CST43242_SR_HALT			(1<<25)
#define CST43242_SR_CHIP_STATUS_2		27 /* bit 27 */

/* 4331 resources */
#define RES4331_REGULATOR		0
#define RES4331_ILP_REQUEST		1
#define RES4331_XTAL_PU			2
#define RES4331_ALP_AVAIL		3
#define RES4331_SI_PLL_ON		4
#define RES4331_HT_SI_AVAIL		5

/* 4331 chip-specific ChipControl register bits */
#define CCTRL4331_BT_COEXIST		(1<<0)	/* 0 disable */
#define CCTRL4331_SECI			(1<<1)	/* 0 SECI is disabled (JATG functional) */
#define CCTRL4331_EXT_LNA_G		(1<<2)	/* 0 disable */
#define CCTRL4331_SPROM_GPIO13_15       (1<<3)  /* sprom/gpio13-15 mux */
#define CCTRL4331_EXTPA_EN		(1<<4)	/* 0 ext pa disable, 1 ext pa enabled */
#define CCTRL4331_GPIOCLK_ON_SPROMCS	(1<<5)	/* set drive out GPIO_CLK on sprom_cs pin */
#define CCTRL4331_PCIE_MDIO_ON_SPROMCS	(1<<6)	/* use sprom_cs pin as PCIE mdio interface */
#define CCTRL4331_EXTPA_ON_GPIO2_5	(1<<7)	/* aband extpa will be at gpio2/5 and sprom_dout */
#define CCTRL4331_OVR_PIPEAUXCLKEN	(1<<8)	/* override core control on pipe_AuxClkEnable */
#define CCTRL4331_OVR_PIPEAUXPWRDOWN	(1<<9)	/* override core control on pipe_AuxPowerDown */
#define CCTRL4331_PCIE_AUXCLKEN		(1<<10)	/* pcie_auxclkenable */
#define CCTRL4331_PCIE_PIPE_PLLDOWN	(1<<11)	/* pcie_pipe_pllpowerdown */
#define CCTRL4331_EXTPA_EN2		(1<<12)	/* 0 ext pa disable, 1 ext pa enabled */
#define CCTRL4331_EXT_LNA_A		(1<<13)	/* 0 disable */
#define CCTRL4331_BT_SHD0_ON_GPIO4	(1<<16)	/* enable bt_shd0 at gpio4 */
#define CCTRL4331_BT_SHD1_ON_GPIO5	(1<<17)	/* enable bt_shd1 at gpio5 */
#define CCTRL4331_EXTPA_ANA_EN		(1<<24)	/* 0 ext pa disable, 1 ext pa enabled */

/* 4331 Chip specific ChipStatus register bits */
#define	CST4331_XTAL_FREQ		0x00000001	/* crystal frequency 20/40Mhz */
#define	CST4331_SPROM_OTP_SEL_MASK	0x00000006
#define	CST4331_SPROM_OTP_SEL_SHIFT	1
#define	CST4331_SPROM_PRESENT		0x00000002
#define	CST4331_OTP_PRESENT		0x00000004
#define	CST4331_LDO_RF			0x00000008
#define	CST4331_LDO_PAR			0x00000010

/* 4315 resource */
#define RES4315_CBUCK_LPOM		1	/* 0x00000002 */
#define RES4315_CBUCK_BURST		2	/* 0x00000004 */
#define RES4315_CBUCK_PWM		3	/* 0x00000008 */
#define RES4315_CLDO_PU			4	/* 0x00000010 */
#define RES4315_PALDO_PU		5	/* 0x00000020 */
#define RES4315_ILP_REQUEST		6	/* 0x00000040 */
#define RES4315_LNLDO1_PU		9	/* 0x00000200 */
#define RES4315_OTP_PU			10	/* 0x00000400 */
#define RES4315_LNLDO2_PU		12	/* 0x00001000 */
#define RES4315_XTAL_PU			13	/* 0x00002000 */
#define RES4315_ALP_AVAIL		14	/* 0x00004000 */
#define RES4315_RX_PWRSW_PU		15	/* 0x00008000 */
#define RES4315_TX_PWRSW_PU		16	/* 0x00010000 */
#define RES4315_RFPLL_PWRSW_PU		17	/* 0x00020000 */
#define RES4315_LOGEN_PWRSW_PU		18	/* 0x00040000 */
#define RES4315_AFE_PWRSW_PU		19	/* 0x00080000 */
#define RES4315_BBPLL_PWRSW_PU		20	/* 0x00100000 */
#define RES4315_HT_AVAIL		21	/* 0x00200000 */

/* 4315 chip-specific ChipStatus register bits */
#define CST4315_SPROM_OTP_SEL_MASK	0x00000003	/* gpio [7:6], SDIO CIS selection */
#define CST4315_DEFCIS_SEL		0x00000000	/* use default CIS, OTP is powered up */
#define CST4315_SPROM_SEL		0x00000001	/* use SPROM, OTP is powered up */
#define CST4315_OTP_SEL			0x00000002	/* use OTP, OTP is powered up */
#define CST4315_OTP_PWRDN		0x00000003	/* use SPROM, OTP is powered down */
#define CST4315_SDIO_MODE		0x00000004	/* gpio [8], sdio/usb mode */
#define CST4315_RCAL_VALID		0x00000008
#define CST4315_RCAL_VALUE_MASK		0x000001f0
#define CST4315_RCAL_VALUE_SHIFT	4
#define CST4315_PALDO_EXTPNP		0x00000200	/* PALDO is configured with external PNP */
#define CST4315_CBUCK_MODE_MASK		0x00000c00
#define CST4315_CBUCK_MODE_BURST	0x00000400
#define CST4315_CBUCK_MODE_LPBURST	0x00000c00

/* 4319 resources */
#define RES4319_CBUCK_LPOM		1	/* 0x00000002 */
#define RES4319_CBUCK_BURST		2	/* 0x00000004 */
#define RES4319_CBUCK_PWM		3	/* 0x00000008 */
#define RES4319_CLDO_PU			4	/* 0x00000010 */
#define RES4319_PALDO_PU		5	/* 0x00000020 */
#define RES4319_ILP_REQUEST		6	/* 0x00000040 */
#define RES4319_LNLDO1_PU		9	/* 0x00000200 */
#define RES4319_OTP_PU			10	/* 0x00000400 */
#define RES4319_LNLDO2_PU		12	/* 0x00001000 */
#define RES4319_XTAL_PU			13	/* 0x00002000 */
#define RES4319_ALP_AVAIL		14	/* 0x00004000 */
#define RES4319_RX_PWRSW_PU		15	/* 0x00008000 */
#define RES4319_TX_PWRSW_PU		16	/* 0x00010000 */
#define RES4319_RFPLL_PWRSW_PU		17	/* 0x00020000 */
#define RES4319_LOGEN_PWRSW_PU		18	/* 0x00040000 */
#define RES4319_AFE_PWRSW_PU		19	/* 0x00080000 */
#define RES4319_BBPLL_PWRSW_PU		20	/* 0x00100000 */
#define RES4319_HT_AVAIL		21	/* 0x00200000 */

/* 4319 chip-specific ChipStatus register bits */
#define	CST4319_SPI_CPULESSUSB		0x00000001
#define	CST4319_SPI_CLK_POL		0x00000002
#define	CST4319_SPI_CLK_PH		0x00000008
#define	CST4319_SPROM_OTP_SEL_MASK	0x000000c0	/* gpio [7:6], SDIO CIS selection */
#define	CST4319_SPROM_OTP_SEL_SHIFT	6
#define	CST4319_DEFCIS_SEL		0x00000000	/* use default CIS, OTP is powered up */
#define	CST4319_SPROM_SEL		0x00000040	/* use SPROM, OTP is powered up */
#define	CST4319_OTP_SEL			0x00000080      /* use OTP, OTP is powered up */
#define	CST4319_OTP_PWRDN		0x000000c0      /* use SPROM, OTP is powered down */
#define	CST4319_SDIO_USB_MODE		0x00000100	/* gpio [8], sdio/usb mode */
#define	CST4319_REMAP_SEL_MASK		0x00000600
#define	CST4319_ILPDIV_EN		0x00000800
#define	CST4319_XTAL_PD_POL		0x00001000
#define	CST4319_LPO_SEL			0x00002000
#define	CST4319_RES_INIT_MODE		0x0000c000
#define	CST4319_PALDO_EXTPNP		0x00010000	/* PALDO is configured with external PNP */
#define	CST4319_CBUCK_MODE_MASK		0x00060000
#define CST4319_CBUCK_MODE_BURST	0x00020000
#define CST4319_CBUCK_MODE_LPBURST	0x00060000
#define	CST4319_RCAL_VALID		0x01000000
#define	CST4319_RCAL_VALUE_MASK		0x3e000000
#define	CST4319_RCAL_VALUE_SHIFT	25

#define PMU1_PLL0_CHIPCTL0		0
#define PMU1_PLL0_CHIPCTL1		1
#define PMU1_PLL0_CHIPCTL2		2
#define CCTL_4319USB_XTAL_SEL_MASK	0x00180000
#define CCTL_4319USB_XTAL_SEL_SHIFT	19
#define CCTL_4319USB_48MHZ_PLL_SEL	1
#define CCTL_4319USB_24MHZ_PLL_SEL	2

/* PMU resources for 4336 */
#define	RES4336_CBUCK_LPOM		0
#define	RES4336_CBUCK_BURST		1
#define	RES4336_CBUCK_LP_PWM		2
#define	RES4336_CBUCK_PWM		3
#define	RES4336_CLDO_PU			4
#define	RES4336_DIS_INT_RESET_PD	5
#define	RES4336_ILP_REQUEST		6
#define	RES4336_LNLDO_PU		7
#define	RES4336_LDO3P3_PU		8
#define	RES4336_OTP_PU			9
#define	RES4336_XTAL_PU			10
#define	RES4336_ALP_AVAIL		11
#define	RES4336_RADIO_PU		12
#define	RES4336_BG_PU			13
#define	RES4336_VREG1p4_PU_PU		14
#define	RES4336_AFE_PWRSW_PU		15
#define	RES4336_RX_PWRSW_PU		16
#define	RES4336_TX_PWRSW_PU		17
#define	RES4336_BB_PWRSW_PU		18
#define	RES4336_SYNTH_PWRSW_PU		19
#define	RES4336_MISC_PWRSW_PU		20
#define	RES4336_LOGEN_PWRSW_PU		21
#define	RES4336_BBPLL_PWRSW_PU		22
#define	RES4336_MACPHY_CLKAVAIL		23
#define	RES4336_HT_AVAIL		24
#define	RES4336_RSVD			25

/* 4336 chip-specific ChipStatus register bits */
#define	CST4336_SPI_MODE_MASK		0x00000001
#define	CST4336_SPROM_PRESENT		0x00000002
#define	CST4336_OTP_PRESENT		0x00000004
#define	CST4336_ARMREMAP_0		0x00000008
#define	CST4336_ILPDIV_EN_MASK		0x00000010
#define	CST4336_ILPDIV_EN_SHIFT		4
#define	CST4336_XTAL_PD_POL_MASK	0x00000020
#define	CST4336_XTAL_PD_POL_SHIFT	5
#define	CST4336_LPO_SEL_MASK		0x00000040
#define	CST4336_LPO_SEL_SHIFT		6
#define	CST4336_RES_INIT_MODE_MASK	0x00000180
#define	CST4336_RES_INIT_MODE_SHIFT	7
#define	CST4336_CBUCK_MODE_MASK		0x00000600
#define	CST4336_CBUCK_MODE_SHIFT	9

/* 4336 Chip specific PMU ChipControl register bits */
#define PCTL_4336_SERIAL_ENAB	(1  << 24)

/* 4330 resources */
#define	RES4330_CBUCK_LPOM		0
#define	RES4330_CBUCK_BURST		1
#define	RES4330_CBUCK_LP_PWM		2
#define	RES4330_CBUCK_PWM		3
#define	RES4330_CLDO_PU			4
#define	RES4330_DIS_INT_RESET_PD	5
#define	RES4330_ILP_REQUEST		6
#define	RES4330_LNLDO_PU		7
#define	RES4330_LDO3P3_PU		8
#define	RES4330_OTP_PU			9
#define	RES4330_XTAL_PU			10
#define	RES4330_ALP_AVAIL		11
#define	RES4330_RADIO_PU		12
#define	RES4330_BG_PU			13
#define	RES4330_VREG1p4_PU_PU		14
#define	RES4330_AFE_PWRSW_PU		15
#define	RES4330_RX_PWRSW_PU		16
#define	RES4330_TX_PWRSW_PU		17
#define	RES4330_BB_PWRSW_PU		18
#define	RES4330_SYNTH_PWRSW_PU		19
#define	RES4330_MISC_PWRSW_PU		20
#define	RES4330_LOGEN_PWRSW_PU		21
#define	RES4330_BBPLL_PWRSW_PU		22
#define	RES4330_MACPHY_CLKAVAIL		23
#define	RES4330_HT_AVAIL		24
#define	RES4330_5gRX_PWRSW_PU		25
#define	RES4330_5gTX_PWRSW_PU		26
#define	RES4330_5g_LOGEN_PWRSW_PU	27

/* 4330 chip-specific ChipStatus register bits */
#define CST4330_CHIPMODE_SDIOD(cs)	(((cs) & 0x7) < 6)	/* SDIO || gSPI */
#define CST4330_CHIPMODE_USB20D(cs)	(((cs) & 0x7) >= 6)	/* USB || USBDA */
#define CST4330_CHIPMODE_SDIO(cs)	(((cs) & 0x4) == 0)	/* SDIO */
#define CST4330_CHIPMODE_GSPI(cs)	(((cs) & 0x6) == 4)	/* gSPI */
#define CST4330_CHIPMODE_USB(cs)	(((cs) & 0x7) == 6)	/* USB packet-oriented */
#define CST4330_CHIPMODE_USBDA(cs)	(((cs) & 0x7) == 7)	/* USB Direct Access */
#define	CST4330_OTP_PRESENT		0x00000010
#define	CST4330_LPO_AUTODET_EN		0x00000020
#define	CST4330_ARMREMAP_0		0x00000040
#define	CST4330_SPROM_PRESENT		0x00000080	/* takes priority over OTP if both set */
#define	CST4330_ILPDIV_EN		0x00000100
#define	CST4330_LPO_SEL			0x00000200
#define	CST4330_RES_INIT_MODE_SHIFT	10
#define	CST4330_RES_INIT_MODE_MASK	0x00000c00
#define CST4330_CBUCK_MODE_SHIFT	12
#define CST4330_CBUCK_MODE_MASK		0x00003000
#define	CST4330_CBUCK_POWER_OK		0x00004000
#define	CST4330_BB_PLL_LOCKED		0x00008000
#define SOCDEVRAM_BP_ADDR		0x1E000000
#define SOCDEVRAM_ARM_ADDR		0x00800000

/* 4330 Chip specific PMU ChipControl register bits */
#define PCTL_4330_SERIAL_ENAB	(1  << 24)

/* 4330 Chip specific ChipControl register bits */
#define CCTRL_4330_GPIO_SEL		0x00000001    /* 1=select GPIOs to be muxed out */
#define CCTRL_4330_ERCX_SEL		0x00000002    /* 1=select ERCX BT coex to be muxed out */
#define CCTRL_4330_SDIO_HOST_WAKE	0x00000004    /* SDIO: 1=configure GPIO0 for host wake */
#define CCTRL_4330_JTAG_DISABLE	0x00000008    /* 1=disable JTAG interface on mux'd pins */

#define PMU_VREG0_ADDR				0
#define PMU_VREG0_DISABLE_PULLD_BT_SHIFT	2
#define PMU_VREG0_DISABLE_PULLD_WL_SHIFT	3

#define PMU_VREG4_ADDR			4

#define PMU_VREG4_CLDO_PWM_SHIFT	4
#define PMU_VREG4_CLDO_PWM_MASK		0x7

#define PMU_VREG4_LPLDO1_SHIFT		15
#define PMU_VREG4_LPLDO1_MASK		0x7
#define PMU_VREG4_LPLDO1_1p20V		0
#define PMU_VREG4_LPLDO1_1p15V		1
#define PMU_VREG4_LPLDO1_1p10V		2
#define PMU_VREG4_LPLDO1_1p25V		3
#define PMU_VREG4_LPLDO1_1p05V		4
#define PMU_VREG4_LPLDO1_1p00V		5
#define PMU_VREG4_LPLDO1_0p95V		6
#define PMU_VREG4_LPLDO1_0p90V		7

/* 4350/4345 VREG4 settings */
#define PMU4350_VREG4_LPLDO1_1p10V	0
#define PMU4350_VREG4_LPLDO1_1p15V	1
#define PMU4350_VREG4_LPLDO1_1p21V	2
#define PMU4350_VREG4_LPLDO1_1p24V	3
#define PMU4350_VREG4_LPLDO1_0p90V	4
#define PMU4350_VREG4_LPLDO1_0p96V	5
#define PMU4350_VREG4_LPLDO1_1p01V	6
#define PMU4350_VREG4_LPLDO1_1p04V	7

#define PMU_VREG4_LPLDO2_LVM_SHIFT	18
#define PMU_VREG4_LPLDO2_LVM_MASK	0x7
#define PMU_VREG4_LPLDO2_HVM_SHIFT	21
#define PMU_VREG4_LPLDO2_HVM_MASK	0x7
#define PMU_VREG4_LPLDO2_LVM_HVM_MASK	0x3f
#define PMU_VREG4_LPLDO2_1p00V		0
#define PMU_VREG4_LPLDO2_1p15V		1
#define PMU_VREG4_LPLDO2_1p20V		2
#define PMU_VREG4_LPLDO2_1p10V		3
#define PMU_VREG4_LPLDO2_0p90V		4	/* 4 - 7 is 0.90V */

#define PMU_VREG4_HSICLDO_BYPASS_SHIFT	27
#define PMU_VREG4_HSICLDO_BYPASS_MASK	0x1

#define PMU_VREG5_ADDR			5
#define PMU_VREG5_HSICAVDD_PD_SHIFT	6
#define PMU_VREG5_HSICAVDD_PD_MASK	0x1
#define PMU_VREG5_HSICDVDD_PD_SHIFT	11
#define PMU_VREG5_HSICDVDD_PD_MASK	0x1

/* 4334 resources */
#define RES4334_LPLDO_PU		0
#define RES4334_RESET_PULLDN_DIS	1
#define RES4334_PMU_BG_PU		2
#define RES4334_HSIC_LDO_PU		3
#define RES4334_CBUCK_LPOM_PU		4
#define RES4334_CBUCK_PFM_PU		5
#define RES4334_CLDO_PU			6
#define RES4334_LPLDO2_LVM		7
#define RES4334_LNLDO_PU		8
#define RES4334_LDO3P3_PU		9
#define RES4334_OTP_PU			10
#define RES4334_XTAL_PU			11
#define RES4334_WL_PWRSW_PU		12
#define RES4334_LQ_AVAIL		13
#define RES4334_LOGIC_RET		14
#define RES4334_MEM_SLEEP		15
#define RES4334_MACPHY_RET		16
#define RES4334_WL_CORE_READY		17
#define RES4334_ILP_REQ			18
#define RES4334_ALP_AVAIL		19
#define RES4334_MISC_PWRSW_PU		20
#define RES4334_SYNTH_PWRSW_PU		21
#define RES4334_RX_PWRSW_PU		22
#define RES4334_RADIO_PU		23
#define RES4334_WL_PMU_PU		24
#define RES4334_VCO_LDO_PU		25
#define RES4334_AFE_LDO_PU		26
#define RES4334_RX_LDO_PU		27
#define RES4334_TX_LDO_PU		28
#define RES4334_HT_AVAIL		29
#define RES4334_MACPHY_CLK_AVAIL	30

/* 4334 chip-specific ChipStatus register bits */
#define CST4334_CHIPMODE_MASK		7
#define CST4334_SDIO_MODE		0x00000000
#define CST4334_SPI_MODE		0x00000004
#define CST4334_HSIC_MODE		0x00000006
#define CST4334_BLUSB_MODE		0x00000007
#define CST4334_CHIPMODE_HSIC(cs)	(((cs) & CST4334_CHIPMODE_MASK) == CST4334_HSIC_MODE)
#define CST4334_OTP_PRESENT		0x00000010
#define CST4334_LPO_AUTODET_EN		0x00000020
#define CST4334_ARMREMAP_0		0x00000040
#define CST4334_SPROM_PRESENT		0x00000080
#define CST4334_ILPDIV_EN_MASK		0x00000100
#define CST4334_ILPDIV_EN_SHIFT		8
#define CST4334_LPO_SEL_MASK		0x00000200
#define CST4334_LPO_SEL_SHIFT		9
#define CST4334_RES_INIT_MODE_MASK	0x00000C00
#define CST4334_RES_INIT_MODE_SHIFT	10

/* 4334 Chip specific PMU ChipControl register bits */
#define PCTL_4334_GPIO3_ENAB    (1  << 3)

/* 4334 Chip control */
#define CCTRL4334_PMU_WAKEUP_GPIO1	(1  << 0)
#define CCTRL4334_PMU_WAKEUP_HSIC	(1  << 1)
#define CCTRL4334_PMU_WAKEUP_AOS	(1  << 2)
#define CCTRL4334_HSIC_WAKE_MODE	(1  << 3)
#define CCTRL4334_HSIC_INBAND_GPIO1	(1  << 4)
#define CCTRL4334_HSIC_LDO_PU		(1  << 23)

/* 4334 Chip control 3 */
#define CCTRL4334_BLOCK_EXTRNL_WAKE		(1  << 4)
#define CCTRL4334_SAVERESTORE_FIX		(1  << 5)

/* 43341 Chip control 3 */
#define CCTRL43341_BLOCK_EXTRNL_WAKE		(1  << 13)
#define CCTRL43341_SAVERESTORE_FIX		(1  << 14)
#define CCTRL43341_BT_ISO_SEL			(1  << 16)

/* 4334 Chip specific ChipControl1 register bits */
#define CCTRL1_4334_GPIO_SEL		(1 << 0)    /* 1=select GPIOs to be muxed out */
#define CCTRL1_4334_ERCX_SEL		(1 << 1)    /* 1=select ERCX BT coex to be muxed out */
#define CCTRL1_4334_SDIO_HOST_WAKE (1 << 2)  /* SDIO: 1=configure GPIO0 for host wake */
#define CCTRL1_4334_JTAG_DISABLE	(1 << 3)    /* 1=disable JTAG interface on mux'd pins */
#define CCTRL1_4334_UART_ON_4_5	(1 << 28)  	/* 1=UART_TX/UART_RX muxed on GPIO_4/5 (4334B0/1) */

/* 4324 Chip specific ChipControl1 register bits */
#define CCTRL1_4324_GPIO_SEL            (1 << 0)    /* 1=select GPIOs to be muxed out */
#define CCTRL1_4324_SDIO_HOST_WAKE (1 << 2)  /* SDIO: 1=configure GPIO0 for host wake */

/* 43143 chip-specific ChipStatus register bits based on Confluence documentation */
/* register contains strap values sampled during POR */
#define CST43143_REMAP_TO_ROM	 (3 << 0)    /* 00=Boot SRAM, 01=Boot ROM, 10=Boot SFLASH */
#define CST43143_SDIO_EN	 (1 << 2)    /* 0 = USB Enab, SDIO pins are GPIO or I2S */
#define CST43143_SDIO_ISO	 (1 << 3)    /* 1 = SDIO isolated */
#define CST43143_USB_CPU_LESS	 (1 << 4)   /* 1 = CPULess mode Enabled */
#define CST43143_CBUCK_MODE	 (3 << 6)   /* Indicates what controller mode CBUCK is in */
#define CST43143_POK_CBUCK	 (1 << 8)   /* 1 = 1.2V CBUCK voltage ready */
#define CST43143_PMU_OVRSPIKE	 (1 << 9)
#define CST43143_PMU_OVRTEMP	 (0xF << 10)
#define CST43143_SR_FLL_CAL_DONE (1 << 14)
#define CST43143_USB_PLL_LOCKDET (1 << 15)
#define CST43143_PMU_PLL_LOCKDET (1 << 16)
#define CST43143_CHIPMODE_SDIOD(cs)	(((cs) & CST43143_SDIO_EN) != 0) /* SDIO */

/* 43143 Chip specific ChipControl register bits */
/* 00: SECI is disabled (JATG functional), 01: 2 wire, 10: 4 wire  */
#define CCTRL_43143_SECI		(1<<0)
#define CCTRL_43143_BT_LEGACY		(1<<1)
#define CCTRL_43143_I2S_MODE		(1<<2)	/* 0: SDIO enabled */
#define CCTRL_43143_I2S_MASTER		(1<<3)	/* 0: I2S MCLK input disabled */
#define CCTRL_43143_I2S_FULL		(1<<4)	/* 0: I2S SDIN and SPDIF_TX inputs disabled */
#define CCTRL_43143_GSIO		(1<<5)	/* 0: sFlash enabled */
#define CCTRL_43143_RF_SWCTRL_MASK	(7<<6)	/* 0: disabled */
#define CCTRL_43143_RF_SWCTRL_0		(1<<6)
#define CCTRL_43143_RF_SWCTRL_1		(2<<6)
#define CCTRL_43143_RF_SWCTRL_2		(4<<6)
#define CCTRL_43143_RF_XSWCTRL		(1<<9)	/* 0: UART enabled */
#define CCTRL_43143_HOST_WAKE0		(1<<11)	/* 1: SDIO separate interrupt output from GPIO4 */
#define CCTRL_43143_HOST_WAKE1		(1<<12)	/* 1: SDIO separate interrupt output from GPIO16 */

/* 43143 resources, based on pmu_params.xls V1.19 */
#define RES43143_EXT_SWITCHER_PWM	0	/* 0x00001 */
#define RES43143_XTAL_PU		1	/* 0x00002 */
#define RES43143_ILP_REQUEST		2	/* 0x00004 */
#define RES43143_ALP_AVAIL		3	/* 0x00008 */
#define RES43143_WL_CORE_READY		4	/* 0x00010 */
#define RES43143_BBPLL_PWRSW_PU		5	/* 0x00020 */
#define RES43143_HT_AVAIL		6	/* 0x00040 */
#define RES43143_RADIO_PU		7	/* 0x00080 */
#define RES43143_MACPHY_CLK_AVAIL	8	/* 0x00100 */
#define RES43143_OTP_PU			9	/* 0x00200 */
#define RES43143_LQ_AVAIL		10	/* 0x00400 */

#define PMU43143_XTAL_CORE_SIZE_MASK	0x3F

/* 4313 resources */
#define	RES4313_BB_PU_RSRC		0
#define	RES4313_ILP_REQ_RSRC		1
#define	RES4313_XTAL_PU_RSRC		2
#define	RES4313_ALP_AVAIL_RSRC		3
#define	RES4313_RADIO_PU_RSRC		4
#define	RES4313_BG_PU_RSRC		5
#define	RES4313_VREG1P4_PU_RSRC		6
#define	RES4313_AFE_PWRSW_RSRC		7
#define	RES4313_RX_PWRSW_RSRC		8
#define	RES4313_TX_PWRSW_RSRC		9
#define	RES4313_BB_PWRSW_RSRC		10
#define	RES4313_SYNTH_PWRSW_RSRC	11
#define	RES4313_MISC_PWRSW_RSRC		12
#define	RES4313_BB_PLL_PWRSW_RSRC	13
#define	RES4313_HT_AVAIL_RSRC		14
#define	RES4313_MACPHY_CLK_AVAIL_RSRC	15

/* 4313 chip-specific ChipStatus register bits */
#define	CST4313_SPROM_PRESENT			1
#define	CST4313_OTP_PRESENT			2
#define	CST4313_SPROM_OTP_SEL_MASK		0x00000002
#define	CST4313_SPROM_OTP_SEL_SHIFT		0

/* 4313 Chip specific ChipControl register bits */
#define CCTRL_4313_12MA_LED_DRIVE    0x00000007    /* 12 mA drive strengh for later 4313 */

/* PMU respources for 4314 */
#define RES4314_LPLDO_PU		0
#define RES4314_PMU_SLEEP_DIS		1
#define RES4314_PMU_BG_PU		2
#define RES4314_CBUCK_LPOM_PU		3
#define RES4314_CBUCK_PFM_PU		4
#define RES4314_CLDO_PU			5
#define RES4314_LPLDO2_LVM		6
#define RES4314_WL_PMU_PU		7
#define RES4314_LNLDO_PU		8
#define RES4314_LDO3P3_PU		9
#define RES4314_OTP_PU			10
#define RES4314_XTAL_PU			11
#define RES4314_WL_PWRSW_PU		12
#define RES4314_LQ_AVAIL		13
#define RES4314_LOGIC_RET		14
#define RES4314_MEM_SLEEP		15
#define RES4314_MACPHY_RET		16
#define RES4314_WL_CORE_READY		17
#define RES4314_ILP_REQ			18
#define RES4314_ALP_AVAIL		19
#define RES4314_MISC_PWRSW_PU		20
#define RES4314_SYNTH_PWRSW_PU		21
#define RES4314_RX_PWRSW_PU		22
#define RES4314_RADIO_PU		23
#define RES4314_VCO_LDO_PU		24
#define RES4314_AFE_LDO_PU		25
#define RES4314_RX_LDO_PU		26
#define RES4314_TX_LDO_PU		27
#define RES4314_HT_AVAIL		28
#define RES4314_MACPHY_CLK_AVAIL	29

/* 4314 chip-specific ChipStatus register bits */
#define CST4314_OTP_ENABLED		0x00200000

/* 43228 resources */
#define RES43228_NOT_USED		0
#define RES43228_ILP_REQUEST		1
#define RES43228_XTAL_PU		2
#define RES43228_ALP_AVAIL		3
#define RES43228_PLL_EN			4
#define RES43228_HT_PHY_AVAIL		5

/* 43228 chipstatus  reg bits */
#define CST43228_ILP_DIV_EN		0x1
#define	CST43228_OTP_PRESENT		0x2
#define	CST43228_SERDES_REFCLK_PADSEL	0x4
#define	CST43228_SDIO_MODE		0x8
#define	CST43228_SDIO_OTP_PRESENT	0x10
#define	CST43228_SDIO_RESET		0x20

/* 4706 chipstatus reg bits */
#define	CST4706_PKG_OPTION		(1<<0) /* 0: full-featured package 1: low-cost package */
#define	CST4706_SFLASH_PRESENT	(1<<1) /* 0: parallel, 1: serial flash is present */
#define	CST4706_SFLASH_TYPE		(1<<2) /* 0: 8b-p/ST-s flash, 1: 16b-p/Atmal-s flash */
#define	CST4706_MIPS_BENDIAN	(1<<3) /* 0: little,  1: big endian */
#define	CST4706_PCIE1_DISABLE	(1<<5) /* PCIE1 enable strap pin */

/* 4706 flashstrconfig reg bits */
#define FLSTRCF4706_MASK		0x000000ff
#define FLSTRCF4706_SF1			0x00000001	/* 2nd serial flash present */
#define FLSTRCF4706_PF1			0x00000002	/* 2nd parallel flash present */
#define FLSTRCF4706_SF1_TYPE	0x00000004	/* 2nd serial flash type : 0 : ST, 1 : Atmel */
#define FLSTRCF4706_NF1			0x00000008	/* 2nd NAND flash present */
#define FLSTRCF4706_1ST_MADDR_SEG_MASK		0x000000f0	/* Valid value mask */
#define FLSTRCF4706_1ST_MADDR_SEG_4MB		0x00000010	/* 4MB */
#define FLSTRCF4706_1ST_MADDR_SEG_8MB		0x00000020	/* 8MB */
#define FLSTRCF4706_1ST_MADDR_SEG_16MB		0x00000030	/* 16MB */
#define FLSTRCF4706_1ST_MADDR_SEG_32MB		0x00000040	/* 32MB */
#define FLSTRCF4706_1ST_MADDR_SEG_64MB		0x00000050	/* 64MB */
#define FLSTRCF4706_1ST_MADDR_SEG_128MB		0x00000060	/* 128MB */
#define FLSTRCF4706_1ST_MADDR_SEG_256MB		0x00000070	/* 256MB */

/* 4360 Chip specific ChipControl register bits */
#define CCTRL4360_I2C_MODE			(1 << 0)
#define CCTRL4360_UART_MODE			(1 << 1)
#define CCTRL4360_SECI_MODE			(1 << 2)
#define CCTRL4360_BTSWCTRL_MODE			(1 << 3)
#define CCTRL4360_DISCRETE_FEMCTRL_MODE		(1 << 4)
#define CCTRL4360_DIGITAL_PACTRL_MODE		(1 << 5)
#define CCTRL4360_BTSWCTRL_AND_DIGPA_PRESENT	(1 << 6)
#define CCTRL4360_EXTRA_GPIO_MODE		(1 << 7)
#define CCTRL4360_EXTRA_FEMCTRL_MODE		(1 << 8)
#define CCTRL4360_BT_LGCY_MODE			(1 << 9)
#define CCTRL4360_CORE2FEMCTRL4_ON		(1 << 21)
#define CCTRL4360_SECI_ON_GPIO01		(1 << 24)

/* 4360 Chip specific Regulator Control register bits */
#define RCTRL4360_RFLDO_PWR_DOWN		(1 << 1)

/* 4360 PMU resources and chip status bits */
#define RES4360_REGULATOR          0
#define RES4360_ILP_AVAIL          1
#define RES4360_ILP_REQ            2
#define RES4360_XTAL_LDO_PU        3
#define RES4360_XTAL_PU            4
#define RES4360_ALP_AVAIL          5
#define RES4360_BBPLLPWRSW_PU      6
#define RES4360_HT_AVAIL           7
#define RES4360_OTP_PU             8

#define CST4360_XTAL_40MZ                  0x00000001
#define CST4360_SFLASH                     0x00000002
#define CST4360_SPROM_PRESENT              0x00000004
#define CST4360_SFLASH_TYPE                0x00000004
#define CST4360_OTP_ENABLED                0x00000008
#define CST4360_REMAP_ROM                  0x00000010
#define CST4360_RSRC_INIT_MODE_MASK        0x00000060
#define CST4360_RSRC_INIT_MODE_SHIFT       5
#define CST4360_ILP_DIVEN                  0x00000080
#define CST4360_MODE_USB                   0x00000100
#define CST4360_SPROM_SIZE_MASK            0x00000600
#define CST4360_SPROM_SIZE_SHIFT           9
#define CST4360_BBPLL_LOCK                 0x00000800
#define CST4360_AVBBPLL_LOCK               0x00001000
#define CST4360_USBBBPLL_LOCK              0x00002000
#define CST4360_RSRC_INIT_MODE(cs)	((cs & CST4360_RSRC_INIT_MODE_MASK) >> \
					CST4360_RSRC_INIT_MODE_SHIFT)

#define CCTRL_4360_UART_SEL	0x2
#define CST4360_RSRC_INIT_MODE(cs)	((cs & CST4360_RSRC_INIT_MODE_MASK) >> \
					CST4360_RSRC_INIT_MODE_SHIFT)


/* 43602 PMU resources based on pmu_params.xls version v0.95 */
#define RES43602_LPLDO_PU		0
#define RES43602_REGULATOR		1
#define RES43602_PMU_SLEEP		2
#define RES43602_RSVD_3			3
#define RES43602_XTALLDO_PU		4
#define RES43602_SERDES_PU		5
#define RES43602_BBPLL_PWRSW_PU		6
#define RES43602_SR_CLK_START		7
#define RES43602_SR_PHY_PWRSW		8
#define RES43602_SR_SUBCORE_PWRSW	9
#define RES43602_XTAL_PU		10
#define	RES43602_PERST_OVR		11
#define RES43602_SR_CLK_STABLE		12
#define RES43602_SR_SAVE_RESTORE	13
#define RES43602_SR_SLEEP		14
#define RES43602_LQ_START		15
#define RES43602_LQ_AVAIL		16
#define RES43602_WL_CORE_RDY		17
#define RES43602_ILP_REQ		18
#define RES43602_ALP_AVAIL		19
#define RES43602_RADIO_PU		20
#define RES43602_RFLDO_PU		21
#define RES43602_HT_START		22
#define RES43602_HT_AVAIL		23
#define RES43602_MACPHY_CLKAVAIL	24
#define RES43602_PARLDO_PU		25
#define RES43602_RSVD_26		26

/* 43602 chip status bits */
#define CST43602_SPROM_PRESENT             (1<<1)
#define CST43602_SPROM_SIZE                (1<<10) /* 0 = 16K, 1 = 4K */
#define CST43602_BBPLL_LOCK                (1<<11)
#define CST43602_RF_LDO_OUT_OK             (1<<15) /* RF LDO output OK */

#define PMU43602_CC1_GPIO12_OVRD           (1<<28) /* GPIO12 override */

#define PMU43602_CC2_PCIE_CLKREQ_L_WAKE_EN (1<<1)  /* creates gated_pcie_wake, pmu_wakeup logic */
#define PMU43602_CC2_PCIE_PERST_L_WAKE_EN  (1<<2)  /* creates gated_pcie_wake, pmu_wakeup logic */
#define PMU43602_CC2_ENABLE_L2REFCLKPAD_PWRDWN (1<<3)
#define PMU43602_CC2_PMU_WAKE_ALP_AVAIL_EN (1<<5)  /* enable pmu_wakeup to request for ALP_AVAIL */
#define PMU43602_CC2_PERST_L_EXTEND_EN     (1<<9)  /* extend perst_l until rsc PERST_OVR comes up */
#define PMU43602_CC2_FORCE_EXT_LPO         (1<<19) /* 1=ext LPO clock is the final LPO clock */
#define PMU43602_CC2_XTAL32_SEL            (1<<30) /* 0=ext_clock, 1=xtal */

#define CC_SR1_43602_SR_ASM_ADDR	(0x0)

/* PLL CTL register values for open loop, used during S/R operation */
#define PMU43602_PLL_CTL6_VAL		0x68000528
#define PMU43602_PLL_CTL7_VAL		0x6

#define PMU43602_CC3_ARMCR4_DBG_CLK	(1 << 29)

/* 4349 related */
#define RES4349_LPLDO_PU			0
#define RES4349_BG_PU				1
#define RES4349_PMU_SLEEP			2
#define RES4349_PALDO3P3_PU			3
#define RES4349_CBUCK_LPOM_PU		4
#define RES4349_CBUCK_PFM_PU		5
#define RES4349_COLD_START_WAIT		6
#define RES4349_RSVD_7				7
#define RES4349_LNLDO_PU			8
#define RES4349_XTALLDO_PU			9
#define RES4349_LDO3P3_PU			10
#define RES4349_OTP_PU				11
#define RES4349_XTAL_PU				12
#define RES4349_SR_CLK_START		13
#define RES4349_LQ_AVAIL			14
#define RES4349_LQ_START			15
#define RES4349_PERST_OVR			16
#define RES4349_WL_CORE_RDY			17
#define RES4349_ILP_REQ				18
#define RES4349_ALP_AVAIL			19
#define RES4349_MINI_PMU			20
#define RES4349_RADIO_PU			21
#define RES4349_SR_CLK_STABLE		22
#define RES4349_SR_SAVE_RESTORE		23
#define RES4349_SR_PHY_PWRSW		24
#define RES4349_SR_VDDM_PWRSW		25
#define RES4349_SR_SUBCORE_PWRSW	26
#define RES4349_SR_SLEEP			27
#define RES4349_HT_START			28
#define RES4349_HT_AVAIL			29
#define RES4349_MACPHY_CLKAVAIL		30

#define CR4_4349_RAM_BASE			(0x180000)
#define CC4_4349_SR_ASM_ADDR		(0x48)

#define CST4349_CHIPMODE_SDIOD(cs)	(((cs) & (1 << 6)) != 0)	/* SDIO */
#define CST4349_CHIPMODE_PCIE(cs)	(((cs) & (1 << 7)) != 0)	/* PCIE */

#define CST4349_SPROM_PRESENT		0x00000010


/* 43430 PMU resources based on pmu_params.xls */
#define RES43430_LPLDO_PU				0
#define RES43430_BG_PU					1
#define RES43430_PMU_SLEEP				2
#define RES43430_RSVD_3					3
#define RES43430_CBUCK_LPOM_PU			4
#define RES43430_CBUCK_PFM_PU			5
#define RES43430_COLD_START_WAIT		6
#define RES43430_RSVD_7					7
#define RES43430_LNLDO_PU				8
#define RES43430_RSVD_9					9
#define RES43430_LDO3P3_PU				10
#define RES43430_OTP_PU					11
#define RES43430_XTAL_PU				12
#define RES43430_SR_CLK_START			13
#define RES43430_LQ_AVAIL				14
#define RES43430_LQ_START				15
#define RES43430_RSVD_16				16
#define RES43430_WL_CORE_RDY			17
#define RES43430_ILP_REQ				18
#define RES43430_ALP_AVAIL				19
#define RES43430_MINI_PMU				20
#define RES43430_RADIO_PU				21
#define RES43430_SR_CLK_STABLE			22
#define RES43430_SR_SAVE_RESTORE		23
#define RES43430_SR_PHY_PWRSW			24
#define RES43430_SR_VDDM_PWRSW			25
#define RES43430_SR_SUBCORE_PWRSW		26
#define RES43430_SR_SLEEP				27
#define RES43430_HT_START				28
#define RES43430_HT_AVAIL				29
#define RES43430_MACPHY_CLK_AVAIL		30

/* 43430 chip status bits */
#define CST43430_SDIO_MODE				0x00000001
#define CST43430_GSPI_MODE				0x00000002
#define CST43430_RSRC_INIT_MODE_0		0x00000080
#define CST43430_RSRC_INIT_MODE_1		0x00000100
#define CST43430_SEL0_SDIO				0x00000200
#define CST43430_SEL1_SDIO				0x00000400
#define CST43430_SEL2_SDIO				0x00000800
#define CST43430_BBPLL_LOCKED			0x00001000
#define CST43430_DBG_INST_DETECT		0x00004000
#define CST43430_CLB2WL_BT_READY		0x00020000
#define CST43430_JTAG_MODE				0x00100000
#define CST43430_HOST_IFACE				0x00400000
#define CST43430_TRIM_EN				0x00800000
#define CST43430_DIN_PACKAGE_OPTION		0x10000000

/* defines to detect active host interface in use */
#define CHIP_HOSTIF_PCIEMODE	0x1
#define CHIP_HOSTIF_USBMODE	0x2
#define CHIP_HOSTIF_SDIOMODE	0x4
#define CHIP_HOSTIF_PCIE(sih)	(si_chip_hostif(sih) == CHIP_HOSTIF_PCIEMODE)
#define CHIP_HOSTIF_USB(sih)	(si_chip_hostif(sih) == CHIP_HOSTIF_USBMODE)
#define CHIP_HOSTIF_SDIO(sih)	(si_chip_hostif(sih) == CHIP_HOSTIF_SDIOMODE)

/* 4335 resources */
#define RES4335_LPLDO_PO           0
#define RES4335_PMU_BG_PU          1
#define RES4335_PMU_SLEEP          2
#define RES4335_RSVD_3             3
#define RES4335_CBUCK_LPOM_PU		4
#define RES4335_CBUCK_PFM_PU		5
#define RES4335_RSVD_6             6
#define RES4335_RSVD_7             7
#define RES4335_LNLDO_PU           8
#define RES4335_XTALLDO_PU         9
#define RES4335_LDO3P3_PU			10
#define RES4335_OTP_PU				11
#define RES4335_XTAL_PU				12
#define RES4335_SR_CLK_START       13
#define RES4335_LQ_AVAIL			14
#define RES4335_LQ_START           15
#define RES4335_RSVD_16            16
#define RES4335_WL_CORE_RDY        17
#define RES4335_ILP_REQ				18
#define RES4335_ALP_AVAIL			19
#define RES4335_MINI_PMU           20
#define RES4335_RADIO_PU			21
#define RES4335_SR_CLK_STABLE		22
#define RES4335_SR_SAVE_RESTORE		23
#define RES4335_SR_PHY_PWRSW		24
#define RES4335_SR_VDDM_PWRSW      25
#define RES4335_SR_SUBCORE_PWRSW	26
#define RES4335_SR_SLEEP           27
#define RES4335_HT_START           28
#define RES4335_HT_AVAIL			29
#define RES4335_MACPHY_CLKAVAIL		30

/* 4335 Chip specific ChipStatus register bits */
#define CST4335_SPROM_MASK			0x00000020
#define CST4335_SFLASH_MASK			0x00000040
#define	CST4335_RES_INIT_MODE_SHIFT	7
#define	CST4335_RES_INIT_MODE_MASK	0x00000180
#define CST4335_CHIPMODE_MASK		0xF
#define CST4335_CHIPMODE_SDIOD(cs)	(((cs) & (1 << 0)) != 0)	/* SDIO */
#define CST4335_CHIPMODE_GSPI(cs)	(((cs) & (1 << 1)) != 0)	/* gSPI */
#define CST4335_CHIPMODE_USB20D(cs)	(((cs) & (1 << 2)) != 0)	/* HSIC || USBDA */
#define CST4335_CHIPMODE_PCIE(cs)	(((cs) & (1 << 3)) != 0)	/* PCIE */

/* 4335 Chip specific ChipControl1 register bits */
#define CCTRL1_4335_GPIO_SEL		(1 << 0)    /* 1=select GPIOs to be muxed out */
#define CCTRL1_4335_SDIO_HOST_WAKE (1 << 2)  /* SDIO: 1=configure GPIO0 for host wake */

/* 4335 Chip specific ChipControl2 register bits */
#define CCTRL2_4335_AOSBLOCK		(1 << 30)
#define CCTRL2_4335_PMUWAKE		(1 << 31)
#define PATCHTBL_SIZE			(0x800)
#define CR4_4335_RAM_BASE                    (0x180000)
#define CR4_4345_RAM_BASE                    (0x1b0000)
#define CR4_4349_RAM_BASE                    (0x180000)
#define CR4_4350_RAM_BASE                    (0x180000)
#define CR4_4360_RAM_BASE                    (0x0)
#define CR4_43602_RAM_BASE                   (0x180000)

/* 4335 chip OTP present & OTP select bits. */
#define SPROM4335_OTP_SELECT	0x00000010
#define SPROM4335_OTP_PRESENT	0x00000020

/* 4335 GCI specific bits. */
#define CC4335_GCI_STRAP_OVERRIDE_SFLASH_PRESENT	(1 << 24)
#define CC4335_GCI_STRAP_OVERRIDE_SFLASH_TYPE	25
#define CC4335_GCI_FUNC_SEL_PAD_SDIO	0x00707770

/* SFLASH clkdev specific bits. */
#define CC4335_SFLASH_CLKDIV_MASK	0x1F000000
#define CC4335_SFLASH_CLKDIV_SHIFT	25

/* 4335 OTP bits for SFLASH. */
#define CC4335_SROM_OTP_SFLASH	40
#define CC4335_SROM_OTP_SFLASH_PRESENT	0x1
#define CC4335_SROM_OTP_SFLASH_TYPE	0x2
#define CC4335_SROM_OTP_SFLASH_CLKDIV_MASK	0x003C
#define CC4335_SROM_OTP_SFLASH_CLKDIV_SHIFT	2


/* 4335 chip OTP present & OTP select bits. */
#define SPROM4335_OTP_SELECT	0x00000010
#define SPROM4335_OTP_PRESENT	0x00000020

/* 4335 GCI specific bits. */
#define CC4335_GCI_STRAP_OVERRIDE_SFLASH_PRESENT	(1 << 24)
#define CC4335_GCI_STRAP_OVERRIDE_SFLASH_TYPE	25
#define CC4335_GCI_FUNC_SEL_PAD_SDIO	0x00707770

/* SFLASH clkdev specific bits. */
#define CC4335_SFLASH_CLKDIV_MASK	0x1F000000
#define CC4335_SFLASH_CLKDIV_SHIFT	25

/* 4335 OTP bits for SFLASH. */
#define CC4335_SROM_OTP_SFLASH	40
#define CC4335_SROM_OTP_SFLASH_PRESENT	0x1
#define CC4335_SROM_OTP_SFLASH_TYPE	0x2
#define CC4335_SROM_OTP_SFLASH_CLKDIV_MASK	0x003C
#define CC4335_SROM_OTP_SFLASH_CLKDIV_SHIFT	2

/* 4335 resources--END */

/* 4345 Chip specific ChipStatus register bits */
#define CST4345_SPROM_MASK		0x00000020
#define CST4345_SFLASH_MASK		0x00000040
#define CST4345_RES_INIT_MODE_SHIFT	7
#define CST4345_RES_INIT_MODE_MASK	0x00000180
#define CST4345_CHIPMODE_MASK		0x4000F
#define CST4345_CHIPMODE_SDIOD(cs)	(((cs) & (1 << 0)) != 0)	/* SDIO */
#define CST4345_CHIPMODE_GSPI(cs)	(((cs) & (1 << 1)) != 0)	/* gSPI */
#define CST4345_CHIPMODE_HSIC(cs)	(((cs) & (1 << 2)) != 0)	/* HSIC */
#define CST4345_CHIPMODE_PCIE(cs)	(((cs) & (1 << 3)) != 0)	/* PCIE */
#define CST4345_CHIPMODE_USB20D(cs)	(((cs) & (1 << 18)) != 0)	/* USBDA */

/* 4350 Chipcommon ChipStatus bits */
#define CST4350_SDIO_MODE		0x00000001
#define CST4350_HSIC20D_MODE		0x00000002
#define CST4350_BP_ON_HSIC_CLK		0x00000004
#define CST4350_PCIE_MODE		0x00000008
#define CST4350_USB20D_MODE		0x00000010
#define CST4350_USB30D_MODE		0x00000020
#define CST4350_SPROM_PRESENT		0x00000040
#define CST4350_RSRC_INIT_MODE_0	0x00000080
#define CST4350_RSRC_INIT_MODE_1	0x00000100
#define CST4350_SEL0_SDIO		0x00000200
#define CST4350_SEL1_SDIO		0x00000400
#define CST4350_SDIO_PAD_MODE		0x00000800
#define CST4350_BBPLL_LOCKED		0x00001000
#define CST4350_USBPLL_LOCKED		0x00002000
#define CST4350_LINE_STATE		0x0000C000
#define CST4350_SERDES_PIPE_PLLLOCK	0x00010000
#define CST4350_BT_READY		0x00020000
#define CST4350_SFLASH_PRESENT		0x00040000
#define CST4350_CPULESS_ENABLE		0x00080000
#define CST4350_STRAP_HOST_IFC_1	0x00100000
#define CST4350_STRAP_HOST_IFC_2	0x00200000
#define CST4350_STRAP_HOST_IFC_3	0x00400000
#define CST4350_RAW_SPROM_PRESENT	0x00800000
#define CST4350_APP_CLK_SWITCH_SEL_RDBACK	0x01000000
#define CST4350_RAW_RSRC_INIT_MODE_0	0x02000000
#define CST4350_SDIO_PAD_VDDIO		0x04000000
#define CST4350_GSPI_MODE		0x08000000
#define CST4350_PACKAGE_OPTION		0xF0000000
#define CST4350_PACKAGE_SHIFT		28

/* package option for 4350 */
#define CST4350_PACKAGE_WLCSP		0x0
#define CST4350_PACKAGE_PCIE		0x1
#define CST4350_PACKAGE_WLBGA		0x2
#define CST4350_PACKAGE_DBG		0x3
#define CST4350_PACKAGE_USB		0x4
#define CST4350_PACKAGE_USB_HSIC	0x4

#define CST4350_PKG_MODE(cs)	((cs & CST4350_PACKAGE_OPTION) >> CST4350_PACKAGE_SHIFT)

#define CST4350_PKG_WLCSP(cs)		(CST4350_PKG_MODE(cs) == (CST4350_PACKAGE_WLCSP))
#define CST4350_PKG_PCIE(cs)		(CST4350_PKG_MODE(cs) == (CST4350_PACKAGE_PCIE))
#define CST4350_PKG_WLBGA(cs)		(CST4350_PKG_MODE(cs) == (CST4350_PACKAGE_WLBGA))
#define CST4350_PKG_USB(cs)		(CST4350_PKG_MODE(cs) == (CST4350_PACKAGE_USB))
#define CST4350_PKG_USB_HSIC(cs)	(CST4350_PKG_MODE(cs) == (CST4350_PACKAGE_USB_HSIC))

/* 4350C0 USB PACKAGE using raw_sprom_present to indicate 40mHz xtal */
#define CST4350_PKG_USB_40M(cs)		(cs & CST4350_RAW_SPROM_PRESENT)

#define CST4350_CHIPMODE_SDIOD(cs)	(CST4350_IFC_MODE(cs) == (CST4350_IFC_MODE_SDIOD))
#define CST4350_CHIPMODE_USB20D(cs)	((CST4350_IFC_MODE(cs)) == (CST4350_IFC_MODE_USB20D))
#define CST4350_CHIPMODE_HSIC20D(cs)	(CST4350_IFC_MODE(cs) == (CST4350_IFC_MODE_HSIC20D))
#define CST4350_CHIPMODE_HSIC30D(cs)	(CST4350_IFC_MODE(cs) == (CST4350_IFC_MODE_HSIC30D))
#define CST4350_CHIPMODE_USB30D(cs)	(CST4350_IFC_MODE(cs) == (CST4350_IFC_MODE_USB30D))
#define CST4350_CHIPMODE_USB30D_WL(cs)	(CST4350_IFC_MODE(cs) == (CST4350_IFC_MODE_USB30D_WL))
#define CST4350_CHIPMODE_PCIE(cs)	(CST4350_IFC_MODE(cs) == (CST4350_IFC_MODE_PCIE))

/* strap_host_ifc strap value */
#define CST4350_HOST_IFC_MASK		0x00700000
#define CST4350_HOST_IFC_SHIFT		20

/* host_ifc raw mode */
#define CST4350_IFC_MODE_SDIOD			0x0
#define CST4350_IFC_MODE_HSIC20D		0x1
#define CST4350_IFC_MODE_HSIC30D		0x2
#define CST4350_IFC_MODE_PCIE			0x3
#define CST4350_IFC_MODE_USB20D			0x4
#define CST4350_IFC_MODE_USB30D			0x5
#define CST4350_IFC_MODE_USB30D_WL		0x6
#define CST4350_IFC_MODE_USB30D_BT		0x7

#define CST4350_IFC_MODE(cs)	((cs & CST4350_HOST_IFC_MASK) >> CST4350_HOST_IFC_SHIFT)

/* 4350 PMU resources */
#define RES4350_LPLDO_PU	0
#define RES4350_PMU_BG_PU	1
#define RES4350_PMU_SLEEP	2
#define RES4350_RSVD_3		3
#define RES4350_CBUCK_LPOM_PU	4
#define RES4350_CBUCK_PFM_PU	5
#define RES4350_COLD_START_WAIT	6
#define RES4350_RSVD_7		7
#define RES4350_LNLDO_PU	8
#define RES4350_XTALLDO_PU	9
#define RES4350_LDO3P3_PU	10
#define RES4350_OTP_PU		11
#define RES4350_XTAL_PU		12
#define RES4350_SR_CLK_START	13
#define RES4350_LQ_AVAIL	14
#define RES4350_LQ_START	15
#define RES4350_PERST_OVR	16
#define RES4350_WL_CORE_RDY	17
#define RES4350_ILP_REQ		18
#define RES4350_ALP_AVAIL	19
#define RES4350_MINI_PMU	20
#define RES4350_RADIO_PU	21
#define RES4350_SR_CLK_STABLE	22
#define RES4350_SR_SAVE_RESTORE	23
#define RES4350_SR_PHY_PWRSW	24
#define RES4350_SR_VDDM_PWRSW	25
#define RES4350_SR_SUBCORE_PWRSW	26
#define RES4350_SR_SLEEP	27
#define RES4350_HT_START	28
#define RES4350_HT_AVAIL	29
#define RES4350_MACPHY_CLKAVAIL	30

#define MUXENAB4350_UART_MASK		(0x0000000f)
#define MUXENAB4350_UART_SHIFT		0
#define MUXENAB4350_HOSTWAKE_MASK	(0x000000f0)	/* configure GPIO for SDIO host_wake */
#define MUXENAB4350_HOSTWAKE_SHIFT	4


/* 4350 GCI function sel values */
#define CC4350_FNSEL_HWDEF		(0)
#define CC4350_FNSEL_SAMEASPIN		(1)
#define CC4350_FNSEL_UART		(2)
#define CC4350_FNSEL_SFLASH		(3)
#define CC4350_FNSEL_SPROM		(4)
#define CC4350_FNSEL_I2C		(5)
#define CC4350_FNSEL_MISC0		(6)
#define CC4350_FNSEL_GCI		(7)
#define CC4350_FNSEL_MISC1		(8)
#define CC4350_FNSEL_MISC2		(9)
#define CC4350_FNSEL_PWDOG 		(10)
#define CC4350_FNSEL_IND		(12)
#define CC4350_FNSEL_PDN		(13)
#define CC4350_FNSEL_PUP		(14)
#define CC4350_FNSEL_TRISTATE		(15)
#define CC4350C_FNSEL_UART		(3)


/* 4350 GPIO */
#define CC4350_PIN_GPIO_00		(0)
#define CC4350_PIN_GPIO_01		(1)
#define CC4350_PIN_GPIO_02		(2)
#define CC4350_PIN_GPIO_03		(3)
#define CC4350_PIN_GPIO_04		(4)
#define CC4350_PIN_GPIO_05		(5)
#define CC4350_PIN_GPIO_06		(6)
#define CC4350_PIN_GPIO_07		(7)
#define CC4350_PIN_GPIO_08		(8)
#define CC4350_PIN_GPIO_09		(9)
#define CC4350_PIN_GPIO_10		(10)
#define CC4350_PIN_GPIO_11		(11)
#define CC4350_PIN_GPIO_12		(12)
#define CC4350_PIN_GPIO_13		(13)
#define CC4350_PIN_GPIO_14		(14)
#define CC4350_PIN_GPIO_15		(15)

#define CC4350_RSVD_16_SHIFT		16

#define CC2_4350_PHY_PWRSW_UPTIME_MASK		(0xf << 0)
#define CC2_4350_PHY_PWRSW_UPTIME_SHIFT		(0)
#define CC2_4350_VDDM_PWRSW_UPDELAY_MASK	(0xf << 4)
#define CC2_4350_VDDM_PWRSW_UPDELAY_SHIFT	(4)
#define CC2_4350_VDDM_PWRSW_UPTIME_MASK		(0xf << 8)
#define CC2_4350_VDDM_PWRSW_UPTIME_SHIFT	(8)
#define CC2_4350_SBC_PWRSW_DNDELAY_MASK		(0x3 << 12)
#define CC2_4350_SBC_PWRSW_DNDELAY_SHIFT	(12)
#define CC2_4350_PHY_PWRSW_DNDELAY_MASK		(0x3 << 14)
#define CC2_4350_PHY_PWRSW_DNDELAY_SHIFT	(14)
#define CC2_4350_VDDM_PWRSW_DNDELAY_MASK	(0x3 << 16)
#define CC2_4350_VDDM_PWRSW_DNDELAY_SHIFT	(16)
#define CC2_4350_VDDM_PWRSW_EN_MASK		(1 << 20)
#define CC2_4350_VDDM_PWRSW_EN_SHIFT		(20)
#define CC2_4350_MEMLPLDO_PWRSW_EN_MASK		(1 << 21)
#define CC2_4350_MEMLPLDO_PWRSW_EN_SHIFT	(21)
#define CC2_4350_SDIO_AOS_WAKEUP_MASK		(1 << 24)
#define CC2_4350_SDIO_AOS_WAKEUP_SHIFT		(24)

/* Applies to 4335/4350/4345 */
#define CC3_SR_CLK_SR_MEM_MASK			(1 << 0)
#define CC3_SR_CLK_SR_MEM_SHIFT			(0)
#define CC3_SR_BIT1_TBD_MASK			(1 << 1)
#define CC3_SR_BIT1_TBD_SHIFT			(1)
#define CC3_SR_ENGINE_ENABLE_MASK		(1 << 2)
#define CC3_SR_ENGINE_ENABLE_SHIFT		(2)
#define CC3_SR_BIT3_TBD_MASK			(1 << 3)
#define CC3_SR_BIT3_TBD_SHIFT			(3)
#define CC3_SR_MINDIV_FAST_CLK_MASK		(0xF << 4)
#define CC3_SR_MINDIV_FAST_CLK_SHIFT		(4)
#define CC3_SR_R23_SR2_RISE_EDGE_TRIG_MASK	(1 << 8)
#define CC3_SR_R23_SR2_RISE_EDGE_TRIG_SHIFT	(8)
#define CC3_SR_R23_SR2_FALL_EDGE_TRIG_MASK	(1 << 9)
#define CC3_SR_R23_SR2_FALL_EDGE_TRIG_SHIFT	(9)
#define CC3_SR_R23_SR_RISE_EDGE_TRIG_MASK	(1 << 10)
#define CC3_SR_R23_SR_RISE_EDGE_TRIG_SHIFT	(10)
#define CC3_SR_R23_SR_FALL_EDGE_TRIG_MASK	(1 << 11)
#define CC3_SR_R23_SR_FALL_EDGE_TRIG_SHIFT	(11)
#define CC3_SR_NUM_CLK_HIGH_MASK		(0x7 << 12)
#define CC3_SR_NUM_CLK_HIGH_SHIFT		(12)
#define CC3_SR_BIT15_TBD_MASK			(1 << 15)
#define CC3_SR_BIT15_TBD_SHIFT			(15)
#define CC3_SR_PHY_FUNC_PIC_MASK		(1 << 16)
#define CC3_SR_PHY_FUNC_PIC_SHIFT		(16)
#define CC3_SR_BIT17_19_TBD_MASK		(0x7 << 17)
#define CC3_SR_BIT17_19_TBD_SHIFT		(17)
#define CC3_SR_CHIP_TRIGGER_1_MASK		(1 << 20)
#define CC3_SR_CHIP_TRIGGER_1_SHIFT		(20)
#define CC3_SR_CHIP_TRIGGER_2_MASK		(1 << 21)
#define CC3_SR_CHIP_TRIGGER_2_SHIFT		(21)
#define CC3_SR_CHIP_TRIGGER_3_MASK		(1 << 22)
#define CC3_SR_CHIP_TRIGGER_3_SHIFT		(22)
#define CC3_SR_CHIP_TRIGGER_4_MASK		(1 << 23)
#define CC3_SR_CHIP_TRIGGER_4_SHIFT		(23)
#define CC3_SR_ALLOW_SBC_FUNC_PIC_MASK		(1 << 24)
#define CC3_SR_ALLOW_SBC_FUNC_PIC_SHIFT		(24)
#define CC3_SR_BIT25_26_TBD_MASK		(0x3 << 25)
#define CC3_SR_BIT25_26_TBD_SHIFT		(25)
#define CC3_SR_ALLOW_SBC_STBY_MASK		(1 << 27)
#define CC3_SR_ALLOW_SBC_STBY_SHIFT		(27)
#define CC3_SR_GPIO_MUX_MASK			(0xF << 28)
#define CC3_SR_GPIO_MUX_SHIFT			(28)

/* Applies to 4335/4350/4345 */
#define CC4_SR_INIT_ADDR_MASK		(0x3FF0000)
#define 	CC4_4350_SR_ASM_ADDR	(0x30)
#define CC4_4350_C0_SR_ASM_ADDR		(0x0)
#define 	CC4_4335_SR_ASM_ADDR	(0x48)
#define 	CC4_4345_SR_ASM_ADDR	(0x48)
#define CC4_SR_INIT_ADDR_SHIFT		(16)

#define CC4_4350_EN_SR_CLK_ALP_MASK	(1 << 30)
#define CC4_4350_EN_SR_CLK_ALP_SHIFT	(30)
#define CC4_4350_EN_SR_CLK_HT_MASK	(1 << 31)
#define CC4_4350_EN_SR_CLK_HT_SHIFT	(31)

#define VREG4_4350_MEMLPDO_PU_MASK	(1 << 31)
#define VREG4_4350_MEMLPDO_PU_SHIFT	31

#define VREG6_4350_SR_EXT_CLKDIR_MASK	(1 << 20)
#define VREG6_4350_SR_EXT_CLKDIR_SHIFT	20
#define VREG6_4350_SR_EXT_CLKDIV_MASK	(0x3 << 21)
#define VREG6_4350_SR_EXT_CLKDIV_SHIFT	21
#define VREG6_4350_SR_EXT_CLKEN_MASK	(1 << 23)
#define VREG6_4350_SR_EXT_CLKEN_SHIFT	23

#define CC5_4350_PMU_EN_ASSERT_MASK	(1 << 13)
#define CC5_4350_PMU_EN_ASSERT_SHIFT	(13)

#define CC6_4350_PCIE_CLKREQ_WAKEUP_MASK	(1 << 4)
#define CC6_4350_PCIE_CLKREQ_WAKEUP_SHIFT	(4)
#define CC6_4350_PMU_WAKEUP_ALPAVAIL_MASK	(1 << 6)
#define CC6_4350_PMU_WAKEUP_ALPAVAIL_SHIFT	(6)
#define CC6_4350_PMU_EN_EXT_PERST_MASK		(1 << 17)
#define CC6_4350_PMU_EN_EXT_PERST_SHIFT		(17)
#define CC6_4350_PMU_EN_WAKEUP_MASK		(1 << 18)
#define CC6_4350_PMU_EN_WAKEUP_SHIFT		(18)

#define CC7_4350_PMU_EN_ASSERT_L2_MASK	(1 << 26)
#define CC7_4350_PMU_EN_ASSERT_L2_SHIFT	(26)
#define CC7_4350_PMU_EN_MDIO_MASK	(1 << 27)
#define CC7_4350_PMU_EN_MDIO_SHIFT	(27)

#define CC6_4345_PMU_EN_PERST_DEASSERT_MASK		(1 << 13)
#define CC6_4345_PMU_EN_PERST_DEASSERT_SHIF		(13)
#define CC6_4345_PMU_EN_L2_DEASSERT_MASK		(1 << 14)
#define CC6_4345_PMU_EN_L2_DEASSERT_SHIF		(14)
#define CC6_4345_PMU_EN_ASSERT_L2_MASK		(1 << 15)
#define CC6_4345_PMU_EN_ASSERT_L2_SHIFT		(15)
#define CC6_4345_PMU_EN_MDIO_MASK		(1 << 24)
#define CC6_4345_PMU_EN_MDIO_SHIFT		(24)

/* GCI chipcontrol register indices */
#define CC_GCI_CHIPCTRL_00	(0)
#define CC_GCI_CHIPCTRL_01	(1)
#define CC_GCI_CHIPCTRL_02	(2)
#define CC_GCI_CHIPCTRL_03	(3)
#define CC_GCI_CHIPCTRL_04	(4)
#define CC_GCI_CHIPCTRL_05	(5)
#define CC_GCI_CHIPCTRL_06	(6)
#define CC_GCI_CHIPCTRL_07	(7)
#define CC_GCI_CHIPCTRL_08	(8)
#define CC_GCI_XTAL_BUFSTRG_NFC (0xff << 12)

#define CC_GCI_06_JTAG_SEL_SHIFT	4
#define CC_GCI_06_JTAG_SEL_MASK		(1 << 4)

#define CC_GCI_NUMCHIPCTRLREGS(cap1)	((cap1 & 0xF00) >> 8)

/* 4345 PMU resources */
#define RES4345_LPLDO_PU		0
#define RES4345_PMU_BG_PU		1
#define RES4345_PMU_SLEEP 		2
#define RES4345_HSICLDO_PU		3
#define RES4345_CBUCK_LPOM_PU		4
#define RES4345_CBUCK_PFM_PU		5
#define RES4345_COLD_START_WAIT		6
#define RES4345_RSVD_7			7
#define RES4345_LNLDO_PU		8
#define RES4345_XTALLDO_PU		9
#define RES4345_LDO3P3_PU		10
#define RES4345_OTP_PU			11
#define RES4345_XTAL_PU			12
#define RES4345_SR_CLK_START		13
#define RES4345_LQ_AVAIL		14
#define RES4345_LQ_START		15
#define RES4345_PERST_OVR		16
#define RES4345_WL_CORE_RDY		17
#define RES4345_ILP_REQ			18
#define RES4345_ALP_AVAIL		19
#define RES4345_MINI_PMU		20
#define RES4345_RADIO_PU		21
#define RES4345_SR_CLK_STABLE		22
#define RES4345_SR_SAVE_RESTORE		23
#define RES4345_SR_PHY_PWRSW		24
#define RES4345_SR_VDDM_PWRSW		25
#define RES4345_SR_SUBCORE_PWRSW	26
#define RES4345_SR_SLEEP		27
#define RES4345_HT_START		28
#define RES4345_HT_AVAIL		29
#define RES4345_MACPHY_CLK_AVAIL	30

/* 4335 pins
* note: only the values set as default/used are added here.
*/
#define CC4335_PIN_GPIO_00		(0)
#define CC4335_PIN_GPIO_01		(1)
#define CC4335_PIN_GPIO_02		(2)
#define CC4335_PIN_GPIO_03		(3)
#define CC4335_PIN_GPIO_04		(4)
#define CC4335_PIN_GPIO_05		(5)
#define CC4335_PIN_GPIO_06		(6)
#define CC4335_PIN_GPIO_07		(7)
#define CC4335_PIN_GPIO_08		(8)
#define CC4335_PIN_GPIO_09		(9)
#define CC4335_PIN_GPIO_10		(10)
#define CC4335_PIN_GPIO_11		(11)
#define CC4335_PIN_GPIO_12		(12)
#define CC4335_PIN_GPIO_13		(13)
#define CC4335_PIN_GPIO_14		(14)
#define CC4335_PIN_GPIO_15		(15)
#define CC4335_PIN_SDIO_CLK		(16)
#define CC4335_PIN_SDIO_CMD		(17)
#define CC4335_PIN_SDIO_DATA0	(18)
#define CC4335_PIN_SDIO_DATA1	(19)
#define CC4335_PIN_SDIO_DATA2	(20)
#define CC4335_PIN_SDIO_DATA3	(21)
#define CC4335_PIN_RF_SW_CTRL_6	(22)
#define CC4335_PIN_RF_SW_CTRL_7	(23)
#define CC4335_PIN_RF_SW_CTRL_8	(24)
#define CC4335_PIN_RF_SW_CTRL_9	(25)
/* Last GPIO Pad */
#define CC4335_PIN_GPIO_LAST	(31)

/* 4335 GCI function sel values
*/
#define CC4335_FNSEL_HWDEF		(0)
#define CC4335_FNSEL_SAMEASPIN	(1)
#define CC4335_FNSEL_GPIO0		(2)
#define CC4335_FNSEL_GPIO1		(3)
#define CC4335_FNSEL_GCI0		(4)
#define CC4335_FNSEL_GCI1		(5)
#define CC4335_FNSEL_UART		(6)
#define CC4335_FNSEL_SFLASH		(7)
#define CC4335_FNSEL_SPROM		(8)
#define CC4335_FNSEL_MISC0		(9)
#define CC4335_FNSEL_MISC1		(10)
#define CC4335_FNSEL_MISC2		(11)
#define CC4335_FNSEL_IND		(12)
#define CC4335_FNSEL_PDN		(13)
#define CC4335_FNSEL_PUP		(14)
#define CC4335_FNSEL_TRI		(15)

/* GCI Core Control Reg */
#define	GCI_CORECTRL_SR_MASK	(1 << 0)	/* SECI block Reset */
#define	GCI_CORECTRL_RSL_MASK	(1 << 1)	/* ResetSECILogic */
#define	GCI_CORECTRL_ES_MASK	(1 << 2)	/* EnableSECI */
#define	GCI_CORECTRL_FSL_MASK	(1 << 3)	/* Force SECI Out Low */
#define	GCI_CORECTRL_SOM_MASK	(7 << 4)	/* SECI Op Mode */
#define	GCI_CORECTRL_US_MASK	(1 << 7)	/* Update SECI */
#define	GCI_CORECTRL_BOS_MASK	(1 << 8)	/* Break On Sleep */

/* 4345 pins
* note: only the values set as default/used are added here.
*/
#define CC4345_PIN_GPIO_00		(0)
#define CC4345_PIN_GPIO_01		(1)
#define CC4345_PIN_GPIO_02		(2)
#define CC4345_PIN_GPIO_03		(3)
#define CC4345_PIN_GPIO_04		(4)
#define CC4345_PIN_GPIO_05		(5)
#define CC4345_PIN_GPIO_06		(6)
#define CC4345_PIN_GPIO_07		(7)
#define CC4345_PIN_GPIO_08		(8)
#define CC4345_PIN_GPIO_09		(9)
#define CC4345_PIN_GPIO_10		(10)
#define CC4345_PIN_GPIO_11		(11)
#define CC4345_PIN_GPIO_12		(12)
#define CC4345_PIN_GPIO_13		(13)
#define CC4345_PIN_GPIO_14		(14)
#define CC4345_PIN_GPIO_15		(15)
#define CC4345_PIN_GPIO_16		(16)
#define CC4345_PIN_SDIO_CLK		(17)
#define CC4345_PIN_SDIO_CMD		(18)
#define CC4345_PIN_SDIO_DATA0	(19)
#define CC4345_PIN_SDIO_DATA1	(20)
#define CC4345_PIN_SDIO_DATA2	(21)
#define CC4345_PIN_SDIO_DATA3	(22)
#define CC4345_PIN_RF_SW_CTRL_0	(23)
#define CC4345_PIN_RF_SW_CTRL_1	(24)
#define CC4345_PIN_RF_SW_CTRL_2	(25)
#define CC4345_PIN_RF_SW_CTRL_3	(26)
#define CC4345_PIN_RF_SW_CTRL_4	(27)
#define CC4345_PIN_RF_SW_CTRL_5	(28)
#define CC4345_PIN_RF_SW_CTRL_6	(29)
#define CC4345_PIN_RF_SW_CTRL_7	(30)
#define CC4345_PIN_RF_SW_CTRL_8	(31)
#define CC4345_PIN_RF_SW_CTRL_9	(32)

/* 4345 GCI function sel values
*/
#define CC4345_FNSEL_HWDEF		(0)
#define CC4345_FNSEL_SAMEASPIN		(1)
#define CC4345_FNSEL_GPIO0		(2)
#define CC4345_FNSEL_GPIO1		(3)
#define CC4345_FNSEL_GCI0		(4)
#define CC4345_FNSEL_GCI1		(5)
#define CC4345_FNSEL_UART		(6)
#define CC4345_FNSEL_SFLASH		(7)
#define CC4345_FNSEL_SPROM		(8)
#define CC4345_FNSEL_MISC0		(9)
#define CC4345_FNSEL_MISC1		(10)
#define CC4345_FNSEL_MISC2		(11)
#define CC4345_FNSEL_IND		(12)
#define CC4345_FNSEL_PDN		(13)
#define CC4345_FNSEL_PUP		(14)
#define CC4345_FNSEL_TRI		(15)

#define MUXENAB4345_UART_MASK		(0x0000000f)
#define MUXENAB4345_UART_SHIFT		0
#define MUXENAB4345_HOSTWAKE_MASK	(0x000000f0)
#define MUXENAB4345_HOSTWAKE_SHIFT	4

/* 4349 Group (4349, 4355, 4359) GCI AVS function sel values */
#define CC4349_GRP_GCI_AVS_CTRL_MASK   (0xffe00000)
#define CC4349_GRP_GCI_AVS_CTRL_SHIFT  (21)
#define CC4349_GRP_GCI_AVS_CTRL_ENAB   (1 << 5)

/* 4345 GCI AVS function sel values */
#define CC4345_GCI_AVS_CTRL_MASK   (0xfc)
#define CC4345_GCI_AVS_CTRL_SHIFT  (2)
#define CC4345_GCI_AVS_CTRL_ENAB   (1 << 5)

/* GCI GPIO for function sel GCI-0/GCI-1 */
#define CC_GCI_GPIO_0			(0)
#define CC_GCI_GPIO_1			(1)
#define CC_GCI_GPIO_2			(2)
#define CC_GCI_GPIO_3			(3)
#define CC_GCI_GPIO_4			(4)
#define CC_GCI_GPIO_5			(5)
#define CC_GCI_GPIO_6			(6)
#define CC_GCI_GPIO_7			(7)
#define CC_GCI_GPIO_8			(8)
#define CC_GCI_GPIO_9			(9)
#define CC_GCI_GPIO_10			(10)
#define CC_GCI_GPIO_11			(11)
#define CC_GCI_GPIO_12			(12)
#define CC_GCI_GPIO_13			(13)
#define CC_GCI_GPIO_14			(14)
#define CC_GCI_GPIO_15			(15)


/* indicates Invalid GPIO, e.g. when PAD GPIO doesn't map to GCI GPIO */
#define CC_GCI_GPIO_INVALID		0xFF

/* find the 4 bit mask given the bit position */
#define GCIMASK(pos)  (((uint32)0xF) << pos)
/* get the value which can be used to directly OR with chipcontrol reg */
#define GCIPOSVAL(val, pos)  ((((uint32)val) << pos) & GCIMASK(pos))
/* Extract nibble from a given position */
#define GCIGETNBL(val, pos)	((val >> pos) & 0xF)


/* find the 8 bit mask given the bit position */
#define GCIMASK_8B(pos)  (((uint32)0xFF) << pos)
/* get the value which can be used to directly OR with chipcontrol reg */
#define GCIPOSVAL_8B(val, pos)  ((((uint32)val) << pos) & GCIMASK_8B(pos))
/* Extract nibble from a given position */
#define GCIGETNBL_8B(val, pos)	((val >> pos) & 0xFF)

/* find the 4 bit mask given the bit position */
#define GCIMASK_4B(pos)  (((uint32)0xF) << pos)
/* get the value which can be used to directly OR with chipcontrol reg */
#define GCIPOSVAL_4B(val, pos)  ((((uint32)val) << pos) & GCIMASK_4B(pos))
/* Extract nibble from a given position */
#define GCIGETNBL_4B(val, pos)	((val >> pos) & 0xF)


/* 4335 GCI Intstatus(Mask)/WakeMask Register bits. */
#define GCI_INTSTATUS_RBI	(1 << 0)	/* Rx Break Interrupt */
#define GCI_INTSTATUS_UB	(1 << 1)	/* UART Break Interrupt */
#define GCI_INTSTATUS_SPE	(1 << 2)	/* SECI Parity Error Interrupt */
#define GCI_INTSTATUS_SFE	(1 << 3)	/* SECI Framing Error Interrupt */
#define GCI_INTSTATUS_SRITI	(1 << 9)	/* SECI Rx Idle Timer Interrupt */
#define GCI_INTSTATUS_STFF	(1 << 10)	/* SECI Tx FIFO Full Interrupt */
#define GCI_INTSTATUS_STFAE	(1 << 11)	/* SECI Tx FIFO Almost Empty Intr */
#define GCI_INTSTATUS_SRFAF	(1 << 12)	/* SECI Rx FIFO Almost Full */
#define GCI_INTSTATUS_SRFNE	(1 << 14)	/* SECI Rx FIFO Not Empty */
#define GCI_INTSTATUS_SRFOF	(1 << 15)	/* SECI Rx FIFO Not Empty Timeout */
#define GCI_INTSTATUS_GPIOINT	(1 << 25)	/* GCIGpioInt */
#define GCI_INTSTATUS_GPIOWAKE	(1 << 26)	/* GCIGpioWake */

/* 4335 GCI IntMask Register bits. */
#define GCI_INTMASK_RBI		(1 << 0)	/* Rx Break Interrupt */
#define GCI_INTMASK_UB		(1 << 1)	/* UART Break Interrupt */
#define GCI_INTMASK_SPE		(1 << 2)	/* SECI Parity Error Interrupt */
#define GCI_INTMASK_SFE		(1 << 3)	/* SECI Framing Error Interrupt */
#define GCI_INTMASK_SRITI	(1 << 9)	/* SECI Rx Idle Timer Interrupt */
#define GCI_INTMASK_STFF	(1 << 10)	/* SECI Tx FIFO Full Interrupt */
#define GCI_INTMASK_STFAE	(1 << 11)	/* SECI Tx FIFO Almost Empty Intr */
#define GCI_INTMASK_SRFAF	(1 << 12)	/* SECI Rx FIFO Almost Full */
#define GCI_INTMASK_SRFNE	(1 << 14)	/* SECI Rx FIFO Not Empty */
#define GCI_INTMASK_SRFOF	(1 << 15)	/* SECI Rx FIFO Not Empty Timeout */
#define GCI_INTMASK_GPIOINT	(1 << 25)	/* GCIGpioInt */
#define GCI_INTMASK_GPIOWAKE	(1 << 26)	/* GCIGpioWake */

/* 4335 GCI WakeMask Register bits. */
#define GCI_WAKEMASK_RBI	(1 << 0)	/* Rx Break Interrupt */
#define GCI_WAKEMASK_UB		(1 << 1)	/* UART Break Interrupt */
#define GCI_WAKEMASK_SPE	(1 << 2)	/* SECI Parity Error Interrupt */
#define GCI_WAKEMASK_SFE	(1 << 3)	/* SECI Framing Error Interrupt */
#define GCI_WAKE_SRITI		(1 << 9)	/* SECI Rx Idle Timer Interrupt */
#define GCI_WAKEMASK_STFF	(1 << 10)	/* SECI Tx FIFO Full Interrupt */
#define GCI_WAKEMASK_STFAE	(1 << 11)	/* SECI Tx FIFO Almost Empty Intr */
#define GCI_WAKEMASK_SRFAF	(1 << 12)	/* SECI Rx FIFO Almost Full */
#define GCI_WAKEMASK_SRFNE	(1 << 14)	/* SECI Rx FIFO Not Empty */
#define GCI_WAKEMASK_SRFOF	(1 << 15)	/* SECI Rx FIFO Not Empty Timeout */
#define GCI_WAKEMASK_GPIOINT	(1 << 25)	/* GCIGpioInt */
#define GCI_WAKEMASK_GPIOWAKE	(1 << 26)	/* GCIGpioWake */

#define	GCI_WAKE_ON_GCI_GPIO1	1
#define	GCI_WAKE_ON_GCI_GPIO2	2
#define	GCI_WAKE_ON_GCI_GPIO3	3
#define	GCI_WAKE_ON_GCI_GPIO4	4
#define	GCI_WAKE_ON_GCI_GPIO5	5
#define	GCI_WAKE_ON_GCI_GPIO6	6
#define	GCI_WAKE_ON_GCI_GPIO7	7
#define	GCI_WAKE_ON_GCI_GPIO8	8
#define	GCI_WAKE_ON_GCI_SECI_IN	9

/* 4335 MUX options. each nibble belongs to a setting. Non-zero value specifies a logic
* for now only UART for bootloader.
*/
#define MUXENAB4335_UART_MASK		(0x0000000f)

#define MUXENAB4335_UART_SHIFT		0
#define MUXENAB4335_HOSTWAKE_MASK	(0x000000f0)	/* configure GPIO for SDIO host_wake */
#define MUXENAB4335_HOSTWAKE_SHIFT	4
#define MUXENAB4335_GETIX(val, name) \
	((((val) & MUXENAB4335_ ## name ## _MASK) >> MUXENAB4335_ ## name ## _SHIFT) - 1)

/*
* Maximum delay for the PMU state transition in us.
* This is an upper bound intended for spinwaits etc.
*/
#define PMU_MAX_TRANSITION_DLY	15000

/* PMU resource up transition time in ILP cycles */
#define PMURES_UP_TRANSITION	2


/* SECI configuration */
#define SECI_MODE_UART			0x0
#define SECI_MODE_SECI			0x1
#define SECI_MODE_LEGACY_3WIRE_BT	0x2
#define SECI_MODE_LEGACY_3WIRE_WLAN	0x3
#define SECI_MODE_HALF_SECI		0x4

#define SECI_RESET		(1 << 0)
#define SECI_RESET_BAR_UART	(1 << 1)
#define SECI_ENAB_SECI_ECI	(1 << 2)
#define SECI_ENAB_SECIOUT_DIS	(1 << 3)
#define SECI_MODE_MASK		0x7
#define SECI_MODE_SHIFT		4 /* (bits 5, 6, 7) */
#define SECI_UPD_SECI		(1 << 7)

#define SECI_SLIP_ESC_CHAR	0xDB
#define SECI_SIGNOFF_0		SECI_SLIP_ESC_CHAR
#define SECI_SIGNOFF_1     0
#define SECI_REFRESH_REQ	0xDA

/* seci clk_ctl_st bits */
#define CLKCTL_STS_SECI_CLK_REQ		(1 << 8)
#define CLKCTL_STS_SECI_CLK_AVAIL	(1 << 24)

#define SECI_UART_MSR_CTS_STATE		(1 << 0)
#define SECI_UART_MSR_RTS_STATE		(1 << 1)
#define SECI_UART_SECI_IN_STATE		(1 << 2)
#define SECI_UART_SECI_IN2_STATE	(1 << 3)

/* GCI RX FIFO Control Register */
#define	GCI_RXF_LVL_MASK	(0xFF << 0)
#define	GCI_RXF_TIMEOUT_MASK	(0xFF << 8)

/* GCI UART Registers' Bit definitions */
/* Seci Fifo Level Register */
#define	SECI_TXF_LVL_MASK	(0x3F << 8)
#define	TXF_AE_LVL_DEFAULT	0x4
#define	SECI_RXF_LVL_FC_MASK	(0x3F << 16)

/* SeciUARTFCR Bit definitions */
#define	SECI_UART_FCR_RFR		(1 << 0)
#define	SECI_UART_FCR_TFR		(1 << 1)
#define	SECI_UART_FCR_SR		(1 << 2)
#define	SECI_UART_FCR_THP		(1 << 3)
#define	SECI_UART_FCR_AB		(1 << 4)
#define	SECI_UART_FCR_ATOE		(1 << 5)
#define	SECI_UART_FCR_ARTSOE		(1 << 6)
#define	SECI_UART_FCR_ABV		(1 << 7)
#define	SECI_UART_FCR_ALM		(1 << 8)

/* SECI UART LCR register bits */
#define SECI_UART_LCR_STOP_BITS		(1 << 0) /* 0 - 1bit, 1 - 2bits */
#define SECI_UART_LCR_PARITY_EN		(1 << 1)
#define SECI_UART_LCR_PARITY		(1 << 2) /* 0 - odd, 1 - even */
#define SECI_UART_LCR_RX_EN		(1 << 3)
#define SECI_UART_LCR_LBRK_CTRL		(1 << 4) /* 1 => SECI_OUT held low */
#define SECI_UART_LCR_TXO_EN		(1 << 5)
#define SECI_UART_LCR_RTSO_EN		(1 << 6)
#define SECI_UART_LCR_SLIPMODE_EN	(1 << 7)
#define SECI_UART_LCR_RXCRC_CHK		(1 << 8)
#define SECI_UART_LCR_TXCRC_INV		(1 << 9)
#define SECI_UART_LCR_TXCRC_LSBF	(1 << 10)
#define SECI_UART_LCR_TXCRC_EN		(1 << 11)
#define	SECI_UART_LCR_RXSYNC_EN		(1 << 12)

#define SECI_UART_MCR_TX_EN		(1 << 0)
#define SECI_UART_MCR_PRTS		(1 << 1)
#define SECI_UART_MCR_SWFLCTRL_EN	(1 << 2)
#define SECI_UART_MCR_HIGHRATE_EN	(1 << 3)
#define SECI_UART_MCR_LOOPBK_EN		(1 << 4)
#define SECI_UART_MCR_AUTO_RTS		(1 << 5)
#define SECI_UART_MCR_AUTO_TX_DIS	(1 << 6)
#define SECI_UART_MCR_BAUD_ADJ_EN	(1 << 7)
#define SECI_UART_MCR_XONOFF_RPT	(1 << 9)

/* SeciUARTLSR Bit Mask */
#define	SECI_UART_LSR_RXOVR_MASK	(1 << 0)
#define	SECI_UART_LSR_RFF_MASK		(1 << 1)
#define	SECI_UART_LSR_TFNE_MASK		(1 << 2)
#define	SECI_UART_LSR_TI_MASK		(1 << 3)
#define	SECI_UART_LSR_TPR_MASK		(1 << 4)
#define	SECI_UART_LSR_TXHALT_MASK	(1 << 5)

/* SeciUARTMSR Bit Mask */
#define	SECI_UART_MSR_CTSS_MASK		(1 << 0)
#define	SECI_UART_MSR_RTSS_MASK		(1 << 1)
#define	SECI_UART_MSR_SIS_MASK		(1 << 2)
#define	SECI_UART_MSR_SIS2_MASK		(1 << 3)

/* SeciUARTData Bits */
#define SECI_UART_DATA_RF_NOT_EMPTY_BIT	(1 << 12)
#define SECI_UART_DATA_RF_FULL_BIT	(1 << 13)
#define SECI_UART_DATA_RF_OVRFLOW_BIT	(1 << 14)
#define	SECI_UART_DATA_FIFO_PTR_MASK	0xFF
#define	SECI_UART_DATA_RF_RD_PTR_SHIFT	16
#define	SECI_UART_DATA_RF_WR_PTR_SHIFT	24

/* LTECX: ltecxmux */
#define LTECX_EXTRACT_MUX(val, idx)	(getbit4(&(val), (idx)))

/* LTECX: ltecxmux MODE */
#define LTECX_MUX_MODE_IDX		0
#define LTECX_MUX_MODE_WCI2		0x0
#define LTECX_MUX_MODE_GPIO		0x1


/* LTECX GPIO Information Index */
#define LTECX_NVRAM_FSYNC_IDX	0
#define LTECX_NVRAM_LTERX_IDX	1
#define LTECX_NVRAM_LTETX_IDX	2
#define LTECX_NVRAM_WLPRIO_IDX	3

/* LTECX WCI2 Information Index */
#define LTECX_NVRAM_WCI2IN_IDX	0
#define LTECX_NVRAM_WCI2OUT_IDX	1

/* LTECX: Macros to get GPIO/FNSEL/GCIGPIO */
#define LTECX_EXTRACT_PADNUM(val, idx)	(getbit8(&(val), (idx)))
#define LTECX_EXTRACT_FNSEL(val, idx)	(getbit4(&(val), (idx)))
#define LTECX_EXTRACT_GCIGPIO(val, idx)	(getbit4(&(val), (idx)))

/* WLAN channel numbers - used from wifi.h */

/* WLAN BW */
#define ECI_BW_20   0x0
#define ECI_BW_25   0x1
#define ECI_BW_30   0x2
#define ECI_BW_35   0x3
#define ECI_BW_40   0x4
#define ECI_BW_45   0x5
#define ECI_BW_50   0x6
#define ECI_BW_ALL  0x7

/* WLAN - number of antenna */
#define WLAN_NUM_ANT1 TXANT_0
#define WLAN_NUM_ANT2 TXANT_1

/* otpctrl1 0xF4 */
#define OTPC_FORCE_PWR_OFF	0x02000000
/* chipcommon s/r registers introduced with cc rev >= 48 */
#define CC_SR_CTL0_ENABLE_MASK             0x1
#define CC_SR_CTL0_ENABLE_SHIFT              0
#define CC_SR_CTL0_EN_SR_ENG_CLK_SHIFT       1 /* sr_clk to sr_memory enable */
#define CC_SR_CTL0_RSRC_TRIGGER_SHIFT        2 /* Rising edge resource trigger 0 to sr_engine  */
#define CC_SR_CTL0_MIN_DIV_SHIFT             6 /* Min division value for fast clk in sr_engine */
#define CC_SR_CTL0_EN_SBC_STBY_SHIFT        16 /* Allow Subcore mem StandBy? */
#define CC_SR_CTL0_EN_SR_ALP_CLK_MASK_SHIFT 18
#define CC_SR_CTL0_EN_SR_HT_CLK_SHIFT       19
#define CC_SR_CTL0_ALLOW_PIC_SHIFT          20 /* Allow pic to separate power domains */
#define CC_SR_CTL0_MAX_SR_LQ_CLK_CNT_SHIFT  25
#define CC_SR_CTL0_EN_MEM_DISABLE_FOR_SLEEP 30

#define CC_SR_CTL1_SR_INIT_MASK             0x3FF
#define CC_SR_CTL1_SR_INIT_SHIFT            0

#define	ECI_INLO_PKTDUR_MASK	0x000000f0 /* [7:4] - 4 bits */
#define ECI_INLO_PKTDUR_SHIFT	4

/* gci chip control bits */
#define GCI_GPIO_CHIPCTRL_ENAB_IN_BIT		0
#define GCI_GPIO_CHIPCTRL_ENAB_OP_BIT		1
#define GCI_GPIO_CHIPCTRL_INVERT_BIT		2
#define GCI_GPIO_CHIPCTRL_PULLUP_BIT		3
#define GCI_GPIO_CHIPCTRL_PULLDN_BIT		4
#define GCI_GPIO_CHIPCTRL_ENAB_BTSIG_BIT	5
#define GCI_GPIO_CHIPCTRL_ENAB_OD_OP_BIT	6
#define GCI_GPIO_CHIPCTRL_ENAB_EXT_GPIO_BIT	7

/* gci GPIO input status bits */
#define GCI_GPIO_STS_VALUE_BIT			0
#define GCI_GPIO_STS_POS_EDGE_BIT		1
#define GCI_GPIO_STS_NEG_EDGE_BIT		2
#define GCI_GPIO_STS_FAST_EDGE_BIT		3
#define GCI_GPIO_STS_CLEAR			0xF

#define GCI_GPIO_STS_VALUE	(1 << GCI_GPIO_STS_VALUE_BIT)

#endif	/* _SBCHIPC_H */
