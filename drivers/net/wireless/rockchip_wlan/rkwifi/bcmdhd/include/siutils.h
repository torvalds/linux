/*
 * Misc utility routines for accessing the SOC Interconnects
 * of Broadcom HNBU chips.
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef	_siutils_h_
#define	_siutils_h_

#include <osl_decl.h>

/* Make the d11 core(s) selectable by the user config... */
#ifndef D11_CORE_UNIT_MASK
/* By default we allow all d11 cores to be used */
#define D11_CORE_UNIT_MASK 0xFFFFFFFFu
#endif

/* Generic interrupt bit mask definitions */
enum bcm_int_reg_idx {
	BCM_INT_REG_IDX_0 = 0,
	BCM_INT_REG_IDX_1 = 1,
	/* temp work around to avoid > 50K invalidation on 4388a0-roml */
#ifndef ROM_COMPAT_INT_REG_IDX
	BCM_INT_REG_IDX_2 = 2,
#endif /* ROM_COMPAT_INT_REG_IDX */
	BCM_INT_REGS_NUM
};

typedef struct bcm_int_bitmask {
	uint32 bits[BCM_INT_REGS_NUM];
} bcm_int_bitmask_t;

#ifndef ROM_COMPAT_INT_REG_IDX

#define BCM_INT_BITMASK_IS_EQUAL(b, cmp) (\
	(b)->bits[BCM_INT_REG_IDX_0] == (cmp)->bits[BCM_INT_REG_IDX_0] && \
	(b)->bits[BCM_INT_REG_IDX_1] == (cmp)->bits[BCM_INT_REG_IDX_1] && \
	(b)->bits[BCM_INT_REG_IDX_2] == (cmp)->bits[BCM_INT_REG_IDX_2])

#define BCM_INT_BITMASK_IS_ZERO(b) (\
	(b)->bits[BCM_INT_REG_IDX_0] == 0 && \
	(b)->bits[BCM_INT_REG_IDX_1] == 0 && \
	(b)->bits[BCM_INT_REG_IDX_2] == 0)

#define BCM_INT_BITMASK_SET(to, from) do { \
	(to)->bits[BCM_INT_REG_IDX_0] = (from)->bits[BCM_INT_REG_IDX_0]; \
	(to)->bits[BCM_INT_REG_IDX_1] = (from)->bits[BCM_INT_REG_IDX_1]; \
	(to)->bits[BCM_INT_REG_IDX_2] = (from)->bits[BCM_INT_REG_IDX_2]; \
} while (0)
#define BCM_INT_BITMASK_OR(to, from) do { \
	(to)->bits[BCM_INT_REG_IDX_0] |= (from)->bits[BCM_INT_REG_IDX_0]; \
	(to)->bits[BCM_INT_REG_IDX_1] |= (from)->bits[BCM_INT_REG_IDX_1]; \
	(to)->bits[BCM_INT_REG_IDX_2] |= (from)->bits[BCM_INT_REG_IDX_2]; \
} while (0)

#define BCM_INT_BITMASK_AND(to, mask) do { \
	(to)->bits[BCM_INT_REG_IDX_0] &= (mask)->bits[BCM_INT_REG_IDX_0]; \
	(to)->bits[BCM_INT_REG_IDX_1] &= (mask)->bits[BCM_INT_REG_IDX_1]; \
	(to)->bits[BCM_INT_REG_IDX_2] &= (mask)->bits[BCM_INT_REG_IDX_2]; \
} while (0)

#else

#define BCM_INT_BITMASK_IS_EQUAL(b, cmp) (\
	(b)->bits[BCM_INT_REG_IDX_0] == (cmp)->bits[BCM_INT_REG_IDX_0] && \
	(b)->bits[BCM_INT_REG_IDX_1] == (cmp)->bits[BCM_INT_REG_IDX_1]) \

#define BCM_INT_BITMASK_IS_ZERO(b) (\
	(b)->bits[BCM_INT_REG_IDX_0] == 0 && \
	(b)->bits[BCM_INT_REG_IDX_1] == 0)

#define BCM_INT_BITMASK_SET(to, from) do { \
	(to)->bits[BCM_INT_REG_IDX_0] = (from)->bits[BCM_INT_REG_IDX_0]; \
	(to)->bits[BCM_INT_REG_IDX_1] = (from)->bits[BCM_INT_REG_IDX_1]; \
} while (0)

#define BCM_INT_BITMASK_OR(to, from) do { \
	(to)->bits[BCM_INT_REG_IDX_0] |= (from)->bits[BCM_INT_REG_IDX_0]; \
	(to)->bits[BCM_INT_REG_IDX_1] |= (from)->bits[BCM_INT_REG_IDX_1]; \
} while (0)

#define BCM_INT_BITMASK_AND(to, mask) do { \
	(to)->bits[BCM_INT_REG_IDX_0] &= (mask)->bits[BCM_INT_REG_IDX_0]; \
	(to)->bits[BCM_INT_REG_IDX_1] &= (mask)->bits[BCM_INT_REG_IDX_1]; \
} while (0)

#endif /* ROM_COMPAT_INT_REG_IDX */

#define WARM_BOOT	0xA0B0C0D0

typedef struct si_axi_error_info si_axi_error_info_t;

#ifdef AXI_TIMEOUTS_NIC
#define SI_MAX_ERRLOG_SIZE	4
typedef struct si_axi_error
{
	uint32 error;
	uint32 coreid;
	uint32 errlog_lo;
	uint32 errlog_hi;
	uint32 errlog_id;
	uint32 errlog_flags;
	uint32 errlog_status;
} si_axi_error_t;

struct si_axi_error_info
{
	uint32 count;
	si_axi_error_t axi_error[SI_MAX_ERRLOG_SIZE];
};
#endif /* AXI_TIMEOUTS_NIC */

/**
 * Data structure to export all chip specific common variables
 *   public (read-only) portion of siutils handle returned by si_attach()/si_kattach()
 */
struct si_pub {
	bool	issim;			/**< chip is in simulation or emulation */

	uint16	socitype;		/**< SOCI_SB, SOCI_AI */
	int16	socirev;		/**< SOC interconnect rev */

	uint16	bustype;		/**< SI_BUS, PCI_BUS */
	uint16	buscoretype;		/**< PCI_CORE_ID, PCIE_CORE_ID */
	int16	buscorerev;		/**< buscore rev */
	uint16	buscoreidx;		/**< buscore index */

	int16	ccrev;			/**< chip common core rev */
	uint32	cccaps;			/**< chip common capabilities */
	uint32  cccaps_ext;			/**< chip common capabilities extension */
	int16	pmurev;			/**< pmu core rev */
	uint32	pmucaps;		/**< pmu capabilities */

	uint32	boardtype;		/**< board type */
	uint32	boardrev;               /* board rev */
	uint32	boardvendor;		/**< board vendor */
	uint32	boardflags;		/**< board flags */
	uint32	boardflags2;		/**< board flags2 */
	uint32	boardflags4;		/**< board flags4 */

	uint32	chip;			/**< chip number */
	uint16	chiprev;		/**< chip revision */
	uint16	chippkg;		/**< chip package option */
	uint32	chipst;			/**< chip status */

	int16	gcirev;			/**< gci core rev */
	int16	lhlrev;			/**< gci core rev */

	uint32	lpflags;		/**< low power flags */
	uint32	enum_base;	/**< backplane address where the chipcommon core resides */
	bool	_multibp_enable;
	bool	rffe_debug_mode;
	bool	rffe_elnabyp_mode;

	si_axi_error_info_t * err_info;
};

/* for HIGH_ONLY driver, the si_t must be writable to allow states sync from BMAC to HIGH driver
 * for monolithic driver, it is readonly to prevent accident change
 */
typedef struct si_pub si_t;

/*
 * Many of the routines below take an 'sih' handle as their first arg.
 * Allocate this by calling si_attach().  Free it by calling si_detach().
 * At any one time, the sih is logically focused on one particular si core
 * (the "current core").
 * Use si_setcore() or si_setcoreidx() to change the association to another core.
 */
#define	SI_OSH		NULL	/**< Use for si_kattach when no osh is available */

#ifndef SOCI_NCI_BUS
#define	BADIDX		(SI_MAXCORES + 1)
#else
#define	BADIDX		(0xffffu)	/* MAXCORES will be dynamically calculated for NCI. */
#endif /* SOCI_NCI_BUS */

/* clkctl xtal what flags */
#define	XTAL			0x1	/**< primary crystal oscillator (2050) */
#define	PLL			0x2	/**< main chip pll */

/* clkctl clk mode */
#define	CLK_FAST		0	/**< force fast (pll) clock */
#define	CLK_DYNAMIC		2	/**< enable dynamic clock control */

/* GPIO usage priorities */
#define GPIO_DRV_PRIORITY	0	/**< Driver */
#define GPIO_APP_PRIORITY	1	/**< Application */
#define GPIO_HI_PRIORITY	2	/**< Highest priority. Ignore GPIO reservation */

/* GPIO pull up/down */
#define GPIO_PULLUP		0
#define GPIO_PULLDN		1

/* GPIO event regtype */
#define GPIO_REGEVT		0	/**< GPIO register event */
#define GPIO_REGEVT_INTMSK	1	/**< GPIO register event int mask */
#define GPIO_REGEVT_INTPOL	2	/**< GPIO register event int polarity */

/* device path */
#define SI_DEVPATH_BUFSZ	16	/**< min buffer size in bytes */

/* SI routine enumeration: to be used by update function with multiple hooks */
#define	SI_DOATTACH	1
#define SI_PCIDOWN	2	/**< wireless interface is down */
#define SI_PCIUP	3	/**< wireless interface is up */

#ifdef SR_DEBUG
#define PMU_RES		31
#endif /* SR_DEBUG */

/* "access" param defines for si_seci_access() below */
#define SECI_ACCESS_STATUSMASK_SET	0
#define SECI_ACCESS_INTRS			1
#define SECI_ACCESS_UART_CTS		2
#define SECI_ACCESS_UART_RTS		3
#define SECI_ACCESS_UART_RXEMPTY	4
#define SECI_ACCESS_UART_GETC		5
#define SECI_ACCESS_UART_TXFULL		6
#define SECI_ACCESS_UART_PUTC		7
#define SECI_ACCESS_STATUSMASK_GET	8

#if defined(BCMQT)
#define	ISSIM_ENAB(sih)	TRUE
#else /* !defined(BCMQT) */
#define	ISSIM_ENAB(sih)	FALSE
#endif /* defined(BCMQT) */

#if defined(ATE_BUILD)
#define ATE_BLD_ENAB(sih)	TRUE
#else
#define ATE_BLD_ENAB(sih)	FALSE
#endif

#define INVALID_ADDR (0xFFFFFFFFu)

/* PMU clock/power control */
#if defined(BCMPMUCTL)
#define PMUCTL_ENAB(sih)	(BCMPMUCTL)
#else
#define PMUCTL_ENAB(sih)	((sih)->cccaps & CC_CAP_PMU)
#endif

#if defined(BCMAOBENAB)
#define AOB_ENAB(sih)  (BCMAOBENAB)
#else
#define AOB_ENAB(sih)	((sih)->ccrev >= 35 ? \
			((sih)->cccaps_ext & CC_CAP_EXT_AOB_PRESENT) : 0)
#endif /* BCMAOBENAB */

/* chipcommon clock/power control (exclusive with PMU's) */
#if defined(BCMPMUCTL) && BCMPMUCTL
#define CCCTL_ENAB(sih)		(0)
#define CCPLL_ENAB(sih)		(0)
#else
#define CCCTL_ENAB(sih)		((sih)->cccaps & CC_CAP_PWR_CTL)
#define CCPLL_ENAB(sih)		((sih)->cccaps & CC_CAP_PLL_MASK)
#endif

typedef void (*gci_gpio_handler_t)(uint32 stat, void *arg);

typedef void (*wci2_handler_t)(void *ctx, char *buf, int len);

/* External BT Coex enable mask */
#define CC_BTCOEX_EN_MASK  0x01
/* External PA enable mask */
#define GPIO_CTRL_EPA_EN_MASK 0x40
/* WL/BT control enable mask */
#define GPIO_CTRL_5_6_EN_MASK 0x60
#define GPIO_CTRL_7_6_EN_MASK 0xC0
#define GPIO_OUT_7_EN_MASK 0x80

#define UCODE_WAKE_STATUS_BIT	1

#if defined(BCMDONGLEHOST)

/* CR4 specific defines used by the host driver */
#define SI_CR4_CAP			(0x04)
#define SI_CR4_BANKIDX		(0x40)
#define SI_CR4_BANKINFO		(0x44)
#define SI_CR4_BANKPDA		(0x4C)

#define	ARMCR4_TCBBNB_MASK	0xf0
#define	ARMCR4_TCBBNB_SHIFT	4
#define	ARMCR4_TCBANB_MASK	0xf
#define	ARMCR4_TCBANB_SHIFT	0

#define	SICF_CPUHALT		(0x0020)
#define	ARMCR4_BSZ_MASK		0x7f
#define	ARMCR4_BUNITSZ_MASK	0x200
#define	ARMCR4_BSZ_8K		8192
#define	ARMCR4_BSZ_1K		1024
#endif /* BCMDONGLEHOST */
#define	SI_BPIND_1BYTE		0x1
#define	SI_BPIND_2BYTE		0x3
#define	SI_BPIND_4BYTE		0xF

#define GET_GCI_OFFSET(sih, gci_reg)	\
	(AOB_ENAB(sih)? OFFSETOF(gciregs_t, gci_reg) : OFFSETOF(chipcregs_t, gci_reg))

#define GET_GCI_CORE(sih)	\
	(AOB_ENAB(sih)? si_findcoreidx(sih, GCI_CORE_ID, 0) : SI_CC_IDX)

#define VARBUF_PRIO_INVALID		0u
#define VARBUF_PRIO_NVRAM		1u
#define VARBUF_PRIO_SROM		2u
#define VARBUF_PRIO_OTP			3u
#define VARBUF_PRIO_SH_SFLASH		4u

#define BT_IN_RESET_BIT_SHIFT		19u
#define BT_IN_PDS_BIT_SHIFT		10u

/* === exported functions === */
extern si_t *si_attach(uint pcidev, osl_t *osh, volatile void *regs, uint bustype,
                       void *sdh, char **vars, uint *varsz);
extern si_t *si_kattach(osl_t *osh);
extern void si_detach(si_t *sih);
extern volatile void *si_d11_switch_addrbase(si_t *sih, uint coreunit);
extern uint si_corelist(const si_t *sih, uint coreid[]);
extern uint si_coreid(const si_t *sih);
extern uint si_flag(si_t *sih);
extern uint si_flag_alt(const si_t *sih);
extern uint si_intflag(si_t *sih);
extern uint si_coreidx(const si_t *sih);
extern uint si_get_num_cores(const si_t *sih);
extern uint si_coreunit(const si_t *sih);
extern uint si_corevendor(const si_t *sih);
extern uint si_corerev(const si_t *sih);
extern uint si_corerev_minor(const si_t *sih);
extern void *si_osh(si_t *sih);
extern void si_setosh(si_t *sih, osl_t *osh);
extern int si_backplane_access(si_t *sih, uint addr, uint size, uint *val, bool read);

/* precommit failed when this is removed */
/* BLAZAR_BRANCH_101_10_DHD_002/build/dhd/linux-fc30/brix-brcm */
/* TBD: Revisit later */
#ifdef BCMINTERNAL
extern int si_backplane_access_64(si_t *sih, uint addr, uint size,
    uint64 *val, bool read);
#endif /* BCMINTERNAL */

extern uint si_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern uint si_corereg_writeonly(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern uint si_pmu_corereg(si_t *sih, uint32 idx, uint regoff, uint mask, uint val);
extern volatile uint32 *si_corereg_addr(si_t *sih, uint coreidx, uint regoff);
extern volatile void *si_coreregs(const si_t *sih);
extern uint si_wrapperreg(const si_t *sih, uint32 offset, uint32 mask, uint32 val);
extern uint si_core_wrapperreg(si_t *sih, uint32 coreidx, uint32 offset, uint32 mask, uint32 val);
extern void *si_wrapperregs(const si_t *sih);
extern uint32 si_core_cflags(const si_t *sih, uint32 mask, uint32 val);
extern void si_core_cflags_wo(const si_t *sih, uint32 mask, uint32 val);
extern uint32 si_core_sflags(const si_t *sih, uint32 mask, uint32 val);
extern void si_commit(si_t *sih);
extern bool si_iscoreup(const si_t *sih);
extern uint si_numcoreunits(const si_t *sih, uint coreid);
extern uint si_numd11coreunits(const si_t *sih);
extern uint si_findcoreidx(const si_t *sih, uint coreid, uint coreunit);
extern uint si_findcoreid(const si_t *sih, uint coreidx);
extern volatile void *si_setcoreidx(si_t *sih, uint coreidx);
extern volatile void *si_setcore(si_t *sih, uint coreid, uint coreunit);
extern uint32 si_oobr_baseaddr(const si_t *sih, bool second);
#if !defined(BCMDONGLEHOST)
extern uint si_corereg_ifup(si_t *sih, uint core_id, uint regoff, uint mask, uint val);
extern void si_lowpwr_opt(si_t *sih);
#endif /* !defined(BCMDONGLEHOST */
extern volatile void *si_switch_core(si_t *sih, uint coreid, uint *origidx,
	bcm_int_bitmask_t *intr_val);
extern void si_restore_core(si_t *sih, uint coreid, bcm_int_bitmask_t *intr_val);
#ifdef USE_NEW_COREREV_API
extern uint si_corerev_ext(si_t *sih, uint coreid, uint coreunit);
#else
uint si_get_corerev(si_t *sih, uint core_id);
#endif
extern int si_numaddrspaces(const si_t *sih);
extern uint32 si_addrspace(const si_t *sih, uint spidx, uint baidx);
extern uint32 si_addrspacesize(const si_t *sih, uint spidx, uint baidx);
extern void si_coreaddrspaceX(const si_t *sih, uint asidx, uint32 *addr, uint32 *size);
extern int si_corebist(const si_t *sih);
extern void si_core_reset(si_t *sih, uint32 bits, uint32 resetbits);
extern void si_core_disable(const si_t *sih, uint32 bits);
extern uint32 si_clock_rate(uint32 pll_type, uint32 n, uint32 m);
extern uint si_chip_hostif(const si_t *sih);
extern uint32 si_clock(si_t *sih);
extern uint32 si_alp_clock(si_t *sih); /* returns [Hz] units */
extern uint32 si_ilp_clock(si_t *sih); /* returns [Hz] units */
extern void si_pci_setup(si_t *sih, uint coremask);
extern int si_pcie_setup(si_t *sih, uint coreidx);
extern void si_setint(const si_t *sih, int siflag);
extern bool si_backplane64(const si_t *sih);
extern void si_register_intr_callback(si_t *sih, void *intrsoff_fn, void *intrsrestore_fn,
	void *intrsenabled_fn, void *intr_arg);
extern void si_deregister_intr_callback(si_t *sih);
extern void si_clkctl_init(si_t *sih);
extern uint16 si_clkctl_fast_pwrup_delay(si_t *sih);
extern bool si_clkctl_cc(si_t *sih, uint mode);
extern int si_clkctl_xtal(si_t *sih, uint what, bool on);
extern void si_btcgpiowar(si_t *sih);
extern bool si_deviceremoved(const si_t *sih);
extern void si_set_device_removed(si_t *sih, bool status);
extern uint32 si_sysmem_size(si_t *sih);
extern uint32 si_socram_size(si_t *sih);
extern uint32 si_socram_srmem_size(si_t *sih);
extern void si_socram_set_bankpda(si_t *sih, uint32 bankidx, uint32 bankpda);
extern bool si_is_bus_mpu_present(si_t *sih);

extern void si_watchdog(si_t *sih, uint ticks);
extern void si_watchdog_ms(si_t *sih, uint32 ms);
extern uint32 si_watchdog_msticks(void);
extern volatile void *si_gpiosetcore(si_t *sih);
extern uint32 si_gpiocontrol(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpioouten(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpioout(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpioin(si_t *sih);
extern uint32 si_gpiointpolarity(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpiointmask(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpioeventintmask(si_t *sih, uint32 mask, uint32 val, uint8 priority);
extern uint32 si_gpioled(si_t *sih, uint32 mask, uint32 val);
extern uint32 si_gpioreserve(const si_t *sih, uint32 gpio_num, uint8 priority);
extern uint32 si_gpiorelease(const si_t *sih, uint32 gpio_num, uint8 priority);
extern uint32 si_gpiopull(si_t *sih, bool updown, uint32 mask, uint32 val);
extern uint32 si_gpioevent(si_t *sih, uint regtype, uint32 mask, uint32 val);
extern uint32 si_gpio_int_enable(si_t *sih, bool enable);
extern void si_gci_uart_init(si_t *sih, osl_t *osh, uint8 seci_mode);
extern void si_gci_enable_gpio(si_t *sih, uint8 gpio, uint32 mask, uint32 value);
extern uint8 si_gci_host_wake_gpio_init(si_t *sih);
extern uint8 si_gci_time_sync_gpio_init(si_t *sih);
extern void si_gci_host_wake_gpio_enable(si_t *sih, uint8 gpio, bool state);
extern void si_gci_time_sync_gpio_enable(si_t *sih, uint8 gpio, bool state);
extern void si_gci_host_wake_gpio_tristate(si_t *sih, uint8 gpio, bool state);
extern int si_gpio_enable(si_t *sih, uint32 mask);

extern void si_invalidate_second_bar0win(si_t *sih);

extern void si_gci_shif_config_wake_pin(si_t *sih, uint8 gpio_n,
		uint8 wake_events, bool gci_gpio);
extern void si_shif_int_enable(si_t *sih, uint8 gpio_n, uint8 wake_events, bool enable);

/* GCI interrupt handlers */
extern void si_gci_handler_process(si_t *sih);

extern void si_enable_gpio_wake(si_t *sih, uint8 *wake_mask, uint8 *cur_status, uint8 gci_gpio,
	uint32 pmu_cc2_mask, uint32 pmu_cc2_value);

/* GCI GPIO event handlers */
extern void *si_gci_gpioint_handler_register(si_t *sih, uint8 gpio, uint8 sts,
	gci_gpio_handler_t cb, void *arg);
extern void si_gci_gpioint_handler_unregister(si_t *sih, void* gci_i);

extern void si_gci_gpio_chipcontrol_ex(si_t *si, uint8 gpoi, uint8 opt);
extern uint8 si_gci_gpio_status(si_t *sih, uint8 gci_gpio, uint8 mask, uint8 value);
extern void si_gci_config_wake_pin(si_t *sih, uint8 gpio_n, uint8 wake_events,
	bool gci_gpio);
extern void si_gci_free_wake_pin(si_t *sih, uint8 gpio_n);
#if !defined(BCMDONGLEHOST)
extern uint8 si_gci_gpio_wakemask(si_t *sih, uint8 gpio, uint8 mask, uint8 value);
extern uint8 si_gci_gpio_intmask(si_t *sih, uint8 gpio, uint8 mask, uint8 value);
#endif /* !defined(BCMDONGLEHOST) */

/* Wake-on-wireless-LAN (WOWL) */
extern bool si_pci_pmestat(const si_t *sih);
extern void si_pci_pmeclr(const si_t *sih);
extern void si_pci_pmeen(const si_t *sih);
extern void si_pci_pmestatclr(const si_t *sih);
extern uint si_pcie_readreg(void *sih, uint addrtype, uint offset);
extern uint si_pcie_writereg(void *sih, uint addrtype, uint offset, uint val);

#ifdef BCMSDIO
extern void si_sdio_init(si_t *sih);
#endif

extern uint16 si_d11_devid(si_t *sih);
extern int si_corepciid(si_t *sih, uint func, uint16 *pcivendor, uint16 *pcidevice,
	uint8 *pciclass, uint8 *pcisubclass, uint8 *pciprogif, uint8 *pciheader);

extern uint32 si_seci_access(si_t *sih, uint32 val, int access);
extern volatile void* si_seci_init(si_t *sih, uint8 seci_mode);
extern void si_seci_clk_force(si_t *sih, bool val);
extern bool si_seci_clk_force_status(si_t *sih);

#if (defined(BCMECICOEX) && !defined(BCMDONGLEHOST))
extern bool si_eci(const si_t *sih);
extern int si_eci_init(si_t *sih);
extern void si_eci_notify_bt(si_t *sih, uint32 mask, uint32 val, bool interrupt);
extern bool si_seci(const si_t *sih);
extern void* si_gci_init(si_t *sih);
extern void si_seci_down(si_t *sih);
extern void si_seci_upd(si_t *sih, bool enable);
extern bool si_gci(const si_t *sih);
extern bool si_sraon(const si_t *sih);
#else
#define si_eci(sih) 0
#define si_eci_init(sih) 0
#define si_eci_notify_bt(sih, type, val)  (0)
#define si_seci(sih) 0
#define si_seci_upd(sih, a)	do {} while (0)
#define si_gci_init(sih) NULL
#define si_seci_down(sih) do {} while (0)
#define si_gci(sih) 0
#define si_sraon(sih) 0
#endif /* BCMECICOEX */

/* OTP status */
extern bool si_is_otp_disabled(const si_t *sih);
extern bool si_is_otp_powered(si_t *sih);
extern void si_otp_power(si_t *sih, bool on, uint32* min_res_mask);

/* SPROM availability */
extern bool si_is_sprom_available(si_t *sih);
#ifdef SI_SPROM_PROBE
extern void si_sprom_init(si_t *sih);
#endif /* SI_SPROM_PROBE */

/* SFlash availability */
bool si_is_sflash_available(const si_t *sih);

/* OTP/SROM CIS stuff */
extern int si_cis_source(const si_t *sih);
#define CIS_DEFAULT	0
#define CIS_SROM	1
#define CIS_OTP		2

/* Fab-id information */
#define	DEFAULT_FAB	0x0	/**< Original/first fab used for this chip */
#define	CSM_FAB7	0x1	/**< CSM Fab7 chip */
#define	TSMC_FAB12	0x2	/**< TSMC Fab12/Fab14 chip */
#define	SMIC_FAB4	0x3	/**< SMIC Fab4 chip */

/* bp_ind_access default timeout */
#define BP_ACCESS_TO (500u * 1000u)

extern uint16 BCMATTACHFN(si_fabid)(si_t *sih);
extern uint16 BCMINITFN(si_chipid)(const si_t *sih);

/*
 * Build device path. Path size must be >= SI_DEVPATH_BUFSZ.
 * The returned path is NULL terminated and has trailing '/'.
 * Return 0 on success, nonzero otherwise.
 */
extern int si_devpath(const si_t *sih, char *path, int size);
extern int si_devpath_pcie(const si_t *sih, char *path, int size);
/* Read variable with prepending the devpath to the name */
extern char *si_getdevpathvar(const si_t *sih, const char *name);
extern int si_getdevpathintvar(const si_t *sih, const char *name);
extern char *si_coded_devpathvar(const si_t *sih, char *varname, int var_len, const char *name);

/* === HW PR WARs === */
extern uint8 si_pcieclkreq(const si_t *sih, uint32 mask, uint32 val);
extern uint32 si_pcielcreg(const si_t *sih, uint32 mask, uint32 val);
extern uint8 si_pcieltrenable(const si_t *sih, uint32 mask, uint32 val);
extern uint8 si_pcieobffenable(const si_t *sih, uint32 mask, uint32 val);
extern uint32 si_pcieltr_reg(const si_t *sih, uint32 reg, uint32 mask, uint32 val);
extern uint32 si_pcieltrspacing_reg(const si_t *sih, uint32 mask, uint32 val);
extern uint32 si_pcieltrhysteresiscnt_reg(const si_t *sih, uint32 mask, uint32 val);
extern void si_pcie_set_error_injection(const si_t *sih, uint32 mode);
extern void si_pcie_set_L1substate(const si_t *sih, uint32 substate);
#ifndef BCM_BOOTLOADER
extern uint32 si_pcie_get_L1substate(const si_t *sih);
#endif /* BCM_BOOTLOADER */
extern void si_pci_down(const si_t *sih);
extern void si_pci_up(const si_t *sih);
extern void si_pci_sleep(const si_t *sih);
extern void si_pcie_war_ovr_update(const si_t *sih, uint8 aspm);
extern void si_pcie_power_save_enable(const si_t *sih, bool enable);
extern int si_pci_fixcfg(si_t *sih);
extern bool si_is_warmboot(void);

extern void si_chipcontrl_restore(si_t *sih, uint32 val);
extern uint32 si_chipcontrl_read(si_t *sih);
extern void si_chipcontrl_srom4360(si_t *sih, bool on);
extern void si_srom_clk_set(si_t *sih); /**< for chips with fast BP clock */
extern void si_btc_enable_chipcontrol(si_t *sih);
extern void si_pmu_avb_clk_set(si_t *sih, osl_t *osh, bool set_flag);
/* === debug routines === */

extern bool si_taclear(si_t *sih, bool details);

#ifdef BCMDBG
extern void si_view(si_t *sih, bool verbose);
extern void si_viewall(si_t *sih, bool verbose);
#endif /* BCMDBG */
#if defined(BCMDBG) || defined(BCMDBG_DUMP) || defined(BCMDBG_PHYDUMP) || \
	defined(WLTEST)
struct bcmstrbuf;
extern int si_dump_pcieinfo(const si_t *sih, struct bcmstrbuf *b);
extern void si_dump_pmuregs(si_t *sih, struct bcmstrbuf *b);
extern int si_dump_pcieregs(const si_t *sih, struct bcmstrbuf *b);
#endif /* BCMDBG || BCMDBG_DUMP || WLTEST */

#if defined(BCMDBG) || defined(BCMDBG_DUMP) || defined(BCMDBG_PHYDUMP)
extern void si_dump(const si_t *sih, struct bcmstrbuf *b);
extern void si_ccreg_dump(si_t *sih, struct bcmstrbuf *b);
extern void si_clkctl_dump(si_t *sih, struct bcmstrbuf *b);
extern int si_gpiodump(si_t *sih, struct bcmstrbuf *b);

extern void si_dumpregs(si_t *sih, struct bcmstrbuf *b);
#endif /* BCMDBG || BCMDBG_DUMP || BCMDBG_PHYDUMP */

extern uint32 si_ccreg(si_t *sih, uint32 offset, uint32 mask, uint32 val);
extern uint32 si_pciereg(const si_t *sih, uint32 offset, uint32 mask, uint32 val, uint type);
extern int si_bpind_access(si_t *sih, uint32 addr_high, uint32 addr_low,
	int32* data, bool read, uint32 us_timeout);
extern void sih_write_sraon(si_t *sih, int offset, int len, const uint32* data);
#ifdef SR_DEBUG
extern void si_dump_pmu(si_t *sih, void *pmu_var);
extern void si_pmu_keep_on(const si_t *sih, int32 int_val);
extern uint32 si_pmu_keep_on_get(const si_t *sih);
extern uint32 si_power_island_set(si_t *sih, uint32 int_val);
extern uint32 si_power_island_get(si_t *sih);
#endif /* SR_DEBUG */

extern uint32 si_pcieserdesreg(const si_t *sih, uint32 mdioslave, uint32 offset,
	uint32 mask, uint32 val);
extern void si_pcie_set_request_size(const si_t *sih, uint16 size);
extern uint16 si_pcie_get_request_size(const si_t *sih);
extern void si_pcie_set_maxpayload_size(const si_t *sih, uint16 size);
extern uint16 si_pcie_get_maxpayload_size(const si_t *sih);
extern uint16 si_pcie_get_ssid(const si_t *sih);
extern uint32 si_pcie_get_bar0(const si_t *sih);
extern int si_pcie_configspace_cache(const si_t *sih);
extern int si_pcie_configspace_restore(const si_t *sih);
extern int si_pcie_configspace_get(const si_t *sih, uint8 *buf, uint size);

#ifndef BCMDONGLEHOST
extern void si_muxenab(si_t *sih, uint32 w);
extern uint32 si_clear_backplane_to(si_t *sih);
extern void si_slave_wrapper_add(si_t *sih);

#ifdef AXI_TIMEOUTS_NIC
extern uint32 si_clear_backplane_to_fast(void *sih, void *addr);
#endif /* AXI_TIMEOUTS_NIC */

#if defined(AXI_TIMEOUTS) || defined(AXI_TIMEOUTS_NIC)
extern uint32 si_clear_backplane_to_per_core(si_t *sih, uint coreid, uint coreunit, void *wrap);
#endif /* AXI_TIMEOUTS || AXI_TIMEOUTS_NIC */
#endif /* !BCMDONGLEHOST */

extern uint32 si_findcoreidx_by_axiid(const si_t *sih, uint32 axiid);
extern void si_wrapper_get_last_error(const si_t *sih, uint32 *error_status, uint32 *core,
	uint32 *lo, uint32 *hi, uint32 *id);
extern uint32 si_get_axi_timeout_reg(const si_t *sih);

#ifdef AXI_TIMEOUTS_NIC
extern const si_axi_error_info_t * si_get_axi_errlog_info(const si_t *sih);
extern void si_reset_axi_errlog_info(const si_t * sih);
#endif /* AXI_TIMEOUTS_NIC */

extern void si_update_backplane_timeouts(const si_t *sih, bool enable, uint32 timeout, uint32 cid);

#if defined(BCMDONGLEHOST)
extern uint32 si_tcm_size(si_t *sih);
extern bool si_has_flops(si_t *sih);
#endif /* BCMDONGLEHOST */

extern int si_set_sromctl(si_t *sih, uint32 value);
extern uint32 si_get_sromctl(si_t *sih);

extern uint32 si_gci_direct(si_t *sih, uint offset, uint32 mask, uint32 val);
extern uint32 si_gci_indirect(si_t *sih, uint regidx, uint offset, uint32 mask, uint32 val);
extern uint32 si_gci_output(si_t *sih, uint reg, uint32 mask, uint32 val);
extern uint32 si_gci_input(si_t *sih, uint reg);
extern uint32 si_gci_int_enable(si_t *sih, bool enable);
extern void si_gci_reset(si_t *sih);
#ifdef BCMLTECOEX
extern void si_ercx_init(si_t *sih, uint32 ltecx_mux, uint32 ltecx_padnum,
	uint32 ltecx_fnsel, uint32 ltecx_gcigpio);
#endif /* BCMLTECOEX */
#if defined(BCMLTECOEX) && !defined(WLTEST)
extern int si_wci2_rxfifo_handler_register(si_t *sih, wci2_handler_t rx_cb, void *ctx);
extern void si_wci2_rxfifo_handler_unregister(si_t *sih);
#endif /* BCMLTECOEX && !WLTEST */
extern void si_gci_seci_init(si_t *sih);
extern void si_wci2_init(si_t *sih, uint8 baudrate, uint32 ltecx_mux, uint32 ltecx_padnum,
	uint32 ltecx_fnsel, uint32 ltecx_gcigpio, uint32 xtalfreq);

extern bool si_btcx_wci2_init(si_t *sih);

extern void si_gci_set_functionsel(si_t *sih, uint32 pin, uint8 fnsel);
extern uint32 si_gci_get_functionsel(si_t *sih, uint32 pin);
extern void si_gci_clear_functionsel(si_t *sih, uint8 fnsel);
extern uint8 si_gci_get_chipctrlreg_idx(uint32 pin, uint32 *regidx, uint32 *pos);
extern uint32 si_gci_chipcontrol(si_t *sih, uint reg, uint32 mask, uint32 val);
extern uint32 si_gci_chipstatus(si_t *sih, uint reg);
extern uint8 si_enable_device_wake(si_t *sih, uint8 *wake_status, uint8 *cur_status);
extern uint8 si_get_device_wake_opt(si_t *sih);
extern void si_swdenable(si_t *sih, uint32 swdflag);
extern uint8 si_enable_perst_wake(si_t *sih, uint8 *perst_wake_mask, uint8 *perst_cur_status);

extern uint32 si_get_pmu_reg_addr(si_t *sih, uint32 offset);
#define CHIPCTRLREG1 0x1
#define CHIPCTRLREG2 0x2
#define CHIPCTRLREG3 0x3
#define CHIPCTRLREG4 0x4
#define CHIPCTRLREG5 0x5
#define MINRESMASKREG 0x618
#define MAXRESMASKREG 0x61c
#define CHIPCTRLADDR 0x650
#define CHIPCTRLDATA 0x654
#define RSRCTABLEADDR 0x620
#define RSRCUPDWNTIME 0x628
#define PMUREG_RESREQ_MASK 0x68c

void si_update_masks(si_t *sih);
void si_force_islanding(si_t *sih, bool enable);
extern uint32 si_pmu_res_req_timer_clr(si_t *sih);
extern void si_pmu_rfldo(si_t *sih, bool on);
extern void si_pcie_ltr_war(const si_t *sih);
extern void si_pcie_hw_LTR_war(const si_t *sih);
extern void si_pcie_hw_L1SS_war(const si_t *sih);
extern void si_pciedev_crwlpciegen2(const si_t *sih);
extern void si_pcie_prep_D3(const si_t *sih, bool enter_D3);
extern void si_pciedev_reg_pm_clk_period(const si_t *sih);
extern void si_pcie_disable_oobselltr(const si_t *sih);
extern uint32 si_raw_reg(const si_t *sih, uint32 reg, uint32 val, uint32 wrire_req);

/* Macro to enable clock gating changes in different cores */
#define MEM_CLK_GATE_BIT	5
#define GCI_CLK_GATE_BIT	18

#define USBAPP_CLK_BIT		0
#define PCIE_CLK_BIT		3
#define ARMCR4_DBG_CLK_BIT	4
#define SAMPLE_SYNC_CLK_BIT	17
#define PCIE_TL_CLK_BIT		18
#define HQ_REQ_BIT		24
#define PLL_DIV2_BIT_START	9
#define PLL_DIV2_MASK		(0x37 << PLL_DIV2_BIT_START)
#define PLL_DIV2_DIS_OP		(0x37 << PLL_DIV2_BIT_START)

#define pmu_corereg(si, cc_idx, member, mask, val) \
	(AOB_ENAB(si) ? \
		si_pmu_corereg(si, si_findcoreidx(si, PMU_CORE_ID, 0), \
			       OFFSETOF(pmuregs_t, member), mask, val): \
		si_pmu_corereg(si, cc_idx, OFFSETOF(chipcregs_t, member), mask, val))

#define PMU_REG(si, member, mask, val) \
	(AOB_ENAB(si) ? \
		si_corereg(si, si_findcoreidx(si, PMU_CORE_ID, 0), \
			OFFSETOF(pmuregs_t, member), mask, val): \
		si_corereg(si, SI_CC_IDX, OFFSETOF(chipcregs_t, member), mask, val))

/* Used only for the regs present in the pmu core and not present in the old cc core */
#define PMU_REG_NEW(si, member, mask, val) \
		si_corereg(si, si_findcoreidx(si, PMU_CORE_ID, 0), \
			OFFSETOF(pmuregs_t, member), mask, val)

#define GCI_REG(si, offset, mask, val) \
		(AOB_ENAB(si) ? \
			si_corereg(si, si_findcoreidx(si, GCI_CORE_ID, 0), \
				offset, mask, val): \
			si_corereg(si, SI_CC_IDX, offset, mask, val))

/* Used only for the regs present in the gci core and not present in the old cc core */
#define GCI_REG_NEW(si, member, mask, val) \
		si_corereg(si, si_findcoreidx(si, GCI_CORE_ID, 0), \
			OFFSETOF(gciregs_t, member), mask, val)

#define LHL_REG(si, member, mask, val) \
		si_corereg(si, si_findcoreidx(si, GCI_CORE_ID, 0), \
			OFFSETOF(gciregs_t, member), mask, val)

#define CHIPC_REG(si, member, mask, val) \
		si_corereg(si, SI_CC_IDX, OFFSETOF(chipcregs_t, member), mask, val)

/* GCI Macros */
#define ALLONES_32				0xFFFFFFFF
#define GCI_CCTL_SECIRST_OFFSET			0 /**< SeciReset */
#define GCI_CCTL_RSTSL_OFFSET			1 /**< ResetSeciLogic */
#define GCI_CCTL_SECIEN_OFFSET			2 /**< EnableSeci  */
#define GCI_CCTL_FSL_OFFSET			3 /**< ForceSeciOutLow */
#define GCI_CCTL_SMODE_OFFSET			4 /**< SeciOpMode, 6:4 */
#define GCI_CCTL_US_OFFSET			7 /**< UpdateSeci */
#define GCI_CCTL_BRKONSLP_OFFSET		8 /**< BreakOnSleep */
#define GCI_CCTL_SILOWTOUT_OFFSET		9 /**< SeciInLowTimeout, 10:9 */
#define GCI_CCTL_RSTOCC_OFFSET			11 /**< ResetOffChipCoex */
#define GCI_CCTL_ARESEND_OFFSET			12 /**< AutoBTSigResend */
#define GCI_CCTL_FGCR_OFFSET			16 /**< ForceGciClkReq */
#define GCI_CCTL_FHCRO_OFFSET			17 /**< ForceHWClockReqOff */
#define GCI_CCTL_FREGCLK_OFFSET			18 /**< ForceRegClk */
#define GCI_CCTL_FSECICLK_OFFSET		19 /**< ForceSeciClk */
#define GCI_CCTL_FGCA_OFFSET			20 /**< ForceGciClkAvail */
#define GCI_CCTL_FGCAV_OFFSET			21 /**< ForceGciClkAvailValue */
#define GCI_CCTL_SCS_OFFSET			24 /**< SeciClkStretch, 31:24 */
#define GCI_CCTL_SCS				25 /* SeciClkStretch */

#define GCI_MODE_UART				0x0
#define GCI_MODE_SECI				0x1
#define GCI_MODE_BTSIG				0x2
#define GCI_MODE_GPIO				0x3
#define GCI_MODE_MASK				0x7

#define GCI_CCTL_LOWTOUT_DIS			0x0
#define GCI_CCTL_LOWTOUT_10BIT			0x1
#define GCI_CCTL_LOWTOUT_20BIT			0x2
#define GCI_CCTL_LOWTOUT_30BIT			0x3
#define GCI_CCTL_LOWTOUT_MASK			0x3

#define GCI_CCTL_SCS_DEF			0x19
#define GCI_CCTL_SCS_MASK			0xFF

#define GCI_SECIIN_MODE_OFFSET			0
#define GCI_SECIIN_GCIGPIO_OFFSET		4
#define GCI_SECIIN_RXID2IP_OFFSET		8

#define GCI_SECIIN_MODE_MASK                    0x7
#define GCI_SECIIN_GCIGPIO_MASK                 0xF

#define GCI_SECIOUT_MODE_OFFSET			0
#define GCI_SECIOUT_GCIGPIO_OFFSET		4
#define	GCI_SECIOUT_LOOPBACK_OFFSET		8
#define GCI_SECIOUT_SECIINRELATED_OFFSET	16

#define GCI_SECIOUT_MODE_MASK                   0x7
#define GCI_SECIOUT_GCIGPIO_MASK                0xF
#define GCI_SECIOUT_SECIINRELATED_MASK          0x1

#define GCI_SECIOUT_SECIINRELATED               0x1

#define GCI_SECIAUX_RXENABLE_OFFSET		0
#define GCI_SECIFIFO_RXENABLE_OFFSET		16

#define GCI_SECITX_ENABLE_OFFSET		0

#define GCI_GPIOCTL_INEN_OFFSET			0
#define GCI_GPIOCTL_OUTEN_OFFSET		1
#define GCI_GPIOCTL_PDN_OFFSET			4

#define GCI_GPIOIDX_OFFSET			16

#define GCI_LTECX_SECI_ID			0 /**< SECI port for LTECX */
#define GCI_LTECX_TXCONF_EN_OFFSET		2
#define GCI_LTECX_PRISEL_EN_OFFSET		3

/* To access per GCI bit registers */
#define GCI_REG_WIDTH				32

/* number of event summary bits */
#define GCI_EVENT_NUM_BITS			32

/* gci event bits per core */
#define GCI_EVENT_BITS_PER_CORE	4
#define GCI_EVENT_HWBIT_1			1
#define GCI_EVENT_HWBIT_2			2
#define GCI_EVENT_SWBIT_1			3
#define GCI_EVENT_SWBIT_2			4

#define GCI_MBDATA_TOWLAN_POS	96
#define GCI_MBACK_TOWLAN_POS	104
#define GCI_WAKE_TOWLAN_PO		112
#define GCI_SWREADY_POS			120

/* GCI bit positions */
/* GCI [127:000] = WLAN [127:0] */
#define GCI_WLAN_IP_ID				0
#define GCI_WLAN_BEGIN				0
#define GCI_WLAN_PRIO_POS			(GCI_WLAN_BEGIN + 4)
#define GCI_WLAN_PERST_POS			(GCI_WLAN_BEGIN + 15)

/* GCI [255:128] = BT [127:0] */
#define GCI_BT_IP_ID					1
#define GCI_BT_BEGIN					128
#define GCI_BT_MBDATA_TOWLAN_POS	(GCI_BT_BEGIN + GCI_MBDATA_TOWLAN_POS)
#define GCI_BT_MBACK_TOWLAN_POS	(GCI_BT_BEGIN + GCI_MBACK_TOWLAN_POS)
#define GCI_BT_WAKE_TOWLAN_POS	(GCI_BT_BEGIN + GCI_WAKE_TOWLAN_PO)
#define GCI_BT_SWREADY_POS			(GCI_BT_BEGIN + GCI_SWREADY_POS)

/* GCI [639:512] = LTE [127:0] */
#define GCI_LTE_IP_ID				4
#define GCI_LTE_BEGIN				512
#define GCI_LTE_FRAMESYNC_POS			(GCI_LTE_BEGIN + 0)
#define GCI_LTE_RX_POS				(GCI_LTE_BEGIN + 1)
#define GCI_LTE_TX_POS				(GCI_LTE_BEGIN + 2)
#define GCI_LTE_WCI2TYPE_POS			(GCI_LTE_BEGIN + 48)
#define GCI_LTE_WCI2TYPE_MASK			7
#define GCI_LTE_AUXRXDVALID_POS			(GCI_LTE_BEGIN + 56)

/* Reg Index corresponding to ECI bit no x of ECI space */
#define GCI_REGIDX(x)				((x)/GCI_REG_WIDTH)
/* Bit offset of ECI bit no x in 32-bit words */
#define GCI_BITOFFSET(x)			((x)%GCI_REG_WIDTH)

#define GCI_ECI_HW0(ip_id)	(((ip_id) * GCI_EVENT_BITS_PER_CORE) + 0)
#define GCI_ECI_HW1(ip_id)	(((ip_id) * GCI_EVENT_BITS_PER_CORE) + 1)
#define GCI_ECI_SW0(ip_id)	(((ip_id) * GCI_EVENT_BITS_PER_CORE) + 2)
#define GCI_ECI_SW1(ip_id)	(((ip_id) * GCI_EVENT_BITS_PER_CORE) + 3)

/* BT SMEM Control Register 0 */
#define GCI_BT_SMEM_CTRL0_SUBCORE_ENABLE_PKILL	(1 << 28)

/* GCI RXFIFO Common control */
#define GCI_RXFIFO_CTRL_AUX_EN		0xFF
#define GCI_RXFIFO_CTRL_FIFO_EN		0xFF00
#define GCI_RXFIFO_CTRL_FIFO_TYPE2_EN	0x400

/* End - GCI Macros */

extern void si_pll_sr_reinit(si_t *sih);
extern void si_pll_closeloop(si_t *sih);
extern uint si_num_slaveports(const si_t *sih, uint coreid);
extern uint32 si_get_slaveport_addr(si_t *sih, uint spidx, uint baidx,
	uint core_id, uint coreunit);
extern uint32 si_get_d11_slaveport_addr(si_t *sih, uint spidx,
	uint baidx, uint coreunit);
void si_introff(const si_t *sih, bcm_int_bitmask_t *intr_val);
void si_intrrestore(const si_t *sih, bcm_int_bitmask_t *intr_val);
bool si_get_nvram_rfldo3p3_war(const si_t *sih);
void si_nvram_res_masks(const si_t *sih, uint32 *min_mask, uint32 *max_mask);
extern uint32 si_xtalfreq(const si_t *sih);
extern uint8 si_getspurmode(const si_t *sih);
extern uint32 si_get_openloop_dco_code(const si_t *sih);
extern void si_set_openloop_dco_code(si_t *sih, uint32 openloop_dco_code);
extern uint32 si_wrapper_dump_buf_size(const si_t *sih);
extern uint32 si_wrapper_dump_binary(const si_t *sih, uchar *p);
extern uint32 si_wrapper_dump_last_timeout(const si_t *sih, uint32 *error, uint32 *core,
	uint32 *ba, uchar *p);

/* SR Power Control */
extern uint32 si_srpwr_request(const si_t *sih, uint32 mask, uint32 val);
extern uint32 si_srpwr_request_on_rev80(si_t *sih, uint32 mask, uint32 val,
	uint32 ucode_awake);
extern uint32 si_srpwr_stat_spinwait(const si_t *sih, uint32 mask, uint32 val);
extern uint32 si_srpwr_stat(si_t *sih);
extern uint32 si_srpwr_domain(si_t *sih);
extern uint32 si_srpwr_domain_all_mask(const si_t *sih);
extern uint8 si_srpwr_domain_wl(si_t *sih);
extern uint32 si_srpwr_bt_status(si_t *sih);
/* SR Power Control */
bool si_srpwr_cap(si_t *sih);
#define SRPWR_CAP(sih) (si_srpwr_cap(sih))

#ifdef BCMSRPWR
	extern bool _bcmsrpwr;
#if defined(ROM_ENAB_RUNTIME_CHECK) || !defined(DONGLEBUILD)
	#define SRPWR_ENAB()    (_bcmsrpwr)
#elif defined(BCMSRPWR_DISABLED)
	#define SRPWR_ENAB()    (0)
#else
	#define SRPWR_ENAB()    (1)
#endif
#else
	#define SRPWR_ENAB()            (0)
#endif /* BCMSRPWR */

/*
 * Multi-BackPlane architecture.  Each can power up/down independently.
 *   Common backplane: shared between BT and WL
 *      ChipC, PCIe, GCI, PMU, SRs
 *      HW powers up as needed
 *   WL BackPlane (WLBP):
 *      ARM, TCM, Main, Aux
 *      Host needs to power up
 */
#define MULTIBP_CAP(sih)	(BCM4378_CHIP(sih->chip) || \
				BCM4387_CHIP(sih->chip) || BCM4388_CHIP(sih->chip) || \
				BCM4389_CHIP(sih->chip) || BCM4385_CHIP(sih->chip) || \
				BCM4376_CHIP(sih->chip) || BCM4397_CHIP(sih->chip))
#define MULTIBP_ENAB(sih)      ((sih) && (sih)->_multibp_enable)

#ifdef DONGLEBUILD
extern bool si_check_enable_backplane_log(const si_t *sih);
#endif /* DONGLEBUILD */

uint32 si_enum_base(uint devid);

/* Default ARM PLL freq 4369/4368 */
#define ARMPLL_FREQ_400MHZ             (400u)
#define ARMPLL_FREQ_800MHZ	       (800u)
/* ARM PLL freq computed using chip defaults is 1002.8235 Mhz */
#define ARMPLL_FREQ_1000MHZ	       (1003u)

extern uint8 si_lhl_ps_mode(const si_t *sih);
extern uint32 si_get_armpllclkfreq(const si_t *sih);
uint8 si_get_ccidiv(const si_t *sih);
extern uint8 si_hib_ext_wakeup_isenab(const si_t *sih);

#ifdef UART_TRAP_DBG
void si_dump_APB_Bridge_registers(const si_t *sih);
#endif /* UART_TRAP_DBG */
void si_force_clocks(const si_t *sih, uint clock_state);

#if defined(BCMSDIODEV_ENABLED) && defined(ATE_BUILD)
bool si_chipcap_sdio_ate_only(const si_t *sih);
#endif /* BCMSDIODEV_ENABLED && ATE_BUILD */

/* indicates to the siutils how the PICe BAR0 is mappend.
 * here is the current scheme, which are all using BAR0:
 * id     enum       wrapper
 * ====   =========  =========
 *    0   0000-0FFF  1000-1FFF
 *    1   4000-4FFF  5000-5FFF
 *    2   9000-9FFF  A000-AFFF
 * >= 3   not supported
 */
void si_set_slice_id(si_t *sih, uint8 slice);
uint8 si_get_slice_id(const si_t *sih);

/* query the d11 core type */
#define D11_CORE_TYPE_NORM	0u
#define D11_CORE_TYPE_SCAN	1u
uint si_core_d11_type(si_t *sih, uint coreunit);

/* check if the package option allows the d11 core */
bool si_pkgopt_d11_allowed(si_t *sih, uint coreuint);

/* return if scan core is present */
bool si_scan_core_present(const si_t *sih);
void si_configure_pwrthrottle_gpio(si_t *sih, uint8 pwrthrottle_gpio_pin);
void si_configure_onbody_gpio(si_t *sih, uint8 onbody_gpio_pin);

/* check if HWA core present */
bool si_hwa_present(const si_t *sih);

/* check if SYSMEM present */
bool si_sysmem_present(const si_t *sih);

/* return BT state */
bool si_btc_bt_status_in_reset(si_t *sih);
bool si_btc_bt_status_in_pds(si_t *sih);
int si_btc_bt_pds_wakeup_force(si_t *sih, bool force);

/* RFFE RFEM Functions */
#ifndef BCMDONGLEHOST
void si_rffe_rfem_init(si_t *sih);
void si_rffe_set_debug_mode(si_t *sih, bool enable);
bool si_rffe_get_debug_mode(si_t *sih);
int si_rffe_set_elnabyp_mode(si_t *sih, uint8 mode);
int8 si_rffe_get_elnabyp_mode(si_t *sih);
int si_rffe_rfem_read(si_t *sih, uint8 dev_id, uint8 antenna, uint16 reg_addr, uint32 *val);
int si_rffe_rfem_write(si_t *sih, uint8 dev_id, uint8 antenna, uint16 reg_addr, uint32 data);
#endif /* !BCMDONGLEHOST */
extern void si_jtag_udr_pwrsw_main_toggle(si_t *sih, bool on);
extern int si_pmu_res_state_pwrsw_main_wait(si_t *sih);
extern uint32 si_d11_core_sssr_addr(si_t *sih, uint unit, uint32 *sssr_dmp_sz);

#ifdef USE_LHL_TIMER
/* Get current HIB time API */
uint32 si_cur_hib_time(si_t *sih);
#endif

#endif	/* _siutils_h_ */
