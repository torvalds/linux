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

#ifndef	_aiutils_h_
#define	_aiutils_h_

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif

/* Include the soci specific files */
#include <aidmp.h>

/*
 * SOC Interconnect Address Map.
 * All regions may not exist on all chips.
 */
/* Physical SDRAM */
#define SI_SDRAM_BASE		0x00000000
/* Host Mode sb2pcitranslation0 (64 MB) */
#define SI_PCI_MEM		0x08000000
#define SI_PCI_MEM_SZ		(64 * 1024 * 1024)
/* Host Mode sb2pcitranslation1 (64 MB) */
#define SI_PCI_CFG		0x0c000000
/* Byteswapped Physical SDRAM */
#define	SI_SDRAM_SWAPPED	0x10000000
/* Region 2 for sdram (512 MB) */
#define SI_SDRAM_R2		0x80000000

#ifdef SI_ENUM_BASE_VARIABLE
#define SI_ENUM_BASE		(sii->pub.si_enum_base)
#else
#define SI_ENUM_BASE		0x18000000	/* Enumeration space base */
#endif				/* SI_ENUM_BASE_VARIABLE */

/* Wrapper space base */
#define SI_WRAP_BASE		0x18100000
/* each core gets 4Kbytes for registers */
#define SI_CORE_SIZE		0x1000
/*
 * Max cores (this is arbitrary, for software
 * convenience and could be changed if we
 * make any larger chips
 */
#define	SI_MAXCORES		16

/* On-chip RAM on chips that also have DDR */
#define	SI_FASTRAM		0x19000000
#define	SI_FASTRAM_SWAPPED	0x19800000

/* Flash Region 2 (region 1 shadowed here) */
#define	SI_FLASH2		0x1c000000
/* Size of Flash Region 2 */
#define	SI_FLASH2_SZ		0x02000000
/* ARM Cortex-M3 ROM */
#define	SI_ARMCM3_ROM		0x1e000000
/* MIPS Flash Region 1 */
#define	SI_FLASH1		0x1fc00000
/* MIPS Size of Flash Region 1 */
#define	SI_FLASH1_SZ		0x00400000
/* ARM7TDMI-S ROM */
#define	SI_ARM7S_ROM		0x20000000
/* ARM Cortex-M3 SRAM Region 2 */
#define	SI_ARMCM3_SRAM2		0x60000000
/* ARM7TDMI-S SRAM Region 2 */
#define	SI_ARM7S_SRAM2		0x80000000
/* ARM Flash Region 1 */
#define	SI_ARM_FLASH1		0xffff0000
/* ARM Size of Flash Region 1 */
#define	SI_ARM_FLASH1_SZ	0x00010000

/* Client Mode sb2pcitranslation2 (1 GB) */
#define SI_PCI_DMA		0x40000000
/* Client Mode sb2pcitranslation2 (1 GB) */
#define SI_PCI_DMA2		0x80000000
/* Client Mode sb2pcitranslation2 size in bytes */
#define SI_PCI_DMA_SZ		0x40000000
/* PCIE Client Mode sb2pcitranslation2 (2 ZettaBytes), low 32 bits */
#define SI_PCIE_DMA_L32		0x00000000
/* PCIE Client Mode sb2pcitranslation2 (2 ZettaBytes), high 32 bits */
#define SI_PCIE_DMA_H32		0x80000000

/* core codes */
#define	NODEV_CORE_ID		0x700	/* Invalid coreid */
#define	CC_CORE_ID		0x800	/* chipcommon core */
#define	ILINE20_CORE_ID		0x801	/* iline20 core */
#define	SRAM_CORE_ID		0x802	/* sram core */
#define	SDRAM_CORE_ID		0x803	/* sdram core */
#define	PCI_CORE_ID		0x804	/* pci core */
#define	MIPS_CORE_ID		0x805	/* mips core */
#define	ENET_CORE_ID		0x806	/* enet mac core */
#define	CODEC_CORE_ID		0x807	/* v90 codec core */
#define	USB_CORE_ID		0x808	/* usb 1.1 host/device core */
#define	ADSL_CORE_ID		0x809	/* ADSL core */
#define	ILINE100_CORE_ID	0x80a	/* iline100 core */
#define	IPSEC_CORE_ID		0x80b	/* ipsec core */
#define	UTOPIA_CORE_ID		0x80c	/* utopia core */
#define	PCMCIA_CORE_ID		0x80d	/* pcmcia core */
#define	SOCRAM_CORE_ID		0x80e	/* internal memory core */
#define	MEMC_CORE_ID		0x80f	/* memc sdram core */
#define	OFDM_CORE_ID		0x810	/* OFDM phy core */
#define	EXTIF_CORE_ID		0x811	/* external interface core */
#define	D11_CORE_ID		0x812	/* 802.11 MAC core */
#define	APHY_CORE_ID		0x813	/* 802.11a phy core */
#define	BPHY_CORE_ID		0x814	/* 802.11b phy core */
#define	GPHY_CORE_ID		0x815	/* 802.11g phy core */
#define	MIPS33_CORE_ID		0x816	/* mips3302 core */
#define	USB11H_CORE_ID		0x817	/* usb 1.1 host core */
#define	USB11D_CORE_ID		0x818	/* usb 1.1 device core */
#define	USB20H_CORE_ID		0x819	/* usb 2.0 host core */
#define	USB20D_CORE_ID		0x81a	/* usb 2.0 device core */
#define	SDIOH_CORE_ID		0x81b	/* sdio host core */
#define	ROBO_CORE_ID		0x81c	/* roboswitch core */
#define	ATA100_CORE_ID		0x81d	/* parallel ATA core */
#define	SATAXOR_CORE_ID		0x81e	/* serial ATA & XOR DMA core */
#define	GIGETH_CORE_ID		0x81f	/* gigabit ethernet core */
#define	PCIE_CORE_ID		0x820	/* pci express core */
#define	NPHY_CORE_ID		0x821	/* 802.11n 2x2 phy core */
#define	SRAMC_CORE_ID		0x822	/* SRAM controller core */
#define	MINIMAC_CORE_ID		0x823	/* MINI MAC/phy core */
#define	ARM11_CORE_ID		0x824	/* ARM 1176 core */
#define	ARM7S_CORE_ID		0x825	/* ARM7tdmi-s core */
#define	LPPHY_CORE_ID		0x826	/* 802.11a/b/g phy core */
#define	PMU_CORE_ID		0x827	/* PMU core */
#define	SSNPHY_CORE_ID		0x828	/* 802.11n single-stream phy core */
#define	SDIOD_CORE_ID		0x829	/* SDIO device core */
#define	ARMCM3_CORE_ID		0x82a	/* ARM Cortex M3 core */
#define	HTPHY_CORE_ID		0x82b	/* 802.11n 4x4 phy core */
#define	MIPS74K_CORE_ID		0x82c	/* mips 74k core */
#define	GMAC_CORE_ID		0x82d	/* Gigabit MAC core */
#define	DMEMC_CORE_ID		0x82e	/* DDR1/2 memory controller core */
#define	PCIERC_CORE_ID		0x82f	/* PCIE Root Complex core */
#define	OCP_CORE_ID		0x830	/* OCP2OCP bridge core */
#define	SC_CORE_ID		0x831	/* shared common core */
#define	AHB_CORE_ID		0x832	/* OCP2AHB bridge core */
#define	SPIH_CORE_ID		0x833	/* SPI host core */
#define	I2S_CORE_ID		0x834	/* I2S core */
#define	DMEMS_CORE_ID		0x835	/* SDR/DDR1 memory controller core */
#define	DEF_SHIM_COMP		0x837	/* SHIM component in ubus/6362 */
#define OOB_ROUTER_CORE_ID	0x367	/* OOB router core ID */
#define	DEF_AI_COMP		0xfff	/* Default component, in ai chips it
					 * maps all unused address ranges
					 */

/* There are TWO constants on all HND chips: SI_ENUM_BASE above,
 * and chipcommon being the first core:
 */
#define	SI_CC_IDX		0

/* SOC Interconnect types (aka chip types) */
#define	SOCI_AI			1

/* Common core control flags */
#define	SICF_BIST_EN		0x8000
#define	SICF_PME_EN		0x4000
#define	SICF_CORE_BITS		0x3ffc
#define	SICF_FGC		0x0002
#define	SICF_CLOCK_EN		0x0001

/* Common core status flags */
#define	SISF_BIST_DONE		0x8000
#define	SISF_BIST_ERROR		0x4000
#define	SISF_GATED_CLK		0x2000
#define	SISF_DMA64		0x1000
#define	SISF_CORE_BITS		0x0fff

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

/* A boot/binary may have an embedded block that describes its size  */
#define	BISZ_OFFSET		0x3e0	/* At this offset into the binary */
#define	BISZ_MAGIC		0x4249535a	/* Marked with value: 'BISZ' */
#define	BISZ_MAGIC_IDX		0	/* Word 0: magic */
#define	BISZ_TXTST_IDX		1	/*      1: text start */
#define	BISZ_TXTEND_IDX		2	/*      2: text end */
#define	BISZ_DATAST_IDX		3	/*      3: data start */
#define	BISZ_DATAEND_IDX	4	/*      4: data end */
#define	BISZ_BSSST_IDX		5	/*      5: bss start */
#define	BISZ_BSSEND_IDX		6	/*      6: bss end */
#define BISZ_SIZE		7	/* descriptor size in 32-bit integers */

#define	SI_INFO(sih)	(si_info_t *)sih

#define	GOODCOREADDR(x, b) \
	(((x) >= (b)) && ((x) < ((b) + SI_MAXCORES * SI_CORE_SIZE)) && \
		IS_ALIGNED((x), SI_CORE_SIZE))
#define	GOODREGS(regs) \
	((regs) != NULL && IS_ALIGNED((unsigned long)(regs), SI_CORE_SIZE))
#define BADCOREADDR	0
#define	GOODIDX(idx)	(((uint)idx) < SI_MAXCORES)
#define	NOREV		-1	/* Invalid rev */

/* Newer chips can access PCI/PCIE and CC core without requiring to change
 * PCI BAR0 WIN
 */
#define SI_FAST(si) (((si)->pub.buscoretype == PCIE_CORE_ID) ||	\
		     (((si)->pub.buscoretype == PCI_CORE_ID) && \
		      (si)->pub.buscorerev >= 13))

#define PCIEREGS(si) (((char *)((si)->curmap) + PCI_16KB0_PCIREGS_OFFSET))
#define CCREGS_FAST(si) (((char *)((si)->curmap) + PCI_16KB0_CCREGS_OFFSET))

/*
 * Macros to disable/restore function core(D11, ENET, ILINE20, etc) interrupts
 * before after core switching to avoid invalid register accesss inside ISR.
 */
#define INTR_OFF(si, intr_val) \
	if ((si)->intrsoff_fn && \
	    (si)->coreid[(si)->curidx] == (si)->dev_coreid) \
		intr_val = (*(si)->intrsoff_fn)((si)->intr_arg)
#define INTR_RESTORE(si, intr_val) \
	if ((si)->intrsrestore_fn && \
	    (si)->coreid[(si)->curidx] == (si)->dev_coreid) \
		(*(si)->intrsrestore_fn)((si)->intr_arg, intr_val)

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
#define DEFAULT_GPIOTIMERVAL \
	((DEFAULT_GPIO_ONTIME << GPIO_ONTIME_SHIFT) | DEFAULT_GPIO_OFFTIME)
#endif

/*
 * Data structure to export all chip specific common variables
 *   public (read-only) portion of aiutils handle returned by si_attach()
 */
struct si_pub {
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

/*
 * for HIGH_ONLY driver, the si_t must be writable to allow states sync from
 * BMAC to HIGH driver for monolithic driver, it is readonly to prevent accident
 * change
 */
typedef const struct si_pub si_t;

/*
 * Many of the routines below take an 'sih' handle as their first arg.
 * Allocate this by calling si_attach().  Free it by calling si_detach().
 * At any one time, the sih is logically focused on one particular si core
 * (the "current core").
 * Use si_setcore() or si_setcoreidx() to change the association to another core
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

#define	SI_ERROR(args)

#ifdef BCMDBG
#define	SI_MSG(args)	printk args
#else
#define	SI_MSG(args)
#endif				/* BCMDBG */

/* Define SI_VMSG to printf for verbose debugging, but don't check it in */
#define	SI_VMSG(args)

#define	IS_SIM(chippkg)	\
	((chippkg == HDLSIM_PKG_ID) || (chippkg == HWSIM_PKG_ID))

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
	struct si_pub pub;	/* back plane public state (must be first) */
	void *pbus;		/* handle to bus (pci/sdio/..) */
	uint dev_coreid;	/* the core provides driver functions */
	void *intr_arg;		/* interrupt callback function arg */
	si_intrsoff_t intrsoff_fn;	/* turns chip interrupts off */
	si_intrsrestore_t intrsrestore_fn; /* restore chip interrupts */
	si_intrsenabled_t intrsenabled_fn; /* check if interrupts are enabled */

	void *pch;		/* PCI/E core handle */

	gpioh_item_t *gpioh_head;	/* GPIO event handlers list */

	bool memseg;		/* flag to toggle MEM_SEG register */

	char *vars;
	uint varsz;

	void *curmap;		/* current regs va */
	void *regs[SI_MAXCORES];	/* other regs va */

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
} si_info_t;

/* AMBA Interconnect exported externs */
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

/* === exported functions === */
extern si_t *ai_attach(uint pcidev, void *regs, uint bustype,
		       void *sdh, char **vars, uint *varsz);

extern void ai_detach(si_t *sih);
extern bool ai_pci_war16165(si_t *sih);

extern uint ai_coreid(si_t *sih);
extern uint ai_corerev(si_t *sih);
extern uint ai_corereg(si_t *sih, uint coreidx, uint regoff, uint mask,
		uint val);
extern void ai_write_wrapperreg(si_t *sih, u32 offset, u32 val);
extern u32 ai_core_cflags(si_t *sih, u32 mask, u32 val);
extern u32 ai_core_sflags(si_t *sih, u32 mask, u32 val);
extern bool ai_iscoreup(si_t *sih);
extern uint ai_findcoreidx(si_t *sih, uint coreid, uint coreunit);
extern void *ai_setcoreidx(si_t *sih, uint coreidx);
extern void *ai_setcore(si_t *sih, uint coreid, uint coreunit);
extern void *ai_switch_core(si_t *sih, uint coreid, uint *origidx,
			    uint *intr_val);
extern void ai_restore_core(si_t *sih, uint coreid, uint intr_val);
extern void ai_core_reset(si_t *sih, u32 bits, u32 resetbits);
extern void ai_core_disable(si_t *sih, u32 bits);
extern u32 ai_alp_clock(si_t *sih);
extern u32 ai_ilp_clock(si_t *sih);
extern void ai_pci_setup(si_t *sih, uint coremask);
extern void ai_setint(si_t *sih, int siflag);
extern bool ai_backplane64(si_t *sih);
extern void ai_register_intr_callback(si_t *sih, void *intrsoff_fn,
				      void *intrsrestore_fn,
				      void *intrsenabled_fn, void *intr_arg);
extern void ai_deregister_intr_callback(si_t *sih);
extern void ai_clkctl_init(si_t *sih);
extern u16 ai_clkctl_fast_pwrup_delay(si_t *sih);
extern bool ai_clkctl_cc(si_t *sih, uint mode);
extern int ai_clkctl_xtal(si_t *sih, uint what, bool on);
extern bool ai_deviceremoved(si_t *sih);
extern u32 ai_gpiocontrol(si_t *sih, u32 mask, u32 val,
			     u8 priority);

/* OTP status */
extern bool ai_is_otp_disabled(si_t *sih);
extern bool ai_is_otp_powered(si_t *sih);
extern void ai_otp_power(si_t *sih, bool on);

/* SPROM availability */
extern bool ai_is_sprom_available(si_t *sih);

/*
 * Build device path. Path size must be >= SI_DEVPATH_BUFSZ.
 * The returned path is NULL terminated and has trailing '/'.
 * Return 0 on success, nonzero otherwise.
 */
extern int ai_devpath(si_t *sih, char *path, int size);
/* Read variable with prepending the devpath to the name */
extern char *ai_getdevpathvar(si_t *sih, const char *name);
extern int ai_getdevpathintvar(si_t *sih, const char *name);

extern void ai_pci_sleep(si_t *sih);
extern void ai_pci_down(si_t *sih);
extern void ai_pci_up(si_t *sih);
extern int ai_pci_fixcfg(si_t *sih);

extern void ai_chipcontrl_epa4331(si_t *sih, bool on);
/* Enable Ex-PA for 4313 */
extern void ai_epa_4313war(si_t *sih);

char *ai_getnvramflvar(si_t *sih, const char *name);

#endif				/* _aiutils_h_ */
