/*
 * Copyright (c) 2011 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_BRCM_AIUTILS_H_
#define	_BRCM_AIUTILS_H_

#include "types.h"

/*
 * SOC Interconnect Address Map.
 * All regions may not exist on all chips.
 */
/* each core gets 4Kbytes for registers */
#define SI_CORE_SIZE		0x1000
/*
 * Max cores (this is arbitrary, for software
 * convenience and could be changed if we
 * make any larger chips
 */
#define	SI_MAXCORES		16

/* Client Mode sb2pcitranslation2 size in bytes */
#define SI_PCI_DMA_SZ		0x40000000

/* PCIE Client Mode sb2pcitranslation2 (2 ZettaBytes), high 32 bits */
#define SI_PCIE_DMA_H32		0x80000000

/* chipcommon being the first core: */
#define	SI_CC_IDX		0

/* SOC Interconnect types (aka chip types) */
#define	SOCI_AI			1

/* A register that is common to all cores to
 * communicate w/PMU regarding clock control.
 */
#define SI_CLK_CTL_ST		0x1e0	/* clock control and status */

/* clk_ctl_st register */
#define	CCS_FORCEALP		0x00000001	/* force ALP request */
#define	CCS_FORCEHT		0x00000002	/* force HT request */
#define	CCS_FORCEILP		0x00000004	/* force ILP request */
#define	CCS_ALPAREQ		0x00000008	/* ALP Avail Request */
#define	CCS_HTAREQ		0x00000010	/* HT Avail Request */
#define	CCS_FORCEHWREQOFF	0x00000020	/* Force HW Clock Request Off */
#define CCS_ERSRC_REQ_MASK	0x00000700	/* external resource requests */
#define CCS_ERSRC_REQ_SHIFT	8
#define	CCS_ALPAVAIL		0x00010000	/* ALP is available */
#define	CCS_HTAVAIL		0x00020000	/* HT is available */
#define CCS_BP_ON_APL		0x00040000	/* RO: running on ALP clock */
#define CCS_BP_ON_HT		0x00080000	/* RO: running on HT clock */
#define CCS_ERSRC_STS_MASK	0x07000000	/* external resource status */
#define CCS_ERSRC_STS_SHIFT	24

/* HT avail in chipc and pcmcia on 4328a0 */
#define	CCS0_HTAVAIL		0x00010000
/* ALP avail in chipc and pcmcia on 4328a0 */
#define	CCS0_ALPAVAIL		0x00020000

/* Not really related to SOC Interconnect, but a couple of software
 * conventions for the use the flash space:
 */

/* Minumum amount of flash we support */
#define FLASH_MIN		0x00020000	/* Minimum flash size */

#define	CC_SROM_OTP		0x800	/* SROM/OTP address space */

/* gpiotimerval */
#define GPIO_ONTIME_SHIFT	16

/* Fields in clkdiv */
#define	CLKD_OTP		0x000f0000
#define	CLKD_OTP_SHIFT		16

/* Package IDs */
#define	BCM4717_PKG_ID		9	/* 4717 package id */
#define	BCM4718_PKG_ID		10	/* 4718 package id */
#define BCM43224_FAB_SMIC	0xa	/* the chip is manufactured by SMIC */

/* these are router chips */
#define	BCM4716_CHIP_ID		0x4716	/* 4716 chipcommon chipid */
#define	BCM47162_CHIP_ID	47162	/* 47162 chipcommon chipid */
#define	BCM4748_CHIP_ID		0x4748	/* 4716 chipcommon chipid (OTP, RBBU) */

/* dynamic clock control defines */
#define	LPOMINFREQ		25000	/* low power oscillator min */
#define	LPOMAXFREQ		43000	/* low power oscillator max */
#define	XTALMINFREQ		19800000	/* 20 MHz - 1% */
#define	XTALMAXFREQ		20200000	/* 20 MHz + 1% */
#define	PCIMINFREQ		25000000	/* 25 MHz */
#define	PCIMAXFREQ		34000000	/* 33 MHz + fudge */

#define	ILP_DIV_5MHZ		0	/* ILP = 5 MHz */
#define	ILP_DIV_1MHZ		4	/* ILP = 1 MHz */

/* clkctl xtal what flags */
#define	XTAL			0x1	/* primary crystal oscillator (2050) */
#define	PLL			0x2	/* main chip pll */

/* clkctl clk mode */
#define	CLK_FAST		0	/* force fast (pll) clock */
#define	CLK_DYNAMIC		2	/* enable dynamic clock control */

/* GPIO usage priorities */
#define GPIO_DRV_PRIORITY	0	/* Driver */
#define GPIO_APP_PRIORITY	1	/* Application */
#define GPIO_HI_PRIORITY	2	/* Highest priority. Ignore GPIO
					 * reservation
					 */

/* GPIO pull up/down */
#define GPIO_PULLUP		0
#define GPIO_PULLDN		1

/* GPIO event regtype */
#define GPIO_REGEVT		0	/* GPIO register event */
#define GPIO_REGEVT_INTMSK	1	/* GPIO register event int mask */
#define GPIO_REGEVT_INTPOL	2	/* GPIO register event int polarity */

/* device path */
#define SI_DEVPATH_BUFSZ	16	/* min buffer size in bytes */

/* SI routine enumeration: to be used by update function with multiple hooks */
#define	SI_DOATTACH	1
#define SI_PCIDOWN	2
#define SI_PCIUP	3

/*
 * Data structure to export all chip specific common variables
 *   public (read-only) portion of aiutils handle returned by si_attach()
 */
struct si_pub {
	uint buscoretype;	/* PCI_CORE_ID, PCIE_CORE_ID, PCMCIA_CORE_ID */
	uint buscorerev;	/* buscore rev */
	uint buscoreidx;	/* buscore index */
	int ccrev;		/* chip common core rev */
	u32 cccaps;		/* chip common capabilities */
	u32 cccaps_ext;	/* chip common capabilities extension */
	int pmurev;		/* pmu core rev */
	u32 pmucaps;		/* pmu capabilities */
	uint boardtype;		/* board type */
	uint boardvendor;	/* board vendor */
	uint boardflags;	/* board flags */
	uint boardflags2;	/* board flags2 */
	uint chip;		/* chip number */
	uint chiprev;		/* chip revision */
	uint chippkg;		/* chip package option */
	u32 chipst;		/* chip status */
	bool issim;		/* chip is in simulation or emulation */
	uint socirev;		/* SOC interconnect rev */
	bool pci_pr32414;

};

struct pci_dev;

struct gpioh_item {
	void *arg;
	bool level;
	void (*handler) (u32 stat, void *arg);
	u32 event;
	struct gpioh_item *next;
};

/* misc si info needed by some of the routines */
struct si_info {
	struct si_pub pub;	/* back plane public state (must be first) */
	struct pci_dev *pbus;	/* handle to pci bus */
	uint dev_coreid;	/* the core provides driver functions */
	void *intr_arg;		/* interrupt callback function arg */
	u32 (*intrsoff_fn) (void *intr_arg); /* turns chip interrupts off */
	/* restore chip interrupts */
	void (*intrsrestore_fn) (void *intr_arg, u32 arg);
	/* check if interrupts are enabled */
	bool (*intrsenabled_fn) (void *intr_arg);

	struct pcicore_info *pch; /* PCI/E core handle */

	struct list_head var_list; /* list of srom variables */

	void __iomem *curmap;			/* current regs va */
	void __iomem *regs[SI_MAXCORES];	/* other regs va */

	uint curidx;		/* current core index */
	uint numcores;		/* # discovered cores */
	uint coreid[SI_MAXCORES]; /* id of each core */
	u32 coresba[SI_MAXCORES]; /* backplane address of each core */
	void *regs2[SI_MAXCORES]; /* 2nd virtual address per core (usbh20) */
	u32 coresba2[SI_MAXCORES]; /* 2nd phys address per core (usbh20) */
	u32 coresba_size[SI_MAXCORES]; /* backplane address space size */
	u32 coresba2_size[SI_MAXCORES];	/* second address space size */

	void *curwrap;		/* current wrapper va */
	void *wrappers[SI_MAXCORES];	/* other cores wrapper va */
	u32 wrapba[SI_MAXCORES];	/* address of controlling wrapper */

	u32 cia[SI_MAXCORES];	/* erom cia entry for each core */
	u32 cib[SI_MAXCORES];	/* erom cia entry for each core */
	u32 oob_router;	/* oob router registers for axi */
};

/*
 * Many of the routines below take an 'sih' handle as their first arg.
 * Allocate this by calling si_attach().  Free it by calling si_detach().
 * At any one time, the sih is logically focused on one particular si core
 * (the "current core").
 * Use si_setcore() or si_setcoreidx() to change the association to another core
 */


/* AMBA Interconnect exported externs */
extern uint ai_flag(struct si_pub *sih);
extern void ai_setint(struct si_pub *sih, int siflag);
extern uint ai_coreidx(struct si_pub *sih);
extern uint ai_corevendor(struct si_pub *sih);
extern uint ai_corerev(struct si_pub *sih);
extern bool ai_iscoreup(struct si_pub *sih);
extern u32 ai_core_cflags(struct si_pub *sih, u32 mask, u32 val);
extern void ai_core_cflags_wo(struct si_pub *sih, u32 mask, u32 val);
extern u32 ai_core_sflags(struct si_pub *sih, u32 mask, u32 val);
extern uint ai_corereg(struct si_pub *sih, uint coreidx, uint regoff, uint mask,
		       uint val);
extern void ai_core_reset(struct si_pub *sih, u32 bits, u32 resetbits);
extern void ai_core_disable(struct si_pub *sih, u32 bits);
extern int ai_numaddrspaces(struct si_pub *sih);
extern u32 ai_addrspace(struct si_pub *sih, uint asidx);
extern u32 ai_addrspacesize(struct si_pub *sih, uint asidx);
extern void ai_write_wrap_reg(struct si_pub *sih, u32 offset, u32 val);

/* === exported functions === */
extern struct si_pub *ai_attach(void __iomem *regs, struct pci_dev *sdh);
extern void ai_detach(struct si_pub *sih);
extern uint ai_coreid(struct si_pub *sih);
extern uint ai_corerev(struct si_pub *sih);
extern uint ai_corereg(struct si_pub *sih, uint coreidx, uint regoff, uint mask,
		uint val);
extern void ai_write_wrapperreg(struct si_pub *sih, u32 offset, u32 val);
extern u32 ai_core_cflags(struct si_pub *sih, u32 mask, u32 val);
extern u32 ai_core_sflags(struct si_pub *sih, u32 mask, u32 val);
extern bool ai_iscoreup(struct si_pub *sih);
extern uint ai_findcoreidx(struct si_pub *sih, uint coreid, uint coreunit);
extern void __iomem *ai_setcoreidx(struct si_pub *sih, uint coreidx);
extern void __iomem *ai_setcore(struct si_pub *sih, uint coreid, uint coreunit);
extern void __iomem *ai_switch_core(struct si_pub *sih, uint coreid,
				    uint *origidx, uint *intr_val);
extern void ai_restore_core(struct si_pub *sih, uint coreid, uint intr_val);
extern void ai_core_reset(struct si_pub *sih, u32 bits, u32 resetbits);
extern void ai_core_disable(struct si_pub *sih, u32 bits);
extern u32 ai_alp_clock(struct si_pub *sih);
extern u32 ai_ilp_clock(struct si_pub *sih);
extern void ai_pci_setup(struct si_pub *sih, uint coremask);
extern void ai_setint(struct si_pub *sih, int siflag);
extern bool ai_backplane64(struct si_pub *sih);
extern void ai_register_intr_callback(struct si_pub *sih, void *intrsoff_fn,
				      void *intrsrestore_fn,
				      void *intrsenabled_fn, void *intr_arg);
extern void ai_deregister_intr_callback(struct si_pub *sih);
extern void ai_clkctl_init(struct si_pub *sih);
extern u16 ai_clkctl_fast_pwrup_delay(struct si_pub *sih);
extern bool ai_clkctl_cc(struct si_pub *sih, uint mode);
extern int ai_clkctl_xtal(struct si_pub *sih, uint what, bool on);
extern bool ai_deviceremoved(struct si_pub *sih);
extern u32 ai_gpiocontrol(struct si_pub *sih, u32 mask, u32 val,
			     u8 priority);

/* OTP status */
extern bool ai_is_otp_disabled(struct si_pub *sih);

/* SPROM availability */
extern bool ai_is_sprom_available(struct si_pub *sih);

/*
 * Build device path. Path size must be >= SI_DEVPATH_BUFSZ.
 * The returned path is NULL terminated and has trailing '/'.
 * Return 0 on success, nonzero otherwise.
 */
extern int ai_devpath(struct si_pub *sih, char *path, int size);

extern void ai_pci_sleep(struct si_pub *sih);
extern void ai_pci_down(struct si_pub *sih);
extern void ai_pci_up(struct si_pub *sih);
extern int ai_pci_fixcfg(struct si_pub *sih);

extern void ai_chipcontrl_epa4331(struct si_pub *sih, bool on);
/* Enable Ex-PA for 4313 */
extern void ai_epa_4313war(struct si_pub *sih);

#endif				/* _BRCM_AIUTILS_H_ */
