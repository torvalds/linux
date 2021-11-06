/*
 * Include file private to the SOC Interconnect support files.
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

#ifndef	_siutils_priv_h_
#define	_siutils_priv_h_

#if defined(BCMDBG_ERR) && defined(ERR_USE_LOG_EVENT)
#define	SI_ERROR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_SI_ERROR, args)
#elif defined(BCMDBG_ERR) || defined(SI_ERROR_ENFORCE)
#define	SI_ERROR(args)	printf args
#else
#define	SI_ERROR(args)
#endif	/* BCMDBG_ERR */

#if defined(ENABLE_CORECAPTURE)

#if !defined(BCMDBG)
#define	SI_PRINT(args)	osl_wificc_logDebug args
#else
#define	SI_PRINT(args)	printf args
#endif /* !BCMDBG */

#else

#define	SI_PRINT(args)	printf args

#endif /* ENABLE_CORECAPTURE */

#ifdef BCMDBG
#define	SI_MSG(args)	printf args
#else
#define	SI_MSG(args)
#endif	/* BCMDBG */

#ifdef BCMDBG_SI
#define	SI_VMSG(args)	printf args
#else
#define	SI_VMSG(args)
#endif

#define	IS_SIM(chippkg)	((chippkg == HDLSIM_PKG_ID) || (chippkg == HWSIM_PKG_ID))

typedef void (*si_intrsoff_t)(void *intr_arg, bcm_int_bitmask_t *mask);
typedef void (*si_intrsrestore_t)(void *intr_arg, bcm_int_bitmask_t *mask);
typedef bool (*si_intrsenabled_t)(void *intr_arg);

#define SI_GPIO_MAX		16

typedef struct gci_gpio_item {
	void			*arg;
	uint8			gci_gpio;
	uint8			status;
	gci_gpio_handler_t	handler;
	struct gci_gpio_item	*next;
} gci_gpio_item_t;

typedef struct wci2_cbs {
    void *context;
    wci2_handler_t handler;
} wci2_cbs_t;

typedef struct wci2_rxfifo_info {
	char *rx_buf;
	int	rx_idx;
	wci2_cbs_t	*cbs;
} wci2_rxfifo_info_t;

#define AI_SLAVE_WRAPPER        0
#define AI_MASTER_WRAPPER       1

typedef struct axi_wrapper {
	uint32  mfg;
	uint32  cid;
	uint32  rev;
	uint32  wrapper_type;
	uint32  wrapper_addr;
	uint32  wrapper_size;
	uint32  node_type;
} axi_wrapper_t;

#ifdef SOCI_NCI_BUS
#define SI_MAX_AXI_WRAPPERS		65u
#else
#define SI_MAX_AXI_WRAPPERS		32u
#endif /* SOCI_NCI_BUS */
#define AI_REG_READ_TIMEOUT		300u /* in msec */

/* for some combo chips, BT side accesses chipcommon->0x190, as a 16 byte addr */
/* register at 0x19C doesn't exist, so error is logged at the slave wrapper */
/* Since this can't be fixed in the boot rom, WAR it */
#define BT_CC_SPROM_BADREG_LO   0x18000190
#define BT_CC_SPROM_BADREG_SIZE 4
#define BT_CC_SPROM_BADREG_HI   0

#define BCM4389_BT_AXI_ID	2
#define BCM4388_BT_AXI_ID	2
#define BCM4369_BT_AXI_ID	4
#define BCM4378_BT_AXI_ID	2
#define BCM43602_BT_AXI_ID	1
#define BCM4378_ARM_PREFETCH_AXI_ID     9

#define BCM4378_BT_ADDR_HI	0
#define BCM4378_BT_ADDR_LO	0x19000000	/* BT address space */
#define BCM4378_BT_SIZE		0x01000000	/* BT address space size */
#define BCM4378_UNUSED_AXI_ID	0xffffffff
#define BCM4378_CC_AXI_ID	0
#define BCM4378_PCIE_AXI_ID	1

#define BCM4387_BT_ADDR_HI	0
#define BCM4387_BT_ADDR_LO	0x19000000	/* BT address space */
#define BCM4387_BT_SIZE		0x01000000	/* BT address space size */
#define BCM4387_UNUSED_AXI_ID	0xffffffff
#define BCM4387_CC_AXI_ID	0
#define BCM4387_PCIE_AXI_ID	1

#define BCM_AXI_ID_MASK	0xFu
#define BCM_AXI_ACCESS_TYPE_MASK 0xF0u

#define BCM43xx_CR4_AXI_ID	3
#define BCM43xx_AXI_ACCESS_TYPE_PREFETCH (1 << 4)

typedef struct si_cores_info {
	volatile void	*regs[SI_MAXCORES];	/* other regs va */

	uint	coreid[SI_MAXCORES];	/**< id of each core */
	uint32	coresba[SI_MAXCORES];	/**< backplane address of each core */
	void	*regs2[SI_MAXCORES];	/**< va of each core second register set (usbh20) */
	uint32	coresba2[SI_MAXCORES];	/**< address of each core second register set (usbh20) */
	uint32	coresba_size[SI_MAXCORES]; /**< backplane address space size */
	uint32	coresba2_size[SI_MAXCORES]; /**< second address space size */

	void	*wrappers[SI_MAXCORES];	/**< other cores wrapper va */
	uint32	wrapba[SI_MAXCORES];	/**< address of controlling wrapper */

	void	*wrappers2[SI_MAXCORES];	/**< other cores wrapper va */
	uint32	wrapba2[SI_MAXCORES];	/**< address of controlling wrapper */

	void	*wrappers3[SI_MAXCORES];	/**< other cores wrapper va */
	uint32	wrapba3[SI_MAXCORES];	/**< address of controlling wrapper */

	uint32	cia[SI_MAXCORES];	/**< erom cia entry for each core */
	uint32	cib[SI_MAXCORES];	/**< erom cia entry for each core */

	uint32  csp2ba[SI_MAXCORES];		/**< Second slave port base addr 0 */
	uint32  csp2ba_size[SI_MAXCORES];	/**< Second slave port addr space size */
} si_cores_info_t;

#define RES_PEND_STATS_COUNT	8

typedef struct res_state_info
{
	uint32 low;
	uint32 low_time;
	uint32 high;
	uint32 high_time;
} si_res_state_info_t;

/** misc si info needed by some of the routines */
typedef struct si_info {
	struct si_pub pub;		/**< back plane public state (must be first field) */

	void	*osh;			/**< osl os handle */
	void	*sdh;			/**< bcmsdh handle */

	uint	dev_coreid;		/**< the core provides driver functions */
	void	*intr_arg;		/**< interrupt callback function arg */
	si_intrsoff_t intrsoff_fn;	/**< turns chip interrupts off */
	si_intrsrestore_t intrsrestore_fn; /**< restore chip interrupts */
	si_intrsenabled_t intrsenabled_fn; /**< check if interrupts are enabled */

	void	*pch;			/**< PCI/E core handle */

	bool	memseg;			/**< flag to toggle MEM_SEG register */

	char	*vars;
	uint	varsz;

	volatile void *curmap;		/* current regs va */

	uint	curidx;			/**< current core index */
	uint	numcores;		/**< # discovered cores */

	void	*curwrap;		/**< current wrapper va */

	uint32	oob_router;		/**< oob router registers for axi */
	uint32	oob_router1;		/**< oob router registers for axi */

	si_cores_info_t *cores_info;
#if !defined(BCMDONGLEHOST)
	/* Store NVRAM data so that it is available after reclaim. */
	uint32 nvram_min_mask;
	bool min_mask_valid;
	uint32 nvram_max_mask;
	bool max_mask_valid;
#endif /* !BCMDONGLEHOST */
	gci_gpio_item_t	*gci_gpio_head;	/**< gci gpio interrupts head */
	uint	chipnew;		/**< new chip number */
	uint second_bar0win;		/**< Backplane region */
	uint	num_br;			/**< # discovered bridges */
	uint32	br_wrapba[SI_MAXBR];	/**< address of bridge controlling wrapper */
	uint32	xtalfreq;
	uint32	openloop_dco_code;	/**< OPEN loop calibration dco code */
	uint8	spurmode;
	bool	device_removed;
	uint	axi_num_wrappers;
	axi_wrapper_t *axi_wrapper;
	uint8	device_wake_opt;	/* device_wake GPIO number */
	uint8	lhl_ps_mode;
	uint8	hib_ext_wakeup_enab;
	uint32  armpllclkfreq;             /**< arm clock rate from nvram */
	uint32	ccidiv;			/**< arm clock : cci clock ratio
					 * (determines sysmem frequency)
					 */
	wci2_rxfifo_info_t *wci2_info;	/* wci2_rxfifo interrupt info */
	uint8	slice;			/* this instance of the si accesses
					 * the first(0)/second(1)/...
					 * d11 core
					 */
	si_res_state_info_t res_state[RES_PEND_STATS_COUNT];
	uint32	res_pend_count;
	bool    rfldo3p3_war;		/**< singing cap war enable from nvram */
	void    *nci_info;
} si_info_t;

#define	SI_INFO(sih)	((si_info_t *)(uintptr)sih)

#define	GOODCOREADDR(x, b) (((x) >= (b)) && ((x) < ((b) + SI_MAXCORES * SI_CORE_SIZE)) && \
		ISALIGNED((x), SI_CORE_SIZE))
#define	GOODREGS(regs)	((regs) != NULL && ISALIGNED((uintptr)(regs), SI_CORE_SIZE))
#define BADCOREADDR	0
#define	GOODIDX(idx, maxcores)	(((uint)idx) < maxcores)
#define	NOREV		(int16)-1		/**< Invalid rev */

#define PCI(si)		((BUSTYPE((si)->pub.bustype) == PCI_BUS) &&	\
			 ((si)->pub.buscoretype == PCI_CORE_ID))

#define PCIE_GEN1(si)	((BUSTYPE((si)->pub.bustype) == PCI_BUS) &&	\
			 ((si)->pub.buscoretype == PCIE_CORE_ID))

#define PCIE_GEN2(si)	((BUSTYPE((si)->pub.bustype) == PCI_BUS) &&	\
			 ((si)->pub.buscoretype == PCIE2_CORE_ID))

#define PCIE(si)	(PCIE_GEN1(si) || PCIE_GEN2(si))

/** Newer chips can access PCI/PCIE and CC core without requiring to change PCI BAR0 WIN */
#define SI_FAST(si) (PCIE(si) || (PCI(si) && ((si)->pub.buscorerev >= 13)))

#define CCREGS_FAST(si) \
	(((si)->curmap == NULL) ? NULL : \
	    ((volatile char *)((si)->curmap) + PCI_16KB0_CCREGS_OFFSET))
#define PCIEREGS(si) (((volatile char *)((si)->curmap) + PCI_16KB0_PCIREGS_OFFSET))

/*
 * Macros to disable/restore function core(D11, ENET, ILINE20, etc) interrupts before/
 * after core switching to avoid invalid register accesss inside ISR.
 * Adding SOCI_NCI_BUS to avoid abandons in the branches that use this MACRO.
 */
#ifdef SOCI_NCI_BUS
#define INTR_OFF(si, intr_val) \
	if ((si)->intrsoff_fn && (si_coreid(&(si)->pub) == (si)->dev_coreid)) { \
		(*(si)->intrsoff_fn)((si)->intr_arg, intr_val); }
#define INTR_RESTORE(si, intr_val) \
	if ((si)->intrsrestore_fn && (si_coreid(&(si)->pub) == (si)->dev_coreid)) { \
		(*(si)->intrsrestore_fn)((si)->intr_arg, intr_val); }
#else
#define INTR_OFF(si, intr_val) \
	if ((si)->intrsoff_fn && (si)->cores_info->coreid[(si)->curidx] == (si)->dev_coreid) { \
		(*(si)->intrsoff_fn)((si)->intr_arg, intr_val); }
#define INTR_RESTORE(si, intr_val) \
	if ((si)->intrsrestore_fn && (si)->cores_info->coreid[(si)->curidx] == (si)->dev_coreid) { \
		(*(si)->intrsrestore_fn)((si)->intr_arg, intr_val); }
#endif /* SOCI_NCI_BUS */

/* dynamic clock control defines */
#define	LPOMINFREQ		25000		/**< low power oscillator min */
#define	LPOMAXFREQ		43000		/**< low power oscillator max */
#define	XTALMINFREQ		19800000	/**< 20 MHz - 1% */
#define	XTALMAXFREQ		20200000	/**< 20 MHz + 1% */
#define	PCIMINFREQ		25000000	/**< 25 MHz */
#define	PCIMAXFREQ		34000000	/**< 33 MHz + fudge */

#define	ILP_DIV_5MHZ		0		/**< ILP = 5 MHz */
#define	ILP_DIV_1MHZ		4		/**< ILP = 1 MHz */

/* GPIO Based LED powersave defines */
#define DEFAULT_GPIO_ONTIME	10		/**< Default: 10% on */
#define DEFAULT_GPIO_OFFTIME	90		/**< Default: 10% on */

#ifndef DEFAULT_GPIOTIMERVAL
#define DEFAULT_GPIOTIMERVAL  ((DEFAULT_GPIO_ONTIME << GPIO_ONTIME_SHIFT) | DEFAULT_GPIO_OFFTIME)
#endif

/* Silicon Backplane externs */
extern void sb_scan(si_t *sih, volatile void *regs, uint devid);
extern uint sb_coreid(const si_t *sih);
extern uint sb_intflag(si_t *sih);
extern uint sb_flag(const si_t *sih);
extern void sb_setint(const si_t *sih, int siflag);
extern uint sb_corevendor(const si_t *sih);
extern uint sb_corerev(const si_t *sih);
extern uint sb_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern volatile uint32 *sb_corereg_addr(const si_t *sih, uint coreidx, uint regoff);
extern bool sb_iscoreup(const si_t *sih);
extern volatile void *sb_setcoreidx(si_t *sih, uint coreidx);
extern uint32 sb_core_cflags(const si_t *sih, uint32 mask, uint32 val);
extern void sb_core_cflags_wo(const si_t *sih, uint32 mask, uint32 val);
extern uint32 sb_core_sflags(const si_t *sih, uint32 mask, uint32 val);
extern void sb_commit(si_t *sih);
extern uint32 sb_base(uint32 admatch);
extern uint32 sb_size(uint32 admatch);
extern void sb_core_reset(const si_t *sih, uint32 bits, uint32 resetbits);
extern void sb_core_disable(const si_t *sih, uint32 bits);
extern uint32 sb_addrspace(const si_t *sih, uint asidx);
extern uint32 sb_addrspacesize(const si_t *sih, uint asidx);
extern int sb_numaddrspaces(const si_t *sih);

extern bool sb_taclear(si_t *sih, bool details);

#ifdef BCMDBG
extern void sb_view(si_t *sih, bool verbose);
extern void sb_viewall(si_t *sih, bool verbose);
#endif
#if defined(BCMDBG) || defined(BCMDBG_DUMP)
extern void sb_dump(si_t *sih, struct bcmstrbuf *b);
#endif
#if defined(BCMDBG) || defined(BCMDBG_DUMP)|| defined(BCMDBG_PHYDUMP)
extern void sb_dumpregs(si_t *sih, struct bcmstrbuf *b);
#endif /* BCMDBG || BCMDBG_DUMP|| BCMDBG_PHYDUMP */

/* AMBA Interconnect exported externs */
extern si_t *ai_attach(uint pcidev, osl_t *osh, void *regs, uint bustype,
                       void *sdh, char **vars, uint *varsz);
extern si_t *ai_kattach(osl_t *osh);
extern void ai_scan(si_t *sih, void *regs, uint devid);

extern uint ai_flag(si_t *sih);
extern uint ai_flag_alt(const si_t *sih);
extern void ai_setint(const si_t *sih, int siflag);
extern uint ai_corevendor(const si_t *sih);
extern uint ai_corerev(const si_t *sih);
extern uint ai_corerev_minor(const si_t *sih);
extern volatile uint32 *ai_corereg_addr(si_t *sih, uint coreidx, uint regoff);
extern bool ai_iscoreup(const si_t *sih);
extern volatile void *ai_setcoreidx(si_t *sih, uint coreidx);
extern volatile void *ai_setcoreidx_2ndwrap(si_t *sih, uint coreidx);
extern volatile void *ai_setcoreidx_3rdwrap(si_t *sih, uint coreidx);
extern uint32 ai_core_cflags(const si_t *sih, uint32 mask, uint32 val);
extern void ai_core_cflags_wo(const si_t *sih, uint32 mask, uint32 val);
extern uint32 ai_core_sflags(const si_t *sih, uint32 mask, uint32 val);
extern uint ai_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern uint ai_corereg_writeonly(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern void ai_core_reset(si_t *sih, uint32 bits, uint32 resetbits);
extern void ai_d11rsdb_core_reset(si_t *sih, uint32 bits,
	uint32 resetbits, void *p, volatile void *s);
extern void ai_core_disable(const si_t *sih, uint32 bits);
extern void ai_d11rsdb_core_disable(const si_info_t *sii, uint32 bits,
	aidmp_t *pmacai, aidmp_t *smacai);
extern int ai_numaddrspaces(const si_t *sih);
extern uint32 ai_addrspace(const si_t *sih, uint spidx, uint baidx);
extern uint32 ai_addrspacesize(const si_t *sih, uint spidx, uint baidx);
extern void ai_coreaddrspaceX(const si_t *sih, uint asidx, uint32 *addr, uint32 *size);
extern uint ai_wrap_reg(const si_t *sih, uint32 offset, uint32 mask, uint32 val);
extern void ai_update_backplane_timeouts(const si_t *sih, bool enable, uint32 timeout, uint32 cid);
extern uint32 ai_clear_backplane_to(si_t *sih);
void ai_force_clocks(const si_t *sih, uint clock_state);
extern uint ai_num_slaveports(const si_t *sih, uint coreidx);

#ifdef AXI_TIMEOUTS_NIC
uint32 ai_clear_backplane_to_fast(si_t *sih, void * addr);
#endif /* AXI_TIMEOUTS_NIC */

#ifdef BOOKER_NIC400_INF
extern void ai_core_reset_ext(const si_t *sih, uint32 bits, uint32 resetbits);
#endif /* BOOKER_NIC400_INF */

#if defined(AXI_TIMEOUTS) || defined(AXI_TIMEOUTS_NIC)
extern uint32 ai_clear_backplane_to_per_core(si_t *sih, uint coreid, uint coreunit, void * wrap);
#endif /* AXI_TIMEOUTS || AXI_TIMEOUTS_NIC */

#ifdef BCMDBG
extern void ai_view(const si_t *sih, bool verbose);
extern void ai_viewall(si_t *sih, bool verbose);
#endif
#if defined(BCMDBG) || defined(BCMDBG_DUMP)|| defined(BCMDBG_PHYDUMP)
extern void ai_dumpregs(const si_t *sih, struct bcmstrbuf *b);
#endif /* BCMDBG || BCMDBG_DUMP|| BCMDBG_PHYDUMP */

extern uint32 ai_wrapper_dump_buf_size(const si_t *sih);
extern uint32 ai_wrapper_dump_binary(const si_t *sih, uchar *p);
extern bool ai_check_enable_backplane_log(const si_t *sih);
extern uint32 ai_wrapper_dump_last_timeout(const si_t *sih, uint32 *error, uint32 *core,
	uint32 *ba, uchar *p);
extern uint32 ai_findcoreidx_by_axiid(const si_t *sih, uint32 axiid);
#if defined(AXI_TIMEOUTS_NIC) || defined(AXI_TIMEOUTS)
extern void ai_wrapper_get_last_error(const si_t *sih, uint32 *error_status, uint32 *core,
	uint32 *lo, uint32 *hi, uint32 *id);
extern uint32 ai_get_axi_timeout_reg(void);
#endif /* (AXI_TIMEOUTS_NIC) || (AXI_TIMEOUTS) */

#ifdef UART_TRAP_DBG
void ai_dump_APB_Bridge_registers(const si_t *sih);
#endif /* UART_TRAP_DBG */
void ai_force_clocks(const si_t *sih, uint clock_state);

#define ub_scan(a, b, c) do {} while (0)
#define ub_flag(a) (0)
#define ub_setint(a, b) do {} while (0)
#define ub_coreidx(a) (0)
#define ub_corevendor(a) (0)
#define ub_corerev(a) (0)
#define ub_iscoreup(a) (0)
#define ub_setcoreidx(a, b) (0)
#define ub_core_cflags(a, b, c) (0)
#define ub_core_cflags_wo(a, b, c) do {} while (0)
#define ub_core_sflags(a, b, c) (0)
#define ub_corereg(a, b, c, d, e) (0)
#define ub_core_reset(a, b, c) do {} while (0)
#define ub_core_disable(a, b) do {} while (0)
#define ub_numaddrspaces(a) (0)
#define ub_addrspace(a, b)  (0)
#define ub_addrspacesize(a, b) (0)
#define ub_view(a, b) do {} while (0)
#define ub_dumpregs(a, b) do {} while (0)

#ifndef SOCI_NCI_BUS
#define nci_uninit(a) do {} while (0)
#define nci_scan(a) (0)
#define nci_dump_erom(a) do {} while (0)
#define nci_init(a, b, c) (NULL)
#define nci_setcore(a, b, c) (NULL)
#define nci_setcoreidx(a, b) (NULL)
#define nci_findcoreidx(a, b, c) (0)
#define nci_corereg_addr(a, b, c) (NULL)
#define nci_corereg_writeonly(a, b, c, d, e) (0)
#define nci_corereg(a, b, c, d, e) (0)
#define nci_corerev_minor(a) (0)
#define nci_corerev(a) (0)
#define nci_corevendor(a) (0)
#define nci_get_wrap_reg(a, b, c, d) (0)
#define nci_core_reset(a, b, c) do {} while (0)
#define nci_core_disable(a, b) do {} while (0)
#define nci_iscoreup(a) (FALSE)
#define nci_coreid(a, b) (0)
#define nci_numcoreunits(a, b) (0)
#define nci_addr_space(a, b, c) (0)
#define nci_addr_space_size(a, b, c) (0)
#define nci_iscoreup(a) (FALSE)
#define nci_intflag(a) (0)
#define nci_flag(a) (0)
#define nci_flag_alt(a) (0)
#define nci_setint(a, b) do {} while (0)
#define nci_oobr_baseaddr(a, b) (0)
#define nci_coreunit(a) (0)
#define nci_corelist(a, b) (0)
#define nci_numaddrspaces(a) (0)
#define nci_addrspace(a, b, c) (0)
#define nci_addrspacesize(a, b, c) (0)
#define nci_coreaddrspaceX(a, b, c, d) do {} while (0)
#define nci_core_cflags(a, b, c) (0)
#define nci_core_cflags_wo(a, b, c) do {} while (0)
#define nci_core_sflags(a, b, c) (0)
#define nci_wrapperreg(a, b, c, d) (0)
#define nci_invalidate_second_bar0win(a) do {} while (0)
#define nci_backplane_access(a, b, c, d, e) (0)
#define nci_backplane_access_64(a, b, c, d, e) (0)
#define nci_num_slaveports(a, b) (0)
#if defined(BCMDBG) || defined(BCMDBG_DUMP) || defined(BCMDBG_PHYDUMP)
#define nci_dumpregs(a, b) do {} while (0)
#endif  /* BCMDBG || BCMDBG_DUMP || BCMDBG_PHYDUMP */
#ifdef BCMDBG
#define nci_view(a, b) do {} while (0)
#define nci_viewall(a, b) do {} while (0)
#endif /* BCMDBG */
#define nci_get_nth_wrapper(a, b) (0)
#define nci_get_axi_addr(a, b) (0)
#define nci_wrapper_dump_binary_one(a, b, c) (NULL)
#define nci_wrapper_dump_binary(a, b) (0)
#define nci_wrapper_dump_last_timeout(a, b, c, d, e) (0)
#define nci_check_enable_backplane_log(a) (FALSE)
#define nci_get_core_baaddr(a, b, c) (0)
#define nci_clear_backplane_to(a) (0)
#define nci_clear_backplane_to_per_core(a, b, c, d) (0)
#define nci_ignore_errlog(a, b, c, d, e, f) (FALSE)
#define nci_wrapper_get_last_error(a, b, c, d, e, f) do {} while (0)
#define nci_get_axi_timeout_reg() (0)
#define nci_findcoreidx_by_axiid(a, b) (0)
#define nci_wrapper_dump_binary_one(a, b, c) (NULL)
#define nci_wrapper_dump_binary(a, b) (0)
#define nci_wrapper_dump_last_timeout(a, b, c, d, e) (0)
#define nci_check_enable_backplane_log(a) (FALSE)
#define nci_wrapper_dump_buf_size(a) (0)
#endif /* SOCI_NCI_BUS */
#endif	/* _siutils_priv_h_ */
