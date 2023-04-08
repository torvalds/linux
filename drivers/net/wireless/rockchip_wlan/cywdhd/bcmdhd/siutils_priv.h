/*
 * Include file private to the SOC Interconnect support files.
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
 * $Id: siutils_priv.h 698933 2017-05-11 06:05:10Z $
 */

#ifndef	_siutils_priv_h_
#define	_siutils_priv_h_

#if defined(SI_ERROR_ENFORCE)
#define	SI_ERROR(args)	printf args
#else
#define	SI_ERROR(args)
#endif // endif

#if defined(ENABLE_CORECAPTURE)

#define	SI_PRINT(args)	osl_wificc_logDebug args

#else

#define	SI_PRINT(args)	printf args

#endif /* ENABLE_CORECAPTURE */

#define	SI_MSG(args)

#ifdef BCMDBG_SI
#define	SI_VMSG(args)	printf args
#else
#define	SI_VMSG(args)
#endif // endif

#define	IS_SIM(chippkg)	((chippkg == HDLSIM_PKG_ID) || (chippkg == HWSIM_PKG_ID))

typedef uint32 (*si_intrsoff_t)(void *intr_arg);
typedef void (*si_intrsrestore_t)(void *intr_arg, uint32 arg);
typedef bool (*si_intrsenabled_t)(void *intr_arg);

#define SI_GPIO_MAX		16

typedef struct gci_gpio_item {
	void			*arg;
	uint8			gci_gpio;
	uint8			status;
	gci_gpio_handler_t	handler;
	struct gci_gpio_item	*next;
} gci_gpio_item_t;

#define AI_SLAVE_WRAPPER        0
#define AI_MASTER_WRAPPER       1

typedef struct axi_wrapper {
	uint32  mfg;
	uint32  cid;
	uint32  rev;
	uint32  wrapper_type;
	uint32  wrapper_addr;
	uint32  wrapper_size;
} axi_wrapper_t;

#define SI_MAX_AXI_WRAPPERS		32
#define AI_REG_READ_TIMEOUT		300 /* in msec */

/* for some combo chips, BT side accesses chipcommon->0x190, as a 16 byte addr */
/* register at 0x19C doesn't exist, so error is logged at the slave wrapper */
#define BT_CC_SPROM_BADREG_LO   0x18000190
#define BT_CC_SPROM_BADREG_SIZE 4
#define BT_CC_SPROM_BADREG_HI   0
#define BCM4350_BT_AXI_ID	6
#define BCM4345_BT_AXI_ID	6
#define BCM4349_BT_AXI_ID	5
#define BCM4364_BT_AXI_ID	5

/* for BT logging and memory dump, ignore failed access to BT memory */
#define BCM4347_BT_ADDR_HI	0
#define BCM4347_BT_ADDR_LO	0x19000000	/* BT address space */
#define BCM4347_BT_SIZE		0x01000000	/* BT address space size */
#define BCM4347_UNUSED_AXI_ID	0xffffffff
#define BCM4347_CC_AXI_ID	0
#define BCM4347_PCIE_AXI_ID	1

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

	void *pch;			/**< PCI/E core handle */

	bool	memseg;			/**< flag to toggle MEM_SEG register */

	char *vars;
	uint varsz;

	volatile void	*curmap;		/* current regs va */

	uint	curidx;			/**< current core index */
	uint	numcores;		/**< # discovered cores */

	void	*curwrap;		/**< current wrapper va */

	uint32	oob_router;		/**< oob router registers for axi */
	uint32	oob_router1;		/**< oob router registers for axi */

	si_cores_info_t *cores_info;
	gci_gpio_item_t	*gci_gpio_head;	/**< gci gpio interrupts head */
	uint	chipnew;		/**< new chip number */
	uint second_bar0win;		/**< Backplane region */
	uint	num_br;		/**< # discovered bridges */
	uint32	br_wrapba[SI_MAXBR];	/**< address of bridge controlling wrapper */
	uint32	xtalfreq;
	uint32	openloop_dco_code;	/**< OPEN loop calibration dco code */
	uint8	spurmode;
	bool	device_removed;
	uint	axi_num_wrappers;
	axi_wrapper_t   * axi_wrapper;
	uint8	device_wake_opt; /* device_wake GPIO number */
	uint8	lhl_ps_mode;
} si_info_t;

#define	SI_INFO(sih)	((si_info_t *)(uintptr)sih)

#define	GOODCOREADDR(x, b) (((x) >= (b)) && ((x) < ((b) + SI_MAXCORES * SI_CORE_SIZE)) && \
		ISALIGNED((x), SI_CORE_SIZE))
#define	GOODREGS(regs)	((regs) != NULL && ISALIGNED((uintptr)(regs), SI_CORE_SIZE))
#define BADCOREADDR	0
#define	GOODIDX(idx)	(((uint)idx) < SI_MAXCORES)
#define	NOREV		-1		/**< Invalid rev */

#define PCI(si)		((BUSTYPE((si)->pub.bustype) == PCI_BUS) &&	\
			 ((si)->pub.buscoretype == PCI_CORE_ID))

#define PCIE_GEN1(si)	((BUSTYPE((si)->pub.bustype) == PCI_BUS) &&	\
			 ((si)->pub.buscoretype == PCIE_CORE_ID))

#define PCIE_GEN2(si)	((BUSTYPE((si)->pub.bustype) == PCI_BUS) &&	\
			 ((si)->pub.buscoretype == PCIE2_CORE_ID))

#define PCIE(si)	(PCIE_GEN1(si) || PCIE_GEN2(si))

#define PCMCIA(si)	((BUSTYPE((si)->pub.bustype) == PCMCIA_BUS) && ((si)->memseg == TRUE))

/** Newer chips can access PCI/PCIE and CC core without requiring to change PCI BAR0 WIN */
#define SI_FAST(si) (PCIE(si) || (PCI(si) && ((si)->pub.buscorerev >= 13)))

#define CCREGS_FAST(si) \
	(((si)->curmap == NULL) ? NULL : \
	    ((volatile char *)((si)->curmap) + PCI_16KB0_CCREGS_OFFSET))
#define PCIEREGS(si) (((volatile char *)((si)->curmap) + PCI_16KB0_PCIREGS_OFFSET))

/*
 * Macros to disable/restore function core(D11, ENET, ILINE20, etc) interrupts before/
 * after core switching to avoid invalid register accesss inside ISR.
 */
#define INTR_OFF(si, intr_val) \
	if ((si)->intrsoff_fn && (si)->cores_info->coreid[(si)->curidx] == (si)->dev_coreid) { \
		intr_val = (*(si)->intrsoff_fn)((si)->intr_arg); }
#define INTR_RESTORE(si, intr_val) \
	if ((si)->intrsrestore_fn && (si)->cores_info->coreid[(si)->curidx] == (si)->dev_coreid) { \
		(*(si)->intrsrestore_fn)((si)->intr_arg, intr_val); }

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
#endif // endif

/* Silicon Backplane externs */
extern void sb_scan(si_t *sih, volatile void *regs, uint devid);
extern uint sb_coreid(si_t *sih);
extern uint sb_intflag(si_t *sih);
extern uint sb_flag(si_t *sih);
extern void sb_setint(si_t *sih, int siflag);
extern uint sb_corevendor(si_t *sih);
extern uint sb_corerev(si_t *sih);
extern uint sb_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern volatile uint32 *sb_corereg_addr(si_t *sih, uint coreidx, uint regoff);
extern bool sb_iscoreup(si_t *sih);
extern volatile void *sb_setcoreidx(si_t *sih, uint coreidx);
extern uint32 sb_core_cflags(si_t *sih, uint32 mask, uint32 val);
extern void sb_core_cflags_wo(si_t *sih, uint32 mask, uint32 val);
extern uint32 sb_core_sflags(si_t *sih, uint32 mask, uint32 val);
extern void sb_commit(si_t *sih);
extern uint32 sb_base(uint32 admatch);
extern uint32 sb_size(uint32 admatch);
extern void sb_core_reset(si_t *sih, uint32 bits, uint32 resetbits);
extern void sb_core_disable(si_t *sih, uint32 bits);
extern uint32 sb_addrspace(si_t *sih, uint asidx);
extern uint32 sb_addrspacesize(si_t *sih, uint asidx);
extern int sb_numaddrspaces(si_t *sih);

extern uint32 sb_set_initiator_to(si_t *sih, uint32 to, uint idx);

extern bool sb_taclear(si_t *sih, bool details);

#if defined(BCMDBG_PHYDUMP)
extern void sb_dumpregs(si_t *sih, struct bcmstrbuf *b);
#endif // endif

/* Wake-on-wireless-LAN (WOWL) */
extern bool sb_pci_pmecap(si_t *sih);
struct osl_info;
extern bool sb_pci_fastpmecap(struct osl_info *osh);
extern bool sb_pci_pmeclr(si_t *sih);
extern void sb_pci_pmeen(si_t *sih);
extern uint sb_pcie_readreg(void *sih, uint addrtype, uint offset);

/* AMBA Interconnect exported externs */
extern si_t *ai_attach(uint pcidev, osl_t *osh, void *regs, uint bustype,
                       void *sdh, char **vars, uint *varsz);
extern si_t *ai_kattach(osl_t *osh);
extern void ai_scan(si_t *sih, void *regs, uint32 erombase, uint devid);

extern uint ai_flag(si_t *sih);
extern uint ai_flag_alt(si_t *sih);
extern void ai_setint(si_t *sih, int siflag);
extern uint ai_coreidx(si_t *sih);
extern uint ai_corevendor(si_t *sih);
extern uint ai_corerev(si_t *sih);
extern uint ai_corerev_minor(si_t *sih);
extern volatile uint32 *ai_corereg_addr(si_t *sih, uint coreidx, uint regoff);
extern bool ai_iscoreup(si_t *sih);
extern volatile void *ai_setcoreidx(si_t *sih, uint coreidx);
extern volatile void *ai_setcoreidx_2ndwrap(si_t *sih, uint coreidx);
extern volatile void *ai_setcoreidx_3rdwrap(si_t *sih, uint coreidx);
extern uint32 ai_core_cflags(si_t *sih, uint32 mask, uint32 val);
extern void ai_core_cflags_wo(si_t *sih, uint32 mask, uint32 val);
extern uint32 ai_core_sflags(si_t *sih, uint32 mask, uint32 val);
extern uint ai_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern uint ai_corereg_writeonly(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
extern void ai_core_reset(si_t *sih, uint32 bits, uint32 resetbits);
extern void ai_d11rsdb_core_reset(si_t *sih, uint32 bits,
	uint32 resetbits, void *p, volatile void *s);
extern void ai_core_disable(si_t *sih, uint32 bits);
extern void ai_d11rsdb_core_disable(const si_info_t *sii, uint32 bits,
	aidmp_t *pmacai, aidmp_t *smacai);
extern int ai_numaddrspaces(si_t *sih);
extern uint32 ai_addrspace(si_t *sih, uint spidx, uint baidx);
extern uint32 ai_addrspacesize(si_t *sih, uint spidx, uint baidx);
extern void ai_coreaddrspaceX(si_t *sih, uint asidx, uint32 *addr, uint32 *size);
extern uint ai_wrap_reg(si_t *sih, uint32 offset, uint32 mask, uint32 val);
extern void ai_update_backplane_timeouts(si_t *sih, bool enable, uint32 timeout, uint32 cid);
extern uint32 ai_clear_backplane_to(si_t *sih);
void ai_force_clocks(si_t *sih, uint clock_state);
extern uint ai_num_slaveports(si_t *sih, uint coreidx);

#ifdef BCM_BACKPLANE_TIMEOUT
uint32 ai_clear_backplane_to_fast(si_t *sih, void * addr);
#endif /* BCM_BACKPLANE_TIMEOUT */

#if defined(AXI_TIMEOUTS) || defined(BCM_BACKPLANE_TIMEOUT)
extern uint32 ai_clear_backplane_to_per_core(si_t *sih, uint coreid, uint coreunit, void * wrap);
#endif /* AXI_TIMEOUTS || BCM_BACKPLANE_TIMEOUT */

#if defined(BCMDBG_PHYDUMP)
extern void ai_dumpregs(si_t *sih, struct bcmstrbuf *b);
#endif // endif

extern uint32 ai_wrapper_dump_buf_size(si_t *sih);
extern uint32 ai_wrapper_dump_binary(si_t *sih, uchar *p);
extern bool ai_check_enable_backplane_log(si_t *sih);
extern uint32 ai_wrapper_dump_last_timeout(si_t *sih, uint32 *error, uint32 *core, uint32 *ba,
	uchar *p);

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

#endif	/* _siutils_priv_h_ */
