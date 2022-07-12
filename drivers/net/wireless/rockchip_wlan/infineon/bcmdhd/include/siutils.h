/*
 * Misc utility routines for accessing the SOC Interconnects
 * of Broadcom HNBU chips.
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
 *
 * $Id: siutils.h 699906 2017-05-16 22:39:33Z $
 */

#ifndef	_siutils_h_
#define	_siutils_h_

#ifdef SR_DEBUG
#include "wlioctl.h"
#endif /* SR_DEBUG */

#define WARM_BOOT	0xA0B0C0D0

#ifdef BCM_BACKPLANE_TIMEOUT

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

typedef struct si_axi_error_info
{
	uint32 count;
	si_axi_error_t axi_error[SI_MAX_ERRLOG_SIZE];
} si_axi_error_info_t;
#endif /* BCM_BACKPLANE_TIMEOUT */

/**
 * Data structure to export all chip specific common variables
 *   public (read-only) portion of siutils handle returned by si_attach()/si_kattach()
 */
struct si_pub {
	uint	socitype;		/**< SOCI_SB, SOCI_AI */

	uint	bustype;		/**< SI_BUS, PCI_BUS */
	uint	buscoretype;		/**< PCI_CORE_ID, PCIE_CORE_ID, PCMCIA_CORE_ID */
	uint	buscorerev;		/**< buscore rev */
	uint	buscoreidx;		/**< buscore index */
	int	ccrev;			/**< chip common core rev */
	uint32	cccaps;			/**< chip common capabilities */
	uint32  cccaps_ext;			/**< chip common capabilities extension */
	int	pmurev;			/**< pmu core rev */
	uint32	pmucaps;		/**< pmu capabilities */
	uint	boardtype;		/**< board type */
	uint    boardrev;               /* board rev */
	uint	boardvendor;		/**< board vendor */
	uint	boardflags;		/**< board flags */
	uint	boardflags2;		/**< board flags2 */
	uint	boardflags4;		/**< board flags4 */
	uint	chip;			/**< chip number */
	uint	chiprev;		/**< chip revision */
	uint	chippkg;		/**< chip package option */
	uint32	chipst;			/**< chip status */
	bool	issim;			/**< chip is in simulation or emulation */
	uint    socirev;		/**< SOC interconnect rev */
	bool	pci_pr32414;
	int	gcirev;			/**< gci core rev */
	int	lpflags;		/**< low power flags */
	uint32	enum_base;	/**< backplane address where the chipcommon core resides */

#ifdef BCM_BACKPLANE_TIMEOUT
	si_axi_error_info_t * err_info;
#endif /* BCM_BACKPLANE_TIMEOUT */

	bool	_multibp_enable;
	bool	secureboot;
	bool    chipidpresent;
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

#define	BADIDX		(SI_MAXCORES + 1)

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
#else
#define	ISSIM_ENAB(sih)	FALSE
#endif // endif

#define INVALID_ADDR (~0)

/* PMU clock/power control */
#if defined(BCMPMUCTL)
#define PMUCTL_ENAB(sih)	(BCMPMUCTL)
#else
#define PMUCTL_ENAB(sih)	((sih)->cccaps & CC_CAP_PMU)
#endif // endif

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
#endif // endif

typedef void (*gci_gpio_handler_t)(uint32 stat, void *arg);

/* External BT Coex enable mask */
#define CC_BTCOEX_EN_MASK  0x01
/* External PA enable mask */
#define GPIO_CTRL_EPA_EN_MASK 0x40
/* WL/BT control enable mask */
#define GPIO_CTRL_5_6_EN_MASK 0x60
#define GPIO_CTRL_7_6_EN_MASK 0xC0
#define GPIO_OUT_7_EN_MASK 0x80

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
#define	SI_BPIND_1BYTE		0x1
#define	SI_BPIND_2BYTE		0x3
#define	SI_BPIND_4BYTE		0xF

#define GET_GCI_OFFSET(sih, gci_reg)	\
	(AOB_ENAB(sih)? OFFSETOF(gciregs_t, gci_reg) : OFFSETOF(chipcregs_t, gci_reg))

#define GET_GCI_CORE(sih)	\
	(AOB_ENAB(sih)? si_findcoreidx(sih, GCI_CORE_ID, 0) : SI_CC_IDX)

#include <osl_decl.h>
/* === exported functions === */
extern si_t *si_attach(uint pcidev, osl_t *osh, volatile void *regs, uint bustype,
                       void *sdh, char **vars, uint *varsz);
extern si_t *si_kattach(osl_t *osh);
extern void si_detach(si_t *sih);
extern volatile void *
si_d11_switch_addrbase(si_t *sih, uint coreunit);
extern uint si_corelist(si_t *sih, uint coreid[]);
extern uint si_coreid(si_t *sih);
extern uint si_flag(si_t *sih);
extern uint si_flag_alt(si_t *sih);
extern uint si_intflag(si_t *sih);
extern uint si_coreidx(si_t *sih);
extern uint si_coreunit(si_t *sih);
extern uint si_corevendor(si_t *sih);
extern uint si_corerev(si_t *sih);
extern uint si_corerev_minor(si_t *sih);
extern void *si_osh(si_t *sih);
extern void si_setosh(si_t *sih, osl_t *osh);
extern int si_backplane_access(si_t *sih, uint addr, uint size,
	uint *val, bool read);
extern uint si_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern uint si_corereg_writeonly(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern uint si_pmu_corereg(si_t *sih, uint32 idx, uint regoff, uint mask, uint val);
extern volatile uint32 *si_corereg_addr(si_t *sih, uint coreidx, uint regoff);
extern volatile void *si_coreregs(si_t *sih);
extern uint si_wrapperreg(si_t *sih, uint32 offset, uint32 mask, uint32 val);
extern uint si_core_wrapperreg(si_t *sih, uint32 coreidx, uint32 offset, uint32 mask, uint32 val);
extern void *si_wrapperregs(si_t *sih);
extern uint32 si_core_cflags(si_t *sih, uint32 mask, uint32 val);
extern void si_core_cflags_wo(si_t *sih, uint32 mask, uint32 val);
extern uint32 si_core_sflags(si_t *sih, uint32 mask, uint32 val);
extern void si_commit(si_t *sih);
extern bool si_iscoreup(si_t *sih);
extern uint si_numcoreunits(si_t *sih, uint coreid);
extern uint si_numd11coreunits(si_t *sih);
extern uint si_findcoreidx(si_t *sih, uint coreid, uint coreunit);
extern volatile void *si_setcoreidx(si_t *sih, uint coreidx);
extern volatile void *si_setcore(si_t *sih, uint coreid, uint coreunit);
extern uint32 si_oobr_baseaddr(si_t *sih, bool second);
extern volatile void *si_switch_core(si_t *sih, uint coreid, uint *origidx, uint *intr_val);
extern void si_restore_core(si_t *sih, uint coreid, uint intr_val);
extern int si_numaddrspaces(si_t *sih);
extern uint32 si_addrspace(si_t *sih, uint spidx, uint baidx);
extern uint32 si_addrspacesize(si_t *sih, uint spidx, uint baidx);
extern void si_coreaddrspaceX(si_t *sih, uint asidx, uint32 *addr, uint32 *size);
extern int si_corebist(si_t *sih);
extern void si_core_reset(si_t *sih, uint32 bits, uint32 resetbits);
extern void si_core_disable(si_t *sih, uint32 bits);
extern uint32 si_clock_rate(uint32 pll_type, uint32 n, uint32 m);
extern uint si_chip_hostif(si_t *sih);
extern uint32 si_clock(si_t *sih);
extern uint32 si_alp_clock(si_t *sih); /* returns [Hz] units */
extern uint32 si_ilp_clock(si_t *sih); /* returns [Hz] units */
extern void si_pci_setup(si_t *sih, uint coremask);
extern void si_pcmcia_init(si_t *sih);
extern void si_setint(si_t *sih, int siflag);
extern bool si_backplane64(si_t *sih);
extern void si_register_intr_callback(si_t *sih, void *intrsoff_fn, void *intrsrestore_fn,
	void *intrsenabled_fn, void *intr_arg);
extern void si_deregister_intr_callback(si_t *sih);
extern void si_clkctl_init(si_t *sih);
extern uint16 si_clkctl_fast_pwrup_delay(si_t *sih);
extern bool si_clkctl_cc(si_t *sih, uint mode);
extern int si_clkctl_xtal(si_t *sih, uint what, bool on);
extern uint32 si_gpiotimerval(si_t *sih, uint32 mask, uint32 val);
extern void si_btcgpiowar(si_t *sih);
extern bool si_deviceremoved(si_t *sih);
extern void si_set_device_removed(si_t *sih, bool status);
extern uint32 si_sysmem_size(si_t *sih);
extern uint32 si_socram_size(si_t *sih);
extern uint32 si_socdevram_size(si_t *sih);
extern uint32 si_socram_srmem_size(si_t *sih);
extern void si_socram_set_bankpda(si_t *sih, uint32 bankidx, uint32 bankpda);
extern void si_socdevram(si_t *sih, bool set, uint8 *ennable, uint8 *protect, uint8 *remap);
extern bool si_socdevram_pkg(si_t *sih);
extern bool si_socdevram_remap_isenb(si_t *sih);
extern uint32 si_socdevram_remap_size(si_t *sih);

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
extern uint32 si_gpioreserve(si_t *sih, uint32 gpio_num, uint8 priority);
extern uint32 si_gpiorelease(si_t *sih, uint32 gpio_num, uint8 priority);
extern uint32 si_gpiopull(si_t *sih, bool updown, uint32 mask, uint32 val);
extern uint32 si_gpioevent(si_t *sih, uint regtype, uint32 mask, uint32 val);
extern uint32 si_gpio_int_enable(si_t *sih, bool enable);
extern void si_gci_uart_init(si_t *sih, osl_t *osh, uint8 seci_mode);
extern void si_gci_enable_gpio(si_t *sih, uint8 gpio, uint32 mask, uint32 value);
extern uint8 si_gci_host_wake_gpio_init(si_t *sih);
extern uint8 si_gci_time_sync_gpio_init(si_t *sih);
extern void si_gci_host_wake_gpio_enable(si_t *sih, uint8 gpio, bool state);
extern void si_gci_time_sync_gpio_enable(si_t *sih, uint8 gpio, bool state);

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

extern uint8 si_gci_gpio_status(si_t *sih, uint8 gci_gpio, uint8 mask, uint8 value);
extern void si_gci_config_wake_pin(si_t *sih, uint8 gpio_n, uint8 wake_events,
	bool gci_gpio);
extern void si_gci_free_wake_pin(si_t *sih, uint8 gpio_n);

/* Wake-on-wireless-LAN (WOWL) */
extern bool si_pci_pmecap(si_t *sih);
extern bool si_pci_fastpmecap(struct osl_info *osh);
extern bool si_pci_pmestat(si_t *sih);
extern void si_pci_pmeclr(si_t *sih);
extern void si_pci_pmeen(si_t *sih);
extern void si_pci_pmestatclr(si_t *sih);
extern uint si_pcie_readreg(void *sih, uint addrtype, uint offset);
extern uint si_pcie_writereg(void *sih, uint addrtype, uint offset, uint val);
extern void si_deepsleep_count(si_t *sih, bool arm_wakeup);

#ifdef BCMSDIO
extern void si_sdio_init(si_t *sih);
extern void *si_get_sdio_addrbase(void *sdh);
#endif // endif

extern uint16 si_d11_devid(si_t *sih);
extern int si_corepciid(si_t *sih, uint func, uint16 *pcivendor, uint16 *pcidevice,
	uint8 *pciclass, uint8 *pcisubclass, uint8 *pciprogif, uint8 *pciheader);

extern uint32 si_seci_access(si_t *sih, uint32 val, int access);
extern volatile void* si_seci_init(si_t *sih, uint8 seci_mode);
extern void si_seci_clk_force(si_t *sih, bool val);
extern bool si_seci_clk_force_status(si_t *sih);

#define si_eci(sih) 0
static INLINE void * si_eci_init(si_t *sih) {return NULL;}
#define si_eci_notify_bt(sih, type, val)  (0)
#define si_seci(sih) 0
#define si_seci_upd(sih, a)	do {} while (0)
static INLINE void * si_gci_init(si_t *sih) {return NULL;}
#define si_seci_down(sih) do {} while (0)
#define si_gci(sih) 0

/* OTP status */
extern bool si_is_otp_disabled(si_t *sih);
extern bool si_is_otp_powered(si_t *sih);
extern void si_otp_power(si_t *sih, bool on, uint32* min_res_mask);

/* SPROM availability */
extern bool si_is_sprom_available(si_t *sih);

/* OTP/SROM CIS stuff */
extern int si_cis_source(si_t *sih);
#define CIS_DEFAULT	0
#define CIS_SROM	1
#define CIS_OTP		2

/* Fab-id information */
#define	DEFAULT_FAB	0x0	/**< Original/first fab used for this chip */
#define	CSM_FAB7	0x1	/**< CSM Fab7 chip */
#define	TSMC_FAB12	0x2	/**< TSMC Fab12/Fab14 chip */
#define	SMIC_FAB4	0x3	/**< SMIC Fab4 chip */

extern uint16 si_fabid(si_t *sih);
extern uint16 si_chipid(si_t *sih);

/*
 * Build device path. Path size must be >= SI_DEVPATH_BUFSZ.
 * The returned path is NULL terminated and has trailing '/'.
 * Return 0 on success, nonzero otherwise.
 */
extern int si_devpath(si_t *sih, char *path, int size);
extern int si_devpath_pcie(si_t *sih, char *path, int size);
/* Read variable with prepending the devpath to the name */
extern char *si_getdevpathvar(si_t *sih, const char *name);
extern int si_getdevpathintvar(si_t *sih, const char *name);
extern char *si_coded_devpathvar(si_t *sih, char *varname, int var_len, const char *name);

extern uint8 si_pcieclkreq(si_t *sih, uint32 mask, uint32 val);
extern uint32 si_pcielcreg(si_t *sih, uint32 mask, uint32 val);
extern uint8 si_pcieltrenable(si_t *sih, uint32 mask, uint32 val);
extern uint8 si_pcieobffenable(si_t *sih, uint32 mask, uint32 val);
extern uint32 si_pcieltr_reg(si_t *sih, uint32 reg, uint32 mask, uint32 val);
extern uint32 si_pcieltrspacing_reg(si_t *sih, uint32 mask, uint32 val);
extern uint32 si_pcieltrhysteresiscnt_reg(si_t *sih, uint32 mask, uint32 val);
extern void si_pcie_set_error_injection(si_t *sih, uint32 mode);
extern void si_pcie_set_L1substate(si_t *sih, uint32 substate);
extern uint32 si_pcie_get_L1substate(si_t *sih);
extern void si_war42780_clkreq(si_t *sih, bool clkreq);
extern void si_pci_down(si_t *sih);
extern void si_pci_up(si_t *sih);
extern void si_pci_sleep(si_t *sih);
extern void si_pcie_war_ovr_update(si_t *sih, uint8 aspm);
extern void si_pcie_power_save_enable(si_t *sih, bool enable);
extern void si_pcie_extendL1timer(si_t *sih, bool extend);
extern int si_pci_fixcfg(si_t *sih);
extern void si_chippkg_set(si_t *sih, uint);
extern bool si_is_warmboot(void);

extern void si_chipcontrl_restore(si_t *sih, uint32 val);
extern uint32 si_chipcontrl_read(si_t *sih);
extern void si_chipcontrl_srom4360(si_t *sih, bool on);
extern void si_srom_clk_set(si_t *sih); /**< for chips with fast BP clock */
extern void si_btc_enable_chipcontrol(si_t *sih);
extern void si_pmu_avb_clk_set(si_t *sih, osl_t *osh, bool set_flag);
/* === debug routines === */

extern bool si_taclear(si_t *sih, bool details);

#if defined(BCMDBG_PHYDUMP)
struct bcmstrbuf;
extern int si_dump_pcieinfo(si_t *sih, struct bcmstrbuf *b);
extern void si_dump_pmuregs(si_t *sih, struct bcmstrbuf *b);
extern int si_dump_pcieregs(si_t *sih, struct bcmstrbuf *b);
#endif // endif

#if defined(BCMDBG_PHYDUMP)
extern void si_dumpregs(si_t *sih, struct bcmstrbuf *b);
#endif // endif

extern uint32 si_ccreg(si_t *sih, uint32 offset, uint32 mask, uint32 val);
extern uint32 si_pciereg(si_t *sih, uint32 offset, uint32 mask, uint32 val, uint type);
extern int si_bpind_access(si_t *sih, uint32 addr_high, uint32 addr_low,
	int32* data, bool read);
#ifdef SR_DEBUG
extern void si_dump_pmu(si_t *sih, void *pmu_var);
extern void si_pmu_keep_on(si_t *sih, int32 int_val);
extern uint32 si_pmu_keep_on_get(si_t *sih);
extern uint32 si_power_island_set(si_t *sih, uint32 int_val);
extern uint32 si_power_island_get(si_t *sih);
#endif /* SR_DEBUG */
extern uint32 si_pcieserdesreg(si_t *sih, uint32 mdioslave, uint32 offset, uint32 mask, uint32 val);
extern void si_pcie_set_request_size(si_t *sih, uint16 size);
extern uint16 si_pcie_get_request_size(si_t *sih);
extern void si_pcie_set_maxpayload_size(si_t *sih, uint16 size);
extern uint16 si_pcie_get_maxpayload_size(si_t *sih);
extern uint16 si_pcie_get_ssid(si_t *sih);
extern uint32 si_pcie_get_bar0(si_t *sih);
extern int si_pcie_configspace_cache(si_t *sih);
extern int si_pcie_configspace_restore(si_t *sih);
extern int si_pcie_configspace_get(si_t *sih, uint8 *buf, uint size);

#ifdef BCM_BACKPLANE_TIMEOUT
extern const si_axi_error_info_t * si_get_axi_errlog_info(si_t *sih);
extern void si_reset_axi_errlog_info(si_t * sih);
#endif /* BCM_BACKPLANE_TIMEOUT */

extern void si_update_backplane_timeouts(si_t *sih, bool enable, uint32 timeout, uint32 cid);

extern uint32 si_tcm_size(si_t *sih);
extern bool si_has_flops(si_t *sih);

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
extern uint32 si_pcie_set_ctrlreg(si_t *sih, uint32 sperst_mask, uint32 spert_val);
extern void si_pcie_ltr_war(si_t *sih);
extern void si_pcie_hw_LTR_war(si_t *sih);
extern void si_pcie_hw_L1SS_war(si_t *sih);
extern void si_pciedev_crwlpciegen2(si_t *sih);
extern void si_pcie_prep_D3(si_t *sih, bool enter_D3);
extern void si_pciedev_reg_pm_clk_period(si_t *sih);
extern void si_d11rsdb_core1_alt_reg_clk_dis(si_t *sih);
extern void si_d11rsdb_core1_alt_reg_clk_en(si_t *sih);
extern void si_pcie_disable_oobselltr(si_t *sih);
extern uint32 si_raw_reg(si_t *sih, uint32 reg, uint32 val, uint32 wrire_req);

#ifdef WLRSDB
extern void si_d11rsdb_core_disable(si_t *sih, uint32 bits);
extern void si_d11rsdb_core_reset(si_t *sih, uint32 bits, uint32 resetbits);
extern void set_secondary_d11_core(si_t *sih, volatile void **secmap, volatile void **secwrap);
#endif // endif

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

/* Used only for the regs present in the pmu core and not present in the old cc core */
#define PMU_REG_NEW(si, member, mask, val) \
		si_corereg(si, si_findcoreidx(si, PMU_CORE_ID, 0), \
			OFFSETOF(pmuregs_t, member), mask, val)

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

/* BT SMEM Control Register 0 */
#define GCI_BT_SMEM_CTRL0_SUBCORE_ENABLE_PKILL	(1 << 28)

/* End - GCI Macros */

#define AXI_OOB		0x7

extern void si_pll_sr_reinit(si_t *sih);
extern void si_pll_closeloop(si_t *sih);
void si_config_4364_d11_oob(si_t *sih, uint coreid);
extern void si_gci_set_femctrl(si_t *sih, osl_t *osh, bool set);
extern void si_gci_set_femctrl_mask_ant01(si_t *sih, osl_t *osh, bool set);
extern uint si_num_slaveports(si_t *sih, uint coreid);
extern uint32 si_get_slaveport_addr(si_t *sih, uint spidx, uint baidx,
	uint core_id, uint coreunit);
extern uint32 si_get_d11_slaveport_addr(si_t *sih, uint spidx,
	uint baidx, uint coreunit);
uint si_introff(si_t *sih);
void si_intrrestore(si_t *sih, uint intr_val);
void si_nvram_res_masks(si_t *sih, uint32 *min_mask, uint32 *max_mask);
extern uint32 si_xtalfreq(si_t *sih);
extern uint8 si_getspurmode(si_t *sih);
extern uint32 si_get_openloop_dco_code(si_t *sih);
extern void si_set_openloop_dco_code(si_t *sih, uint32 openloop_dco_code);
extern uint32 si_wrapper_dump_buf_size(si_t *sih);
extern uint32 si_wrapper_dump_binary(si_t *sih, uchar *p);
extern uint32 si_wrapper_dump_last_timeout(si_t *sih, uint32 *error, uint32 *core, uint32 *ba,
	uchar *p);

/* SR Power Control */
extern uint32 si_srpwr_request(si_t *sih, uint32 mask, uint32 val);
extern uint32 si_srpwr_stat_spinwait(si_t *sih, uint32 mask, uint32 val);
extern uint32 si_srpwr_stat(si_t *sih);
extern uint32 si_srpwr_domain(si_t *sih);
extern uint32 si_srpwr_domain_all_mask(si_t *sih);

/* SR Power Control */
	/* No capabilities bit so using chipid for now */
#define SRPWR_CAP(sih)  (BCM4347_CHIP(sih->chip) || BCM4369_CHIP(sih->chip))

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
#ifdef CHIPS_CUSTOMER_HW6
#define MULTIBP_CAP(sih)	(BCM4368_CHIP(sih->chip) || BCM4378_CHIP(sih->chip) || \
				BCM4387_CHIP(sih->chip))
#else /* !CHIPS_CUSTOMER_HW6 */
#define MULTIBP_CAP(sih)	(FALSE)
#endif /* CHIPS_CUSTOMER_HW6 */
#define MULTIBP_ENAB(sih)      ((sih) && (sih)->_multibp_enable)

uint32 si_enum_base(uint devid);
uint32 si_pcie_enum_base(uint devid);

extern uint8 si_lhl_ps_mode(si_t *sih);

#ifdef UART_TRAP_DBG
void ai_dump_APB_Bridge_registers(si_t *sih);
#endif /* UART_TRAP_DBG */

void si_clrirq_idx(si_t *sih, uint core_idx);

/* return if scan core is present */
bool si_scan_core_present(si_t *sih);

#endif	/* _siutils_h_ */
