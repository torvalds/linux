/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __BFA_IOC_H__
#define __BFA_IOC_H__

#include "bfad_drv.h"
#include "bfa_cs.h"
#include "bfi.h"

#define BFA_DBG_FWTRC_ENTS	(BFI_IOC_TRC_ENTS)
#define BFA_DBG_FWTRC_LEN					\
	(BFA_DBG_FWTRC_ENTS * sizeof(struct bfa_trc_s) +	\
	(sizeof(struct bfa_trc_mod_s) -				\
	BFA_TRC_MAX * sizeof(struct bfa_trc_s)))
/*
 * BFA timer declarations
 */
typedef void (*bfa_timer_cbfn_t)(void *);

/*
 * BFA timer data structure
 */
struct bfa_timer_s {
	struct list_head	qe;
	bfa_timer_cbfn_t timercb;
	void		*arg;
	int		timeout;	/* in millisecs */
};

/*
 * Timer module structure
 */
struct bfa_timer_mod_s {
	struct list_head timer_q;
};

#define BFA_TIMER_FREQ 200 /* specified in millisecs */

void bfa_timer_beat(struct bfa_timer_mod_s *mod);
void bfa_timer_begin(struct bfa_timer_mod_s *mod, struct bfa_timer_s *timer,
			bfa_timer_cbfn_t timercb, void *arg,
			unsigned int timeout);
void bfa_timer_stop(struct bfa_timer_s *timer);

/*
 * Generic Scatter Gather Element used by driver
 */
struct bfa_sge_s {
	u32	sg_len;
	void		*sg_addr;
};

#define bfa_sge_word_swap(__sge) do {					     \
	((u32 *)(__sge))[0] = swab32(((u32 *)(__sge))[0]);      \
	((u32 *)(__sge))[1] = swab32(((u32 *)(__sge))[1]);      \
	((u32 *)(__sge))[2] = swab32(((u32 *)(__sge))[2]);      \
} while (0)

#define bfa_swap_words(_x)  (	\
	((_x) << 32) | ((_x) >> 32))

#ifdef __BIG_ENDIAN
#define bfa_sge_to_be(_x)
#define bfa_sge_to_le(_x)	bfa_sge_word_swap(_x)
#define bfa_sgaddr_le(_x)	bfa_swap_words(_x)
#else
#define	bfa_sge_to_be(_x)	bfa_sge_word_swap(_x)
#define bfa_sge_to_le(_x)
#define bfa_sgaddr_le(_x)	(_x)
#endif

/*
 * BFA memory resources
 */
struct bfa_mem_dma_s {
	struct list_head qe;		/* Queue of DMA elements */
	u32		mem_len;	/* Total Length in Bytes */
	u8		*kva;		/* kernel virtual address */
	u64		dma;		/* dma address if DMA memory */
	u8		*kva_curp;	/* kva allocation cursor */
	u64		dma_curp;	/* dma allocation cursor */
};
#define bfa_mem_dma_t struct bfa_mem_dma_s

struct bfa_mem_kva_s {
	struct list_head qe;		/* Queue of KVA elements */
	u32		mem_len;	/* Total Length in Bytes */
	u8		*kva;		/* kernel virtual address */
	u8		*kva_curp;	/* kva allocation cursor */
};
#define bfa_mem_kva_t struct bfa_mem_kva_s

struct bfa_meminfo_s {
	struct bfa_mem_dma_s dma_info;
	struct bfa_mem_kva_s kva_info;
};

/* BFA memory segment setup macros */
#define bfa_mem_dma_setup(_meminfo, _dm_ptr, _seg_sz) do {	\
	((bfa_mem_dma_t *)(_dm_ptr))->mem_len = (_seg_sz);	\
	if (_seg_sz)						\
		list_add_tail(&((bfa_mem_dma_t *)_dm_ptr)->qe,	\
			      &(_meminfo)->dma_info.qe);	\
} while (0)

#define bfa_mem_kva_setup(_meminfo, _kva_ptr, _seg_sz) do {	\
	((bfa_mem_kva_t *)(_kva_ptr))->mem_len = (_seg_sz);	\
	if (_seg_sz)						\
		list_add_tail(&((bfa_mem_kva_t *)_kva_ptr)->qe,	\
			      &(_meminfo)->kva_info.qe);	\
} while (0)

/* BFA dma memory segments iterator */
#define bfa_mem_dma_sptr(_mod, _i)	(&(_mod)->dma_seg[(_i)])
#define bfa_mem_dma_seg_iter(_mod, _sptr, _nr, _i)			\
	for (_i = 0, _sptr = bfa_mem_dma_sptr(_mod, _i); _i < (_nr);	\
	     _i++, _sptr = bfa_mem_dma_sptr(_mod, _i))

#define bfa_mem_kva_curp(_mod)	((_mod)->kva_seg.kva_curp)
#define bfa_mem_dma_virt(_sptr)	((_sptr)->kva_curp)
#define bfa_mem_dma_phys(_sptr)	((_sptr)->dma_curp)
#define bfa_mem_dma_len(_sptr)	((_sptr)->mem_len)

/* Get the corresponding dma buf kva for a req - from the tag */
#define bfa_mem_get_dmabuf_kva(_mod, _tag, _rqsz)			      \
	(((u8 *)(_mod)->dma_seg[BFI_MEM_SEG_FROM_TAG(_tag, _rqsz)].kva_curp) +\
	 BFI_MEM_SEG_REQ_OFFSET(_tag, _rqsz) * (_rqsz))

/* Get the corresponding dma buf pa for a req - from the tag */
#define bfa_mem_get_dmabuf_pa(_mod, _tag, _rqsz)			\
	((_mod)->dma_seg[BFI_MEM_SEG_FROM_TAG(_tag, _rqsz)].dma_curp +	\
	 BFI_MEM_SEG_REQ_OFFSET(_tag, _rqsz) * (_rqsz))

/*
 * PCI device information required by IOC
 */
struct bfa_pcidev_s {
	int		pci_slot;
	u8		pci_func;
	u16		device_id;
	u16		ssid;
	void __iomem	*pci_bar_kva;
};

/*
 * Structure used to remember the DMA-able memory block's KVA and Physical
 * Address
 */
struct bfa_dma_s {
	void		*kva;	/* ! Kernel virtual address	*/
	u64	pa;	/* ! Physical address		*/
};

#define BFA_DMA_ALIGN_SZ	256
#define BFA_ROUNDUP(_l, _s)	(((_l) + ((_s) - 1)) & ~((_s) - 1))

/*
 * smem size for Crossbow and Catapult
 */
#define BFI_SMEM_CB_SIZE	0x200000U	/* ! 2MB for crossbow	*/
#define BFI_SMEM_CT_SIZE	0x280000U	/* ! 2.5MB for catapult	*/

#define bfa_dma_be_addr_set(dma_addr, pa)	\
		__bfa_dma_be_addr_set(&dma_addr, (u64)pa)
static inline void
__bfa_dma_be_addr_set(union bfi_addr_u *dma_addr, u64 pa)
{
	dma_addr->a32.addr_lo = cpu_to_be32(pa);
	dma_addr->a32.addr_hi = cpu_to_be32(pa >> 32);
}

#define bfa_alen_set(__alen, __len, __pa)	\
	__bfa_alen_set(__alen, __len, (u64)__pa)

static inline void
__bfa_alen_set(struct bfi_alen_s *alen, u32 len, u64 pa)
{
	alen->al_len = cpu_to_be32(len);
	bfa_dma_be_addr_set(alen->al_addr, pa);
}

struct bfa_ioc_regs_s {
	void __iomem *hfn_mbox_cmd;
	void __iomem *hfn_mbox;
	void __iomem *lpu_mbox_cmd;
	void __iomem *lpu_mbox;
	void __iomem *lpu_read_stat;
	void __iomem *pss_ctl_reg;
	void __iomem *pss_err_status_reg;
	void __iomem *app_pll_fast_ctl_reg;
	void __iomem *app_pll_slow_ctl_reg;
	void __iomem *ioc_sem_reg;
	void __iomem *ioc_usage_sem_reg;
	void __iomem *ioc_init_sem_reg;
	void __iomem *ioc_usage_reg;
	void __iomem *host_page_num_fn;
	void __iomem *heartbeat;
	void __iomem *ioc_fwstate;
	void __iomem *alt_ioc_fwstate;
	void __iomem *ll_halt;
	void __iomem *alt_ll_halt;
	void __iomem *err_set;
	void __iomem *ioc_fail_sync;
	void __iomem *shirq_isr_next;
	void __iomem *shirq_msk_next;
	void __iomem *smem_page_start;
	u32	smem_pg0;
};

#define bfa_mem_read(_raddr, _off)	swab32(readl(((_raddr) + (_off))))
#define bfa_mem_write(_raddr, _off, _val)	\
			writel(swab32((_val)), ((_raddr) + (_off)))
/*
 * IOC Mailbox structures
 */
struct bfa_mbox_cmd_s {
	struct list_head	qe;
	u32	msg[BFI_IOC_MSGSZ];
};

/*
 * IOC mailbox module
 */
typedef void (*bfa_ioc_mbox_mcfunc_t)(void *cbarg, struct bfi_mbmsg_s *m);
struct bfa_ioc_mbox_mod_s {
	struct list_head		cmd_q;	/*  pending mbox queue	*/
	int			nmclass;	/*  number of handlers */
	struct {
		bfa_ioc_mbox_mcfunc_t	cbfn;	/*  message handlers	*/
		void			*cbarg;
	} mbhdlr[BFI_MC_MAX];
};

/*
 * IOC callback function interfaces
 */
typedef void (*bfa_ioc_enable_cbfn_t)(void *bfa, enum bfa_status status);
typedef void (*bfa_ioc_disable_cbfn_t)(void *bfa);
typedef void (*bfa_ioc_hbfail_cbfn_t)(void *bfa);
typedef void (*bfa_ioc_reset_cbfn_t)(void *bfa);
struct bfa_ioc_cbfn_s {
	bfa_ioc_enable_cbfn_t	enable_cbfn;
	bfa_ioc_disable_cbfn_t	disable_cbfn;
	bfa_ioc_hbfail_cbfn_t	hbfail_cbfn;
	bfa_ioc_reset_cbfn_t	reset_cbfn;
};

/*
 * IOC event notification mechanism.
 */
enum bfa_ioc_event_e {
	BFA_IOC_E_ENABLED	= 1,
	BFA_IOC_E_DISABLED	= 2,
	BFA_IOC_E_FAILED	= 3,
};

typedef void (*bfa_ioc_notify_cbfn_t)(void *, enum bfa_ioc_event_e);

struct bfa_ioc_notify_s {
	struct list_head		qe;
	bfa_ioc_notify_cbfn_t	cbfn;
	void			*cbarg;
};

/*
 * Initialize a IOC event notification structure
 */
#define bfa_ioc_notify_init(__notify, __cbfn, __cbarg) do {	\
	(__notify)->cbfn = (__cbfn);      \
	(__notify)->cbarg = (__cbarg);      \
} while (0)

struct bfa_iocpf_s {
	bfa_fsm_t		fsm;
	struct bfa_ioc_s	*ioc;
	bfa_boolean_t		fw_mismatch_notified;
	bfa_boolean_t		auto_recover;
	u32			poll_time;
};

struct bfa_ioc_s {
	bfa_fsm_t		fsm;
	struct bfa_s		*bfa;
	struct bfa_pcidev_s	pcidev;
	struct bfa_timer_mod_s	*timer_mod;
	struct bfa_timer_s	ioc_timer;
	struct bfa_timer_s	sem_timer;
	struct bfa_timer_s	hb_timer;
	u32		hb_count;
	struct list_head	notify_q;
	void			*dbg_fwsave;
	int			dbg_fwsave_len;
	bfa_boolean_t		dbg_fwsave_once;
	enum bfi_pcifn_class	clscode;
	struct bfa_ioc_regs_s	ioc_regs;
	struct bfa_trc_mod_s	*trcmod;
	struct bfa_ioc_drv_stats_s	stats;
	bfa_boolean_t		fcmode;
	bfa_boolean_t		pllinit;
	bfa_boolean_t		stats_busy;	/*  outstanding stats */
	u8			port_id;
	struct bfa_dma_s	attr_dma;
	struct bfi_ioc_attr_s	*attr;
	struct bfa_ioc_cbfn_s	*cbfn;
	struct bfa_ioc_mbox_mod_s mbox_mod;
	struct bfa_ioc_hwif_s	*ioc_hwif;
	struct bfa_iocpf_s	iocpf;
	enum bfi_asic_gen	asic_gen;
	enum bfi_asic_mode	asic_mode;
	enum bfi_port_mode	port0_mode;
	enum bfi_port_mode	port1_mode;
	enum bfa_mode_s		port_mode;
	u8			ad_cap_bm;	/* adapter cap bit mask */
	u8			port_mode_cfg;	/* config port mode */
	int			ioc_aen_seq;
};

struct bfa_ioc_hwif_s {
	bfa_status_t (*ioc_pll_init) (void __iomem *rb, enum bfi_asic_mode m);
	bfa_boolean_t	(*ioc_firmware_lock)	(struct bfa_ioc_s *ioc);
	void		(*ioc_firmware_unlock)	(struct bfa_ioc_s *ioc);
	void		(*ioc_reg_init)	(struct bfa_ioc_s *ioc);
	void		(*ioc_map_port)	(struct bfa_ioc_s *ioc);
	void		(*ioc_isr_mode_set)	(struct bfa_ioc_s *ioc,
					bfa_boolean_t msix);
	void		(*ioc_notify_fail)	(struct bfa_ioc_s *ioc);
	void		(*ioc_ownership_reset)	(struct bfa_ioc_s *ioc);
	bfa_boolean_t   (*ioc_sync_start)       (struct bfa_ioc_s *ioc);
	void		(*ioc_sync_join)	(struct bfa_ioc_s *ioc);
	void		(*ioc_sync_leave)	(struct bfa_ioc_s *ioc);
	void		(*ioc_sync_ack)		(struct bfa_ioc_s *ioc);
	bfa_boolean_t	(*ioc_sync_complete)	(struct bfa_ioc_s *ioc);
	bfa_boolean_t	(*ioc_lpu_read_stat)	(struct bfa_ioc_s *ioc);
};

/*
 * Queue element to wait for room in request queue. FIFO order is
 * maintained when fullfilling requests.
 */
struct bfa_reqq_wait_s {
	struct list_head	qe;
	void	(*qresume) (void *cbarg);
	void	*cbarg;
};

typedef void	(*bfa_cb_cbfn_t) (void *cbarg, bfa_boolean_t complete);

/*
 * Generic BFA callback element.
 */
struct bfa_cb_qe_s {
	struct list_head	qe;
	bfa_cb_cbfn_t	cbfn;
	bfa_boolean_t	once;
	bfa_boolean_t	pre_rmv;	/* set for stack based qe(s) */
	bfa_status_t	fw_status;	/* to access fw status in comp proc */
	void		*cbarg;
};

/*
 * ASIC block configurtion related
 */

typedef void (*bfa_ablk_cbfn_t)(void *, enum bfa_status);

struct bfa_ablk_s {
	struct bfa_ioc_s	*ioc;
	struct bfa_ablk_cfg_s	*cfg;
	u16			*pcifn;
	struct bfa_dma_s	dma_addr;
	bfa_boolean_t		busy;
	struct bfa_mbox_cmd_s	mb;
	bfa_ablk_cbfn_t		cbfn;
	void			*cbarg;
	struct bfa_ioc_notify_s	ioc_notify;
	struct bfa_mem_dma_s	ablk_dma;
};
#define BFA_MEM_ABLK_DMA(__bfa)		(&((__bfa)->modules.ablk.ablk_dma))

/*
 *	SFP module specific
 */
typedef void	(*bfa_cb_sfp_t) (void *cbarg, bfa_status_t status);

struct bfa_sfp_s {
	void	*dev;
	struct bfa_ioc_s	*ioc;
	struct bfa_trc_mod_s	*trcmod;
	struct sfp_mem_s	*sfpmem;
	bfa_cb_sfp_t		cbfn;
	void			*cbarg;
	enum bfi_sfp_mem_e	memtype; /* mem access type   */
	u32			status;
	struct bfa_mbox_cmd_s	mbcmd;
	u8			*dbuf_kva; /* dma buf virtual address */
	u64			dbuf_pa;   /* dma buf physical address */
	struct bfa_ioc_notify_s	ioc_notify;
	enum bfa_defs_sfp_media_e *media;
	enum bfa_port_speed	portspeed;
	bfa_cb_sfp_t		state_query_cbfn;
	void			*state_query_cbarg;
	u8			lock;
	u8			data_valid; /* data in dbuf is valid */
	u8			state;	    /* sfp state  */
	u8			state_query_lock;
	struct bfa_mem_dma_s	sfp_dma;
	u8			is_elb;	    /* eloopback  */
};

#define BFA_SFP_MOD(__bfa)	(&(__bfa)->modules.sfp)
#define BFA_MEM_SFP_DMA(__bfa)	(&(BFA_SFP_MOD(__bfa)->sfp_dma))

u32	bfa_sfp_meminfo(void);

void	bfa_sfp_attach(struct bfa_sfp_s *sfp, struct bfa_ioc_s *ioc,
			void *dev, struct bfa_trc_mod_s *trcmod);

void	bfa_sfp_memclaim(struct bfa_sfp_s *diag, u8 *dm_kva, u64 dm_pa);
void	bfa_sfp_intr(void *bfaarg, struct bfi_mbmsg_s *msg);

bfa_status_t	bfa_sfp_show(struct bfa_sfp_s *sfp, struct sfp_mem_s *sfpmem,
			     bfa_cb_sfp_t cbfn, void *cbarg);

bfa_status_t	bfa_sfp_media(struct bfa_sfp_s *sfp,
			enum bfa_defs_sfp_media_e *media,
			bfa_cb_sfp_t cbfn, void *cbarg);

bfa_status_t	bfa_sfp_speed(struct bfa_sfp_s *sfp,
			enum bfa_port_speed portspeed,
			bfa_cb_sfp_t cbfn, void *cbarg);

/*
 *	Flash module specific
 */
typedef void	(*bfa_cb_flash_t) (void *cbarg, bfa_status_t status);

struct bfa_flash_s {
	struct bfa_ioc_s *ioc;		/* back pointer to ioc */
	struct bfa_trc_mod_s *trcmod;
	u32		type;           /* partition type */
	u8		instance;       /* partition instance */
	u8		rsv[3];
	u32		op_busy;        /*  operation busy flag */
	u32		residue;        /*  residual length */
	u32		offset;         /*  offset */
	bfa_status_t	status;         /*  status */
	u8		*dbuf_kva;      /*  dma buf virtual address */
	u64		dbuf_pa;        /*  dma buf physical address */
	struct bfa_reqq_wait_s	reqq_wait; /*  to wait for room in reqq */
	bfa_cb_flash_t	cbfn;           /*  user callback function */
	void		*cbarg;         /*  user callback arg */
	u8		*ubuf;          /*  user supplied buffer */
	struct bfa_cb_qe_s	hcb_qe; /*  comp: BFA callback qelem */
	u32		addr_off;       /*  partition address offset */
	struct bfa_mbox_cmd_s	mb;       /*  mailbox */
	struct bfa_ioc_notify_s	ioc_notify; /*  ioc event notify */
	struct bfa_mem_dma_s	flash_dma;
};

#define BFA_FLASH(__bfa)		(&(__bfa)->modules.flash)
#define BFA_MEM_FLASH_DMA(__bfa)	(&(BFA_FLASH(__bfa)->flash_dma))

bfa_status_t bfa_flash_get_attr(struct bfa_flash_s *flash,
			struct bfa_flash_attr_s *attr,
			bfa_cb_flash_t cbfn, void *cbarg);
bfa_status_t bfa_flash_erase_part(struct bfa_flash_s *flash,
			enum bfa_flash_part_type type, u8 instance,
			bfa_cb_flash_t cbfn, void *cbarg);
bfa_status_t bfa_flash_update_part(struct bfa_flash_s *flash,
			enum bfa_flash_part_type type, u8 instance,
			void *buf, u32 len, u32 offset,
			bfa_cb_flash_t cbfn, void *cbarg);
bfa_status_t bfa_flash_read_part(struct bfa_flash_s *flash,
			enum bfa_flash_part_type type, u8 instance, void *buf,
			u32 len, u32 offset, bfa_cb_flash_t cbfn, void *cbarg);
u32	bfa_flash_meminfo(bfa_boolean_t mincfg);
void bfa_flash_attach(struct bfa_flash_s *flash, struct bfa_ioc_s *ioc,
		void *dev, struct bfa_trc_mod_s *trcmod, bfa_boolean_t mincfg);
void bfa_flash_memclaim(struct bfa_flash_s *flash,
		u8 *dm_kva, u64 dm_pa, bfa_boolean_t mincfg);

/*
 *	DIAG module specific
 */

typedef void (*bfa_cb_diag_t) (void *cbarg, bfa_status_t status);
typedef void (*bfa_cb_diag_beacon_t) (void *dev, bfa_boolean_t beacon,
			bfa_boolean_t link_e2e_beacon);

/*
 *      Firmware ping test results
 */
struct bfa_diag_results_fwping {
	u32     data;   /* store the corrupted data */
	u32     status;
	u32     dmastatus;
	u8      rsvd[4];
};

struct bfa_diag_qtest_result_s {
	u32	status;
	u16	count;	/* sucessful queue test count */
	u8	queue;
	u8	rsvd;	/* 64-bit align */
};

/*
 * Firmware ping test results
 */
struct bfa_diag_fwping_s {
	struct bfa_diag_results_fwping *result;
	bfa_cb_diag_t  cbfn;
	void            *cbarg;
	u32             data;
	u8              lock;
	u8              rsv[3];
	u32             status;
	u32             count;
	struct bfa_mbox_cmd_s   mbcmd;
	u8              *dbuf_kva;      /* dma buf virtual address */
	u64             dbuf_pa;        /* dma buf physical address */
};

/*
 *      Temperature sensor query results
 */
struct bfa_diag_results_tempsensor_s {
	u32     status;
	u16     temp;           /* 10-bit A/D value */
	u16     brd_temp;       /* 9-bit board temp */
	u8      ts_junc;        /* show junction tempsensor   */
	u8      ts_brd;         /* show board tempsensor      */
	u8      rsvd[6];        /* keep 8 bytes alignment     */
};

struct bfa_diag_tsensor_s {
	bfa_cb_diag_t   cbfn;
	void            *cbarg;
	struct bfa_diag_results_tempsensor_s *temp;
	u8              lock;
	u8              rsv[3];
	u32             status;
	struct bfa_mbox_cmd_s   mbcmd;
};

struct bfa_diag_sfpshow_s {
	struct sfp_mem_s        *sfpmem;
	bfa_cb_diag_t           cbfn;
	void                    *cbarg;
	u8      lock;
	u8      static_data;
	u8      rsv[2];
	u32     status;
	struct bfa_mbox_cmd_s    mbcmd;
	u8      *dbuf_kva;      /* dma buf virtual address */
	u64     dbuf_pa;        /* dma buf physical address */
};

struct bfa_diag_led_s {
	struct bfa_mbox_cmd_s   mbcmd;
	bfa_boolean_t   lock;   /* 1: ledtest is operating */
};

struct bfa_diag_beacon_s {
	struct bfa_mbox_cmd_s   mbcmd;
	bfa_boolean_t   state;          /* port beacon state */
	bfa_boolean_t   link_e2e;       /* link beacon state */
};

struct bfa_diag_s {
	void	*dev;
	struct bfa_ioc_s		*ioc;
	struct bfa_trc_mod_s		*trcmod;
	struct bfa_diag_fwping_s	fwping;
	struct bfa_diag_tsensor_s	tsensor;
	struct bfa_diag_sfpshow_s	sfpshow;
	struct bfa_diag_led_s		ledtest;
	struct bfa_diag_beacon_s	beacon;
	void	*result;
	struct bfa_timer_s timer;
	bfa_cb_diag_beacon_t  cbfn_beacon;
	bfa_cb_diag_t  cbfn;
	void		*cbarg;
	u8		block;
	u8		timer_active;
	u8		rsvd[2];
	u32		status;
	struct bfa_ioc_notify_s	ioc_notify;
	struct bfa_mem_dma_s	diag_dma;
};

#define BFA_DIAG_MOD(__bfa)     (&(__bfa)->modules.diag_mod)
#define BFA_MEM_DIAG_DMA(__bfa) (&(BFA_DIAG_MOD(__bfa)->diag_dma))

u32	bfa_diag_meminfo(void);
void bfa_diag_memclaim(struct bfa_diag_s *diag, u8 *dm_kva, u64 dm_pa);
void bfa_diag_attach(struct bfa_diag_s *diag, struct bfa_ioc_s *ioc, void *dev,
		     bfa_cb_diag_beacon_t cbfn_beacon,
		     struct bfa_trc_mod_s *trcmod);
bfa_status_t	bfa_diag_reg_read(struct bfa_diag_s *diag, u32 offset,
			u32 len, u32 *buf, u32 force);
bfa_status_t	bfa_diag_reg_write(struct bfa_diag_s *diag, u32 offset,
			u32 len, u32 value, u32 force);
bfa_status_t	bfa_diag_tsensor_query(struct bfa_diag_s *diag,
			struct bfa_diag_results_tempsensor_s *result,
			bfa_cb_diag_t cbfn, void *cbarg);
bfa_status_t	bfa_diag_fwping(struct bfa_diag_s *diag, u32 cnt,
			u32 pattern, struct bfa_diag_results_fwping *result,
			bfa_cb_diag_t cbfn, void *cbarg);
bfa_status_t	bfa_diag_sfpshow(struct bfa_diag_s *diag,
			struct sfp_mem_s *sfpmem, u8 static_data,
			bfa_cb_diag_t cbfn, void *cbarg);
bfa_status_t	bfa_diag_memtest(struct bfa_diag_s *diag,
			struct bfa_diag_memtest_s *memtest, u32 pattern,
			struct bfa_diag_memtest_result *result,
			bfa_cb_diag_t cbfn, void *cbarg);
bfa_status_t	bfa_diag_ledtest(struct bfa_diag_s *diag,
			struct bfa_diag_ledtest_s *ledtest);
bfa_status_t	bfa_diag_beacon_port(struct bfa_diag_s *diag,
			bfa_boolean_t beacon, bfa_boolean_t link_e2e_beacon,
			u32 sec);

/*
 *	PHY module specific
 */
typedef void (*bfa_cb_phy_t) (void *cbarg, bfa_status_t status);

struct bfa_phy_s {
	struct bfa_ioc_s *ioc;          /* back pointer to ioc */
	struct bfa_trc_mod_s *trcmod;   /* trace module */
	u8	instance;       /* port instance */
	u8	op_busy;        /* operation busy flag */
	u8	rsv[2];
	u32	residue;        /* residual length */
	u32	offset;         /* offset */
	bfa_status_t	status;         /* status */
	u8	*dbuf_kva;      /* dma buf virtual address */
	u64	dbuf_pa;        /* dma buf physical address */
	struct bfa_reqq_wait_s reqq_wait; /* to wait for room in reqq */
	bfa_cb_phy_t	cbfn;           /* user callback function */
	void		*cbarg;         /* user callback arg */
	u8		*ubuf;          /* user supplied buffer */
	struct bfa_cb_qe_s	hcb_qe; /* comp: BFA callback qelem */
	u32	addr_off;       /* phy address offset */
	struct bfa_mbox_cmd_s	mb;       /* mailbox */
	struct bfa_ioc_notify_s	ioc_notify; /* ioc event notify */
	struct bfa_mem_dma_s	phy_dma;
};
#define BFA_PHY(__bfa)	(&(__bfa)->modules.phy)
#define BFA_MEM_PHY_DMA(__bfa)	(&(BFA_PHY(__bfa)->phy_dma))

bfa_boolean_t bfa_phy_busy(struct bfa_ioc_s *ioc);
bfa_status_t bfa_phy_get_attr(struct bfa_phy_s *phy, u8 instance,
			struct bfa_phy_attr_s *attr,
			bfa_cb_phy_t cbfn, void *cbarg);
bfa_status_t bfa_phy_get_stats(struct bfa_phy_s *phy, u8 instance,
			struct bfa_phy_stats_s *stats,
			bfa_cb_phy_t cbfn, void *cbarg);
bfa_status_t bfa_phy_update(struct bfa_phy_s *phy, u8 instance,
			void *buf, u32 len, u32 offset,
			bfa_cb_phy_t cbfn, void *cbarg);
bfa_status_t bfa_phy_read(struct bfa_phy_s *phy, u8 instance,
			void *buf, u32 len, u32 offset,
			bfa_cb_phy_t cbfn, void *cbarg);

u32	bfa_phy_meminfo(bfa_boolean_t mincfg);
void bfa_phy_attach(struct bfa_phy_s *phy, struct bfa_ioc_s *ioc,
		void *dev, struct bfa_trc_mod_s *trcmod, bfa_boolean_t mincfg);
void bfa_phy_memclaim(struct bfa_phy_s *phy,
		u8 *dm_kva, u64 dm_pa, bfa_boolean_t mincfg);
void bfa_phy_intr(void *phyarg, struct bfi_mbmsg_s *msg);

/*
 * Driver Config( dconf) specific
 */
#define BFI_DCONF_SIGNATURE	0xabcdabcd
#define BFI_DCONF_VERSION	1

#pragma pack(1)
struct bfa_dconf_hdr_s {
	u32	signature;
	u32	version;
};

struct bfa_dconf_s {
	struct bfa_dconf_hdr_s		hdr;
	struct bfa_lunmask_cfg_s	lun_mask;
};
#pragma pack()

struct bfa_dconf_mod_s {
	bfa_sm_t		sm;
	u8			instance;
	bfa_boolean_t		flashdone;
	bfa_boolean_t		read_data_valid;
	bfa_boolean_t		min_cfg;
	struct bfa_timer_s	timer;
	struct bfa_s		*bfa;
	void			*bfad;
	void			*trcmod;
	struct bfa_dconf_s	*dconf;
	struct bfa_mem_kva_s	kva_seg;
};

#define BFA_DCONF_MOD(__bfa)	\
	(&(__bfa)->modules.dconf_mod)
#define BFA_MEM_DCONF_KVA(__bfa)	(&(BFA_DCONF_MOD(__bfa)->kva_seg))
#define bfa_dconf_read_data_valid(__bfa)	\
	(BFA_DCONF_MOD(__bfa)->read_data_valid)
#define BFA_DCONF_UPDATE_TOV	5000	/* memtest timeout in msec */

void	bfa_dconf_modinit(struct bfa_s *bfa);
void	bfa_dconf_modexit(struct bfa_s *bfa);
bfa_status_t	bfa_dconf_update(struct bfa_s *bfa);

/*
 *	IOC specfic macros
 */
#define bfa_ioc_pcifn(__ioc)		((__ioc)->pcidev.pci_func)
#define bfa_ioc_devid(__ioc)		((__ioc)->pcidev.device_id)
#define bfa_ioc_bar0(__ioc)		((__ioc)->pcidev.pci_bar_kva)
#define bfa_ioc_portid(__ioc)		((__ioc)->port_id)
#define bfa_ioc_asic_gen(__ioc)		((__ioc)->asic_gen)
#define bfa_ioc_is_cna(__ioc)	\
	((bfa_ioc_get_type(__ioc) == BFA_IOC_TYPE_FCoE) ||	\
	 (bfa_ioc_get_type(__ioc) == BFA_IOC_TYPE_LL))
#define bfa_ioc_fetch_stats(__ioc, __stats) \
		(((__stats)->drv_stats) = (__ioc)->stats)
#define bfa_ioc_clr_stats(__ioc)	\
		memset(&(__ioc)->stats, 0, sizeof((__ioc)->stats))
#define bfa_ioc_maxfrsize(__ioc)	((__ioc)->attr->maxfrsize)
#define bfa_ioc_rx_bbcredit(__ioc)	((__ioc)->attr->rx_bbcredit)
#define bfa_ioc_speed_sup(__ioc)	\
	BFI_ADAPTER_GETP(SPEED, (__ioc)->attr->adapter_prop)
#define bfa_ioc_get_nports(__ioc)	\
	BFI_ADAPTER_GETP(NPORTS, (__ioc)->attr->adapter_prop)

#define bfa_ioc_stats(_ioc, _stats)	((_ioc)->stats._stats++)
#define BFA_IOC_FWIMG_MINSZ	(16 * 1024)
#define BFA_IOC_FW_SMEM_SIZE(__ioc)			\
	((bfa_ioc_asic_gen(__ioc) == BFI_ASIC_GEN_CB)	\
	 ? BFI_SMEM_CB_SIZE : BFI_SMEM_CT_SIZE)
#define BFA_IOC_FLASH_CHUNK_NO(off)		(off / BFI_FLASH_CHUNK_SZ_WORDS)
#define BFA_IOC_FLASH_OFFSET_IN_CHUNK(off)	(off % BFI_FLASH_CHUNK_SZ_WORDS)
#define BFA_IOC_FLASH_CHUNK_ADDR(chunkno)  (chunkno * BFI_FLASH_CHUNK_SZ_WORDS)

/*
 * IOC mailbox interface
 */
void bfa_ioc_mbox_queue(struct bfa_ioc_s *ioc, struct bfa_mbox_cmd_s *cmd);
void bfa_ioc_mbox_register(struct bfa_ioc_s *ioc,
		bfa_ioc_mbox_mcfunc_t *mcfuncs);
void bfa_ioc_mbox_isr(struct bfa_ioc_s *ioc);
void bfa_ioc_mbox_send(struct bfa_ioc_s *ioc, void *ioc_msg, int len);
bfa_boolean_t bfa_ioc_msgget(struct bfa_ioc_s *ioc, void *mbmsg);
void bfa_ioc_mbox_regisr(struct bfa_ioc_s *ioc, enum bfi_mclass mc,
		bfa_ioc_mbox_mcfunc_t cbfn, void *cbarg);

/*
 * IOC interfaces
 */

#define bfa_ioc_pll_init_asic(__ioc) \
	((__ioc)->ioc_hwif->ioc_pll_init((__ioc)->pcidev.pci_bar_kva, \
			   (__ioc)->asic_mode))

bfa_status_t bfa_ioc_pll_init(struct bfa_ioc_s *ioc);
bfa_status_t bfa_ioc_cb_pll_init(void __iomem *rb, enum bfi_asic_mode mode);
bfa_status_t bfa_ioc_ct_pll_init(void __iomem *rb, enum bfi_asic_mode mode);
bfa_status_t bfa_ioc_ct2_pll_init(void __iomem *rb, enum bfi_asic_mode mode);

#define bfa_ioc_isr_mode_set(__ioc, __msix) do {			\
	if ((__ioc)->ioc_hwif->ioc_isr_mode_set)			\
		((__ioc)->ioc_hwif->ioc_isr_mode_set(__ioc, __msix));	\
} while (0)
#define	bfa_ioc_ownership_reset(__ioc)				\
			((__ioc)->ioc_hwif->ioc_ownership_reset(__ioc))
#define bfa_ioc_get_fcmode(__ioc)	((__ioc)->fcmode)
#define bfa_ioc_lpu_read_stat(__ioc) do {			\
	if ((__ioc)->ioc_hwif->ioc_lpu_read_stat)		\
		((__ioc)->ioc_hwif->ioc_lpu_read_stat(__ioc));	\
} while (0)

void bfa_ioc_set_cb_hwif(struct bfa_ioc_s *ioc);
void bfa_ioc_set_ct_hwif(struct bfa_ioc_s *ioc);
void bfa_ioc_set_ct2_hwif(struct bfa_ioc_s *ioc);
void bfa_ioc_ct2_poweron(struct bfa_ioc_s *ioc);

void bfa_ioc_attach(struct bfa_ioc_s *ioc, void *bfa,
		struct bfa_ioc_cbfn_s *cbfn, struct bfa_timer_mod_s *timer_mod);
void bfa_ioc_auto_recover(bfa_boolean_t auto_recover);
void bfa_ioc_detach(struct bfa_ioc_s *ioc);
void bfa_ioc_pci_init(struct bfa_ioc_s *ioc, struct bfa_pcidev_s *pcidev,
		enum bfi_pcifn_class clscode);
void bfa_ioc_mem_claim(struct bfa_ioc_s *ioc,  u8 *dm_kva, u64 dm_pa);
void bfa_ioc_enable(struct bfa_ioc_s *ioc);
void bfa_ioc_disable(struct bfa_ioc_s *ioc);
bfa_boolean_t bfa_ioc_intx_claim(struct bfa_ioc_s *ioc);

void bfa_ioc_boot(struct bfa_ioc_s *ioc, u32 boot_type,
		u32 boot_env);
void bfa_ioc_isr(struct bfa_ioc_s *ioc, struct bfi_mbmsg_s *msg);
void bfa_ioc_error_isr(struct bfa_ioc_s *ioc);
bfa_boolean_t bfa_ioc_is_operational(struct bfa_ioc_s *ioc);
bfa_boolean_t bfa_ioc_is_initialized(struct bfa_ioc_s *ioc);
bfa_boolean_t bfa_ioc_is_disabled(struct bfa_ioc_s *ioc);
bfa_boolean_t bfa_ioc_is_acq_addr(struct bfa_ioc_s *ioc);
bfa_boolean_t bfa_ioc_fw_mismatch(struct bfa_ioc_s *ioc);
bfa_boolean_t bfa_ioc_adapter_is_disabled(struct bfa_ioc_s *ioc);
void bfa_ioc_reset_fwstate(struct bfa_ioc_s *ioc);
enum bfa_ioc_type_e bfa_ioc_get_type(struct bfa_ioc_s *ioc);
void bfa_ioc_get_adapter_serial_num(struct bfa_ioc_s *ioc, char *serial_num);
void bfa_ioc_get_adapter_fw_ver(struct bfa_ioc_s *ioc, char *fw_ver);
void bfa_ioc_get_adapter_optrom_ver(struct bfa_ioc_s *ioc, char *optrom_ver);
void bfa_ioc_get_adapter_model(struct bfa_ioc_s *ioc, char *model);
void bfa_ioc_get_adapter_manufacturer(struct bfa_ioc_s *ioc,
		char *manufacturer);
void bfa_ioc_get_pci_chip_rev(struct bfa_ioc_s *ioc, char *chip_rev);
enum bfa_ioc_state bfa_ioc_get_state(struct bfa_ioc_s *ioc);

void bfa_ioc_get_attr(struct bfa_ioc_s *ioc, struct bfa_ioc_attr_s *ioc_attr);
void bfa_ioc_get_adapter_attr(struct bfa_ioc_s *ioc,
		struct bfa_adapter_attr_s *ad_attr);
void bfa_ioc_debug_memclaim(struct bfa_ioc_s *ioc, void *dbg_fwsave);
bfa_status_t bfa_ioc_debug_fwsave(struct bfa_ioc_s *ioc, void *trcdata,
		int *trclen);
bfa_status_t bfa_ioc_debug_fwtrc(struct bfa_ioc_s *ioc, void *trcdata,
				 int *trclen);
bfa_status_t bfa_ioc_debug_fwcore(struct bfa_ioc_s *ioc, void *buf,
	u32 *offset, int *buflen);
bfa_boolean_t bfa_ioc_sem_get(void __iomem *sem_reg);
void bfa_ioc_fwver_get(struct bfa_ioc_s *ioc,
			struct bfi_ioc_image_hdr_s *fwhdr);
bfa_boolean_t bfa_ioc_fwver_cmp(struct bfa_ioc_s *ioc,
			struct bfi_ioc_image_hdr_s *fwhdr);
void bfa_ioc_aen_post(struct bfa_ioc_s *ioc, enum bfa_ioc_aen_event event);
bfa_status_t bfa_ioc_fw_stats_get(struct bfa_ioc_s *ioc, void *stats);
bfa_status_t bfa_ioc_fw_stats_clear(struct bfa_ioc_s *ioc);

/*
 * asic block configuration related APIs
 */
u32	bfa_ablk_meminfo(void);
void bfa_ablk_memclaim(struct bfa_ablk_s *ablk, u8 *dma_kva, u64 dma_pa);
void bfa_ablk_attach(struct bfa_ablk_s *ablk, struct bfa_ioc_s *ioc);
bfa_status_t bfa_ablk_query(struct bfa_ablk_s *ablk,
		struct bfa_ablk_cfg_s *ablk_cfg,
		bfa_ablk_cbfn_t cbfn, void *cbarg);
bfa_status_t bfa_ablk_adapter_config(struct bfa_ablk_s *ablk,
		enum bfa_mode_s mode, int max_pf, int max_vf,
		bfa_ablk_cbfn_t cbfn, void *cbarg);
bfa_status_t bfa_ablk_port_config(struct bfa_ablk_s *ablk, int port,
		enum bfa_mode_s mode, int max_pf, int max_vf,
		bfa_ablk_cbfn_t cbfn, void *cbarg);
bfa_status_t bfa_ablk_pf_create(struct bfa_ablk_s *ablk, u16 *pcifn,
		u8 port, enum bfi_pcifn_class personality, int bw,
		bfa_ablk_cbfn_t cbfn, void *cbarg);
bfa_status_t bfa_ablk_pf_delete(struct bfa_ablk_s *ablk, int pcifn,
		bfa_ablk_cbfn_t cbfn, void *cbarg);
bfa_status_t bfa_ablk_pf_update(struct bfa_ablk_s *ablk, int pcifn, int bw,
		bfa_ablk_cbfn_t cbfn, void *cbarg);
bfa_status_t bfa_ablk_optrom_en(struct bfa_ablk_s *ablk,
		bfa_ablk_cbfn_t cbfn, void *cbarg);
bfa_status_t bfa_ablk_optrom_dis(struct bfa_ablk_s *ablk,
		bfa_ablk_cbfn_t cbfn, void *cbarg);

/*
 * bfa mfg wwn API functions
 */
mac_t bfa_ioc_get_mac(struct bfa_ioc_s *ioc);
mac_t bfa_ioc_get_mfg_mac(struct bfa_ioc_s *ioc);

/*
 * F/W Image Size & Chunk
 */
extern u32 bfi_image_cb_size;
extern u32 bfi_image_ct_size;
extern u32 bfi_image_ct2_size;
extern u32 *bfi_image_cb;
extern u32 *bfi_image_ct;
extern u32 *bfi_image_ct2;

static inline u32 *
bfi_image_cb_get_chunk(u32 off)
{
	return (u32 *)(bfi_image_cb + off);
}

static inline u32 *
bfi_image_ct_get_chunk(u32 off)
{
	return (u32 *)(bfi_image_ct + off);
}

static inline u32 *
bfi_image_ct2_get_chunk(u32 off)
{
	return (u32 *)(bfi_image_ct2 + off);
}

static inline u32*
bfa_cb_image_get_chunk(enum bfi_asic_gen asic_gen, u32 off)
{
	switch (asic_gen) {
	case BFI_ASIC_GEN_CB:
		return bfi_image_cb_get_chunk(off);
		break;
	case BFI_ASIC_GEN_CT:
		return bfi_image_ct_get_chunk(off);
		break;
	case BFI_ASIC_GEN_CT2:
		return bfi_image_ct2_get_chunk(off);
		break;
	default:
		return NULL;
	}
}

static inline u32
bfa_cb_image_get_size(enum bfi_asic_gen asic_gen)
{
	switch (asic_gen) {
	case BFI_ASIC_GEN_CB:
		return bfi_image_cb_size;
		break;
	case BFI_ASIC_GEN_CT:
		return bfi_image_ct_size;
		break;
	case BFI_ASIC_GEN_CT2:
		return bfi_image_ct2_size;
		break;
	default:
		return 0;
	}
}

/*
 * CNA TRCMOD declaration
 */
/*
 * !!! Only append to the enums defined here to avoid any versioning
 * !!! needed between trace utility and driver version
 */
enum {
	BFA_TRC_CNA_PORT	= 1,
	BFA_TRC_CNA_IOC		= 2,
	BFA_TRC_CNA_IOC_CB	= 3,
	BFA_TRC_CNA_IOC_CT	= 4,
};

#endif /* __BFA_IOC_H__ */
