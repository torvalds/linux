/*
 * Copyright (c) 2010 Broadcom Corporation
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

#ifndef	_siutils_h_
#define	_siutils_h_

#include <hndsoc.h>

/*
 * Data structure to export all chip specific common variables
 *   public (read-only) portion of siutils handle returned by si_attach()
 */
struct si_pub {
	uint socitype;		/* SOCI_SB, SOCI_AI */

	uint bustype;		/* SI_BUS, PCI_BUS */
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

/* for HIGH_ONLY driver, the si_t must be writable to allow states sync from BMAC to HIGH driver
 * for monolithic driver, it is readonly to prevent accident change
 */
typedef const struct si_pub si_t;

/*
 * Many of the routines below take an 'sih' handle as their first arg.
 * Allocate this by calling si_attach().  Free it by calling si_detach().
 * At any one time, the sih is logically focused on one particular si core
 * (the "current core").
 * Use si_setcore() or si_setcoreidx() to change the association to another core.
 */

#define	BADIDX		(SI_MAXCORES + 1)

/* clkctl xtal what flags */
#define	XTAL			0x1	/* primary crystal oscillator (2050) */
#define	PLL			0x2	/* main chip pll */

/* clkctl clk mode */
#define	CLK_FAST		0	/* force fast (pll) clock */
#define	CLK_DYNAMIC		2	/* enable dynamic clock control */

/* GPIO usage priorities */
#define GPIO_DRV_PRIORITY	0	/* Driver */
#define GPIO_APP_PRIORITY	1	/* Application */
#define GPIO_HI_PRIORITY	2	/* Highest priority. Ignore GPIO reservation */

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

#define	ISSIM_ENAB(sih)	0

/* PMU clock/power control */
#if defined(BCMPMUCTL)
#define PMUCTL_ENAB(sih)	(BCMPMUCTL)
#else
#define PMUCTL_ENAB(sih)	((sih)->cccaps & CC_CAP_PMU)
#endif

/* chipcommon clock/power control (exclusive with PMU's) */
#if defined(BCMPMUCTL) && BCMPMUCTL
#define CCCTL_ENAB(sih)		(0)
#define CCPLL_ENAB(sih)		(0)
#else
#define CCCTL_ENAB(sih)		((sih)->cccaps & CC_CAP_PWR_CTL)
#define CCPLL_ENAB(sih)		((sih)->cccaps & CC_CAP_PLL_MASK)
#endif

typedef void (*gpio_handler_t) (u32 stat, void *arg);

/* External PA enable mask */
#define GPIO_CTRL_EPA_EN_MASK 0x40

/* === exported functions === */
extern si_t *si_attach(uint pcidev, struct osl_info *osh, void *regs,
		       uint bustype, void *sdh, char **vars, uint *varsz);

extern void si_detach(si_t *sih);
extern bool si_pci_war16165(si_t *sih);

extern uint si_coreid(si_t *sih);
extern uint si_flag(si_t *sih);
extern uint si_coreidx(si_t *sih);
extern uint si_corerev(si_t *sih);
struct osl_info *si_osh(si_t *sih);
extern uint si_corereg(si_t *sih, uint coreidx, uint regoff, uint mask,
		uint val);
extern void si_write_wrapperreg(si_t *sih, u32 offset, u32 val);
extern u32 si_core_cflags(si_t *sih, u32 mask, u32 val);
extern u32 si_core_sflags(si_t *sih, u32 mask, u32 val);
extern bool si_iscoreup(si_t *sih);
extern uint si_findcoreidx(si_t *sih, uint coreid, uint coreunit);
#ifndef BCMSDIO
extern void *si_setcoreidx(si_t *sih, uint coreidx);
#endif
extern void *si_setcore(si_t *sih, uint coreid, uint coreunit);
extern void *si_switch_core(si_t *sih, uint coreid, uint *origidx,
			    uint *intr_val);
extern void si_restore_core(si_t *sih, uint coreid, uint intr_val);
extern void si_core_reset(si_t *sih, u32 bits, u32 resetbits);
extern void si_core_disable(si_t *sih, u32 bits);
extern u32 si_alp_clock(si_t *sih);
extern u32 si_ilp_clock(si_t *sih);
extern void si_pci_setup(si_t *sih, uint coremask);
extern void si_setint(si_t *sih, int siflag);
extern bool si_backplane64(si_t *sih);
extern void si_register_intr_callback(si_t *sih, void *intrsoff_fn,
				      void *intrsrestore_fn,
				      void *intrsenabled_fn, void *intr_arg);
extern void si_deregister_intr_callback(si_t *sih);
extern void si_clkctl_init(si_t *sih);
extern u16 si_clkctl_fast_pwrup_delay(si_t *sih);
extern bool si_clkctl_cc(si_t *sih, uint mode);
extern int si_clkctl_xtal(si_t *sih, uint what, bool on);
extern bool si_deviceremoved(si_t *sih);
extern u32 si_socram_size(si_t *sih);

extern void si_watchdog(si_t *sih, uint ticks);
extern u32 si_gpiocontrol(si_t *sih, u32 mask, u32 val,
			     u8 priority);

#ifdef BCMSDIO
extern void si_sdio_init(si_t *sih);
#endif

#define si_eci(sih) 0
#define si_eci_init(sih) (0)
#define si_eci_notify_bt(sih, type, val)  (0)
#define si_seci(sih) 0
static inline void *si_seci_init(si_t *sih, u8 use_seci)
{
	return NULL;
}

/* OTP status */
extern bool si_is_otp_disabled(si_t *sih);
extern bool si_is_otp_powered(si_t *sih);
extern void si_otp_power(si_t *sih, bool on);

/* SPROM availability */
extern bool si_is_sprom_available(si_t *sih);
#ifdef SI_SPROM_PROBE
extern void si_sprom_init(si_t *sih);
#endif				/* SI_SPROM_PROBE */

#define	SI_ERROR(args)

#ifdef BCMDBG
#define	SI_MSG(args)	printf args
#else
#define	SI_MSG(args)
#endif				/* BCMDBG */

/* Define SI_VMSG to printf for verbose debugging, but don't check it in */
#define	SI_VMSG(args)

#define	IS_SIM(chippkg)	((chippkg == HDLSIM_PKG_ID) || (chippkg == HWSIM_PKG_ID))

typedef u32(*si_intrsoff_t) (void *intr_arg);
typedef void (*si_intrsrestore_t) (void *intr_arg, u32 arg);
typedef bool(*si_intrsenabled_t) (void *intr_arg);

typedef struct gpioh_item {
	void *arg;
	bool level;
	gpio_handler_t handler;
	u32 event;
	struct gpioh_item *next;
} gpioh_item_t;

/* misc si info needed by some of the routines */
typedef struct si_info {
	struct si_pub pub;	/* back plane public state (must be first field) */
	struct osl_info *osh;		/* osl os handle */
	void *sdh;		/* bcmsdh handle */
	uint dev_coreid;	/* the core provides driver functions */
	void *intr_arg;		/* interrupt callback function arg */
	si_intrsoff_t intrsoff_fn;	/* turns chip interrupts off */
	si_intrsrestore_t intrsrestore_fn;	/* restore chip interrupts */
	si_intrsenabled_t intrsenabled_fn;	/* check if interrupts are enabled */

	void *pch;		/* PCI/E core handle */

	gpioh_item_t *gpioh_head;	/* GPIO event handlers list */

	bool memseg;		/* flag to toggle MEM_SEG register */

	char *vars;
	uint varsz;

	void *curmap;		/* current regs va */
	void *regs[SI_MAXCORES];	/* other regs va */

	uint curidx;		/* current core index */
	uint numcores;		/* # discovered cores */
	uint coreid[SI_MAXCORES];	/* id of each core */
	u32 coresba[SI_MAXCORES];	/* backplane address of each core */
	void *regs2[SI_MAXCORES];	/* va of each core second register set (usbh20) */
	u32 coresba2[SI_MAXCORES];	/* address of each core second register set (usbh20) */
	u32 coresba_size[SI_MAXCORES];	/* backplane address space size */
	u32 coresba2_size[SI_MAXCORES];	/* second address space size */

	void *curwrap;		/* current wrapper va */
	void *wrappers[SI_MAXCORES];	/* other cores wrapper va */
	u32 wrapba[SI_MAXCORES];	/* address of controlling wrapper */

	u32 cia[SI_MAXCORES];	/* erom cia entry for each core */
	u32 cib[SI_MAXCORES];	/* erom cia entry for each core */
	u32 oob_router;	/* oob router registers for axi */
} si_info_t;

#define	SI_INFO(sih)	(si_info_t *)sih

#define	GOODCOREADDR(x, b) (((x) >= (b)) && ((x) < ((b) + SI_MAXCORES * SI_CORE_SIZE)) && \
		IS_ALIGNED((x), SI_CORE_SIZE))
#define	GOODREGS(regs)	((regs) != NULL && IS_ALIGNED((unsigned long)(regs), SI_CORE_SIZE))
#define BADCOREADDR	0
#define	GOODIDX(idx)	(((uint)idx) < SI_MAXCORES)
#define	NOREV		-1	/* Invalid rev */

/* Newer chips can access PCI/PCIE and CC core without requiring to change
 * PCI BAR0 WIN
 */
#define SI_FAST(si) (((si)->pub.buscoretype == PCIE_CORE_ID) ||	\
		     (((si)->pub.buscoretype == PCI_CORE_ID) && (si)->pub.buscorerev >= 13))

#define PCIEREGS(si) (((char *)((si)->curmap) + PCI_16KB0_PCIREGS_OFFSET))
#define CCREGS_FAST(si) (((char *)((si)->curmap) + PCI_16KB0_CCREGS_OFFSET))

/*
 * Macros to disable/restore function core(D11, ENET, ILINE20, etc) interrupts
 * before after core switching to avoid invalid register accesss inside ISR.
 */
#define INTR_OFF(si, intr_val) \
	if ((si)->intrsoff_fn && (si)->coreid[(si)->curidx] == (si)->dev_coreid) {	\
		intr_val = (*(si)->intrsoff_fn)((si)->intr_arg); }
#define INTR_RESTORE(si, intr_val) \
	if ((si)->intrsrestore_fn && (si)->coreid[(si)->curidx] == (si)->dev_coreid) {	\
		(*(si)->intrsrestore_fn)((si)->intr_arg, intr_val); }

/* dynamic clock control defines */
#define	LPOMINFREQ		25000	/* low power oscillator min */
#define	LPOMAXFREQ		43000	/* low power oscillator max */
#define	XTALMINFREQ		19800000	/* 20 MHz - 1% */
#define	XTALMAXFREQ		20200000	/* 20 MHz + 1% */
#define	PCIMINFREQ		25000000	/* 25 MHz */
#define	PCIMAXFREQ		34000000	/* 33 MHz + fudge */

#define	ILP_DIV_5MHZ		0	/* ILP = 5 MHz */
#define	ILP_DIV_1MHZ		4	/* ILP = 1 MHz */

#define PCI(si)		(((si)->pub.bustype == PCI_BUS) &&	\
			 ((si)->pub.buscoretype == PCI_CORE_ID))
#define PCIE(si)	(((si)->pub.bustype == PCI_BUS) &&	\
			 ((si)->pub.buscoretype == PCIE_CORE_ID))
#define PCI_FORCEHT(si)	\
	(PCIE(si) && (si->pub.chip == BCM4716_CHIP_ID))

/* GPIO Based LED powersave defines */
#define DEFAULT_GPIO_ONTIME	10	/* Default: 10% on */
#define DEFAULT_GPIO_OFFTIME	90	/* Default: 10% on */

#ifndef DEFAULT_GPIOTIMERVAL
#define DEFAULT_GPIOTIMERVAL  ((DEFAULT_GPIO_ONTIME << GPIO_ONTIME_SHIFT) | DEFAULT_GPIO_OFFTIME)
#endif

/*
 * Build device path. Path size must be >= SI_DEVPATH_BUFSZ.
 * The returned path is NULL terminated and has trailing '/'.
 * Return 0 on success, nonzero otherwise.
 */
extern int si_devpath(si_t *sih, char *path, int size);
/* Read variable with prepending the devpath to the name */
extern char *si_getdevpathvar(si_t *sih, const char *name);
extern int si_getdevpathintvar(si_t *sih, const char *name);

extern void si_war42780_clkreq(si_t *sih, bool clkreq);
extern void si_pci_sleep(si_t *sih);
extern void si_pci_down(si_t *sih);
extern void si_pci_up(si_t *sih);
extern void si_pcie_extendL1timer(si_t *sih, bool extend);
extern int si_pci_fixcfg(si_t *sih);

extern void si_chipcontrl_epa4331(si_t *sih, bool on);
/* Enable Ex-PA for 4313 */
extern void si_epa_4313war(si_t *sih);

char *si_getnvramflvar(si_t *sih, const char *name);

/* AMBA Interconnect exported externs */
extern si_t *ai_attach(uint pcidev, struct osl_info *osh, void *regs,
		       uint bustype, void *sdh, char **vars, uint *varsz);
extern si_t *ai_kattach(struct osl_info *osh);
extern void ai_scan(si_t *sih, void *regs, uint devid);

extern uint ai_flag(si_t *sih);
extern void ai_setint(si_t *sih, int siflag);
extern uint ai_coreidx(si_t *sih);
extern uint ai_corevendor(si_t *sih);
extern uint ai_corerev(si_t *sih);
extern bool ai_iscoreup(si_t *sih);
extern void *ai_setcoreidx(si_t *sih, uint coreidx);
extern u32 ai_core_cflags(si_t *sih, u32 mask, u32 val);
extern void ai_core_cflags_wo(si_t *sih, u32 mask, u32 val);
extern u32 ai_core_sflags(si_t *sih, u32 mask, u32 val);
extern uint ai_corereg(si_t *sih, uint coreidx, uint regoff, uint mask,
		       uint val);
extern void ai_core_reset(si_t *sih, u32 bits, u32 resetbits);
extern void ai_core_disable(si_t *sih, u32 bits);
extern int ai_numaddrspaces(si_t *sih);
extern u32 ai_addrspace(si_t *sih, uint asidx);
extern u32 ai_addrspacesize(si_t *sih, uint asidx);
extern void ai_write_wrap_reg(si_t *sih, u32 offset, u32 val);

#ifdef BCMSDIO
#define si_setcoreidx(sih, idx) sb_setcoreidx(sih, idx)
#define si_coreid(sih) sb_coreid(sih)
#define si_corerev(sih) sb_corerev(sih)
#endif

#endif				/* _siutils_h_ */
