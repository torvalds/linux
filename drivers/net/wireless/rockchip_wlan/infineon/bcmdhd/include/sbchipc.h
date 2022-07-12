/*
 * SiliconBackplane Chipcommon core hardware definitions.
 *
 * The chipcommon core provides chip identification, SB control,
 * JTAG, 0/1/2 UARTs, clock frequency control, a watchdog interrupt timer,
 * GPIO interface, extbus, and support for serial and parallel flashes.
 *
 * $Id: sbchipc.h 701163 2017-05-23 22:21:03Z $
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
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

#define BCM_MASK32(msb, lsb)	((~0u >> (32u - (msb) - 1u)) & (~0u << (lsb)))

/**
 * In chipcommon rev 49 the pmu registers have been moved from chipc to the pmu core if the
 * 'AOBPresent' bit of 'CoreCapabilitiesExt' is set. If this field is set, the traditional chipc to
 * [pmu|gci|sreng] register interface is deprecated and removed. These register blocks would instead
 * be assigned their respective chipc-specific address space and connected to the Always On
 * Backplane via the APB interface.
 */
typedef volatile struct {
	uint32  PAD[384];
	uint32  pmucontrol;             /* 0x600 */
	uint32  pmucapabilities;        /* 0x604 */
	uint32  pmustatus;              /* 0x608 */
	uint32  res_state;              /* 0x60C */
	uint32  res_pending;            /* 0x610 */
	uint32  pmutimer;               /* 0x614 */
	uint32  min_res_mask;           /* 0x618 */
	uint32  max_res_mask;           /* 0x61C */
	uint32  res_table_sel;          /* 0x620 */
	uint32  res_dep_mask;
	uint32  res_updn_timer;
	uint32  res_timer;
	uint32  clkstretch;
	uint32  pmuwatchdog;
	uint32  gpiosel;                /* 0x638, rev >= 1 */
	uint32  gpioenable;             /* 0x63c, rev >= 1 */
	uint32  res_req_timer_sel;      /* 0x640 */
	uint32  res_req_timer;          /* 0x644 */
	uint32  res_req_mask;           /* 0x648 */
	uint32	core_cap_ext;           /* 0x64C */
	uint32  chipcontrol_addr;       /* 0x650 */
	uint32  chipcontrol_data;       /* 0x654 */
	uint32  regcontrol_addr;
	uint32  regcontrol_data;
	uint32  pllcontrol_addr;
	uint32  pllcontrol_data;
	uint32  pmustrapopt;            /* 0x668, corerev >= 28 */
	uint32  pmu_xtalfreq;           /* 0x66C, pmurev >= 10 */
	uint32  retention_ctl;          /* 0x670 */
	uint32  ILPPeriod;              /* 0x674 */
	uint32  PAD[2];
	uint32  retention_grpidx;       /* 0x680 */
	uint32  retention_grpctl;       /* 0x684 */
	uint32  mac_res_req_timer;      /* 0x688 */
	uint32  mac_res_req_mask;       /* 0x68c */
	uint32  PAD[18];
	uint32  pmucontrol_ext;         /* 0x6d8 */
	uint32  slowclkperiod;          /* 0x6dc */
	uint32	pmu_statstimer_addr;	/* 0x6e0 */
	uint32	pmu_statstimer_ctrl;	/* 0x6e4 */
	uint32	pmu_statstimer_N;		/* 0x6e8 */
	uint32	PAD[1];
	uint32  mac_res_req_timer1;	/* 0x6f0 */
	uint32  mac_res_req_mask1;	/* 0x6f4 */
	uint32	PAD[2];
	uint32  pmuintmask0;            /* 0x700 */
	uint32  pmuintmask1;            /* 0x704 */
	uint32  PAD[14];
	uint32  pmuintstatus;           /* 0x740 */
	uint32  extwakeupstatus;        /* 0x744 */
	uint32  watchdog_res_mask;      /* 0x748 */
	uint32  PAD[1];                 /* 0x74C */
	uint32  swscratch;              /* 0x750 */
	uint32  PAD[3];                 /* 0x754-0x75C */
	uint32	extwakemask0; /* 0x760 */
	uint32	extwakemask1; /* 0x764 */
	uint32  PAD[2];                 /* 0x768-0x76C */
	uint32  extwakereqmask[2];      /* 0x770-0x774 */
	uint32  PAD[2];                 /* 0x778-0x77C */
	uint32  pmuintctrl0;            /* 0x780 */
	uint32  pmuintctrl1;            /* 0x784 */
	uint32  PAD[2];
	uint32  extwakectrl[2];         /* 0x790 */
	uint32  PAD[7];
	uint32  fis_ctrl_status;        /* 0x7b4 */
	uint32  fis_min_res_mask;       /* 0x7b8 */
	uint32  PAD[1];
	uint32	PrecisionTmrCtrlStatus;	/* 0x7c0 */
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
	uint32  powerctl;		/* 0x1e8 */
	uint32  PAD[69];

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
	uint32	PAD[50];
	uint32	sr_memrw_addr;		/* 0x4d0 */
	uint32	sr_memrw_data;		/* 0x4d4 */
	uint32	PAD[10];

	/* save/restore, corerev >= 48 */
	uint32	sr_capability;		/* 0x500 */
	uint32	sr_control0;		/* 0x504 */
	uint32	sr_control1;		/* 0x508 */
	uint32  gpio_control;		/* 0x50C */
	uint32	PAD[29];
	/* 2 SR engines case */
	uint32	sr1_control0;		/* 0x584 */
	uint32	sr1_control1;		/* 0x588 */
	uint32	PAD[29];
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
	uint32	core_cap_ext;		/* 0x64c */
	uint32	chipcontrol_addr;	/* 0x650 */
	uint32	chipcontrol_data;	/* 0x654 */
	uint32	regcontrol_addr;
	uint32	regcontrol_data;
	uint32	pllcontrol_addr;
	uint32	pllcontrol_data;
	uint32	pmustrapopt;		/* 0x668, corerev >= 28 */
	uint32	pmu_xtalfreq;		/* 0x66C, pmurev >= 10 */
	uint32  retention_ctl;		/* 0x670 */
	uint32	ILPPeriod;		/* 0x674 */
	uint32  PAD[2];
	uint32  retention_grpidx;	/* 0x680 */
	uint32  retention_grpctl;	/* 0x684 */
	uint32  mac_res_req_timer;	/* 0x688 */
	uint32  mac_res_req_mask;	/* 0x68c */
	uint32  PAD[18];
	uint32	pmucontrol_ext;		/* 0x6d8 */
	uint32	slowclkperiod;		/* 0x6dc */
	uint32	pmu_statstimer_addr;	/* 0x6e0 */
	uint32	pmu_statstimer_ctrl;	/* 0x6e4 */
	uint32	pmu_statstimer_N;		/* 0x6e8 */
	uint32	PAD[1];
	uint32  mac_res_req_timer1;	/* 0x6f0 */
	uint32  mac_res_req_mask1;	/* 0x6f4 */
	uint32	PAD[2];
	uint32	pmuintmask0;		/* 0x700 */
	uint32	pmuintmask1;		/* 0x704 */
	uint32  PAD[14];
	uint32  pmuintstatus;		/* 0x740 */
	uint32  extwakeupstatus;	/* 0x744 */
	uint32	PAD[6];
	uint32  extwakemask0;		/* 0x760 */
	uint32	extwakemask1; /* 0x764 */
	uint32	PAD[2];		/* 0x768-0x76C */
	uint32	extwakereqmask[2]; /* 0x770-0x774 */
	uint32	PAD[2];		/* 0x778-0x77C */
	uint32  pmuintctrl0;		/* 0x780 */
	uint32  PAD[3];			/* 0x784 - 0x78c */
	uint32  extwakectrl[1];		/* 0x790 */
	uint32  PAD[8];
	uint32  fis_ctrl_status;        /* 0x7b4 */
	uint32  fis_min_res_mask;       /* 0x7b8 */
	uint32  PAD[17];
	uint16	sromotp[512];		/* 0x800 */
#ifdef CCNFLASH_SUPPORT
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
#endif /* CCNFLASH_SUPPORT */
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
	uint32  gci_eventsummary; /* 0xC50 */
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
	uint32  gci_seciuartescval; /* DD0 */
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
#define	CC_CAP_EXT		0xac
#define	CC_SYS_CLK_CTL		0xc0
#define	CC_CLKDIV2		0xf0
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
#define PMU_PLL_CONTROL_ADDR	0x660
#define PMU_PLL_CONTROL_DATA	0x664

#define CC_SROM_CTRL		0x190
#define CC_SROM_ADDRESS		0x194u
#define CC_SROM_DATA		0x198u
#ifdef SROM16K_4364_ADDRSPACE
#define	CC_SROM_OTP		0xa000		/* SROM/OTP address space */
#else
#define	CC_SROM_OTP		0x0800
#endif // endif
#define CC_GCI_INDIRECT_ADDR_REG	0xC40
#define CC_GCI_CHIP_CTRL_REG	0xE00
#define CC_GCI_CC_OFFSET_2	2
#define CC_GCI_CC_OFFSET_5	5
#define CC_SWD_CTRL		0x380
#define CC_SWD_REQACK		0x384
#define CC_SWD_DATA		0x388
#define GPIO_SEL_0					0x00001111
#define GPIO_SEL_1					0x11110000
#define GPIO_SEL_8					0x00001111
#define GPIO_SEL_9					0x11110000

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
#define PMUREG_RESREQ_TIMER 0x688
#define PMUREG_RESREQ_MASK1 0x6f4
#define PMUREG_RESREQ_TIMER1 0x6f0
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
#define CC_BP_IND_ACCESS_START_SHIFT		9
#define CC_BP_IND_ACCESS_START_MASK		(1 << CC_BP_IND_ACCESS_START_SHIFT)
#define CC_BP_IND_ACCESS_RDWR_SHIFT		8
#define CC_BP_IND_ACCESS_RDWR_MASK		(1 << CC_BP_IND_ACCESS_RDWR_SHIFT)
#define CC_BP_IND_ACCESS_ERROR_SHIFT		10
#define CC_BP_IND_ACCESS_ERROR_MASK		(1 << CC_BP_IND_ACCESS_ERROR_SHIFT)

#define LPO_SEL_TIMEOUT 1000

#define LPO_FINAL_SEL_SHIFT 18

#define LHL_LPO1_SEL 0
#define LHL_LPO2_SEL 0x1
#define LHL_32k_SEL 0x2
#define LHL_EXT_SEL  0x3

#define EXTLPO_BUF_PD	0x40
#define LPO1_PD_EN	0x1
#define LPO1_PD_SEL	0x6
#define LPO1_PD_SEL_VAL	0x4
#define LPO2_PD_EN	0x8
#define LPO2_PD_SEL	0x30
#define LPO2_PD_SEL_VAL	0x20
#define OSC_32k_PD	0x80

#define LHL_CLK_DET_CTL_AD_CNTR_CLK_SEL	0x3

#define LHL_LPO_AUTO	0x0
#define LHL_LPO1_ENAB	0x1
#define LHL_LPO2_ENAB	0x2
#define LHL_OSC_32k_ENAB	0x3
#define LHL_EXT_LPO_ENAB	0x4
#define RADIO_LPO_ENAB 0x5

#define LHL_CLK_DET_CTL_ADR_LHL_CNTR_EN	0x4
#define LHL_CLK_DET_CTL_ADR_LHL_CNTR_CLR	0x8
#define LHL_CLK_DET_CNT		0xF0
#define LHL_CLK_DET_CNT_SHIFT   4
#define LPO_SEL_SHIFT		9

#define LHL_MAIN_CTL_ADR_FINAL_CLK_SEL	0x3C0000
#define LHL_MAIN_CTL_ADR_LHL_WLCLK_SEL	0x600

#define CLK_DET_CNT_THRESH	8

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

#ifdef CCNFLASH_SUPPORT
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
#endif /* CCNFLASH_SUPPORT */

/* chipid */
#define	CID_ID_MASK		0x0000ffff	/**< Chip Id mask */
#define	CID_REV_MASK		0x000f0000	/**< Chip Revision mask */
#define	CID_REV_SHIFT		16		/**< Chip Revision shift */
#define	CID_PKG_MASK		0x00f00000	/**< Package Option mask */
#define	CID_PKG_SHIFT		20		/**< Package Option shift */
#define	CID_CC_MASK		0x0f000000	/**< CoreCount (corerev >= 4) */
#define CID_CC_SHIFT		24
#define	CID_TYPE_MASK		0xf0000000	/**< Chip Type */
#define CID_TYPE_SHIFT		28

/* capabilities */
#define	CC_CAP_UARTS_MASK	0x00000003	/**< Number of UARTs */
#define CC_CAP_MIPSEB		0x00000004	/**< MIPS is in big-endian mode */
#define CC_CAP_UCLKSEL		0x00000018	/**< UARTs clock select */
#define CC_CAP_UINTCLK		0x00000008	/**< UARTs are driven by internal divided clock */
#define CC_CAP_UARTGPIO		0x00000020	/**< UARTs own GPIOs 15:12 */
#define CC_CAP_EXTBUS_MASK	0x000000c0	/**< External bus mask */
#define CC_CAP_EXTBUS_NONE	0x00000000	/**< No ExtBus present */
#define CC_CAP_EXTBUS_FULL	0x00000040	/**< ExtBus: PCMCIA, IDE & Prog */
#define CC_CAP_EXTBUS_PROG	0x00000080	/**< ExtBus: ProgIf only */
#define	CC_CAP_FLASH_MASK	0x00000700	/**< Type of flash */
#define	CC_CAP_PLL_MASK		0x00038000	/**< Type of PLL */
#define CC_CAP_PWR_CTL		0x00040000	/**< Power control */
#define CC_CAP_OTPSIZE		0x00380000	/**< OTP Size (0 = none) */
#define CC_CAP_OTPSIZE_SHIFT	19		/**< OTP Size shift */
#define CC_CAP_OTPSIZE_BASE	5		/**< OTP Size base */
#define CC_CAP_JTAGP		0x00400000	/**< JTAG Master Present */
#define CC_CAP_ROM		0x00800000	/**< Internal boot rom active */
#define CC_CAP_BKPLN64		0x08000000	/**< 64-bit backplane */
#define	CC_CAP_PMU		0x10000000	/**< PMU Present, rev >= 20 */
#define	CC_CAP_ECI		0x20000000	/**< ECI Present, rev >= 21 */
#define	CC_CAP_SROM		0x40000000	/**< Srom Present, rev >= 32 */
#define	CC_CAP_NFLASH		0x80000000	/**< Nand flash present, rev >= 35 */

#define	CC_CAP2_SECI		0x00000001	/**< SECI Present, rev >= 36 */
#define	CC_CAP2_GSIO		0x00000002	/**< GSIO (spi/i2c) present, rev >= 37 */

/* capabilities extension */
#define CC_CAP_EXT_SECI_PRESENT				0x00000001	/**< SECI present */
#define CC_CAP_EXT_GSIO_PRESENT				0x00000002	/**< GSIO present */
#define CC_CAP_EXT_GCI_PRESENT  			0x00000004	/**< GCI present */
#define CC_CAP_EXT_SECI_PUART_PRESENT		0x00000008  /**< UART present */
#define CC_CAP_EXT_AOB_PRESENT  			0x00000040	/**< AOB present */
#define CC_CAP_EXT_SWD_PRESENT  			0x00000400	/**< SWD present */

/* WL Channel Info to BT via GCI - bits 40 - 47 */
#define GCI_WL_CHN_INFO_MASK	(0xFF00)
/* WL indication of MCHAN enabled/disabled to BT in awdl mode- bit 36 */
#define GCI_WL_MCHAN_BIT_MASK	(0x0010)

#ifdef WLC_SW_DIVERSITY
/* WL indication of SWDIV enabled/disabled to BT - bit 33 */
#define GCI_WL_SWDIV_ANT_VALID_BIT_MASK	(0x0002)
#define GCI_SWDIV_ANT_VALID_SHIFT 0x1
#define GCI_SWDIV_ANT_VALID_DISABLE 0x0
#endif // endif

/* WL Strobe to BT */
#define GCI_WL_STROBE_BIT_MASK	(0x0020)
/* bits [51:48] - reserved for wlan TX pwr index */
/* bits [55:52] btc mode indication */
#define GCI_WL_BTC_MODE_SHIFT	(20)
#define GCI_WL_BTC_MODE_MASK	(0xF << GCI_WL_BTC_MODE_SHIFT)
#define GCI_WL_ANT_BIT_MASK	(0x00c0)
#define GCI_WL_ANT_SHIFT_BITS	(6)
/* PLL type */
#define PLL_NONE		0x00000000
#define PLL_TYPE1		0x00010000	/**< 48MHz base, 3 dividers */
#define PLL_TYPE2		0x00020000	/**< 48MHz, 4 dividers */
#define PLL_TYPE3		0x00030000	/**< 25MHz, 2 dividers */
#define PLL_TYPE4		0x00008000	/**< 48MHz, 4 dividers */
#define PLL_TYPE5		0x00018000	/**< 25MHz, 4 dividers */
#define PLL_TYPE6		0x00028000	/**< 100/200 or 120/240 only */
#define PLL_TYPE7		0x00038000	/**< 25MHz, 4 dividers */

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

#define ALP_CLOCK_53573		40000000

/* HT clock */
#define	HT_CLOCK		80000000

/* corecontrol */
#define CC_UARTCLKO		0x00000001	/**< Drive UART with internal clock */
#define	CC_SE			0x00000002	/**< sync clk out enable (corerev >= 3) */
#define CC_ASYNCGPIO	0x00000004	/**< 1=generate GPIO interrupt without backplane clock */
#define CC_UARTCLKEN		0x00000008	/**< enable UART Clock (corerev > = 21 */

/* retention_ctl */
#define RCTL_MEM_RET_SLEEP_LOG_SHIFT	29
#define RCTL_MEM_RET_SLEEP_LOG_MASK	(1 << RCTL_MEM_RET_SLEEP_LOG_SHIFT)

/* 4321 chipcontrol */
#define CHIPCTRL_4321_PLL_DOWN	0x800000	/**< serdes PLL down override */

/* Fields in the otpstatus register in rev >= 21 */
#define OTPS_OL_MASK		0x000000ff
#define OTPS_OL_MFG		0x00000001	/**< manuf row is locked */
#define OTPS_OL_OR1		0x00000002	/**< otp redundancy row 1 is locked */
#define OTPS_OL_OR2		0x00000004	/**< otp redundancy row 2 is locked */
#define OTPS_OL_GU		0x00000008	/**< general use region is locked */
#define OTPS_GUP_MASK		0x00000f00
#define OTPS_GUP_SHIFT		8
#define OTPS_GUP_HW		0x00000100	/**< h/w subregion is programmed */
#define OTPS_GUP_SW		0x00000200	/**< s/w subregion is programmed */
#define OTPS_GUP_CI		0x00000400	/**< chipid/pkgopt subregion is programmed */
#define OTPS_GUP_FUSE		0x00000800	/**< fuse subregion is programmed */
#define OTPS_READY		0x00001000
#define OTPS_RV(x)		(1 << (16 + (x)))	/**< redundancy entry valid */
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
#define OTPL_WRAP_TYPE_28NM	2
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

/* Opcodes for OTPP_OC field (28NM) */
#define OTPPOC_READ_28NM	0
#define OTPPOC_READBURST_28NM	1
#define OTPPOC_PROG_ENABLE_28NM 2
#define OTPPOC_PROG_DISABLE_28NM	3
#define OTPPOC_PRESCREEN_28NM	4
#define OTPPOC_PRESCREEN_RP_28NM	5
#define OTPPOC_FLUSH_28NM	6
#define OTPPOC_NOP_28NM	7
#define OTPPOC_PROG_ECC_28NM	8
#define OTPPOC_PROG_ECC_READ_28NM	9
#define OTPPOC_PROG_28NM	10
#define OTPPOC_PROGRAM_RP_28NM	11
#define OTPPOC_PROGRAM_OVST_28NM	12
#define OTPPOC_RELOAD_28NM	13
#define OTPPOC_ERASE_28NM	14
#define OTPPOC_LOAD_RF_28NM	15
#define OTPPOC_CTRL_WR_28NM 16
#define OTPPOC_CTRL_RD_28NM	17
#define OTPPOC_READ_HP_28NM	18
#define OTPPOC_READ_OVST_28NM	19
#define OTPPOC_READ_VERIFY0_28NM	20
#define OTPPOC_READ_VERIFY1_28NM	21
#define OTPPOC_READ_FORCE0_28NM	22
#define OTPPOC_READ_FORCE1_28NM	23
#define OTPPOC_BURNIN_28NM	24
#define OTPPOC_PROGRAM_LOCK_28NM	25
#define OTPPOC_PROGRAM_TESTCOL_28NM	26
#define OTPPOC_READ_TESTCOL_28NM	27
#define OTPPOC_READ_FOUT_28NM	28
#define OTPPOC_SFT_RESET_28NM	29

#define OTPP_OC_MASK_28NM		0x0f800000
#define OTPP_OC_SHIFT_28NM		23
#define OTPC_PROGEN_28NM		0x8
#define OTPC_DBLERRCLR		0x20
#define OTPC_CLK_EN_MASK	0x00000040
#define OTPC_CLK_DIV_MASK	0x00000F80

/* Fields in otplayoutextension */
#define OTPLAYOUTEXT_FUSE_MASK	0x3FF

/* Jtagm characteristics that appeared at a given corerev */
#define	JTAGM_CREV_OLD		10	/**< Old command set, 16bit max IR */
#define	JTAGM_CREV_IRP		22	/**< Able to do pause-ir */
#define	JTAGM_CREV_RTI		28	/**< Able to do return-to-idle */

/* jtagcmd */
#define JCMD_START		0x80000000
#define JCMD_BUSY		0x80000000
#define JCMD_STATE_MASK		0x60000000
#define JCMD_STATE_TLR		0x00000000	/**< Test-logic-reset */
#define JCMD_STATE_PIR		0x20000000	/**< Pause IR */
#define JCMD_STATE_PDR		0x40000000	/**< Pause DR */
#define JCMD_STATE_RTI		0x60000000	/**< Run-test-idle */
#define JCMD0_ACC_MASK		0x0000f000
#define JCMD0_ACC_IRDR		0x00000000
#define JCMD0_ACC_DR		0x00001000
#define JCMD0_ACC_IR		0x00002000
#define JCMD0_ACC_RESET		0x00003000
#define JCMD0_ACC_IRPDR		0x00004000
#define JCMD0_ACC_PDR		0x00005000
#define JCMD0_IRW_MASK		0x00000f00
#define JCMD_ACC_MASK		0x000f0000	/**< Changes for corerev 11 */
#define JCMD_ACC_IRDR		0x00000000
#define JCMD_ACC_DR		0x00010000
#define JCMD_ACC_IR		0x00020000
#define JCMD_ACC_RESET		0x00030000
#define JCMD_ACC_IRPDR		0x00040000
#define JCMD_ACC_PDR		0x00050000
#define JCMD_ACC_PIR		0x00060000
#define JCMD_ACC_IRDR_I		0x00070000	/**< rev 28: return to run-test-idle */
#define JCMD_ACC_DR_I		0x00080000	/**< rev 28: return to run-test-idle */
#define JCMD_IRW_MASK		0x00001f00
#define JCMD_IRW_SHIFT		8
#define JCMD_DRW_MASK		0x0000003f

/* jtagctrl */
#define JCTRL_FORCE_CLK		4		/**< Force clock */
#define JCTRL_EXT_EN		2		/**< Enable external targets */
#define JCTRL_EN		1		/**< Enable Jtag master */
#define JCTRL_TAPSEL_BIT	0x00000008	/**< JtagMasterCtrl tap_sel bit */

/* swdmasterctrl */
#define SWDCTRL_INT_EN		8		/**< Enable internal targets */
#define SWDCTRL_FORCE_CLK	4		/**< Force clock */
#define SWDCTRL_OVJTAG		2		/**< Enable shared SWD/JTAG pins */
#define SWDCTRL_EN		1		/**< Enable Jtag master */

/* Fields in clkdiv */
#define	CLKD_SFLASH		0x1f000000
#define	CLKD_SFLASH_SHIFT	24
#define	CLKD_OTP		0x000f0000
#define	CLKD_OTP_SHIFT		16
#define	CLKD_JTAG		0x00000f00
#define	CLKD_JTAG_SHIFT		8
#define	CLKD_UART		0x000000ff

#define	CLKD2_SROM		0x00000007
#define	CLKD2_SROMDIV_32	0
#define	CLKD2_SROMDIV_64	1
#define	CLKD2_SROMDIV_96	2
#define	CLKD2_SROMDIV_128	3
#define	CLKD2_SROMDIV_192	4
#define	CLKD2_SROMDIV_256	5
#define	CLKD2_SROMDIV_384	6
#define	CLKD2_SROMDIV_512	7
#define	CLKD2_SWD		0xf8000000
#define	CLKD2_SWD_SHIFT		27

/* intstatus/intmask */
#define	CI_GPIO			0x00000001	/**< gpio intr */
#define	CI_EI			0x00000002	/**< extif intr (corerev >= 3) */
#define	CI_TEMP			0x00000004	/**< temp. ctrl intr (corerev >= 15) */
#define	CI_SIRQ			0x00000008	/**< serial IRQ intr (corerev >= 15) */
#define	CI_ECI			0x00000010	/**< eci intr (corerev >= 21) */
#define	CI_PMU			0x00000020	/**< pmu intr (corerev >= 21) */
#define	CI_UART			0x00000040	/**< uart intr (corerev >= 21) */
#define	CI_WECI			0x00000080	/* eci wakeup intr (corerev >= 21) */
#define	CI_WDRESET		0x80000000	/**< watchdog reset occurred */

/* slow_clk_ctl */
#define SCC_SS_MASK		0x00000007	/**< slow clock source mask */
#define	SCC_SS_LPO		0x00000000	/**< source of slow clock is LPO */
#define	SCC_SS_XTAL		0x00000001	/**< source of slow clock is crystal */
#define	SCC_SS_PCI		0x00000002	/**< source of slow clock is PCI */
#define SCC_LF			0x00000200	/**< LPOFreqSel, 1: 160Khz, 0: 32KHz */
#define SCC_LP			0x00000400	/**< LPOPowerDown, 1: LPO is disabled,
						 * 0: LPO is enabled
						 */
#define SCC_FS			0x00000800 /**< ForceSlowClk, 1: sb/cores running on slow clock,
						 * 0: power logic control
						 */
#define SCC_IP			0x00001000 /**< IgnorePllOffReq, 1/0: power logic ignores/honors
						 * PLL clock disable requests from core
						 */
#define SCC_XC			0x00002000	/**< XtalControlEn, 1/0: power logic does/doesn't
						 * disable crystal when appropriate
						 */
#define SCC_XP			0x00004000	/**< XtalPU (RO), 1/0: crystal running/disabled */
#define SCC_CD_MASK		0xffff0000	/**< ClockDivider (SlowClk = 1/(4+divisor)) */
#define SCC_CD_SHIFT		16

/* system_clk_ctl */
#define	SYCC_IE			0x00000001	/**< ILPen: Enable Idle Low Power */
#define	SYCC_AE			0x00000002	/**< ALPen: Enable Active Low Power */
#define	SYCC_FP			0x00000004	/**< ForcePLLOn */
#define	SYCC_AR			0x00000008	/**< Force ALP (or HT if ALPen is not set */
#define	SYCC_HR			0x00000010	/**< Force HT */
#define SYCC_CD_MASK		0xffff0000	/**< ClkDiv  (ILP = 1/(4 * (divisor + 1)) */
#define SYCC_CD_SHIFT		16

/* watchdogcounter */
/* WL sub-system reset */
#define WD_SSRESET_PCIE_F0_EN			0x10000000
/* BT sub-system reset */
#define WD_SSRESET_PCIE_F1_EN			0x20000000
#define WD_SSRESET_PCIE_F2_EN			0x40000000
/* Both WL and BT sub-system reset */
#define WD_SSRESET_PCIE_ALL_FN_EN		0x80000000
#define WD_COUNTER_MASK				0x0fffffff
#define WD_ENABLE_MASK	\
	(WD_SSRESET_PCIE_F0_EN | WD_SSRESET_PCIE_F1_EN | \
	WD_SSRESET_PCIE_F2_EN | WD_SSRESET_PCIE_ALL_FN_EN)

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
#define	CF_EN			0x00000001	/**< enable */
#define	CF_EM_MASK		0x0000000e	/**< mode */
#define	CF_EM_SHIFT		1
#define	CF_EM_FLASH		0		/**< flash/asynchronous mode */
#define	CF_EM_SYNC		2		/**< synchronous mode */
#define	CF_EM_PCMCIA		4		/**< pcmcia mode */
#define	CF_DS			0x00000010	/**< destsize:  0=8bit, 1=16bit */
#define	CF_BS			0x00000020	/**< byteswap */
#define	CF_CD_MASK		0x000000c0	/**< clock divider */
#define	CF_CD_SHIFT		6
#define	CF_CD_DIV2		0x00000000	/**< backplane/2 */
#define	CF_CD_DIV3		0x00000040	/**< backplane/3 */
#define	CF_CD_DIV4		0x00000080	/**< backplane/4 */
#define	CF_CE			0x00000100	/**< clock enable */
#define	CF_SB			0x00000200	/**< size/bytestrobe (synch only) */

/* pcmcia_memwait */
#define	PM_W0_MASK		0x0000003f	/**< waitcount0 */
#define	PM_W1_MASK		0x00001f00	/**< waitcount1 */
#define	PM_W1_SHIFT		8
#define	PM_W2_MASK		0x001f0000	/**< waitcount2 */
#define	PM_W2_SHIFT		16
#define	PM_W3_MASK		0x1f000000	/**< waitcount3 */
#define	PM_W3_SHIFT		24

/* pcmcia_attrwait */
#define	PA_W0_MASK		0x0000003f	/**< waitcount0 */
#define	PA_W1_MASK		0x00001f00	/**< waitcount1 */
#define	PA_W1_SHIFT		8
#define	PA_W2_MASK		0x001f0000	/**< waitcount2 */
#define	PA_W2_SHIFT		16
#define	PA_W3_MASK		0x1f000000	/**< waitcount3 */
#define	PA_W3_SHIFT		24

/* pcmcia_iowait */
#define	PI_W0_MASK		0x0000003f	/**< waitcount0 */
#define	PI_W1_MASK		0x00001f00	/**< waitcount1 */
#define	PI_W1_SHIFT		8
#define	PI_W2_MASK		0x001f0000	/**< waitcount2 */
#define	PI_W2_SHIFT		16
#define	PI_W3_MASK		0x1f000000	/**< waitcount3 */
#define	PI_W3_SHIFT		24

/* prog_waitcount */
#define	PW_W0_MASK		0x0000001f	/**< waitcount0 */
#define	PW_W1_MASK		0x00001f00	/**< waitcount1 */
#define	PW_W1_SHIFT		8
#define	PW_W2_MASK		0x001f0000	/**< waitcount2 */
#define	PW_W2_SHIFT		16
#define	PW_W3_MASK		0x1f000000	/**< waitcount3 */
#define	PW_W3_SHIFT		24

#define PW_W0       		0x0000000c
#define PW_W1       		0x00000a00
#define PW_W2       		0x00020000
#define PW_W3       		0x01000000

/* flash_waitcount */
#define	FW_W0_MASK		0x0000003f	/**< waitcount0 */
#define	FW_W1_MASK		0x00001f00	/**< waitcount1 */
#define	FW_W1_SHIFT		8
#define	FW_W2_MASK		0x001f0000	/**< waitcount2 */
#define	FW_W2_SHIFT		16
#define	FW_W3_MASK		0x1f000000	/**< waitcount3 */
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
#define PCTL_PLL_PLLCTL_UPD	0x00000400	/**< rev 2 */
#define PCTL_NOILP_ON_WAIT	0x00000200	/**< rev 1 */
#define	PCTL_HT_REQ_EN		0x00000100
#define	PCTL_ALP_REQ_EN		0x00000080
#define	PCTL_XTALFREQ_MASK	0x0000007c
#define	PCTL_XTALFREQ_SHIFT	2
#define	PCTL_ILP_DIV_EN		0x00000002
#define	PCTL_LPO_SEL		0x00000001

/* Fields in pmucontrol_ext */
#define PCTL_EXT_USE_LHL_TIMER	0x00000010
#define PCTL_EXT_FASTLPO_ENAB	0x00000080
#define PCTL_EXT_FASTLPO_SWENAB	0x00000200
#define PCTL_EXT_FASTSEQ_ENAB	0x00001000
#define PCTL_EXT_FASTLPO_PCIE_SWENAB	0x00004000  /**< rev33 for FLL1M */

#define DEFAULT_43012_MIN_RES_MASK		0x0f8bfe77

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

/* Fields in clkstretch */
#define CSTRETCH_HT		0xffff0000
#define CSTRETCH_ALP		0x0000ffff
#define CSTRETCH_REDUCE_8		0x00080008

/* gpiotimerval */
#define GPIO_ONTIME_SHIFT	16

/* clockcontrol_n */
#define	CN_N1_MASK		0x3f		/**< n1 control */
#define	CN_N2_MASK		0x3f00		/**< n2 control */
#define	CN_N2_SHIFT		8
#define	CN_PLLC_MASK		0xf0000		/**< pll control */
#define	CN_PLLC_SHIFT		16

/* clockcontrol_sb/pci/uart */
#define	CC_M1_MASK		0x3f		/**< m1 control */
#define	CC_M2_MASK		0x3f00		/**< m2 control */
#define	CC_M2_SHIFT		8
#define	CC_M3_MASK		0x3f0000	/**< m3 control */
#define	CC_M3_SHIFT		16
#define	CC_MC_MASK		0x1f000000	/**< mux control */
#define	CC_MC_SHIFT		24

/* N3M Clock control magic field values */
#define	CC_F6_2			0x02		/**< A factor of 2 in */
#define	CC_F6_3			0x03		/**< 6-bit fields like */
#define	CC_F6_4			0x05		/**< N1, M1 or M3 */
#define	CC_F6_5			0x09
#define	CC_F6_6			0x11
#define	CC_F6_7			0x21

#define	CC_F5_BIAS		5		/**< 5-bit fields get this added */

#define	CC_MC_BYPASS		0x08
#define	CC_MC_M1		0x04
#define	CC_MC_M1M2		0x02
#define	CC_MC_M1M2M3		0x01
#define	CC_MC_M1M3		0x11

/* Type 2 Clock control magic field values */
#define	CC_T2_BIAS		2		/**< n1, n2, m1 & m3 bias */
#define	CC_T2M2_BIAS		3		/**< m2 bias */

#define	CC_T2MC_M1BYP		1
#define	CC_T2MC_M2BYP		2
#define	CC_T2MC_M3BYP		4

/* Type 6 Clock control magic field values */
#define	CC_T6_MMASK		1		/**< bits of interest in m */
#define	CC_T6_M0		120000000	/**< sb clock for m = 0 */
#define	CC_T6_M1		100000000	/**< sb clock for m = 1 */
#define	SB2MIPS_T6(sb)		(2 * (sb))

/* Common clock base */
#define	CC_CLOCK_BASE1		24000000	/**< Half the clock freq */
#define CC_CLOCK_BASE2		12500000	/**< Alternate crystal on some PLLs */

/* Clock control values for 200MHz in 5350 */
#define	CLKC_5350_N		0x0311
#define	CLKC_5350_M		0x04020009

/* Flash types in the chipcommon capabilities register */
#define FLASH_NONE		0x000		/**< No flash */
#define SFLASH_ST		0x100		/**< ST serial flash */
#define SFLASH_AT		0x200		/**< Atmel serial flash */
#define NFLASH			0x300
#define	PFLASH			0x700		/**< Parallel flash */
#define QSPIFLASH_ST		0x800
#define QSPIFLASH_AT		0x900

/* Bits in the ExtBus config registers */
#define	CC_CFG_EN		0x0001		/**< Enable */
#define	CC_CFG_EM_MASK		0x000e		/**< Extif Mode */
#define	CC_CFG_EM_ASYNC		0x0000		/**<   Async/Parallel flash */
#define	CC_CFG_EM_SYNC		0x0002		/**<   Synchronous */
#define	CC_CFG_EM_PCMCIA	0x0004		/**<   PCMCIA */
#define	CC_CFG_EM_IDE		0x0006		/**<   IDE */
#define	CC_CFG_DS		0x0010		/**< Data size, 0=8bit, 1=16bit */
#define	CC_CFG_CD_MASK		0x00e0		/**< Sync: Clock divisor, rev >= 20 */
#define	CC_CFG_CE		0x0100		/**< Sync: Clock enable, rev >= 20 */
#define	CC_CFG_SB		0x0200		/**< Sync: Size/Bytestrobe, rev >= 20 */
#define	CC_CFG_IS		0x0400		/**< Extif Sync Clk Select, rev >= 20 */

/* ExtBus address space */
#define	CC_EB_BASE		0x1a000000	/**< Chipc ExtBus base address */
#define	CC_EB_PCMCIA_MEM	0x1a000000	/**< PCMCIA 0 memory base address */
#define	CC_EB_PCMCIA_IO		0x1a200000	/**< PCMCIA 0 I/O base address */
#define	CC_EB_PCMCIA_CFG	0x1a400000	/**< PCMCIA 0 config base address */
#define	CC_EB_IDE		0x1a800000	/**< IDE memory base */
#define	CC_EB_PCMCIA1_MEM	0x1a800000	/**< PCMCIA 1 memory base address */
#define	CC_EB_PCMCIA1_IO	0x1aa00000	/**< PCMCIA 1 I/O base address */
#define	CC_EB_PCMCIA1_CFG	0x1ac00000	/**< PCMCIA 1 config base address */
#define	CC_EB_PROGIF		0x1b000000	/**< ProgIF Async/Sync base address */

/* Start/busy bit in flashcontrol */
#define SFLASH_OPCODE		0x000000ff
#define SFLASH_ACTION		0x00000700
#define	SFLASH_CS_ACTIVE	0x00001000	/**< Chip Select Active, rev >= 20 */
#define SFLASH_START		0x80000000
#define SFLASH_BUSY		SFLASH_START

/* flashcontrol action codes */
#define	SFLASH_ACT_OPONLY	0x0000		/**< Issue opcode only */
#define	SFLASH_ACT_OP1D		0x0100		/**< opcode + 1 data byte */
#define	SFLASH_ACT_OP3A		0x0200		/**< opcode + 3 addr bytes */
#define	SFLASH_ACT_OP3A1D	0x0300		/**< opcode + 3 addr & 1 data bytes */
#define	SFLASH_ACT_OP3A4D	0x0400		/**< opcode + 3 addr & 4 data bytes */
#define	SFLASH_ACT_OP3A4X4D	0x0500		/**< opcode + 3 addr, 4 don't care & 4 data bytes */
#define	SFLASH_ACT_OP3A1X4D	0x0700		/**< opcode + 3 addr, 1 don't care & 4 data bytes */

/* flashcontrol action+opcodes for ST flashes */
#define SFLASH_ST_WREN		0x0006		/**< Write Enable */
#define SFLASH_ST_WRDIS		0x0004		/**< Write Disable */
#define SFLASH_ST_RDSR		0x0105		/**< Read Status Register */
#define SFLASH_ST_WRSR		0x0101		/**< Write Status Register */
#define SFLASH_ST_READ		0x0303		/**< Read Data Bytes */
#define SFLASH_ST_PP		0x0302		/**< Page Program */
#define SFLASH_ST_SE		0x02d8		/**< Sector Erase */
#define SFLASH_ST_BE		0x00c7		/**< Bulk Erase */
#define SFLASH_ST_DP		0x00b9		/**< Deep Power-down */
#define SFLASH_ST_RES		0x03ab		/**< Read Electronic Signature */
#define SFLASH_ST_CSA		0x1000		/**< Keep chip select asserted */
#define SFLASH_ST_SSE		0x0220		/**< Sub-sector Erase */

#define SFLASH_ST_READ4B	0x6313		/* Read Data Bytes in 4Byte address */
#define SFLASH_ST_PP4B		0x6312		/* Page Program in 4Byte address */
#define SFLASH_ST_SE4B		0x62dc		/* Sector Erase in 4Byte address */
#define SFLASH_ST_SSE4B		0x6221		/* Sub-sector Erase */

#define SFLASH_MXIC_RDID	0x0390		/* Read Manufacture ID */
#define SFLASH_MXIC_MFID	0xc2		/* MXIC Manufacture ID */

/* Status register bits for ST flashes */
#define SFLASH_ST_WIP		0x01		/**< Write In Progress */
#define SFLASH_ST_WEL		0x02		/**< Write Enable Latch */
#define SFLASH_ST_BP_MASK	0x1c		/**< Block Protect */
#define SFLASH_ST_BP_SHIFT	2
#define SFLASH_ST_SRWD		0x80		/**< Status Register Write Disable */

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

/* GCI UART Function sel related */
#define MUXENAB_GCI_UART_MASK		(0x00000f00)
#define MUXENAB_GCI_UART_SHIFT		8
#define MUXENAB_GCI_UART_FNSEL_MASK	(0x00003000)
#define MUXENAB_GCI_UART_FNSEL_SHIFT	12

/*
 * These are the UART port assignments, expressed as offsets from the base
 * register.  These assignments should hold for any serial port based on
 * a 8250, 16450, or 16550(A).
 */

#define UART_RX		0	/**< In:  Receive buffer (DLAB=0) */
#define UART_TX		0	/**< Out: Transmit buffer (DLAB=0) */
#define UART_DLL	0	/**< Out: Divisor Latch Low (DLAB=1) */
#define UART_IER	1	/**< In/Out: Interrupt Enable Register (DLAB=0) */
#define UART_DLM	1	/**< Out: Divisor Latch High (DLAB=1) */
#define UART_IIR	2	/**< In: Interrupt Identity Register  */
#define UART_FCR	2	/**< Out: FIFO Control Register */
#define UART_LCR	3	/**< Out: Line Control Register */
#define UART_MCR	4	/**< Out: Modem Control Register */
#define UART_LSR	5	/**< In:  Line Status Register */
#define UART_MSR	6	/**< In:  Modem Status Register */
#define UART_SCR	7	/**< I/O: Scratch Register */
#define UART_LCR_DLAB	0x80	/**< Divisor latch access bit */
#define UART_LCR_WLEN8	0x03	/**< Word length: 8 bits */
#define UART_MCR_OUT2	0x08	/**< MCR GPIO out 2 */
#define UART_MCR_LOOP	0x10	/**< Enable loopback test mode */
#define UART_LSR_RX_FIFO 	0x80	/**< Receive FIFO error */
#define UART_LSR_TDHR		0x40	/**< Data-hold-register empty */
#define UART_LSR_THRE		0x20	/**< Transmit-hold-register empty */
#define UART_LSR_BREAK		0x10	/**< Break interrupt */
#define UART_LSR_FRAMING	0x08	/**< Framing error */
#define UART_LSR_PARITY		0x04	/**< Parity error */
#define UART_LSR_OVERRUN	0x02	/**< Overrun error */
#define UART_LSR_RXRDY		0x01	/**< Receiver ready */
#define UART_FCR_FIFO_ENABLE 1	/**< FIFO control register bit controlling FIFO enable/disable */

/* Interrupt Identity Register (IIR) bits */
#define UART_IIR_FIFO_MASK	0xc0	/**< IIR FIFO disable/enabled mask */
#define UART_IIR_INT_MASK	0xf	/**< IIR interrupt ID source */
#define UART_IIR_MDM_CHG	0x0	/**< Modem status changed */
#define UART_IIR_NOINT		0x1	/**< No interrupt pending */
#define UART_IIR_THRE		0x2	/**< THR empty */
#define UART_IIR_RCVD_DATA	0x4	/**< Received data available */
#define UART_IIR_RCVR_STATUS 	0x6	/**< Receiver status */
#define UART_IIR_CHAR_TIME 	0xc	/**< Character time */

/* Interrupt Enable Register (IER) bits */
#define UART_IER_PTIME	128	/**< Programmable THRE Interrupt Mode Enable */
#define UART_IER_EDSSI	8	/**< enable modem status interrupt */
#define UART_IER_ELSI	4	/**< enable receiver line status interrupt */
#define UART_IER_ETBEI  2	/**< enable transmitter holding register empty interrupt */
#define UART_IER_ERBFI	1	/**< enable data available interrupt */

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
#define	PST_ILPFASTLPO	0x00010000

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
#define PCAP5_PC_MASK	0x003e0000	/**< PMU corerev >= 5 */
#define PCAP5_PC_SHIFT	17
#define PCAP5_VC_MASK	0x07c00000
#define PCAP5_VC_SHIFT	22
#define PCAP5_CC_MASK	0xf8000000
#define PCAP5_CC_SHIFT	27

/* pmucapabilities ext */
#define PCAP_EXT_ST_NUM_SHIFT			(8)		/* stat timer number */
#define PCAP_EXT_ST_NUM_MASK			(0xf << PCAP_EXT_ST_NUM_SHIFT)
#define PCAP_EXT_ST_SRC_NUM_SHIFT		(12)	/* stat timer source number */
#define PCAP_EXT_ST_SRC_NUM_MASK		(0xf << PCAP_EXT_ST_SRC_NUM_SHIFT)

/* pmustattimer ctrl */
#define PMU_ST_SRC_SHIFT		(0)		/* stat timer source number */
#define PMU_ST_SRC_MASK			(0xff << PMU_ST_SRC_SHIFT)
#define PMU_ST_CNT_MODE_SHIFT	(10)	/* stat timer count mode */
#define PMU_ST_CNT_MODE_MASK	(0x3 << PMU_ST_CNT_MODE_SHIFT)
#define PMU_ST_EN_SHIFT		(8)		/* stat timer enable */
#define PMU_ST_EN_MASK		(0x1 << PMU_ST_EN_SHIFT)
#define PMU_ST_ENAB			1
#define PMU_ST_DISAB		0
#define PMU_ST_INT_EN_SHIFT	(9)		/* stat timer enable */
#define PMU_ST_INT_EN_MASK		(0x1 << PMU_ST_INT_EN_SHIFT)
#define PMU_ST_INT_ENAB		1
#define PMU_ST_INT_DISAB	0

/* CoreCapabilitiesExtension */
#define PCAP_EXT_USE_MUXED_ILP_CLK_MASK	0x04000000

/* PMU Resource Request Timer registers */
/* This is based on PmuRev0 */
#define	PRRT_TIME_MASK	0x03ff
#define	PRRT_INTEN	0x0400
/* ReqActive	25
 * The hardware sets this field to 1 when the timer expires.
 * Software writes this field to 1 to make immediate resource requests.
 */
#define	PRRT_REQ_ACTIVE	0x0800	/* To check h/w status */
#define	PRRT_IMMEDIATE_RES_REQ	0x0800	/* macro for sw immediate res req */
#define	PRRT_ALP_REQ	0x1000
#define	PRRT_HT_REQ	0x2000
#define PRRT_HQ_REQ 0x4000

/* PMU Int Control register bits */
#define PMU_INTC_ALP_REQ	0x1
#define PMU_INTC_HT_REQ		0x2
#define PMU_INTC_HQ_REQ		0x4

/* bit 0 of the PMU interrupt vector is asserted if this mask is enabled */
#define RSRC_INTR_MASK_TIMER_INT_0 1
#define PMU_INTR_MASK_EXTWAKE_REQ_ACTIVE_0 (1 << 20)

/* bit 16 of the PMU interrupt vector - Stats Timer Interrupt */
#define PMU_INT_STAT_TIMER_INT_SHIFT 16
#define PMU_INT_STAT_TIMER_INT_MASK (1 <<  PMU_INT_STAT_TIMER_INT_SHIFT)

/* PMU resource bit position */
#define PMURES_BIT(bit)	(1 << (bit))

/* PMU resource number limit */
#define PMURES_MAX_RESNUM	30

/* PMU chip control0 register */
#define	PMU_CHIPCTL0		0

#define PMU_CC0_4369_XTALCORESIZE_BIAS_ADJ_START_VAL	(0x20 << 0)
#define PMU_CC0_4369_XTALCORESIZE_BIAS_ADJ_START_MASK	(0x3F << 0)
#define PMU_CC0_4369_XTALCORESIZE_BIAS_ADJ_NORMAL_VAL	(0xF << 6)
#define PMU_CC0_4369_XTALCORESIZE_BIAS_ADJ_NORMAL_MASK	(0x3F << 6)
#define PMU_CC0_4369_XTAL_RES_BYPASS_START_VAL			(0 << 12)
#define PMU_CC0_4369_XTAL_RES_BYPASS_START_MASK			(0x7 << 12)
#define PMU_CC0_4369_XTAL_RES_BYPASS_NORMAL_VAL			(0x1 << 15)
#define PMU_CC0_4369_XTAL_RES_BYPASS_NORMAL_MASK		(0x7 << 15)

/* clock req types */
#define PMU_CC1_CLKREQ_TYPE_SHIFT	19
#define PMU_CC1_CLKREQ_TYPE_MASK	(1 << PMU_CC1_CLKREQ_TYPE_SHIFT)

#define CLKREQ_TYPE_CONFIG_OPENDRAIN		0
#define CLKREQ_TYPE_CONFIG_PUSHPULL		1

/* Power Control */
#define PWRCTL_ENAB_MEM_CLK_GATE_SHIFT		5
#define PWRCTL_AUTO_MEM_STBYRET			28

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

#define PMU_CC1_ENABLE_CLOSED_LOOP_MASK 0x00000080
#define PMU_CC1_ENABLE_CLOSED_LOOP      0x00000000

#define PMU_CC1_PWRSW_CLKSTRSTP_DELAY_MASK	0x00003F00u
#define PMU_CC1_PWRSW_CLKSTRSTP_DELAY		0x00000400u

/* PMU chip control2 register */
#define PMU_CC2_RFLDO3P3_PU_FORCE_ON		(1 << 15)
#define PMU_CC2_RFLDO3P3_PU_CLEAR		0x00000000

#define PMU_CC2_WL2CDIG_I_PMU_SLEEP		(1 << 16)
#define	PMU_CHIPCTL2		2
#define PMU_CC2_FORCE_SUBCORE_PWR_SWITCH_ON	(1 << 18)
#define PMU_CC2_FORCE_PHY_PWR_SWITCH_ON		(1 << 19)
#define PMU_CC2_FORCE_VDDM_PWR_SWITCH_ON	(1 << 20)
#define PMU_CC2_FORCE_MEMLPLDO_PWR_SWITCH_ON	(1 << 21)
#define PMU_CC2_MASK_WL_DEV_WAKE             (1 << 22)
#define PMU_CC2_INV_GPIO_POLARITY_PMU_WAKE   (1 << 25)
#define PMU_CC2_GCI2_WAKE                    (1 << 31)

#define PMU_CC2_4369_XTALCORESIZE_BIAS_ADJ_START_VAL	(0x3 << 26)
#define PMU_CC2_4369_XTALCORESIZE_BIAS_ADJ_START_MASK	(0x3 << 26)
#define PMU_CC2_4369_XTALCORESIZE_BIAS_ADJ_NORMAL_VAL	(0x0 << 28)
#define PMU_CC2_4369_XTALCORESIZE_BIAS_ADJ_NORMAL_MASK	(0x3 << 28)

/* PMU chip control3 register */
#define	PMU_CHIPCTL3		3
#define PMU_CC3_ENABLE_SDIO_WAKEUP_SHIFT  19
#define PMU_CC3_ENABLE_RF_SHIFT           22
#define PMU_CC3_RF_DISABLE_IVALUE_SHIFT   23

#define PMU_CC3_4369_XTALCORESIZE_PMOS_START_VAL	(0x3F << 0)
#define PMU_CC3_4369_XTALCORESIZE_PMOS_START_MASK	(0x3F << 0)
#define PMU_CC3_4369_XTALCORESIZE_PMOS_NORMAL_VAL	(0x3F << 15)
#define PMU_CC3_4369_XTALCORESIZE_PMOS_NORMAL_MASK	(0x3F << 15)
#define PMU_CC3_4369_XTALCORESIZE_NMOS_START_VAL	(0x3F << 6)
#define PMU_CC3_4369_XTALCORESIZE_NMOS_START_MASK	(0x3F << 6)
#define PMU_CC3_4369_XTALCORESIZE_NMOS_NORMAL_VAL	(0x3F << 21)
#define PMU_CC3_4369_XTALCORESIZE_NMOS_NORMAL_MASK	(0x3F << 21)
#define PMU_CC3_4369_XTALSEL_BIAS_RES_START_VAL		(0x2 << 12)
#define PMU_CC3_4369_XTALSEL_BIAS_RES_START_MASK	(0x7 << 12)
#define PMU_CC3_4369_XTALSEL_BIAS_RES_NORMAL_VAL	(0x6 << 27)
#define PMU_CC3_4369_XTALSEL_BIAS_RES_NORMAL_MASK	(0x7 << 27)

/* PMU chip control4 register */
#define PMU_CHIPCTL4                    4

/* 53537 series moved switch_type and gmac_if_type to CC4 [15:14] and [13:12] */
#define PMU_CC4_IF_TYPE_MASK		0x00003000
#define PMU_CC4_IF_TYPE_RMII		0x00000000
#define PMU_CC4_IF_TYPE_MII		0x00001000
#define PMU_CC4_IF_TYPE_RGMII		0x00002000

#define PMU_CC4_SW_TYPE_MASK		0x0000c000
#define PMU_CC4_SW_TYPE_EPHY		0x00000000
#define PMU_CC4_SW_TYPE_EPHYMII		0x00004000
#define PMU_CC4_SW_TYPE_EPHYRMII	0x00008000
#define PMU_CC4_SW_TYPE_RGMII		0x0000c000
#define PMU_CC4_DISABLE_LQ_AVAIL	(1<<27)

#define PMU_CC4_4369_MAIN_PD_CBUCK2VDDB_ON	(1u << 15u)
#define PMU_CC4_4369_MAIN_PD_CBUCK2VDDRET_ON	(1u << 16u)
#define PMU_CC4_4369_MAIN_PD_MEMLPLDO2VDDB_ON	(1u << 17u)
#define PMU_CC4_4369_MAIN_PD_MEMLPDLO2VDDRET_ON	(1u << 18u)

#define PMU_CC4_4369_AUX_PD_CBUCK2VDDB_ON	(1u << 21u)
#define PMU_CC4_4369_AUX_PD_CBUCK2VDDRET_ON	(1u << 22u)
#define PMU_CC4_4369_AUX_PD_MEMLPLDO2VDDB_ON	(1u << 23u)
#define PMU_CC4_4369_AUX_PD_MEMLPLDO2VDDRET_ON	(1u << 24u)

/* PMU chip control5 register */
#define PMU_CHIPCTL5                    5

#define PMU_CC5_4369_SUBCORE_CBUCK2VDDB_ON	(1u << 9u)
#define PMU_CC5_4369_SUBCORE_CBUCK2VDDRET_ON	(1u << 10u)
#define PMU_CC5_4369_SUBCORE_MEMLPLDO2VDDB_ON	(1u << 11u)
#define PMU_CC5_4369_SUBCORE_MEMLPLDO2VDDRET_ON	(1u << 12u)

/* PMU chip control6 register */
#define PMU_CHIPCTL6                    6
#define PMU_CC6_ENABLE_CLKREQ_WAKEUP    (1 << 4)
#define PMU_CC6_ENABLE_PMU_WAKEUP_ALP   (1 << 6)
#define PMU_CC6_ENABLE_PCIE_RETENTION	(1 << 12)
#define PMU_CC6_ENABLE_PMU_EXT_PERST	(1 << 13)
#define PMU_CC6_ENABLE_PMU_WAKEUP_PERST	(1 << 14)

/* PMU chip control7 register */
#define PMU_CHIPCTL7				7
#define PMU_CC7_ENABLE_L2REFCLKPAD_PWRDWN	(1 << 25)
#define PMU_CC7_ENABLE_MDIO_RESET_WAR		(1 << 27)
/* 53537 series have gmca1 gmac_if_type in cc7 [7:6](defalut 0b01) */
#define PMU_CC7_IF_TYPE_MASK		0x000000c0
#define PMU_CC7_IF_TYPE_RMII		0x00000000
#define PMU_CC7_IF_TYPE_MII		0x00000040
#define PMU_CC7_IF_TYPE_RGMII		0x00000080

#define PMU_CHIPCTL8			8
#define PMU_CHIPCTL9			9

#define PMU_CHIPCTL10			10
#define PMU_CC10_PCIE_PWRSW_RESET0_CNT_SHIFT		0
#define PMU_CC10_PCIE_PWRSW_RESET0_CNT_MASK		0x000000ff
#define PMU_CC10_PCIE_PWRSW_RESET1_CNT_SHIFT		8
#define PMU_CC10_PCIE_PWRSW_RESET1_CNT_MASK		0x0000ff00
#define PMU_CC10_PCIE_PWRSW_UP_DLY_SHIFT		16
#define PMU_CC10_PCIE_PWRSW_UP_DLY_MASK		0x000f0000
#define PMU_CC10_PCIE_PWRSW_FORCE_PWROK_DLY_SHIFT	20
#define PMU_CC10_PCIE_PWRSW_FORCE_PWROK_DLY_MASK	0x00f00000
#define PMU_CC10_FORCE_PCIE_ON		(1 << 24)
#define PMU_CC10_FORCE_PCIE_SW_ON	(1 << 25)
#define PMU_CC10_FORCE_PCIE_RETNT_ON	(1 << 26)

#define PMU_CC10_PCIE_PWRSW_RESET_CNT_4US		1
#define PMU_CC10_PCIE_PWRSW_RESET_CNT_8US		2

#define PMU_CC10_PCIE_PWRSW_UP_DLY_0US			0

#define PMU_CC10_PCIE_PWRSW_FORCE_PWROK_DLY_4US	1

#define PMU_CHIPCTL11			11
#define PMU_CHIPCTL12			12

/* PMU chip control13 register */
#define PMU_CHIPCTL13			13

#define PMU_CC13_SUBCORE_CBUCK2VDDB_OFF		(1u << 0u)
#define PMU_CC13_SUBCORE_CBUCK2VDDRET_OFF	(1u << 1u)
#define PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF	(1u << 2u)
#define PMU_CC13_SUBCORE_MEMLPLDO2VDDRET_OFF	(1u << 3u)

#define PMU_CC13_MAIN_CBUCK2VDDB_OFF		(1u << 4u)
#define PMU_CC13_MAIN_CBUCK2VDDRET_OFF		(1u << 5u)
#define PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF		(1u << 6u)
#define PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF	(1u << 7u)

#define PMU_CC13_AUX_CBUCK2VDDB_OFF		(1u << 8u)
#define PMU_CC13_AUX_MEMLPLDO2VDDB_OFF		(1u << 10u)
#define PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF	(1u << 11u)
#define PMU_CC13_AUX_CBUCK2VDDRET_OFF		(1u << 12u)

#define PMU_CHIPCTL14			14
#define PMU_CHIPCTL15			15
#define PMU_CHIPCTL16			16
#define PMU_CC16_CLK4M_DIS		(1 << 4)
#define PMU_CC16_FF_ZERO_ADJ		(4 << 5)

/* PMU chip control14 register */
#define PMU_CC14_MAIN_VDDB2VDDRET_UP_DLY_MASK		(0xF)
#define PMU_CC14_MAIN_VDDB2VDD_UP_DLY_MASK		(0xF << 4)
#define PMU_CC14_AUX_VDDB2VDDRET_UP_DLY_MASK		(0xF << 8)
#define PMU_CC14_AUX_VDDB2VDD_UP_DLY_MASK		(0xF << 12)
#define PMU_CC14_PCIE_VDDB2VDDRET_UP_DLY_MASK		(0xF << 16)
#define PMU_CC14_PCIE_VDDB2VDD_UP_DLY_MASK		(0xF << 20)

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
#define PMU1_PLL0_PC1_M2_M4DIV_MASK     0xff00ff00
#define PMU1_PLL0_PC1_HOLD_LOAD_CH      0x28
#define DOT11MAC_880MHZ_CLK_DIVISOR_SHIFT 8
#define DOT11MAC_880MHZ_CLK_DIVISOR_MASK (0xFF << DOT11MAC_880MHZ_CLK_DIVISOR_SHIFT)
#define DOT11MAC_880MHZ_CLK_DIVISOR_VAL  (0xE << DOT11MAC_880MHZ_CLK_DIVISOR_SHIFT)

/* m<x>div, ndiv_dither_mfb, ndiv_mode, ndiv_int */
#define PMU1_PLL0_PLLCTL2		2
#define PMU1_PLL0_PC2_M5DIV_MASK	0x000000ff
#define PMU1_PLL0_PC2_M5DIV_SHIFT	0
#define PMU1_PLL0_PC2_M5DIV_BY_12	0xc
#define PMU1_PLL0_PC2_M5DIV_BY_18	0x12
#define PMU1_PLL0_PC2_M5DIV_BY_31	0x1f
#define PMU1_PLL0_PC2_M5DIV_BY_36	0x24
#define PMU1_PLL0_PC2_M5DIV_BY_42	0x2a
#define PMU1_PLL0_PC2_M5DIV_BY_60	0x3c
#define PMU1_PLL0_PC2_M6DIV_MASK	0x0000ff00
#define PMU1_PLL0_PC2_M6DIV_SHIFT	8
#define PMU1_PLL0_PC2_M6DIV_BY_18	0x12
#define PMU1_PLL0_PC2_M6DIV_BY_36	0x24
#define PMU1_PLL0_PC2_NDIV_MODE_MASK	0x000e0000
#define PMU1_PLL0_PC2_NDIV_MODE_SHIFT	17
#define PMU1_PLL0_PC2_NDIV_MODE_MASH	1
#define PMU1_PLL0_PC2_NDIV_MODE_MFB	2	/**< recommended for 4319 */
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
#define PMU1_PLL0_PC5_CLK_DRV_MASK 	0xffffff00
#define PMU1_PLL0_PC5_CLK_DRV_SHIFT 	8
#define PMU1_PLL0_PC5_ASSERT_CH_MASK 	0x3f000000
#define PMU1_PLL0_PC5_ASSERT_CH_SHIFT 	24
#define PMU1_PLL0_PC5_DEASSERT_CH_MASK 	0xff000000

#define PMU1_PLL0_PLLCTL6		6
#define PMU1_PLL0_PLLCTL7		7
#define PMU1_PLL0_PLLCTL8		8

#define PMU1_PLLCTL8_OPENLOOP_MASK	(1 << 1)
#define PMU_PLL4350_OPENLOOP_MASK	(1 << 7)

#define PMU1_PLL0_PLLCTL9		9

#define PMU1_PLL0_PLLCTL10		10

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
#define PMU15_ARM_96MHZ			96000000	/**< 96 Mhz */
#define PMU15_ARM_98MHZ			98400000	/**< 98.4 Mhz */
#define PMU15_ARM_97MHZ			97000000	/**< 97 Mhz */

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

/* PLL usage in 4347 */
#define PMU4347_PLL0_PC2_P1DIV_MASK		0x000f0000
#define PMU4347_PLL0_PC2_P1DIV_SHIFT		16
#define PMU4347_PLL0_PC2_NDIV_INT_MASK		0x3ff00000
#define PMU4347_PLL0_PC2_NDIV_INT_SHIFT		20
#define PMU4347_PLL0_PC3_NDIV_FRAC_MASK		0x000fffff
#define PMU4347_PLL0_PC3_NDIV_FRAC_SHIFT		0
#define PMU4347_PLL1_PC5_P1DIV_MASK		0xc0000000
#define PMU4347_PLL1_PC5_P1DIV_SHIFT		30
#define PMU4347_PLL1_PC6_P1DIV_MASK		0x00000003
#define PMU4347_PLL1_PC6_P1DIV_SHIFT		0
#define PMU4347_PLL1_PC6_NDIV_INT_MASK		0x00000ffc
#define PMU4347_PLL1_PC6_NDIV_INT_SHIFT		2
#define PMU4347_PLL1_PC6_NDIV_FRAC_MASK		0xfffff000
#define PMU4347_PLL1_PC6_NDIV_FRAC_SHIFT	12

/* Even though the masks are same as 4347, separate macros are
created for 4369
*/
/* PLL usage in 4369 */
#define PMU4369_PLL0_PC2_PDIV_MASK		0x000f0000
#define PMU4369_PLL0_PC2_PDIV_SHIFT		16
#define PMU4369_PLL0_PC2_NDIV_INT_MASK		0x3ff00000
#define PMU4369_PLL0_PC2_NDIV_INT_SHIFT		20
#define PMU4369_PLL0_PC3_NDIV_FRAC_MASK		0x000fffff
#define PMU4369_PLL0_PC3_NDIV_FRAC_SHIFT	0
#define PMU4369_PLL1_PC5_P1DIV_MASK		0xc0000000
#define PMU4369_PLL1_PC5_P1DIV_SHIFT		30
#define PMU4369_PLL1_PC6_P1DIV_MASK		0x00000003
#define PMU4369_PLL1_PC6_P1DIV_SHIFT		0
#define PMU4369_PLL1_PC6_NDIV_INT_MASK		0x00000ffc
#define PMU4369_PLL1_PC6_NDIV_INT_SHIFT		2
#define PMU4369_PLL1_PC6_NDIV_FRAC_MASK		0xfffff000
#define PMU4369_PLL1_PC6_NDIV_FRAC_SHIFT	12

/* 5357 Chip specific ChipControl register bits */
#define CCTRL5357_EXTPA                 (1<<14) /* extPA in ChipControl 1, bit 14 */
#define CCTRL5357_ANT_MUX_2o3		(1<<15) /* 2o3 in ChipControl 1, bit 15 */
#define CCTRL5357_NFLASH		(1<<16) /* Nandflash in ChipControl 1, bit 16 */
/* 43217 Chip specific ChipControl register bits */
#define CCTRL43217_EXTPA_C0             (1<<13) /* core0 extPA in ChipControl 1, bit 13 */
#define CCTRL43217_EXTPA_C1             (1<<8)  /* core1 extPA in ChipControl 1, bit 8 */

/* 43236 resources */
#define RES43236_REGULATOR		0
#define RES43236_ILP_REQUEST		1
#define RES43236_XTAL_PU		2
#define RES43236_ALP_AVAIL		3
#define RES43236_SI_PLL_ON		4
#define RES43236_HT_SI_AVAIL		5

/* 43236 chip-specific ChipControl register bits */
#define CCTRL43236_BT_COEXIST		(1<<0)	/**< 0 disable */
#define CCTRL43236_SECI			(1<<1)	/**< 0 SECI is disabled (JATG functional) */
#define CCTRL43236_EXT_LNA		(1<<2)	/**< 0 disable */
#define CCTRL43236_ANT_MUX_2o3          (1<<3)	/**< 2o3 mux, chipcontrol bit 3 */
#define CCTRL43236_GSIO			(1<<4)	/**< 0 disable */

/* 43236 Chip specific ChipStatus register bits */
#define CST43236_SFLASH_MASK		0x00000040
#define CST43236_OTP_SEL_MASK		0x00000080
#define CST43236_OTP_SEL_SHIFT		7
#define CST43236_HSIC_MASK		0x00000100	/**< USB/HSIC */
#define CST43236_BP_CLK			0x00000200	/**< 120/96Mbps */
#define CST43236_BOOT_MASK		0x00001800
#define CST43236_BOOT_SHIFT		11
#define CST43236_BOOT_FROM_SRAM		0	/**< boot from SRAM, ARM in reset */
#define CST43236_BOOT_FROM_ROM		1	/**< boot from ROM */
#define CST43236_BOOT_FROM_FLASH	2	/**< boot from FLASH */
#define CST43236_BOOT_FROM_INVALID	3

#define PMU1_PLL0_CHIPCTL0		0
#define PMU1_PLL0_CHIPCTL1		1
#define PMU1_PLL0_CHIPCTL2		2

#define SOCDEVRAM_BP_ADDR		0x1E000000
#define SOCDEVRAM_ARM_ADDR		0x00800000

#define PMU_VREG0_I_SR_CNTL_EN_SHIFT		0
#define PMU_VREG0_DISABLE_PULLD_BT_SHIFT	2
#define PMU_VREG0_DISABLE_PULLD_WL_SHIFT	3
#define PMU_VREG0_CBUCKFSW_ADJ_SHIFT		7
#define PMU_VREG0_CBUCKFSW_ADJ_MASK			0x1F
#define PMU_VREG0_RAMP_SEL_SHIFT			13
#define PMU_VREG0_RAMP_SEL_MASK				0x7
#define PMU_VREG0_VFB_RSEL_SHIFT			17
#define PMU_VREG0_VFB_RSEL_MASK				3

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
#define PMU_VREG4_LPLDO2_0p90V		4	/**< 4 - 7 is 0.90V */

#define PMU_VREG4_HSICLDO_BYPASS_SHIFT	27
#define PMU_VREG4_HSICLDO_BYPASS_MASK	0x1

#define PMU_VREG5_ADDR			5
#define PMU_VREG5_HSICAVDD_PD_SHIFT	6
#define PMU_VREG5_HSICAVDD_PD_MASK	0x1
#define PMU_VREG5_HSICDVDD_PD_SHIFT	11
#define PMU_VREG5_HSICDVDD_PD_MASK	0x1

/* 43228 chipstatus  reg bits */
#define	CST43228_OTP_PRESENT		0x2

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
#define RES4360_AVB_PLL_PWRSW_PU   9
#define RES4360_PCIE_TL_CLK_AVAIL  10

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

#define CCTRL_4360_UART_SEL		0x2

#define CST4360_RSRC_INIT_MODE(cs)	((cs & CST4360_RSRC_INIT_MODE_MASK) >> \
						CST4360_RSRC_INIT_MODE_SHIFT)

#define PMU4360_CC1_GPIO7_OVRD	           (1<<23) /* GPIO7 override */

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

/* 4365 PMU resources */
#define RES4365_REGULATOR_PU			0
#define RES4365_XTALLDO_PU			1
#define RES4365_XTAL_PU				2
#define RES4365_CPU_PLLLDO_PU			3
#define RES4365_CPU_PLL_PU			4
#define RES4365_WL_CORE_RDY			5
#define RES4365_ILP_REQ				6
#define RES4365_ALP_AVAIL			7
#define RES4365_HT_AVAIL			8
#define RES4365_BB_PLLLDO_PU			9
#define RES4365_BB_PLL_PU			10
#define RES4365_MINIMU_PU			11
#define RES4365_RADIO_PU			12
#define RES4365_MACPHY_CLK_AVAIL		13

/* 43684 PMU resources */
#define RES43684_REGULATOR_PU			0
#define RES43684_PCIE_LDO_BG_PU			1
#define RES43684_XTAL_LDO_PU			2
#define RES43684_XTAL_PU			3
#define RES43684_CPU_PLL_LDO_PU			4
#define RES43684_CPU_PLL_PU			5
#define RES43684_WL_CORE_RDY			6
#define RES43684_ILP_REQ			7
#define RES43684_ALP_AVAIL			8
#define RES43684_HT_AVAIL			9
#define RES43684_BB_PLL_LDO_PU			10
#define RES43684_BB_PLL_PU			11
#define RES43684_MINI_PMU_PU			12
#define RES43684_RADIO_PU			13
#define RES43684_MACPHY_CLK_AVAIL		14
#define RES43684_PCIE_LDO_PU			15

/* 7271 PMU resources */
#define RES7271_REGULATOR_PU		0
#define RES7271_WL_CORE_RDY			1
#define RES7271_ILP_REQ				2
#define RES7271_ALP_AVAIL			3
#define RES7271_HT_AVAIL			4
#define RES7271_BB_PLL_PU			5
#define RES7271_MINIPMU_PU			6
#define RES7271_RADIO_PU			7
#define RES7271_MACPHY_CLK_AVAIL	8

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

/* 4373 PMU resources */
#define RES4373_LPLDO_PU			0
#define RES4373_BG_PU				1
#define RES4373_PMU_SLEEP			2
#define RES4373_PALDO3P3_PU			3
#define RES4373_CBUCK_LPOM_PU			4
#define RES4373_CBUCK_PFM_PU			5
#define RES4373_COLD_START_WAIT			6
#define RES4373_RSVD_7				7
#define RES4373_LNLDO_PU			8
#define RES4373_XTALLDO_PU			9
#define RES4373_LDO3P3_PU			10
#define RES4373_OTP_PU				11
#define RES4373_XTAL_PU				12
#define RES4373_SR_CLK_START			13
#define RES4373_LQ_AVAIL			14
#define RES4373_LQ_START			15
#define RES4373_PERST_OVR			16
#define RES4373_WL_CORE_RDY			17
#define RES4373_ILP_REQ				18
#define RES4373_ALP_AVAIL			19
#define RES4373_MINI_PMU			20
#define RES4373_RADIO_PU			21
#define RES4373_SR_CLK_STABLE			22
#define RES4373_SR_SAVE_RESTORE			23
#define RES4373_SR_PHY_PWRSW			24
#define RES4373_SR_VDDM_PWRSW			25
#define RES4373_SR_SUBCORE_PWRSW		26
#define RES4373_SR_SLEEP			27
#define RES4373_HT_START			28
#define RES4373_HT_AVAIL			29
#define RES4373_MACPHY_CLKAVAIL			30
/* SR Control0 bits */
#define CC_SR0_4349_SR_ENG_EN_MASK		0x1
#define CC_SR0_4349_SR_ENG_EN_SHIFT             0
#define CC_SR0_4349_SR_ENG_CLK_EN		(1 << 1)
#define CC_SR0_4349_SR_RSRC_TRIGGER		(0xC << 2)
#define CC_SR0_4349_SR_WD_MEM_MIN_DIV		(0x3 << 6)
#define CC_SR0_4349_SR_MEM_STBY_ALLOW_MSK	(1 << 16)
#define CC_SR0_4349_SR_MEM_STBY_ALLOW_SHIFT	16
#define CC_SR0_4349_SR_ENABLE_ILP		(1 << 17)
#define CC_SR0_4349_SR_ENABLE_ALP		(1 << 18)
#define CC_SR0_4349_SR_ENABLE_HT		(1 << 19)
#define CC_SR0_4349_SR_ALLOW_PIC		(3 << 20)
#define CC_SR0_4349_SR_PMU_MEM_DISABLE		(1 << 30)
/* SR Control0 bits */
#define CC_SR0_4349_SR_ENG_EN_MASK		0x1
#define CC_SR0_4349_SR_ENG_EN_SHIFT             0
#define CC_SR0_4349_SR_ENG_CLK_EN		(1 << 1)
#define CC_SR0_4349_SR_RSRC_TRIGGER		(0xC << 2)
#define CC_SR0_4349_SR_WD_MEM_MIN_DIV		(0x3 << 6)
#define CC_SR0_4349_SR_MEM_STBY_ALLOW		(1 << 16)
#define CC_SR0_4349_SR_ENABLE_ILP		(1 << 17)
#define CC_SR0_4349_SR_ENABLE_ALP		(1 << 18)
#define CC_SR0_4349_SR_ENABLE_HT		(1 << 19)
#define CC_SR0_4349_SR_ALLOW_PIC		(3 << 20)
#define CC_SR0_4349_SR_PMU_MEM_DISABLE		(1 << 30)

/* SR binary offset is at 8K */
#define CC_SR1_4349_SR_ASM_ADDR		(0x10)
#define CST4349_CHIPMODE_SDIOD(cs)	(((cs) & (1 << 6)) != 0)	/* SDIO */
#define CST4349_CHIPMODE_PCIE(cs)	(((cs) & (1 << 7)) != 0)	/* PCIE */
#define CST4349_SPROM_PRESENT		0x00000010

/* 4373 related */
#define CST4373_CHIPMODE_USB20D(cs)	(((cs) & (1 << 8)) != 0)	/* USB */
#define CST4373_CHIPMODE_SDIOD(cs)	(((cs) & (1 << 7)) != 0)	/* SDIO */
#define CST4373_CHIPMODE_PCIE(cs)	(((cs) & (1 << 6)) != 0)	/* PCIE */
#define CST4373_SFLASH_PRESENT		0x00000010

#define	VREG4_4349_MEMLPLDO_PWRUP_MASK		(1 << 31)
#define	VREG4_4349_MEMLPLDO_PWRUP_SHIFT		(31)
#define VREG4_4349_LPLDO1_OUTPUT_VOLT_ADJ_MASK	(0x7 << 15)
#define VREG4_4349_LPLDO1_OUTPUT_VOLT_ADJ_SHIFT	(15)
#define CC2_4349_PHY_PWRSE_RST_CNT_MASK		(0xF << 0)
#define CC2_4349_PHY_PWRSE_RST_CNT_SHIFT	(0)
#define CC2_4349_VDDM_PWRSW_EN_MASK		(1 << 20)
#define CC2_4349_VDDM_PWRSW_EN_SHIFT		(20)
#define CC2_4349_MEMLPLDO_PWRSW_EN_MASK		(1 << 21)
#define CC2_4349_MEMLPLDO_PWRSW_EN_SHIFT	(21)
#define CC2_4349_SDIO_AOS_WAKEUP_MASK		(1 << 24)
#define CC2_4349_SDIO_AOS_WAKEUP_SHIFT		(24)
#define CC2_4349_PMUWAKE_EN_MASK		(1 << 31)
#define CC2_4349_PMUWAKE_EN_SHIFT		(31)

#define CC5_4349_MAC_PHY_CLK_8_DIV              (1 << 27)

#define CC6_4349_PCIE_CLKREQ_WAKEUP_MASK	(1 << 4)
#define CC6_4349_PCIE_CLKREQ_WAKEUP_SHIFT	(4)
#define CC6_4349_PMU_WAKEUP_ALPAVAIL_MASK	(1 << 6)
#define CC6_4349_PMU_WAKEUP_ALPAVAIL_SHIFT	(6)
#define CC6_4349_PMU_EN_EXT_PERST_MASK		(1 << 13)
#define CC6_4349_PMU_EN_L2_DEASSERT_MASK	(1 << 14)
#define CC6_4349_PMU_EN_L2_DEASSERT_SHIF	(14)
#define CC6_4349_PMU_ENABLE_L2REFCLKPAD_PWRDWN	(1 << 15)
#define CC6_4349_PMU_EN_MDIO_MASK		(1 << 16)
#define CC6_4349_PMU_EN_ASSERT_L2_MASK		(1 << 25)

/* 4349 GCI function sel values */
/*
 * Reference
 * http://hwnbu-twiki.sj.broadcom.com/bin/view/Mwgroup/ToplevelArchitecture4349B0#Function_Sel
 */
#define CC4349_FNSEL_HWDEF		(0)
#define CC4349_FNSEL_SAMEASPIN		(1)
#define CC4349_FNSEL_GPIO		(2)
#define CC4349_FNSEL_FAST_UART		(3)
#define CC4349_FNSEL_GCI0		(4)
#define CC4349_FNSEL_GCI1		(5)
#define CC4349_FNSEL_DGB_UART		(6)
#define CC4349_FNSEL_I2C		(7)
#define CC4349_FNSEL_SPROM		(8)
#define CC4349_FNSEL_MISC0		(9)
#define CC4349_FNSEL_MISC1		(10)
#define CC4349_FNSEL_MISC2		(11)
#define CC4349_FNSEL_IND		(12)
#define CC4349_FNSEL_PDN		(13)
#define CC4349_FNSEL_PUP		(14)
#define CC4349_FNSEL_TRISTATE		(15)

/* 4364 related */
#define RES4364_LPLDO_PU				0
#define RES4364_BG_PU					1
#define RES4364_MEMLPLDO_PU				2
#define RES4364_PALDO3P3_PU				3
#define RES4364_CBUCK_1P2				4
#define RES4364_CBUCK_1V8				5
#define RES4364_COLD_START_WAIT				6
#define RES4364_SR_3x3_VDDM_PWRSW			7
#define RES4364_3x3_MACPHY_CLKAVAIL			8
#define RES4364_XTALLDO_PU				9
#define RES4364_LDO3P3_PU				10
#define RES4364_OTP_PU					11
#define RES4364_XTAL_PU					12
#define RES4364_SR_CLK_START				13
#define RES4364_3x3_RADIO_PU				14
#define RES4364_RF_LDO					15
#define RES4364_PERST_OVR				16
#define RES4364_WL_CORE_RDY				17
#define RES4364_ILP_REQ					18
#define RES4364_ALP_AVAIL				19
#define RES4364_1x1_MINI_PMU				20
#define RES4364_1x1_RADIO_PU				21
#define RES4364_SR_CLK_STABLE				22
#define RES4364_SR_SAVE_RESTORE				23
#define RES4364_SR_PHY_PWRSW				24
#define RES4364_SR_VDDM_PWRSW				25
#define RES4364_SR_SUBCORE_PWRSW			26
#define RES4364_SR_SLEEP				27
#define RES4364_HT_START				28
#define RES4364_HT_AVAIL				29
#define RES4364_MACPHY_CLKAVAIL				30

/* 4349 GPIO */
#define CC4349_PIN_GPIO_00		(0)
#define CC4349_PIN_GPIO_01		(1)
#define CC4349_PIN_GPIO_02		(2)
#define CC4349_PIN_GPIO_03		(3)
#define CC4349_PIN_GPIO_04		(4)
#define CC4349_PIN_GPIO_05		(5)
#define CC4349_PIN_GPIO_06		(6)
#define CC4349_PIN_GPIO_07		(7)
#define CC4349_PIN_GPIO_08		(8)
#define CC4349_PIN_GPIO_09		(9)
#define CC4349_PIN_GPIO_10		(10)
#define CC4349_PIN_GPIO_11		(11)
#define CC4349_PIN_GPIO_12		(12)
#define CC4349_PIN_GPIO_13		(13)
#define CC4349_PIN_GPIO_14		(14)
#define CC4349_PIN_GPIO_15		(15)
#define CC4349_PIN_GPIO_16		(16)
#define CC4349_PIN_GPIO_17		(17)
#define CC4349_PIN_GPIO_18		(18)
#define CC4349_PIN_GPIO_19		(19)

/* Mask used to decide whether HOSTWAKE MUX to be performed or not */
#define MUXENAB4349_HOSTWAKE_MASK	(0x000000f0) /* configure GPIO for SDIO host_wake */
#define MUXENAB4349_HOSTWAKE_SHIFT	4
#define MUXENAB4349_GETIX(val, name) \
	((((val) & MUXENAB4349_ ## name ## _MASK) >> MUXENAB4349_ ## name ## _SHIFT) - 1)

#define CR4_4364_RAM_BASE			(0x160000)

/* SR binary offset is at 8K */
#define CC_SR1_4364_SR_CORE0_ASM_ADDR			(0x10)
#define CC_SR1_4364_SR_CORE1_ASM_ADDR			(0x10)

#define CC_SR0_4364_SR_ENG_EN_MASK			0x1
#define CC_SR0_4364_SR_ENG_EN_SHIFT			0
#define CC_SR0_4364_SR_ENG_CLK_EN			(1 << 1)
#define CC_SR0_4364_SR_RSRC_TRIGGER			(0xC << 2)
#define CC_SR0_4364_SR_WD_MEM_MIN_DIV			(0x3 << 6)
#define CC_SR0_4364_SR_MEM_STBY_ALLOW_MSK		(1 << 16)
#define CC_SR0_4364_SR_MEM_STBY_ALLOW_SHIFT		16
#define CC_SR0_4364_SR_ENABLE_ILP			(1 << 17)
#define CC_SR0_4364_SR_ENABLE_ALP			(1 << 18)
#define CC_SR0_4364_SR_ENABLE_HT			(1 << 19)
#define CC_SR0_4364_SR_INVERT_CLK			(1 << 11)
#define CC_SR0_4364_SR_ALLOW_PIC			(3 << 20)
#define CC_SR0_4364_SR_PMU_MEM_DISABLE			(1 << 30)

#define PMU_4364_CC1_ENABLE_BBPLL_PWR_DWN		(0x1 << 4)
#define PMU_4364_CC1_BBPLL_ARESET_LQ_TIME		(0x1 << 8)
#define PMU_4364_CC1_BBPLL_ARESET_HT_UPTIME		(0x1 << 10)
#define PMU_4364_CC1_BBPLL_DRESET_LQ_UPTIME		(0x1 << 12)
#define PMU_4364_CC1_BBPLL_DRESET_HT_UPTIME		(0x4 << 16)
#define PMU_4364_CC1_SUBCORE_PWRSW_UP_DELAY		(0x8 << 20)
#define PMU_4364_CC1_SUBCORE_PWRSW_RESET_CNT		(0x4 << 24)

#define PMU_4364_CC2_PHY_PWRSW_RESET_CNT		(0x2 << 0)
#define PMU_4364_CC2_PHY_PWRSW_RESET_MASK		(0x7)
#define PMU_4364_CC2_SEL_CHIPC_IF_FOR_SR		(1 << 21)

#define PMU_4364_CC3_MEMLPLDO3x3_PWRSW_FORCE_MASK	(1 << 23)
#define PMU_4364_CC3_MEMLPLDO1x1_PWRSW_FORCE_MASK	(1 << 24)
#define PMU_4364_CC3_CBUCK1P2_PU_SR_VDDM_REQ_ON		(1 << 25)
#define PMU_4364_CC3_MEMLPLDO3x3_PWRSW_FORCE_OFF	(0)
#define PMU_4364_CC3_MEMLPLDO1x1_PWRSW_FORCE_OFF	(0)

#define PMU_4364_CC5_DISABLE_BBPLL_CLKOUT6_DIV2_MASK	(1 << 26)
#define PMU_4364_CC5_ENABLE_ARMCR4_DEBUG_CLK_MASK	(1 << 4)
#define PMU_4364_CC5_DISABLE_BBPLL_CLKOUT6_DIV2		(1 << 26)
#define PMU_4364_CC5_ENABLE_ARMCR4_DEBUG_CLK_OFF	(0)

#define PMU_4364_CC6_MDI_RESET_MASK			(1 << 16)
#define PMU_4364_CC6_USE_CLK_REQ_MASK			(1 << 18)
#define PMU_4364_CC6_HIGHER_CLK_REQ_ALP_MASK		(1 << 20)
#define PMU_4364_CC6_HT_AVAIL_REQ_ALP_AVAIL_MASK	(1 << 21)
#define PMU_4364_CC6_PHY_CLK_REQUESTS_ALP_AVAIL_MASK	(1 << 22)
#define PMU_4364_CC6_MDI_RESET				(1 << 16)
#define PMU_4364_CC6_USE_CLK_REQ			(1 << 18)

#define PMU_4364_CC6_HIGHER_CLK_REQ_ALP			(1 << 20)
#define PMU_4364_CC6_HT_AVAIL_REQ_ALP_AVAIL		(1 << 21)
#define PMU_4364_CC6_PHY_CLK_REQUESTS_ALP_AVAIL		(1 << 22)

#define PMU_4364_VREG0_DISABLE_BT_PULL_DOWN		(1 << 2)
#define PMU_4364_VREG1_DISABLE_WL_PULL_DOWN		(1 << 2)

/* Indices of PMU voltage regulator registers */
#define PMU_VREG_0	(0u)
#define PMU_VREG_1	(1u)
#define PMU_VREG_2	(2u)
#define PMU_VREG_3	(3u)
#define PMU_VREG_4	(4u)
#define PMU_VREG_5	(5u)
#define PMU_VREG_6	(6u)
#define PMU_VREG_7	(7u)
#define PMU_VREG_8	(8u)
#define PMU_VREG_9	(9u)
#define PMU_VREG_10	(10u)
#define PMU_VREG_11	(11u)
#define PMU_VREG_12	(12u)
#define PMU_VREG_13	(13u)
#define PMU_VREG_14	(14u)
#define PMU_VREG_15	(15u)
#define PMU_VREG_16	(16u)

/* 43012 Chipcommon ChipStatus bits */
#define CST43012_FLL_LOCK	(1 << 13)
/* 43012 resources - End */

/* 43012 related Cbuck modes */
#define PMU_43012_VREG8_DYNAMIC_CBUCK_MODE0 0x00001c03
#define PMU_43012_VREG9_DYNAMIC_CBUCK_MODE0 0x00492490
#define PMU_43012_VREG8_DYNAMIC_CBUCK_MODE1 0x00001c03
#define PMU_43012_VREG9_DYNAMIC_CBUCK_MODE1 0x00490410

/* 43012 related dynamic cbuck mode mask */
#define PMU_43012_VREG8_DYNAMIC_CBUCK_MODE_MASK  0xFFFFFC07
#define PMU_43012_VREG9_DYNAMIC_CBUCK_MODE_MASK  0xFFFFFFFF

/* 4369 related VREG masks */
#define PMU_4369_VREG_5_MISCLDO_POWER_UP_MASK		(1u << 11u)
#define PMU_4369_VREG_5_MISCLDO_POWER_UP_SHIFT		11u
#define PMU_4369_VREG_5_LPLDO_POWER_UP_MASK		(1u << 27u)
#define PMU_4369_VREG_5_LPLDO_POWER_UP_SHIFT		27u
#define PMU_4369_VREG_5_MEMLPLDO_OP_VLT_ADJ_CTRL_MASK	BCM_MASK32(31, 28)
#define PMU_4369_VREG_5_MEMLPLDO_OP_VLT_ADJ_CTRL_SHIFT	28u

#define PMU_4369_VREG_6_MEMLPLDO_POWER_UP_MASK		(1u << 3u)
#define PMU_4369_VREG_6_MEMLPLDO_POWER_UP_SHIFT		3u

#define PMU_4369_VREG_7_PMU_FORCE_HP_MODE_MASK		(1u << 27u)
#define PMU_4369_VREG_7_PMU_FORCE_HP_MODE_SHIFT		27u
#define PMU_4369_VREG_7_WL_PMU_LP_MODE_MASK		(1u << 28u)
#define PMU_4369_VREG_7_WL_PMU_LP_MODE_SHIFT		28u
#define PMU_4369_VREG_7_WL_PMU_LV_MODE_MASK		(1u << 29u)
#define PMU_4369_VREG_7_WL_PMU_LV_MODE_SHIFT		29u

#define PMU_4369_VREG8_ASR_OVADJ_LPPFM_MASK		BCM_MASK32(4, 0)
#define PMU_4369_VREG8_ASR_OVADJ_LPPFM_SHIFT		0u

#define PMU_4369_VREG13_RSRC_EN_ASR_MASK4_MASK		BCM_MASK32(10, 9)
#define PMU_4369_VREG13_RSRC_EN_ASR_MASK4_SHIFT		9u

#define PMU_4369_VREG14_RSRC_EN_CSR_MASK0_MASK		(1u << 23u)
#define PMU_4369_VREG14_RSRC_EN_CSR_MASK0_SHIFT		23u

#define PMU_4369_VREG16_RSRC0_CBUCK_MODE_MASK		BCM_MASK32(2, 0)
#define PMU_4369_VREG16_RSRC0_CBUCK_MODE_SHIFT		0u
#define PMU_4369_VREG16_RSRC0_ABUCK_MODE_MASK		BCM_MASK32(17, 15)
#define PMU_4369_VREG16_RSRC0_ABUCK_MODE_SHIFT		15u
#define PMU_4369_VREG16_RSRC1_ABUCK_MODE_MASK		BCM_MASK32(20, 18)
#define PMU_4369_VREG16_RSRC1_ABUCK_MODE_SHIFT		18u

/* 4364 related VREG masks */
#define PMU_4364_VREG3_DISABLE_WPT_REG_ON_PULL_DOWN	(1 << 11)

#define PMU_4364_VREG4_MEMLPLDO_PU_ON			(1 << 31)
#define PMU_4364_VREG4_LPLPDO_ADJ			(3 << 16)
#define PMU_4364_VREG4_LPLPDO_ADJ_MASK			(3 << 16)
#define PMU_4364_VREG5_MAC_CLK_1x1_AUTO			(0x1 << 18)
#define PMU_4364_VREG5_SR_AUTO				(0x1 << 20)
#define PMU_4364_VREG5_BT_PWM_MASK			(0x1 << 21)
#define PMU_4364_VREG5_BT_AUTO				(0x1 << 22)
#define PMU_4364_VREG5_WL2CLB_DVFS_EN_MASK		(0x1 << 23)
#define PMU_4364_VREG5_BT_PWMK				(0)
#define PMU_4364_VREG5_WL2CLB_DVFS_EN			(0)

#define PMU_4364_VREG6_BBPLL_AUTO			(0x1 << 17)
#define PMU_4364_VREG6_MINI_PMU_PWM			(0x1 << 18)
#define PMU_4364_VREG6_LNLDO_AUTO			(0x1 << 21)
#define PMU_4364_VREG6_PCIE_PWRDN_0_AUTO		(0x1 << 23)
#define PMU_4364_VREG6_PCIE_PWRDN_1_AUTO		(0x1 << 25)
#define PMU_4364_VREG6_MAC_CLK_3x3_PWM			(0x1 << 27)
#define PMU_4364_VREG6_ENABLE_FINE_CTRL			(0x1 << 30)

#define PMU_4364_PLL0_DISABLE_CHANNEL6			(0x1 << 18)

#define CC_GCI1_REG					(0x1)
#define CC_GCI1_4364_IND_STATE_FOR_GPIO9_11		(0x0ccccccc)
#define CC2_4364_SDIO_AOS_WAKEUP_MASK			(1 << 24)
#define CC2_4364_SDIO_AOS_WAKEUP_SHIFT			(24)

#define CC6_4364_PCIE_CLKREQ_WAKEUP_MASK		(1 << 4)
#define CC6_4364_PCIE_CLKREQ_WAKEUP_SHIFT		(4)
#define CC6_4364_PMU_WAKEUP_ALPAVAIL_MASK		(1 << 6)
#define CC6_4364_PMU_WAKEUP_ALPAVAIL_SHIFT		(6)

#define CST4364_CHIPMODE_SDIOD(cs)	(((cs) & (1 << 6)) != 0)	/* SDIO */
#define CST4364_CHIPMODE_PCIE(cs)	(((cs) & (1 << 7)) != 0)	/* PCIE */
#define CST4364_SPROM_PRESENT		0x00000010

#define PMU_4364_MACCORE_0_RES_REQ_MASK			0x3FCBF7FF
#define PMU_4364_MACCORE_1_RES_REQ_MASK			0x7FFB3647

#define PMU_4364_RSDB_MODE                                              (0)
#define PMU_4364_1x1_MODE                                               (1)
#define PMU_4364_3x3_MODE                                               (2)

#define PMU_4364_MAX_MASK_1x1                                   (0x7FFF3E47)
#define PMU_4364_MAX_MASK_RSDB                                  (0x7FFFFFFF)
#define PMU_4364_MAX_MASK_3x3                                   (0x3FCFFFFF)

#define PMU_4364_SAVE_RESTORE_UPDNTIME_1x1		(0xC000C)
#define PMU_4364_SAVE_RESTORE_UPDNTIME_3x3		(0xF000F)

#define FORCE_CLK_ON                                                    1
#define FORCE_CLK_OFF                                                   0

#define PMU1_PLL0_SWITCH_MACCLOCK_120MHZ			(0)
#define PMU1_PLL0_SWITCH_MACCLOCK_160MHZ			(1)
#define TSF_CLK_FRAC_L_4364_120MHZ					0x8889
#define TSF_CLK_FRAC_H_4364_120MHZ					0x8
#define TSF_CLK_FRAC_L_4364_160MHZ					0x6666
#define TSF_CLK_FRAC_H_4364_160MHZ					0x6
#define PMU1_PLL0_PC1_M2DIV_VALUE_120MHZ			8
#define PMU1_PLL0_PC1_M2DIV_VALUE_160MHZ			6

/* 4347/4369 Related */

/*
 * PMU VREG Definitions:
 *   http://confluence.broadcom.com/display/WLAN/BCM4347+PMU+Vreg+Control+Register
 *   http://confluence.broadcom.com/display/WLAN/BCM4369+PMU+Vreg+Control+Register
 */
/* PMU VREG4 */
#define PMU_28NM_VREG4_WL_LDO_CNTL_EN				(0x1 << 10)

/* PMU VREG6 */
#define PMU_28NM_VREG6_BTLDO3P3_PU				(0x1 << 12)

/* PMU resources */
#define RES4347_MEMLPLDO_PU		0
#define RES4347_AAON			1
#define RES4347_PMU_SLEEP		2
#define RES4347_RESERVED_3		3
#define RES4347_LDO3P3_PU		4
#define RES4347_FAST_LPO_AVAIL		5
#define RES4347_XTAL_PU			6
#define RES4347_XTAL_STABLE		7
#define RES4347_PWRSW_DIG		8
#define RES4347_SR_DIG			9
#define RES4347_SLEEP_DIG		10
#define RES4347_PWRSW_AUX		11
#define RES4347_SR_AUX			12
#define RES4347_SLEEP_AUX		13
#define RES4347_PWRSW_MAIN		14
#define RES4347_SR_MAIN			15
#define RES4347_SLEEP_MAIN		16
#define RES4347_CORE_RDY_DIG		17
#define RES4347_CORE_RDY_AUX		18
#define RES4347_ALP_AVAIL		19
#define RES4347_RADIO_AUX_PU		20
#define RES4347_MINIPMU_AUX_PU		21
#define RES4347_CORE_RDY_MAIN		22
#define RES4347_RADIO_MAIN_PU		23
#define RES4347_MINIPMU_MAIN_PU		24
#define RES4347_PCIE_EP_PU		25
#define RES4347_COLD_START_WAIT		26
#define RES4347_ARMHTAVAIL		27
#define RES4347_HT_AVAIL		28
#define RES4347_MACPHY_AUX_CLK_AVAIL	29
#define RES4347_MACPHY_MAIN_CLK_AVAIL	30
#define RES4347_RESERVED_31		31

/* 4369 PMU Resources */
#define RES4369_DUMMY			0
#define RES4369_ABUCK			1
#define RES4369_PMU_SLEEP		2
#define RES4369_MISCLDO			3
#define RES4369_LDO3P3			4
#define RES4369_FAST_LPO_AVAIL		5
#define RES4369_XTAL_PU			6
#define RES4369_XTAL_STABLE		7
#define RES4369_PWRSW_DIG		8
#define RES4369_SR_DIG			9
#define RES4369_SLEEP_DIG		10
#define RES4369_PWRSW_AUX		11
#define RES4369_SR_AUX			12
#define RES4369_SLEEP_AUX		13
#define RES4369_PWRSW_MAIN		14
#define RES4369_SR_MAIN			15
#define RES4369_SLEEP_MAIN		16
#define RES4369_DIG_CORE_RDY		17
#define RES4369_CORE_RDY_AUX		18
#define RES4369_ALP_AVAIL		19
#define RES4369_RADIO_AUX_PU		20
#define RES4369_MINIPMU_AUX_PU		21
#define RES4369_CORE_RDY_MAIN		22
#define RES4369_RADIO_MAIN_PU		23
#define RES4369_MINIPMU_MAIN_PU		24
#define RES4369_PCIE_EP_PU		25
#define RES4369_COLD_START_WAIT		26
#define RES4369_ARMHTAVAIL		27
#define RES4369_HT_AVAIL		28
#define RES4369_MACPHY_AUX_CLK_AVAIL	29
#define RES4369_MACPHY_MAIN_CLK_AVAIL	30

/* chip status */
#define CST4347_CHIPMODE_SDIOD(cs)	(((cs) & (1 << 6)) != 0)	/* SDIO */
#define CST4347_CHIPMODE_PCIE(cs)	(((cs) & (1 << 7)) != 0)	/* PCIE */
#define CST4347_JTAG_STRAP_ENABLED(cs)	(((cs) & (1 << 20)) != 0)	/* JTAG strap st */
#define CST4347_SPROM_PRESENT		0x00000010

/* GCI chip status */
#define GCI_CS_4347_FLL1MHZ_LOCK_MASK		(1 << 1)

/* GCI chip control registers */
#define GCI_CC7_AAON_BYPASS_PWRSW_SEL          13
#define GCI_CC7_AAON_BYPASS_PWRSW_SEQ_ON       14

/* PMU chip control registers */
#define CC2_4347_VASIP_MEMLPLDO_VDDB_OFF_MASK		(1 << 11)
#define CC2_4347_VASIP_MEMLPLDO_VDDB_OFF_SHIFT		11
#define CC2_4347_MAIN_MEMLPLDO_VDDB_OFF_MASK		(1 << 12)
#define CC2_4347_MAIN_MEMLPLDO_VDDB_OFF_SHIFT		12
#define CC2_4347_AUX_MEMLPLDO_VDDB_OFF_MASK		(1 << 13)
#define CC2_4347_AUX_MEMLPLDO_VDDB_OFF_SHIFT		13
#define CC2_4347_VASIP_VDDRET_ON_MASK			(1 << 14)
#define CC2_4347_VASIP_VDDRET_ON_SHIFT			14
#define CC2_4347_MAIN_VDDRET_ON_MASK			(1 << 15)
#define CC2_4347_MAIN_VDDRET_ON_SHIFT			15
#define CC2_4347_AUX_VDDRET_ON_MASK			(1 << 16)
#define CC2_4347_AUX_VDDRET_ON_SHIFT			16
#define CC2_4347_GCI2WAKE_MASK				(1 << 31)
#define CC2_4347_GCI2WAKE_SHIFT				31

#define CC2_4347_SDIO_AOS_WAKEUP_MASK			(1 << 24)
#define CC2_4347_SDIO_AOS_WAKEUP_SHIFT			24

#define CC4_4347_LHL_TIMER_SELECT			(1 << 0)

#define CC6_4347_PWROK_WDT_EN_IN_MASK			(1 << 6)
#define CC6_4347_PWROK_WDT_EN_IN_SHIFT			6

#define CC6_4347_SDIO_AOS_CHIP_WAKEUP_MASK		(1 << 24)
#define CC6_4347_SDIO_AOS_CHIP_WAKEUP_SHIFT		24

#define PCIE_GPIO1_GPIO_PIN    CC_GCI_GPIO_0
#define PCIE_PERST_GPIO_PIN	CC_GCI_GPIO_1
#define PCIE_CLKREQ_GPIO_PIN	CC_GCI_GPIO_2

#define VREG5_4347_MEMLPLDO_ADJ_MASK				0xF0000000
#define VREG5_4347_MEMLPLDO_ADJ_SHIFT				28
#define VREG5_4347_LPLDO_ADJ_MASK				0x00F00000
#define VREG5_4347_LPLDO_ADJ_SHIFT				20

/* lpldo/memlpldo voltage */
#define	PMU_VREG5_LPLDO_VOLT_0_88	0xf	/* 0.88v */
#define	PMU_VREG5_LPLDO_VOLT_0_86	0xe	/* 0.86v */
#define	PMU_VREG5_LPLDO_VOLT_0_84	0xd	/* 0.84v */
#define	PMU_VREG5_LPLDO_VOLT_0_82	0xc	/* 0.82v */
#define	PMU_VREG5_LPLDO_VOLT_0_80	0xb	/* 0.80v */
#define	PMU_VREG5_LPLDO_VOLT_0_78	0xa	/* 0.78v */
#define	PMU_VREG5_LPLDO_VOLT_0_76	0x9	/* 0.76v */
#define	PMU_VREG5_LPLDO_VOLT_0_74	0x8	/* 0.74v */
#define	PMU_VREG5_LPLDO_VOLT_0_72	0x7	/* 0.72v */
#define	PMU_VREG5_LPLDO_VOLT_1_10	0x6	/* 1.10v */
#define	PMU_VREG5_LPLDO_VOLT_1_00	0x5	/* 1.00v */
#define	PMU_VREG5_LPLDO_VOLT_0_98	0x4	/* 0.98v */
#define	PMU_VREG5_LPLDO_VOLT_0_96	0x3	/* 0.96v */
#define	PMU_VREG5_LPLDO_VOLT_0_94	0x2	/* 0.94v */
#define	PMU_VREG5_LPLDO_VOLT_0_92	0x1	/* 0.92v */
#define	PMU_VREG5_LPLDO_VOLT_0_90	0x0	/* 0.90v */

/* Save/Restore engine */

#define BM_ADDR_TO_SR_ADDR(bmaddr)	((bmaddr) >> 9)

/* Txfifo is 512KB for main core and 128KB for aux core
 * We use first 12kB (0x3000) in BMC buffer for template in main core and
 * 6.5kB (0x1A00) in aux core, followed by ASM code
 */
#define SR_ASM_ADDR_MAIN_4347		(0x18)
#define SR_ASM_ADDR_AUX_4347		(0xd)
#define SR_ASM_ADDR_DIG_4347		(0x0)

#define SR_ASM_ADDR_MAIN_4369		BM_ADDR_TO_SR_ADDR(0xC00)
#define SR_ASM_ADDR_AUX_4369		BM_ADDR_TO_SR_ADDR(0xC00)
#define SR_ASM_ADDR_DIG_4369		(0x0)

/* 512 bytes block */
#define SR_ASM_ADDR_BLK_SIZE_SHIFT	9

/* SR Control0 bits */
#define SR0_SR_ENG_EN_MASK		0x1
#define SR0_SR_ENG_EN_SHIFT		0
#define SR0_SR_ENG_CLK_EN		(1 << 1)
#define SR0_RSRC_TRIGGER		(0xC << 2)
#define SR0_WD_MEM_MIN_DIV		(0x3 << 6)
#define SR0_INVERT_SR_CLK		(1 << 11)
#define SR0_MEM_STBY_ALLOW		(1 << 16)
#define SR0_ENABLE_SR_ILP		(1 << 17)
#define SR0_ENABLE_SR_ALP		(1 << 18)
#define SR0_ENABLE_SR_HT		(1 << 19)
#define SR0_ALLOW_PIC			(3 << 20)
#define SR0_ENB_PMU_MEM_DISABLE		(1 << 30)

/* SR Control0 bits for 4369 */
#define SR0_4369_SR_ENG_EN_MASK		0x1
#define SR0_4369_SR_ENG_EN_SHIFT	0
#define SR0_4369_SR_ENG_CLK_EN		(1 << 1)
#define SR0_4369_RSRC_TRIGGER		(0xC << 2)
#define SR0_4369_WD_MEM_MIN_DIV		(0x2 << 6)
#define SR0_4369_INVERT_SR_CLK		(1 << 11)
#define SR0_4369_MEM_STBY_ALLOW		(1 << 16)
#define SR0_4369_ENABLE_SR_ILP		(1 << 17)
#define SR0_4369_ENABLE_SR_ALP		(1 << 18)
#define SR0_4369_ENABLE_SR_HT		(1 << 19)
#define SR0_4369_ALLOW_PIC		(3 << 20)
#define SR0_4369_ENB_PMU_MEM_DISABLE	(1 << 30)
/* =========== LHL regs =========== */
/* 4369 LHL register settings */
#define LHL4369_UP_CNT			0
#define LHL4369_DN_CNT			2
#define LHL4369_PWRSW_EN_DWN_CNT	(LHL4369_DN_CNT + 2)
#define LHL4369_ISO_EN_DWN_CNT		(LHL4369_PWRSW_EN_DWN_CNT + 3)
#define LHL4369_SLB_EN_DWN_CNT		(LHL4369_ISO_EN_DWN_CNT + 1)
#define LHL4369_ASR_CLK4M_DIS_DWN_CNT	(LHL4369_DN_CNT)
#define LHL4369_ASR_LPPFM_MODE_DWN_CNT	(LHL4369_DN_CNT)
#define LHL4369_ASR_MODE_SEL_DWN_CNT	(LHL4369_DN_CNT)
#define LHL4369_ASR_MANUAL_MODE_DWN_CNT	(LHL4369_DN_CNT)
#define LHL4369_ASR_ADJ_DWN_CNT		(LHL4369_DN_CNT)
#define LHL4369_ASR_OVERI_DIS_DWN_CNT	(LHL4369_DN_CNT)
#define LHL4369_ASR_TRIM_ADJ_DWN_CNT	(LHL4369_DN_CNT)
#define LHL4369_VDDC_SW_DIS_DWN_CNT	(LHL4369_SLB_EN_DWN_CNT + 1)
#define LHL4369_VMUX_ASR_SEL_DWN_CNT	(LHL4369_VDDC_SW_DIS_DWN_CNT + 1)
#define LHL4369_CSR_ADJ_DWN_CNT		(LHL4369_VMUX_ASR_SEL_DWN_CNT + 1)
#define LHL4369_CSR_MODE_DWN_CNT	(LHL4369_VMUX_ASR_SEL_DWN_CNT + 1)
#define LHL4369_CSR_OVERI_DIS_DWN_CNT	(LHL4369_VMUX_ASR_SEL_DWN_CNT + 1)
#define LHL4369_HPBG_CHOP_DIS_DWN_CNT	(LHL4369_VMUX_ASR_SEL_DWN_CNT + 1)
#define LHL4369_SRBG_REF_SEL_DWN_CNT	(LHL4369_VMUX_ASR_SEL_DWN_CNT + 1)
#define LHL4369_PFM_PWR_SLICE_DWN_CNT	(LHL4369_VMUX_ASR_SEL_DWN_CNT + 1)
#define LHL4369_CSR_TRIM_ADJ_DWN_CNT	(LHL4369_VMUX_ASR_SEL_DWN_CNT + 1)
#define LHL4369_CSR_VOLTAGE_DWN_CNT	(LHL4369_VMUX_ASR_SEL_DWN_CNT + 1)
#define LHL4369_HPBG_PU_EN_DWN_CNT	(LHL4369_CSR_MODE_DWN_CNT + 1)

#define LHL4369_HPBG_PU_EN_UP_CNT	(LHL4369_UP_CNT + 1)
#define LHL4369_CSR_ADJ_UP_CNT		(LHL4369_HPBG_PU_EN_UP_CNT + 1)
#define LHL4369_CSR_MODE_UP_CNT		(LHL4369_HPBG_PU_EN_UP_CNT + 1)
#define LHL4369_CSR_OVERI_DIS_UP_CNT	(LHL4369_HPBG_PU_EN_UP_CNT + 1)
#define LHL4369_HPBG_CHOP_DIS_UP_CNT	(LHL4369_HPBG_PU_EN_UP_CNT + 1)
#define LHL4369_SRBG_REF_SEL_UP_CNT	(LHL4369_HPBG_PU_EN_UP_CNT + 1)
#define LHL4369_PFM_PWR_SLICE_UP_CNT	(LHL4369_HPBG_PU_EN_UP_CNT + 1)
#define LHL4369_CSR_TRIM_ADJ_UP_CNT	(LHL4369_HPBG_PU_EN_UP_CNT + 1)
#define LHL4369_CSR_VOLTAGE_UP_CNT	(LHL4369_HPBG_PU_EN_UP_CNT + 1)
#define LHL4369_VMUX_ASR_SEL_UP_CNT	(LHL4369_CSR_MODE_UP_CNT + 1)
#define LHL4369_VDDC_SW_DIS_UP_CNT	(LHL4369_VMUX_ASR_SEL_UP_CNT + 1)
#define LHL4369_SLB_EN_UP_CNT		(LHL4369_VDDC_SW_DIS_UP_CNT + 8)
#define LHL4369_ISO_EN_UP_CNT		(LHL4369_SLB_EN_UP_CNT + 1)
#define LHL4369_PWRSW_EN_UP_CNT		(LHL4369_ISO_EN_UP_CNT + 3)
#define LHL4369_ASR_ADJ_UP_CNT		(LHL4369_PWRSW_EN_UP_CNT + 1)
#define LHL4369_ASR_CLK4M_DIS_UP_CNT	(LHL4369_PWRSW_EN_UP_CNT + 1)
#define LHL4369_ASR_LPPFM_MODE_UP_CNT	(LHL4369_PWRSW_EN_UP_CNT + 1)
#define LHL4369_ASR_MODE_SEL_UP_CNT	(LHL4369_PWRSW_EN_UP_CNT + 1)
#define LHL4369_ASR_MANUAL_MODE_UP_CNT	(LHL4369_PWRSW_EN_UP_CNT + 1)
#define LHL4369_ASR_OVERI_DIS_UP_CNT	(LHL4369_PWRSW_EN_UP_CNT + 1)
#define LHL4369_ASR_TRIM_ADJ_UP_CNT	(LHL4369_PWRSW_EN_UP_CNT + 1)

/* MacResourceReqTimer0/1 */
#define MAC_RSRC_REQ_TIMER_INT_ENAB_SHIFT		24
#define MAC_RSRC_REQ_TIMER_FORCE_ALP_SHIFT		26
#define MAC_RSRC_REQ_TIMER_FORCE_HT_SHIFT		27
#define MAC_RSRC_REQ_TIMER_FORCE_HQ_SHIFT		28
#define MAC_RSRC_REQ_TIMER_CLKREQ_GRP_SEL_SHIFT	29

/* for pmu rev32 and higher */
#define PMU32_MAC_MAIN_RSRC_REQ_TIMER	((1 << MAC_RSRC_REQ_TIMER_INT_ENAB_SHIFT) |	\
					 (1 << MAC_RSRC_REQ_TIMER_FORCE_ALP_SHIFT) |	\
					 (1 << MAC_RSRC_REQ_TIMER_FORCE_HT_SHIFT) |	\
					 (1 << MAC_RSRC_REQ_TIMER_FORCE_HQ_SHIFT) |	\
					 (0 << MAC_RSRC_REQ_TIMER_CLKREQ_GRP_SEL_SHIFT))

#define PMU32_MAC_AUX_RSRC_REQ_TIMER	((1 << MAC_RSRC_REQ_TIMER_INT_ENAB_SHIFT) |	\
					 (1 << MAC_RSRC_REQ_TIMER_FORCE_ALP_SHIFT) |	\
					 (1 << MAC_RSRC_REQ_TIMER_FORCE_HT_SHIFT) |	\
					 (1 << MAC_RSRC_REQ_TIMER_FORCE_HQ_SHIFT) |	\
					 (0 << MAC_RSRC_REQ_TIMER_CLKREQ_GRP_SEL_SHIFT))

/* 4369 related: 4369 parameters
 * http://www.sj.broadcom.com/projects/BCM4369/gallery_backend.RC6.0/design/backplane/pmu_params.xls
 */
#define RES4369_DUMMY				0
#define RES4369_ABUCK				1
#define RES4369_PMU_SLEEP			2
#define RES4369_MISCLDO_PU			3
#define RES4369_LDO3P3_PU			4
#define RES4369_FAST_LPO_AVAIL			5
#define RES4369_XTAL_PU				6
#define RES4369_XTAL_STABLE			7
#define RES4369_PWRSW_DIG			8
#define RES4369_SR_DIG				9
#define RES4369_SLEEP_DIG			10
#define RES4369_PWRSW_AUX			11
#define RES4369_SR_AUX				12
#define RES4369_SLEEP_AUX			13
#define RES4369_PWRSW_MAIN			14
#define RES4369_SR_MAIN				15
#define RES4369_SLEEP_MAIN			16
#define RES4369_DIG_CORE_RDY			17
#define RES4369_CORE_RDY_AUX			18
#define RES4369_ALP_AVAIL			19
#define RES4369_RADIO_AUX_PU			20
#define RES4369_MINIPMU_AUX_PU			21
#define RES4369_CORE_RDY_MAIN			22
#define RES4369_RADIO_MAIN_PU			23
#define RES4369_MINIPMU_MAIN_PU			24
#define RES4369_PCIE_EP_PU			25
#define RES4369_COLD_START_WAIT			26
#define RES4369_ARMHTAVAIL			27
#define RES4369_HT_AVAIL			28
#define RES4369_MACPHY_AUX_CLK_AVAIL		29
#define RES4369_MACPHY_MAIN_CLK_AVAIL		30
#define RES4369_RESERVED_31			31

#define CST4369_CHIPMODE_SDIOD(cs)	(((cs) & (1 << 6)) != 0)	/* SDIO */
#define CST4369_CHIPMODE_PCIE(cs)	(((cs) & (1 << 7)) != 0)	/* PCIE */
#define CST4369_SPROM_PRESENT		0x00000010

#define PMU_4369_MACCORE_0_RES_REQ_MASK			0x3FCBF7FF
#define PMU_4369_MACCORE_1_RES_REQ_MASK			0x7FFB3647

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

#define PMU43430_PLL0_PC2_P1DIV_MASK	0x0000000f
#define PMU43430_PLL0_PC2_P1DIV_SHIFT	0
#define PMU43430_PLL0_PC2_NDIV_INT_MASK	0x0000ff80
#define PMU43430_PLL0_PC2_NDIV_INT_SHIFT	7
#define PMU43430_PLL0_PC4_MDIV2_MASK	0x0000ff00
#define PMU43430_PLL0_PC4_MDIV2_SHIFT	8

/* 43430 chip SR definitions */
#define SRAM_43430_SR_ASM_ADDR			0x7f800
#define CC_SR1_43430_SR_ASM_ADDR		((SRAM_43430_SR_ASM_ADDR - 0x60000) >> 8)

/* 43430 PMU Chip Control bits */
#define CC2_43430_SDIO_AOS_WAKEUP_MASK			(1 << 24)
#define CC2_43430_SDIO_AOS_WAKEUP_SHIFT			(24)

#define PMU_MACCORE_0_RES_REQ_TIMER		0x1d000000
#define PMU_MACCORE_0_RES_REQ_MASK		0x5FF2364F

#define PMU43012_MAC_RES_REQ_TIMER		0x1D000000
#define PMU43012_MAC_RES_REQ_MASK		0x3FBBF7FF

#define PMU_MACCORE_1_RES_REQ_TIMER		0x1d000000
#define PMU_MACCORE_1_RES_REQ_MASK		0x5FF2364F

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
#define CST4335_CHIPMODE_USB20D(cs)	(((cs) & (1 << 2)) != 0)	/**< HSIC || USBDA */
#define CST4335_CHIPMODE_PCIE(cs)	(((cs) & (1 << 3)) != 0)	/* PCIE */

/* 4335 Chip specific ChipControl1 register bits */
#define CCTRL1_4335_GPIO_SEL		(1 << 0)    /* 1=select GPIOs to be muxed out */
#define CCTRL1_4335_SDIO_HOST_WAKE (1 << 2)  /* SDIO: 1=configure GPIO0 for host wake */

/* 55500, Dedicated sapce for TCAM_PATCH and TRX HDR area at RAMSTART */
#define CR4_55500_RAM_START		(0x3a0000)
#define CR4_55500_TCAM_SZ		(0x800)
#define CR4_55500_TRX_HDR_SZ		(0x2b4)
/* 55560, Dedicated sapce for TCAM_PATCH and TRX HDR area at RAMSTART */
#define CR4_55560_RAM_START		     (0x370000)
#define CR4_55560_TCAM_SZ		     (0x800)
#if defined BCMTRXV4
#define CR4_55560_TRX_HDR_SZ		     (0x2b4)
#else
#define CR4_55560_TRX_HDR_SZ		     (0x20)
#endif // endif

/* 4335 Chip specific ChipControl2 register bits */
#define CCTRL2_4335_AOSBLOCK		(1 << 30)
#define CCTRL2_4335_PMUWAKE		(1 << 31)
#define PATCHTBL_SIZE			(0x800)
#define CR4_4335_RAM_BASE                    (0x180000)
#define CR4_4345_LT_C0_RAM_BASE              (0x1b0000)
#define CR4_4345_GE_C0_RAM_BASE              (0x198000)
#define CR4_4349_RAM_BASE                    (0x180000)
#define CR4_4349_RAM_BASE_FROM_REV_9         (0x160000)
#define CR4_4350_RAM_BASE                    (0x180000)
#define CR4_4360_RAM_BASE                    (0x0)
#define CR4_43602_RAM_BASE                   (0x180000)
#define CA7_4365_RAM_BASE                    (0x200000)
#define CR4_4373_RAM_BASE			(0x160000)
#define CST4373_JTAG_ENABLE(cs)			(((cs) & (1 << 0)) != 0)
#define CST4373_CHIPMODE_RSRC_INIT0(cs)		(((cs) & (1 << 1)) != 0)
#define CST4373_SDIO_PADVDDIO(cs)		(((cs) & (1 << 5)) != 0)
#define CST4373_USBHUB_BYPASS(cs)		(((cs) & (1 << 9)) != 0)
#define STRAP4373_CHIPMODE_RSRC_INIT1		0x1
#define STRAP4373_VTRIM_EN			0x1
#define STRAP4373_SFLASH_PRESENT		0x1
#define OTP4373_SFLASH_BYTE_OFFSET		680
#define OTP4373_SFLASH_MASK			0x3F
#define OTP4373_SFLASH_PRESENT_MASK		0x1
#define OTP4373_SFLASH_TYPE_MASK		0x2
#define OTP4373_SFLASH_TYPE_SHIFT		0x1
#define OTP4373_SFLASH_CLKDIV_MASK		0x3C
#define OTP4373_SFLASH_CLKDIV_SHIFT		0x2
#define SPROM4373_OTP_SELECT			0x00000010
#define SPROM4373_OTP_PRESENT			0x00000020
#define CC4373_SFLASH_CLKDIV_MASK		0x1F000000
#define CC4373_SFLASH_CLKDIV_SHIFT		25

#define CR4_4347_RAM_BASE                    (0x170000)
#define CR4_4362_RAM_BASE                    (0x170000)
#define CR4_4369_RAM_BASE                    (0x170000)
#define CR4_4377_RAM_BASE                    (0x170000)
#define CR4_43751_RAM_BASE                   (0x170000)
#define CA7_4367_RAM_BASE                    (0x200000)
#define CR4_4378_RAM_BASE                    (0x352000)
#ifdef CHIPS_CUSTOMER_HW6
#define CA7_4368_RAM_BASE                    (0x200000)
#endif /* CHIPS_CUSTOMER_HW6 */
/* TODO: Fix 55500 RAM BASE */
#define CR4_55500_RAM_BASE		     (CR4_55500_RAM_START + CR4_55500_TCAM_SZ \
						+ CR4_55500_TRX_HDR_SZ)
#define CR4_55560_RAM_BASE                   (CR4_55560_RAM_START + CR4_55560_TCAM_SZ \
						+ CR4_55560_TRX_HDR_SZ)

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

/* 43012 PMU resources based on pmu_params.xls  - Start */
#define RES43012_MEMLPLDO_PU			0
#define RES43012_PMU_SLEEP			1
#define RES43012_FAST_LPO			2
#define RES43012_BTLPO_3P3			3
#define RES43012_SR_POK				4
#define RES43012_DUMMY_PWRSW			5
#define RES43012_DUMMY_LDO3P3			6
#define RES43012_DUMMY_BT_LDO3P3		7
#define RES43012_DUMMY_RADIO			8
#define RES43012_VDDB_VDDRET			9
#define RES43012_HV_LDO3P3			10
#define RES43012_OTP_PU				11
#define RES43012_XTAL_PU			12
#define RES43012_SR_CLK_START			13
#define RES43012_XTAL_STABLE			14
#define RES43012_FCBS				15
#define RES43012_CBUCK_MODE			16
#define RES43012_CORE_READY			17
#define RES43012_ILP_REQ			18
#define RES43012_ALP_AVAIL			19
#define RES43012_RADIOLDO_1P8			20
#define RES43012_MINI_PMU			21
#define RES43012_UNUSED				22
#define RES43012_SR_SAVE_RESTORE		23
#define RES43012_PHY_PWRSW			24
#define RES43012_VDDB_CLDO			25
#define RES43012_SUBCORE_PWRSW			26
#define RES43012_SR_SLEEP			27
#define RES43012_HT_START			28
#define RES43012_HT_AVAIL			29
#define RES43012_MACPHY_CLK_AVAIL		30
#define CST43012_SPROM_PRESENT        0x00000010

/* SR Control0 bits */
#define SR0_43012_SR_ENG_EN_MASK             0x1
#define SR0_43012_SR_ENG_EN_SHIFT            0
#define SR0_43012_SR_ENG_CLK_EN              (1 << 1)
#define SR0_43012_SR_RSRC_TRIGGER            (0xC << 2)
#define SR0_43012_SR_WD_MEM_MIN_DIV          (0x3 << 6)
#define SR0_43012_SR_MEM_STBY_ALLOW_MSK      (1 << 16)
#define SR0_43012_SR_MEM_STBY_ALLOW_SHIFT    16
#define SR0_43012_SR_ENABLE_ILP              (1 << 17)
#define SR0_43012_SR_ENABLE_ALP              (1 << 18)
#define SR0_43012_SR_ENABLE_HT               (1 << 19)
#define SR0_43012_SR_ALLOW_PIC               (3 << 20)
#define SR0_43012_SR_PMU_MEM_DISABLE         (1 << 30)
#define CC_43012_VDDM_PWRSW_EN_MASK          (1 << 20)
#define CC_43012_VDDM_PWRSW_EN_SHIFT         (20)
#define CC_43012_SDIO_AOS_WAKEUP_MASK        (1 << 24)
#define CC_43012_SDIO_AOS_WAKEUP_SHIFT       (24)

/* 43012 - offset at 5K */
#define SR1_43012_SR_INIT_ADDR_MASK          0x3ff
#define SR1_43012_SR_ASM_ADDR                0xA

/* PLL usage in 43012 */
#define PMU43012_PLL0_PC0_NDIV_INT_MASK			0x0000003f
#define PMU43012_PLL0_PC0_NDIV_INT_SHIFT		0
#define PMU43012_PLL0_PC0_NDIV_FRAC_MASK		0xfffffc00
#define PMU43012_PLL0_PC0_NDIV_FRAC_SHIFT		10
#define PMU43012_PLL0_PC3_PDIV_MASK			0x00003c00
#define PMU43012_PLL0_PC3_PDIV_SHIFT			10
#define PMU43012_PLL_NDIV_FRAC_BITS			20
#define PMU43012_PLL_P_DIV_SCALE_BITS			10

#define CCTL_43012_ARM_OFFCOUNT_MASK			0x00000003
#define CCTL_43012_ARM_OFFCOUNT_SHIFT			0
#define CCTL_43012_ARM_ONCOUNT_MASK			0x0000000c
#define CCTL_43012_ARM_ONCOUNT_SHIFT			2

/* PMU Rev >= 30 */
#define PMU30_ALPCLK_ONEMHZ_ENAB			0x80000000

#define BCM7271_PMU30_ALPCLK_ONEMHZ_ENAB		0x00010000

/* 43012 PMU Chip Control Registers */
#define PMUCCTL02_43012_SUBCORE_PWRSW_FORCE_ON		0x00000010
#define PMUCCTL02_43012_PHY_PWRSW_FORCE_ON		0x00000040
#define PMUCCTL02_43012_LHL_TIMER_SELECT		0x00000800
#define PMUCCTL02_43012_RFLDO3P3_PU_FORCE_ON		0x00008000
#define PMUCCTL02_43012_WL2CDIG_I_PMU_SLEEP_ENAB	0x00010000
#define PMUCCTL02_43012_BTLDO3P3_PU_FORCE_OFF		(1 << 12)

#define PMUCCTL04_43012_BBPLL_ENABLE_PWRDN			0x00100000
#define PMUCCTL04_43012_BBPLL_ENABLE_PWROFF			0x00200000
#define PMUCCTL04_43012_FORCE_BBPLL_ARESET			0x00400000
#define PMUCCTL04_43012_FORCE_BBPLL_DRESET			0x00800000
#define PMUCCTL04_43012_FORCE_BBPLL_PWRDN			0x01000000
#define PMUCCTL04_43012_FORCE_BBPLL_ISOONHIGH			0x02000000
#define PMUCCTL04_43012_FORCE_BBPLL_PWROFF			0x04000000
#define PMUCCTL04_43012_DISABLE_LQ_AVAIL			0x08000000
#define PMUCCTL04_43012_DISABLE_HT_AVAIL			0x10000000
#define PMUCCTL04_43012_USE_LOCK				0x20000000
#define PMUCCTL04_43012_OPEN_LOOP_ENABLE			0x40000000
#define PMUCCTL04_43012_FORCE_OPEN_LOOP				0x80000000
#define PMUCCTL05_43012_DISABLE_SPM_CLK				(1 << 8)
#define PMUCCTL05_43012_RADIO_DIG_CLK_GATING_EN			(1 << 14)
#define PMUCCTL06_43012_GCI2RDIG_USE_ASYNCAPB			(1 << 31)
#define PMUCCTL08_43012_XTAL_CORE_SIZE_PMOS_NORMAL_MASK		0x00000FC0
#define PMUCCTL08_43012_XTAL_CORE_SIZE_PMOS_NORMAL_SHIFT	6
#define PMUCCTL08_43012_XTAL_CORE_SIZE_NMOS_NORMAL_MASK		0x00FC0000
#define PMUCCTL08_43012_XTAL_CORE_SIZE_NMOS_NORMAL_SHIFT	18
#define PMUCCTL08_43012_XTAL_SEL_BIAS_RES_NORMAL_MASK		0x07000000
#define PMUCCTL08_43012_XTAL_SEL_BIAS_RES_NORMAL_SHIFT		24
#define PMUCCTL09_43012_XTAL_CORESIZE_BIAS_ADJ_NORMAL_MASK	0x0003F000
#define PMUCCTL09_43012_XTAL_CORESIZE_BIAS_ADJ_NORMAL_SHIFT	12
#define PMUCCTL09_43012_XTAL_CORESIZE_RES_BYPASS_NORMAL_MASK	0x00000038
#define PMUCCTL09_43012_XTAL_CORESIZE_RES_BYPASS_NORMAL_SHIFT	3

#define PMUCCTL09_43012_XTAL_CORESIZE_BIAS_ADJ_STARTUP_MASK	0x00000FC0
#define PMUCCTL09_43012_XTAL_CORESIZE_BIAS_ADJ_STARTUP_SHIFT	6
/* during normal operation normal value is reduced for optimized power */
#define PMUCCTL09_43012_XTAL_CORESIZE_BIAS_ADJ_STARTUP_VAL	0x1F

#define PMUCCTL13_43012_FCBS_UP_TRIG_EN				0x00000400

#define PMUCCTL14_43012_ARMCM3_RESET_INITVAL			0x00000001
#define PMUCCTL14_43012_DOT11MAC_CLKEN_INITVAL			0x00000020
#define PMUCCTL14_43012_DOT11MAC_PHY_CLK_EN_INITVAL		0x00000080
#define PMUCCTL14_43012_DOT11MAC_PHY_CNTL_EN_INITVAL		0x00000200
#define PMUCCTL14_43012_SDIOD_RESET_INIVAL			0x00000400
#define PMUCCTL14_43012_SDIO_CLK_DMN_RESET_INITVAL		0x00001000
#define PMUCCTL14_43012_SOCRAM_CLKEN_INITVAL			0x00004000
#define PMUCCTL14_43012_M2MDMA_RESET_INITVAL			0x00008000
#define PMUCCTL14_43012_DISABLE_LQ_AVAIL			0x08000000

#define VREG6_43012_MEMLPLDO_ADJ_MASK				0x0000F000
#define VREG6_43012_MEMLPLDO_ADJ_SHIFT				12

#define VREG6_43012_LPLDO_ADJ_MASK				0x000000F0
#define VREG6_43012_LPLDO_ADJ_SHIFT				4

#define VREG7_43012_PWRSW_1P8_PU_MASK				0x00400000
#define VREG7_43012_PWRSW_1P8_PU_SHIFT				22

/* 4347 PMU Chip Control Registers */
#define PMUCCTL03_4347_XTAL_CORESIZE_PMOS_NORMAL_MASK		0x001F8000
#define PMUCCTL03_4347_XTAL_CORESIZE_PMOS_NORMAL_SHIFT		15
#define PMUCCTL03_4347_XTAL_CORESIZE_PMOS_NORMAL_VAL		0x3F

#define PMUCCTL03_4347_XTAL_CORESIZE_NMOS_NORMAL_MASK		0x07E00000
#define PMUCCTL03_4347_XTAL_CORESIZE_NMOS_NORMAL_SHIFT		21
#define PMUCCTL03_4347_XTAL_CORESIZE_NMOS_NORMAL_VAL		0x3F

#define PMUCCTL03_4347_XTAL_SEL_BIAS_RES_NORMAL_MASK		0x38000000
#define PMUCCTL03_4347_XTAL_SEL_BIAS_RES_NORMAL_SHIFT		27
#define PMUCCTL03_4347_XTAL_SEL_BIAS_RES_NORMAL_VAL			0x0

#define PMUCCTL00_4347_XTAL_CORESIZE_BIAS_ADJ_NORMAL_MASK	0x00000FC0
#define PMUCCTL00_4347_XTAL_CORESIZE_BIAS_ADJ_NORMAL_SHIFT	6
#define PMUCCTL00_4347_XTAL_CORESIZE_BIAS_ADJ_NORMAL_VAL	0x5

#define PMUCCTL00_4347_XTAL_RES_BYPASS_NORMAL_MASK			0x00038000
#define PMUCCTL00_4347_XTAL_RES_BYPASS_NORMAL_SHIFT			15
#define PMUCCTL00_4347_XTAL_RES_BYPASS_NORMAL_VAL			0x7

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
#define MUXENAB4350_HOSTWAKE_MASK	(0x000000f0)	/**< configure GPIO for host_wake */
#define MUXENAB4350_HOSTWAKE_SHIFT	4
#define MUXENAB4349_UART_MASK           (0xf)

#define CC4350_GPIO_COUNT		16

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

/* 4347 GCI function sel values */
#define CC4347_FNSEL_HWDEF		(0)
#define CC4347_FNSEL_SAMEASPIN		(1)
#define CC4347_FNSEL_GPIO0		(2)
#define CC4347_FNSEL_FUART		(3)
#define CC4347_FNSEL_GCI0		(4)
#define CC4347_FNSEL_GCI1		(5)
#define CC4347_FNSEL_DBG_UART		(6)
#define CC4347_FNSEL_SPI		(7)
#define CC4347_FNSEL_SPROM		(8)
#define CC4347_FNSEL_MISC0		(9)
#define CC4347_FNSEL_MISC1		(10)
#define CC4347_FNSEL_MISC2		(11)
#define CC4347_FNSEL_IND		(12)
#define CC4347_FNSEL_PDN		(13)
#define CC4347_FNSEL_PUP		(14)
#define CC4347_FNSEL_TRISTATE		(15)

/* 4347 GPIO */
#define CC4347_PIN_GPIO_02		(2)
#define CC4347_PIN_GPIO_03		(3)
#define CC4347_PIN_GPIO_04		(4)
#define CC4347_PIN_GPIO_05		(5)
#define CC4347_PIN_GPIO_06		(6)
#define CC4347_PIN_GPIO_07		(7)
#define CC4347_PIN_GPIO_08		(8)
#define CC4347_PIN_GPIO_09		(9)
#define CC4347_PIN_GPIO_10		(10)
#define CC4347_PIN_GPIO_11		(11)
#define CC4347_PIN_GPIO_12		(12)
#define CC4347_PIN_GPIO_13		(13)
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
#define CC_GCI_CHIPCTRL_09	(9)
#define CC_GCI_CHIPCTRL_10	(10)
#define CC_GCI_CHIPCTRL_10	(10)
#define CC_GCI_CHIPCTRL_11	(11)
#define CC_GCI_XTAL_BUFSTRG_NFC (0xff << 12)

#define CC_GCI_04_SDIO_DRVSTR_SHIFT	15
#define CC_GCI_04_SDIO_DRVSTR_MASK	(0x0f << CC_GCI_04_SDIO_DRVSTR_SHIFT)	/* 0x00078000 */
#define CC_GCI_04_SDIO_DRVSTR_OVERRIDE_BIT	(1 << 18)
#define CC_GCI_04_SDIO_DRVSTR_DEFAULT_MA	14
#define CC_GCI_04_SDIO_DRVSTR_MIN_MA	2
#define CC_GCI_04_SDIO_DRVSTR_MAX_MA	16

#define CC_GCI_06_JTAG_SEL_SHIFT	4
#define CC_GCI_06_JTAG_SEL_MASK		(1 << 4)

#define CC_GCI_NUMCHIPCTRLREGS(cap1)	((cap1 & 0xF00) >> 8)

#define CC_GCI_03_LPFLAGS_SFLASH_MASK		(0xFFFFFF << 8)
#define CC_GCI_03_LPFLAGS_SFLASH_VAL		(0xCCCCCC << 8)
#define GPIO_CTRL_REG_DISABLE_INTERRUPT		(3 << 9)
#define GPIO_CTRL_REG_COUNT			40

/* GCI chipstatus register indices */
#define GCI_CHIPSTATUS_00	(0)
#define GCI_CHIPSTATUS_01	(1)
#define GCI_CHIPSTATUS_02	(2)
#define GCI_CHIPSTATUS_03	(3)
#define GCI_CHIPSTATUS_04	(4)
#define GCI_CHIPSTATUS_05	(5)
#define GCI_CHIPSTATUS_06	(6)
#define GCI_CHIPSTATUS_07	(7)
#define GCI_CHIPSTATUS_08	(8)
#define GCI_CHIPSTATUS_09	(9)
#define GCI_CHIPSTATUS_10	(10)
#define GCI_CHIPSTATUS_11	(11)
#define GCI_CHIPSTATUS_12	(12)
#define GCI_CHIPSTATUS_13	(13)

/* 43021 GCI chipstatus registers */
#define GCI43012_CHIPSTATUS_07_BBPLL_LOCK_MASK	(1 << 3)

/* 4345 PMU resources */
#define RES4345_LPLDO_PU		0
#define RES4345_PMU_BG_PU		1
#define RES4345_PMU_SLEEP		2
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

/* 43012 pins
 * note: only the values set as default/used are added here.
 */
#define CC43012_PIN_GPIO_00		(0)
#define CC43012_PIN_GPIO_01		(1)
#define CC43012_PIN_GPIO_02		(2)
#define CC43012_PIN_GPIO_03		(3)
#define CC43012_PIN_GPIO_04		(4)
#define CC43012_PIN_GPIO_05		(5)
#define CC43012_PIN_GPIO_06		(6)
#define CC43012_PIN_GPIO_07		(7)
#define CC43012_PIN_GPIO_08		(8)
#define CC43012_PIN_GPIO_09		(9)
#define CC43012_PIN_GPIO_10		(10)
#define CC43012_PIN_GPIO_11		(11)
#define CC43012_PIN_GPIO_12		(12)
#define CC43012_PIN_GPIO_13		(13)
#define CC43012_PIN_GPIO_14		(14)
#define CC43012_PIN_GPIO_15		(15)

/* 43012 GCI function sel values */
#define CC43012_FNSEL_HWDEF		(0)
#define CC43012_FNSEL_SAMEASPIN	(1)
#define CC43012_FNSEL_GPIO0		(2)
#define CC43012_FNSEL_GPIO1		(3)
#define CC43012_FNSEL_GCI0		(4)
#define CC43012_FNSEL_GCI1		(5)
#define CC43012_FNSEL_DBG_UART	(6)
#define CC43012_FNSEL_I2C		(7)
#define CC43012_FNSEL_BT_SFLASH	(8)
#define CC43012_FNSEL_MISC0		(9)
#define CC43012_FNSEL_MISC1		(10)
#define CC43012_FNSEL_MISC2		(11)
#define CC43012_FNSEL_IND		(12)
#define CC43012_FNSEL_PDN		(13)
#define CC43012_FNSEL_PUP		(14)
#define CC43012_FNSEL_TRI		(15)

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
#define	GCI_CORECTRL_SR_MASK	(1 << 0)	/**< SECI block Reset */
#define	GCI_CORECTRL_RSL_MASK	(1 << 1)	/**< ResetSECILogic */
#define	GCI_CORECTRL_ES_MASK	(1 << 2)	/**< EnableSECI */
#define	GCI_CORECTRL_FSL_MASK	(1 << 3)	/**< Force SECI Out Low */
#define	GCI_CORECTRL_SOM_MASK	(7 << 4)	/**< SECI Op Mode */
#define	GCI_CORECTRL_US_MASK	(1 << 7)	/**< Update SECI */
#define	GCI_CORECTRL_BOS_MASK	(1 << 8)	/**< Break On Sleep */
#define	GCI_CORECTRL_FORCEREGCLK_MASK	(1 << 18)	/* ForceRegClk */

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

/* 43430 Pin */
#define CC43430_PIN_GPIO_00		(0)
#define CC43430_PIN_GPIO_01		(1)
#define CC43430_PIN_GPIO_02		(2)
#define CC43430_PIN_GPIO_07		(7)
#define CC43430_PIN_GPIO_08		(8)
#define CC43430_PIN_GPIO_09		(9)
#define CC43430_PIN_GPIO_10		(10)

#define CC43430_FNSEL_SDIO_INT		(2)
#define CC43430_FNSEL_6_FAST_UART	(6)
#define CC43430_FNSEL_10_FAST_UART	(10)

#define MUXENAB43430_UART_MASK		(0x0000000f)
#define MUXENAB43430_UART_SHIFT		0
#define MUXENAB43430_HOSTWAKE_MASK	(0x000000f0)	/* configure GPIO for SDIO host_wake */
#define MUXENAB43430_HOSTWAKE_SHIFT	4

#define CC43430_FNSEL_SAMEASPIN		(1)
#define CC43430_RFSWCTRL_EN_MASK   (0x7f8)
#define CC43430_RFSWCTRL_EN_SHIFT  (3)

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
#define GCI_INTSTATUS_RBI	(1 << 0)	/**< Rx Break Interrupt */
#define GCI_INTSTATUS_UB	(1 << 1)	/**< UART Break Interrupt */
#define GCI_INTSTATUS_SPE	(1 << 2)	/**< SECI Parity Error Interrupt */
#define GCI_INTSTATUS_SFE	(1 << 3)	/**< SECI Framing Error Interrupt */
#define GCI_INTSTATUS_SRITI	(1 << 9)	/**< SECI Rx Idle Timer Interrupt */
#define GCI_INTSTATUS_STFF	(1 << 10)	/**< SECI Tx FIFO Full Interrupt */
#define GCI_INTSTATUS_STFAE	(1 << 11)	/**< SECI Tx FIFO Almost Empty Intr */
#define GCI_INTSTATUS_SRFAF	(1 << 12)	/**< SECI Rx FIFO Almost Full */
#define GCI_INTSTATUS_SRFNE	(1 << 14)	/**< SECI Rx FIFO Not Empty */
#define GCI_INTSTATUS_SRFOF	(1 << 15)	/**< SECI Rx FIFO Not Empty Timeout */
#define GCI_INTSTATUS_EVENT  (1 << 21)   /* GCI Event Interrupt */
#define GCI_INTSTATUS_LEVELWAKE (1 << 22)   /* GCI Wake Level Interrupt */
#define GCI_INTSTATUS_EVENTWAKE (1 << 23)   /* GCI Wake Event Interrupt */
#define GCI_INTSTATUS_GPIOINT	(1 << 25)	/**< GCIGpioInt */
#define GCI_INTSTATUS_GPIOWAKE	(1 << 26)	/**< GCIGpioWake */
#define GCI_INTSTATUS_LHLWLWAKE	(1 << 30)	/* LHL WL wake */

/* 4335 GCI IntMask Register bits. */
#define GCI_INTMASK_RBI		(1 << 0)	/**< Rx Break Interrupt */
#define GCI_INTMASK_UB		(1 << 1)	/**< UART Break Interrupt */
#define GCI_INTMASK_SPE		(1 << 2)	/**< SECI Parity Error Interrupt */
#define GCI_INTMASK_SFE		(1 << 3)	/**< SECI Framing Error Interrupt */
#define GCI_INTMASK_SRITI	(1 << 9)	/**< SECI Rx Idle Timer Interrupt */
#define GCI_INTMASK_STFF	(1 << 10)	/**< SECI Tx FIFO Full Interrupt */
#define GCI_INTMASK_STFAE	(1 << 11)	/**< SECI Tx FIFO Almost Empty Intr */
#define GCI_INTMASK_SRFAF	(1 << 12)	/**< SECI Rx FIFO Almost Full */
#define GCI_INTMASK_SRFNE	(1 << 14)	/**< SECI Rx FIFO Not Empty */
#define GCI_INTMASK_SRFOF	(1 << 15)	/**< SECI Rx FIFO Not Empty Timeout */
#define GCI_INTMASK_EVENT (1 << 21)   /* GCI Event Interrupt */
#define GCI_INTMASK_LEVELWAKE   (1 << 22)   /* GCI Wake Level Interrupt */
#define GCI_INTMASK_EVENTWAKE   (1 << 23)   /* GCI Wake Event Interrupt */
#define GCI_INTMASK_GPIOINT	(1 << 25)	/**< GCIGpioInt */
#define GCI_INTMASK_GPIOWAKE	(1 << 26)	/**< GCIGpioWake */
#define GCI_INTMASK_LHLWLWAKE	(1 << 30)	/* LHL WL wake */

/* 4335 GCI WakeMask Register bits. */
#define GCI_WAKEMASK_RBI	(1 << 0)	/**< Rx Break Interrupt */
#define GCI_WAKEMASK_UB		(1 << 1)	/**< UART Break Interrupt */
#define GCI_WAKEMASK_SPE	(1 << 2)	/**< SECI Parity Error Interrupt */
#define GCI_WAKEMASK_SFE	(1 << 3)	/**< SECI Framing Error Interrupt */
#define GCI_WAKE_SRITI		(1 << 9)	/**< SECI Rx Idle Timer Interrupt */
#define GCI_WAKEMASK_STFF	(1 << 10)	/**< SECI Tx FIFO Full Interrupt */
#define GCI_WAKEMASK_STFAE	(1 << 11)	/**< SECI Tx FIFO Almost Empty Intr */
#define GCI_WAKEMASK_SRFAF	(1 << 12)	/**< SECI Rx FIFO Almost Full */
#define GCI_WAKEMASK_SRFNE	(1 << 14)	/**< SECI Rx FIFO Not Empty */
#define GCI_WAKEMASK_SRFOF	(1 << 15)	/**< SECI Rx FIFO Not Empty Timeout */
#define GCI_WAKEMASK_EVENT   (1 << 21)   /* GCI Event Interrupt */
#define GCI_WAKEMASK_LEVELWAKE  (1 << 22)   /* GCI Wake Level Interrupt */
#define GCI_WAKEMASK_EVENTWAKE  (1 << 23)   /* GCI Wake Event Interrupt */
#define GCI_WAKEMASK_GPIOINT	(1 << 25)	/**< GCIGpioInt */
#define GCI_WAKEMASK_GPIOWAKE	(1 << 26)	/**< GCIGpioWake */
#define GCI_WAKEMASK_LHLWLWAKE	(1 << 30)	/* LHL WL wake */

#define	GCI_WAKE_ON_GCI_GPIO1	1
#define	GCI_WAKE_ON_GCI_GPIO2	2
#define	GCI_WAKE_ON_GCI_GPIO3	3
#define	GCI_WAKE_ON_GCI_GPIO4	4
#define	GCI_WAKE_ON_GCI_GPIO5	5
#define	GCI_WAKE_ON_GCI_GPIO6	6
#define	GCI_WAKE_ON_GCI_GPIO7	7
#define	GCI_WAKE_ON_GCI_GPIO8	8
#define	GCI_WAKE_ON_GCI_SECI_IN	9

#define	PMU_EXT_WAKE_MASK_0_SDIO		(1 << 2)

/* =========== LHL regs =========== */
#define LHL_PWRSEQCTL_SLEEP_EN			(1 << 0)
#define LHL_PWRSEQCTL_PMU_SLEEP_MODE		(1 << 1)
#define LHL_PWRSEQCTL_PMU_FINAL_PMU_SLEEP_EN	(1 << 2)
#define LHL_PWRSEQCTL_PMU_TOP_ISO_EN		(1 << 3)
#define LHL_PWRSEQCTL_PMU_TOP_SLB_EN		(1 << 4)
#define LHL_PWRSEQCTL_PMU_TOP_PWRSW_EN		(1 << 5)
#define LHL_PWRSEQCTL_PMU_CLDO_PD		(1 << 6)
#define LHL_PWRSEQCTL_PMU_LPLDO_PD		(1 << 7)
#define LHL_PWRSEQCTL_PMU_RSRC6_EN		(1 << 8)

#define PMU_SLEEP_MODE_0	(LHL_PWRSEQCTL_SLEEP_EN |\
				LHL_PWRSEQCTL_PMU_FINAL_PMU_SLEEP_EN)

#define PMU_SLEEP_MODE_1	(LHL_PWRSEQCTL_SLEEP_EN |\
				  LHL_PWRSEQCTL_PMU_SLEEP_MODE |\
				  LHL_PWRSEQCTL_PMU_FINAL_PMU_SLEEP_EN |\
				  LHL_PWRSEQCTL_PMU_TOP_ISO_EN |\
				  LHL_PWRSEQCTL_PMU_TOP_SLB_EN |\
				  LHL_PWRSEQCTL_PMU_TOP_PWRSW_EN |\
				  LHL_PWRSEQCTL_PMU_CLDO_PD |\
				  LHL_PWRSEQCTL_PMU_RSRC6_EN)

#define PMU_SLEEP_MODE_2	(LHL_PWRSEQCTL_SLEEP_EN |\
				  LHL_PWRSEQCTL_PMU_SLEEP_MODE |\
				  LHL_PWRSEQCTL_PMU_FINAL_PMU_SLEEP_EN |\
				  LHL_PWRSEQCTL_PMU_TOP_ISO_EN |\
				  LHL_PWRSEQCTL_PMU_TOP_SLB_EN |\
				  LHL_PWRSEQCTL_PMU_TOP_PWRSW_EN |\
				  LHL_PWRSEQCTL_PMU_CLDO_PD |\
				  LHL_PWRSEQCTL_PMU_LPLDO_PD |\
				  LHL_PWRSEQCTL_PMU_RSRC6_EN)

#define LHL_PWRSEQ_CTL				(0x000000ff)

/* LHL Top Level Power Up Control Register (lhl_top_pwrup_ctl_adr, Offset 0xE78)
* Top Level Counter values for isolation, retention, Power Switch control
*/
#define LHL_PWRUP_ISOLATION_CNT			(0x6 << 8)
#define LHL_PWRUP_RETENTION_CNT			(0x5 << 16)
#define LHL_PWRUP_PWRSW_CNT			(0x7 << 24)
/* Mask is taken only for isolation 8:13 , Retention 16:21 ,
* Power Switch control 24:29
*/
#define LHL_PWRUP_CTL_MASK			(0x3F3F3F00)
#define LHL_PWRUP_CTL				(LHL_PWRUP_ISOLATION_CNT |\
						LHL_PWRUP_RETENTION_CNT |\
						LHL_PWRUP_PWRSW_CNT)

#define LHL_PWRUP_ISOLATION_CNT_4347		(0x7 << 8)
#define LHL_PWRUP_RETENTION_CNT_4347		(0x5 << 16)
#define LHL_PWRUP_PWRSW_CNT_4347		(0x7 << 24)

#define LHL_PWRUP_CTL_4347			(LHL_PWRUP_ISOLATION_CNT_4347 |\
						LHL_PWRUP_RETENTION_CNT_4347 |\
						LHL_PWRUP_PWRSW_CNT_4347)

#define LHL_PWRUP2_CLDO_DN_CNT			(0x0)
#define LHL_PWRUP2_LPLDO_DN_CNT			(0x0 << 8)
#define LHL_PWRUP2_RSRC6_DN_CN			(0x4 << 16)
#define LHL_PWRUP2_RSRC7_DN_CN			(0x0 << 24)
#define LHL_PWRUP2_CTL_MASK			(0x3F3F3F3F)
#define LHL_PWRUP2_CTL				(LHL_PWRUP2_CLDO_DN_CNT |\
						LHL_PWRUP2_LPLDO_DN_CNT |\
						LHL_PWRUP2_RSRC6_DN_CN |\
						LHL_PWRUP2_RSRC7_DN_CN)

/* LHL Top Level Power Down Control Register (lhl_top_pwrdn_ctl_adr, Offset 0xE74) */
#define LHL_PWRDN_SLEEP_CNT			(0x4)
#define LHL_PWRDN_CTL_MASK			(0x3F)

/* LHL Top Level Power Down Control 2 Register (lhl_top_pwrdn2_ctl_adr, Offset 0xE80) */
#define LHL_PWRDN2_CLDO_DN_CNT			(0x4)
#define LHL_PWRDN2_LPLDO_DN_CNT			(0x4 << 8)
#define LHL_PWRDN2_RSRC6_DN_CN			(0x3 << 16)
#define LHL_PWRDN2_RSRC7_DN_CN			(0x0 << 24)
#define LHL_PWRDN2_CTL				(LHL_PWRDN2_CLDO_DN_CNT |\
						LHL_PWRDN2_LPLDO_DN_CNT |\
						LHL_PWRDN2_RSRC6_DN_CN |\
						LHL_PWRDN2_RSRC7_DN_CN)
#define LHL_PWRDN2_CTL_MASK			(0x3F3F3F3F)

#define LHL_FAST_WRITE_EN			(1 << 14)

/* WL ARM Timer0 Interrupt Mask (lhl_wl_armtim0_intrp_adr) */
#define LHL_WL_ARMTIM0_INTRP_EN			0x00000001
#define LHL_WL_ARMTIM0_INTRP_EDGE_TRIGGER	0x00000002

/* WL MAC Timer0 Interrupt Mask (lhl_wl_mactim0_intrp_adr) */
#define LHL_WL_MACTIM0_INTRP_EN			0x00000001
#define LHL_WL_MACTIM0_INTRP_EDGE_TRIGGER	0x00000002

/* LHL Wakeup Status (lhl_wkup_status_adr) */
#define LHL_WKUP_STATUS_WR_PENDING_ARMTIM0	0x00100000

/* WL ARM Timer0 Interrupt Status (lhl_wl_armtim0_st_adr) */
#define LHL_WL_ARMTIM0_ST_WL_ARMTIM_INT_ST	0x00000001

#define LHL_PS_MODE_0	0
#define LHL_PS_MODE_1	1

/* GCI EventIntMask Register SW bits */
#define GCI_MAILBOXDATA_TOWLAN	(1 << 0)
#define GCI_MAILBOXDATA_TOBT		(1 << 1)
#define GCI_MAILBOXDATA_TONFC		(1 << 2)
#define GCI_MAILBOXDATA_TOGPS		(1 << 3)
#define GCI_MAILBOXDATA_TOLTE		(1 << 4)
#define GCI_MAILBOXACK_TOWLAN		(1 << 8)
#define GCI_MAILBOXACK_TOBT		(1 << 9)
#define GCI_MAILBOXACK_TONFC		(1 << 10)
#define GCI_MAILBOXACK_TOGPS		(1 << 11)
#define GCI_MAILBOXACK_TOLTE		(1 << 12)
#define GCI_WAKE_TOWLAN				(1 << 16)
#define GCI_WAKE_TOBT				(1 << 17)
#define GCI_WAKE_TONFC				(1 << 18)
#define GCI_WAKE_TOGPS				(1 << 19)
#define GCI_WAKE_TOLTE				(1 << 20)
#define GCI_SWREADY					(1 << 24)

/* 4349 Group (4349, 4355, 4359) GCI SECI_OUT TX Status Regiser bits */
#define GCI_SECIOUT_TXSTATUS_TXHALT		(1 << 0)
#define GCI_SECIOUT_TXSTATUS_TI			(1 << 16)

/* 4335 MUX options. each nibble belongs to a setting. Non-zero value specifies a logic
* for now only UART for bootloader.
*/
#define MUXENAB4335_UART_MASK		(0x0000000f)

#define MUXENAB4335_UART_SHIFT		0
#define MUXENAB4335_HOSTWAKE_MASK	(0x000000f0)	/**< configure GPIO for SDIO host_wake */
#define MUXENAB4335_HOSTWAKE_SHIFT	4
#define MUXENAB4335_GETIX(val, name) \
	((((val) & MUXENAB4335_ ## name ## _MASK) >> MUXENAB4335_ ## name ## _SHIFT) - 1)

/* 43012 MUX options */
#define MUXENAB43012_HOSTWAKE_MASK	(0x00000001)
#define MUXENAB43012_GETIX(val, name) (val - 1)

/*
* Maximum delay for the PMU state transition in us.
* This is an upper bound intended for spinwaits etc.
*/
#define PMU_MAX_TRANSITION_DLY	15000

/* PMU resource up transition time in ILP cycles */
#define PMURES_UP_TRANSITION	2

/* 53573 PMU Resource */
#define RES53573_REGULATOR_PU     0
#define RES53573_XTALLDO_PU       1
#define RES53573_XTAL_PU          2
#define RES53573_MINI_PMU         3
#define RES53573_RADIO_PU         4
#define RES53573_ILP_REQ          5
#define RES53573_ALP_AVAIL        6
#define RES53573_CPUPLL_LDO_PU    7
#define RES53573_CPU_PLL_PU       8
#define RES53573_WLAN_BB_PLL_PU   9
#define RES53573_MISCPLL_LDO_PU    10
#define RES53573_MISCPLL_PU       11
#define RES53573_AUDIOPLL_PU      12
#define RES53573_PCIEPLL_LDO_PU   13
#define RES53573_PCIEPLL_PU       14
#define RES53573_DDRPLL_LDO_PU    15
#define RES53573_DDRPLL_PU        16
#define RES53573_HT_AVAIL         17
#define RES53573_MACPHY_CLK_AVAIL 18
#define RES53573_OTP_PU           19
#define RES53573_RSVD20           20

/* 53573 Chip status registers */
#define CST53573_LOCK_CPUPLL          0x00000001
#define CST53573_LOCK_MISCPLL         0x00000002
#define CST53573_LOCK_DDRPLL          0x00000004
#define CST53573_LOCK_PCIEPLL         0x00000008
#define CST53573_EPHY_ENERGY_DET      0x00001f00
#define CST53573_RAW_ENERGY           0x0003e000
#define CST53573_BBPLL_LOCKED_O       0x00040000
#define CST53573_SERDES_PIPE_PLLLOCK  0x00080000
#define CST53573_STRAP_PCIE_EP_MODE   0x00100000
#define CST53573_EPHY_PLL_LOCK        0x00200000
#define CST53573_AUDIO_PLL_LOCKED_O   0x00400000
#define CST53573_PCIE_LINK_IN_L11     0x01000000
#define CST53573_PCIE_LINK_IN_L12     0x02000000
#define CST53573_DIN_PACKAGEOPTION    0xf0000000

/* 53573 Chip control registers macro definitions */
#define PMU_53573_CHIPCTL1                      1
#define PMU_53573_CC1_HT_CLK_REQ_CTRL_MASK      0x00000010
#define PMU_53573_CC1_HT_CLK_REQ_CTRL           0x00000010

#define PMU_53573_CHIPCTL3                      3
#define PMU_53573_CC3_ENABLE_CLOSED_LOOP_MASK   0x00000010
#define PMU_53573_CC3_ENABLE_CLOSED_LOOP        0x00000000
#define PMU_53573_CC3_ENABLE_BBPLL_PWRDOWN_MASK 0x00000002
#define PMU_53573_CC3_ENABLE_BBPLL_PWRDOWN      0x00000002

#define CST53573_CHIPMODE_PCIE(cs)		FALSE

/* SECI Status (0x134) & Mask (0x138) bits - Rev 35 */
#define SECI_STAT_BI	(1 << 0)	/* Break Interrupt */
#define SECI_STAT_SPE	(1 << 1)	/* Parity Error */
#define SECI_STAT_SFE	(1 << 2)	/* Parity Error */
#define SECI_STAT_SDU	(1 << 3)	/* Data Updated */
#define SECI_STAT_SADU	(1 << 4)	/* Auxiliary Data Updated */
#define SECI_STAT_SAS	(1 << 6)	/* AUX State */
#define SECI_STAT_SAS2	(1 << 7)	/* AUX2 State */
#define SECI_STAT_SRITI	(1 << 8)	/* Idle Timer Interrupt */
#define SECI_STAT_STFF	(1 << 9)	/* Tx FIFO Full */
#define SECI_STAT_STFAE	(1 << 10)	/* Tx FIFO Almost Empty */
#define SECI_STAT_SRFE	(1 << 11)	/* Rx FIFO Empty */
#define SECI_STAT_SRFAF	(1 << 12)	/* Rx FIFO Almost Full */
#define SECI_STAT_SFCE	(1 << 13)	/* Flow Control Event */

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

#define SECI_AUX_TX_START       (1 << 31)
#define SECI_SLIP_ESC_CHAR	0xDB
#define SECI_SIGNOFF_0		SECI_SLIP_ESC_CHAR
#define SECI_SIGNOFF_1     0
#define SECI_REFRESH_REQ	0xDA

/* seci clk_ctl_st bits */
#define CLKCTL_STS_HT_AVAIL_REQ		(1 << 4)
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

#define GCI_GPIO_STS_EDGE_TRIG_BIT			0
#define GCI_GPIO_STS_NEG_EDGE_TRIG_BIT		1
#define GCI_GPIO_STS_DUAL_EDGE_TRIG_BIT		2
#define GCI_GPIO_STS_WL_DIN_SELECT		6

#define GCI_GPIO_STS_VALUE	(1 << GCI_GPIO_STS_VALUE_BIT)

/* SR Power Control */
#define SRPWR_DMN0_PCIE			(0)				/* PCIE */
#define SRPWR_DMN0_PCIE_SHIFT		(SRPWR_DMN0_PCIE)		/* PCIE */
#define SRPWR_DMN0_PCIE_MASK		(1 << SRPWR_DMN0_PCIE_SHIFT)	/* PCIE */
#define SRPWR_DMN1_ARMBPSD		(1)				/* ARM/BP/SDIO */
#define SRPWR_DMN1_ARMBPSD_SHIFT	(SRPWR_DMN1_ARMBPSD)		/* ARM/BP/SDIO */
#define SRPWR_DMN1_ARMBPSD_MASK		(1 << SRPWR_DMN1_ARMBPSD_SHIFT)	/* ARM/BP/SDIO */
#define SRPWR_DMN2_MACAUX		(2)				/* MAC/Phy Aux */
#define SRPWR_DMN2_MACAUX_SHIFT		(SRPWR_DMN2_MACAUX)		/* MAC/Phy Aux */
#define SRPWR_DMN2_MACAUX_MASK		(1 << SRPWR_DMN2_MACAUX_SHIFT)	/* MAC/Phy Aux */
#define SRPWR_DMN3_MACMAIN		(3)				/* MAC/Phy Main */
#define SRPWR_DMN3_MACMAIN_SHIFT	(SRPWR_DMN3_MACMAIN)	/* MAC/Phy Main */
#define SRPWR_DMN3_MACMAIN_MASK		(1 << SRPWR_DMN3_MACMAIN_SHIFT)	/* MAC/Phy Main */

#define SRPWR_DMN4_MACSCAN		(4)				/* MAC/Phy Scan */
#define SRPWR_DMN4_MACSCAN_SHIFT	(SRPWR_DMN4_MACSCAN)		/* MAC/Phy Scan */
#define SRPWR_DMN4_MACSCAN_MASK		(1 << SRPWR_DMN4_MACSCAN_SHIFT)	/* MAC/Phy Scan */

/* all power domain mask */
#define SRPWR_DMN_ALL_MASK(sih)		si_srpwr_domain_all_mask(sih)

#define SRPWR_REQON_SHIFT		(8)	/* PowerOnRequest[11:8] */
#define SRPWR_REQON_MASK(sih)		(SRPWR_DMN_ALL_MASK(sih) << SRPWR_REQON_SHIFT)

#define SRPWR_STATUS_SHIFT		(16)	/* ExtPwrStatus[19:16], RO */
#define SRPWR_STATUS_MASK(sih)		(SRPWR_DMN_ALL_MASK(sih) << SRPWR_STATUS_SHIFT)

#define SRPWR_DMN_ID_SHIFT			(28)	/* PowerDomain[31:28], RO */
#define SRPWR_DMN_ID_MASK			(0xF)

/* PMU Precision Usec Timer */
#define PMU_PREC_USEC_TIMER_ENABLE	0x1

/* FISCtrlStatus */
#define PMU_CLEAR_FIS_DONE_SHIFT	1u
#define PMU_CLEAR_FIS_DONE_MASK	(1u << PMU_CLEAR_FIS_DONE_SHIFT)

#endif	/* _SBCHIPC_H */
