/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
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
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */

#ifndef __BFA_IOC_H__
#define __BFA_IOC_H__

#include "bfa_cs.h"
#include "bfi.h"
#include "cna.h"

#define BFA_IOC_TOV		3000	/* msecs */
#define BFA_IOC_HWSEM_TOV	500	/* msecs */
#define BFA_IOC_HB_TOV		500	/* msecs */
#define BFA_IOC_POLL_TOV	200	/* msecs */
#define BNA_DBG_FWTRC_LEN      (BFI_IOC_TRC_ENTS * BFI_IOC_TRC_ENT_SZ + \
				BFI_IOC_TRC_HDR_SZ)

/* PCI device information required by IOC */
struct bfa_pcidev {
	int	pci_slot;
	u8	pci_func;
	u16	device_id;
	u16	ssid;
	void	__iomem *pci_bar_kva;
};

/* Structure used to remember the DMA-able memory block's KVA and Physical
 * Address
 */
struct bfa_dma {
	void	*kva;	/* ! Kernel virtual address	*/
	u64	pa;	/* ! Physical address		*/
};

#define BFA_DMA_ALIGN_SZ	256

/* smem size for Crossbow and Catapult */
#define BFI_SMEM_CB_SIZE	0x200000U	/* ! 2MB for crossbow	*/
#define BFI_SMEM_CT_SIZE	0x280000U	/* ! 2.5MB for catapult	*/

/* BFA dma address assignment macro. (big endian format) */
#define bfa_dma_be_addr_set(dma_addr, pa)	\
		__bfa_dma_be_addr_set(&dma_addr, (u64)pa)
static inline void
__bfa_dma_be_addr_set(union bfi_addr_u *dma_addr, u64 pa)
{
	dma_addr->a32.addr_lo = (u32) htonl(pa);
	dma_addr->a32.addr_hi = (u32) htonl(upper_32_bits(pa));
}

#define bfa_alen_set(__alen, __len, __pa)	\
	__bfa_alen_set(__alen, __len, (u64)__pa)

static inline void
__bfa_alen_set(struct bfi_alen *alen, u32 len, u64 pa)
{
	alen->al_len = cpu_to_be32(len);
	bfa_dma_be_addr_set(alen->al_addr, pa);
}

struct bfa_ioc_regs {
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

/* IOC Mailbox structures */
typedef void (*bfa_mbox_cmd_cbfn_t)(void *cbarg);
struct bfa_mbox_cmd {
	struct list_head	qe;
	bfa_mbox_cmd_cbfn_t     cbfn;
	void		    *cbarg;
	u32     msg[BFI_IOC_MSGSZ];
};

/* IOC mailbox module */
typedef void (*bfa_ioc_mbox_mcfunc_t)(void *cbarg, struct bfi_mbmsg *m);
struct bfa_ioc_mbox_mod {
	struct list_head	cmd_q;		/*!< pending mbox queue	*/
	int			nmclass;	/*!< number of handlers */
	struct {
		bfa_ioc_mbox_mcfunc_t	cbfn;	/*!< message handlers	*/
		void			*cbarg;
	} mbhdlr[BFI_MC_MAX];
};

/* IOC callback function interfaces */
typedef void (*bfa_ioc_enable_cbfn_t)(void *bfa, enum bfa_status status);
typedef void (*bfa_ioc_disable_cbfn_t)(void *bfa);
typedef void (*bfa_ioc_hbfail_cbfn_t)(void *bfa);
typedef void (*bfa_ioc_reset_cbfn_t)(void *bfa);
struct bfa_ioc_cbfn {
	bfa_ioc_enable_cbfn_t	enable_cbfn;
	bfa_ioc_disable_cbfn_t	disable_cbfn;
	bfa_ioc_hbfail_cbfn_t	hbfail_cbfn;
	bfa_ioc_reset_cbfn_t	reset_cbfn;
};

/* IOC event notification mechanism. */
enum bfa_ioc_event {
	BFA_IOC_E_ENABLED	= 1,
	BFA_IOC_E_DISABLED	= 2,
	BFA_IOC_E_FAILED	= 3,
};

typedef void (*bfa_ioc_notify_cbfn_t)(void *, enum bfa_ioc_event);

struct bfa_ioc_notify {
	struct list_head	qe;
	bfa_ioc_notify_cbfn_t	cbfn;
	void			*cbarg;
};

/* Initialize a IOC event notification structure */
#define bfa_ioc_notify_init(__notify, __cbfn, __cbarg) do {	\
	(__notify)->cbfn = (__cbfn);				\
	(__notify)->cbarg = (__cbarg);				\
} while (0)

struct bfa_iocpf {
	bfa_fsm_t		fsm;
	struct bfa_ioc		*ioc;
	bool			fw_mismatch_notified;
	bool			auto_recover;
	u32			poll_time;
};

struct bfa_ioc {
	bfa_fsm_t		fsm;
	struct bfa		*bfa;
	struct bfa_pcidev	pcidev;
	struct timer_list	ioc_timer;
	struct timer_list	iocpf_timer;
	struct timer_list	sem_timer;
	struct timer_list	hb_timer;
	u32			hb_count;
	struct list_head	notify_q;
	void			*dbg_fwsave;
	int			dbg_fwsave_len;
	bool			dbg_fwsave_once;
	enum bfi_pcifn_class	clscode;
	struct bfa_ioc_regs	ioc_regs;
	struct bfa_ioc_drv_stats stats;
	bool			fcmode;
	bool			pllinit;
	bool			stats_busy;	/*!< outstanding stats */
	u8			port_id;

	struct bfa_dma		attr_dma;
	struct bfi_ioc_attr	*attr;
	struct bfa_ioc_cbfn	*cbfn;
	struct bfa_ioc_mbox_mod	mbox_mod;
	const struct bfa_ioc_hwif *ioc_hwif;
	struct bfa_iocpf	iocpf;
	enum bfi_asic_gen	asic_gen;
	enum bfi_asic_mode	asic_mode;
	enum bfi_port_mode	port0_mode;
	enum bfi_port_mode	port1_mode;
	enum bfa_mode		port_mode;
	u8			ad_cap_bm;	/*!< adapter cap bit mask */
	u8			port_mode_cfg;	/*!< config port mode */
};

struct bfa_ioc_hwif {
	enum bfa_status (*ioc_pll_init) (void __iomem *rb,
						enum bfi_asic_mode m);
	bool		(*ioc_firmware_lock)	(struct bfa_ioc *ioc);
	void		(*ioc_firmware_unlock)	(struct bfa_ioc *ioc);
	void		(*ioc_reg_init)	(struct bfa_ioc *ioc);
	void		(*ioc_map_port)	(struct bfa_ioc *ioc);
	void		(*ioc_isr_mode_set)	(struct bfa_ioc *ioc,
					bool msix);
	void		(*ioc_notify_fail)	(struct bfa_ioc *ioc);
	void		(*ioc_ownership_reset)	(struct bfa_ioc *ioc);
	bool		(*ioc_sync_start)       (struct bfa_ioc *ioc);
	void		(*ioc_sync_join)	(struct bfa_ioc *ioc);
	void		(*ioc_sync_leave)	(struct bfa_ioc *ioc);
	void		(*ioc_sync_ack)		(struct bfa_ioc *ioc);
	bool		(*ioc_sync_complete)	(struct bfa_ioc *ioc);
	bool		(*ioc_lpu_read_stat)	(struct bfa_ioc *ioc);
	void		(*ioc_set_fwstate)	(struct bfa_ioc *ioc,
					enum bfi_ioc_state fwstate);
	enum bfi_ioc_state (*ioc_get_fwstate) (struct bfa_ioc *ioc);
	void		(*ioc_set_alt_fwstate)	(struct bfa_ioc *ioc,
					enum bfi_ioc_state fwstate);
	enum bfi_ioc_state (*ioc_get_alt_fwstate) (struct bfa_ioc *ioc);

};

#define bfa_ioc_pcifn(__ioc)		((__ioc)->pcidev.pci_func)
#define bfa_ioc_devid(__ioc)		((__ioc)->pcidev.device_id)
#define bfa_ioc_bar0(__ioc)		((__ioc)->pcidev.pci_bar_kva)
#define bfa_ioc_portid(__ioc)		((__ioc)->port_id)
#define bfa_ioc_asic_gen(__ioc)		((__ioc)->asic_gen)
#define bfa_ioc_is_default(__ioc)	\
	(bfa_ioc_pcifn(__ioc) == bfa_ioc_portid(__ioc))
#define bfa_ioc_speed_sup(__ioc)	\
	BFI_ADAPTER_GETP(SPEED, (__ioc)->attr->adapter_prop)
#define bfa_ioc_get_nports(__ioc)	\
	BFI_ADAPTER_GETP(NPORTS, (__ioc)->attr->adapter_prop)

#define bfa_ioc_stats(_ioc, _stats)	((_ioc)->stats._stats++)
#define bfa_ioc_stats_hb_count(_ioc, _hb_count)	\
	((_ioc)->stats.hb_count = (_hb_count))
#define BFA_IOC_FWIMG_MINSZ	(16 * 1024)
#define BFA_IOC_FW_SMEM_SIZE(__ioc)					\
	((bfa_ioc_asic_gen(__ioc) == BFI_ASIC_GEN_CB)			\
	? BFI_SMEM_CB_SIZE : BFI_SMEM_CT_SIZE)
#define BFA_IOC_FLASH_CHUNK_NO(off)		(off / BFI_FLASH_CHUNK_SZ_WORDS)
#define BFA_IOC_FLASH_OFFSET_IN_CHUNK(off)	(off % BFI_FLASH_CHUNK_SZ_WORDS)
#define BFA_IOC_FLASH_CHUNK_ADDR(chunkno)  (chunkno * BFI_FLASH_CHUNK_SZ_WORDS)

/* IOC mailbox interface */
bool bfa_nw_ioc_mbox_queue(struct bfa_ioc *ioc,
			struct bfa_mbox_cmd *cmd,
			bfa_mbox_cmd_cbfn_t cbfn, void *cbarg);
void bfa_nw_ioc_mbox_isr(struct bfa_ioc *ioc);
void bfa_nw_ioc_mbox_regisr(struct bfa_ioc *ioc, enum bfi_mclass mc,
		bfa_ioc_mbox_mcfunc_t cbfn, void *cbarg);

/* IOC interfaces */

#define bfa_ioc_pll_init_asic(__ioc) \
	((__ioc)->ioc_hwif->ioc_pll_init((__ioc)->pcidev.pci_bar_kva, \
			   (__ioc)->asic_mode))

#define bfa_ioc_lpu_read_stat(__ioc) do {				\
		if ((__ioc)->ioc_hwif->ioc_lpu_read_stat)		\
			((__ioc)->ioc_hwif->ioc_lpu_read_stat(__ioc));	\
} while (0)

void bfa_nw_ioc_set_ct_hwif(struct bfa_ioc *ioc);
void bfa_nw_ioc_set_ct2_hwif(struct bfa_ioc *ioc);
void bfa_nw_ioc_ct2_poweron(struct bfa_ioc *ioc);

void bfa_nw_ioc_attach(struct bfa_ioc *ioc, void *bfa,
		struct bfa_ioc_cbfn *cbfn);
void bfa_nw_ioc_auto_recover(bool auto_recover);
void bfa_nw_ioc_detach(struct bfa_ioc *ioc);
void bfa_nw_ioc_pci_init(struct bfa_ioc *ioc, struct bfa_pcidev *pcidev,
		enum bfi_pcifn_class clscode);
u32 bfa_nw_ioc_meminfo(void);
void bfa_nw_ioc_mem_claim(struct bfa_ioc *ioc,  u8 *dm_kva, u64 dm_pa);
void bfa_nw_ioc_enable(struct bfa_ioc *ioc);
void bfa_nw_ioc_disable(struct bfa_ioc *ioc);

void bfa_nw_ioc_error_isr(struct bfa_ioc *ioc);
bool bfa_nw_ioc_is_disabled(struct bfa_ioc *ioc);
bool bfa_nw_ioc_is_operational(struct bfa_ioc *ioc);
void bfa_nw_ioc_get_attr(struct bfa_ioc *ioc, struct bfa_ioc_attr *ioc_attr);
enum bfa_status bfa_nw_ioc_fwsig_invalidate(struct bfa_ioc *ioc);
void bfa_nw_ioc_notify_register(struct bfa_ioc *ioc,
	struct bfa_ioc_notify *notify);
bool bfa_nw_ioc_sem_get(void __iomem *sem_reg);
void bfa_nw_ioc_sem_release(void __iomem *sem_reg);
void bfa_nw_ioc_hw_sem_release(struct bfa_ioc *ioc);
void bfa_nw_ioc_fwver_get(struct bfa_ioc *ioc,
			struct bfi_ioc_image_hdr *fwhdr);
bool bfa_nw_ioc_fwver_cmp(struct bfa_ioc *ioc,
			struct bfi_ioc_image_hdr *fwhdr);
void bfa_nw_ioc_get_mac(struct bfa_ioc *ioc, u8 *mac);
void bfa_nw_ioc_debug_memclaim(struct bfa_ioc *ioc, void *dbg_fwsave);
int bfa_nw_ioc_debug_fwtrc(struct bfa_ioc *ioc, void *trcdata, int *trclen);
int bfa_nw_ioc_debug_fwsave(struct bfa_ioc *ioc, void *trcdata, int *trclen);

/*
 * Timeout APIs
 */
void bfa_nw_ioc_timeout(struct bfa_ioc *ioc);
void bfa_nw_ioc_hb_check(struct bfa_ioc *ioc);
void bfa_nw_iocpf_timeout(struct bfa_ioc *ioc);
void bfa_nw_iocpf_sem_timeout(struct bfa_ioc *ioc);

/*
 * F/W Image Size & Chunk
 */
u32 *bfa_cb_image_get_chunk(enum bfi_asic_gen asic_gen, u32 off);
u32 bfa_cb_image_get_size(enum bfi_asic_gen asic_gen);

/*
 *	Flash module specific
 */
typedef void	(*bfa_cb_flash) (void *cbarg, enum bfa_status status);

struct bfa_flash {
	struct bfa_ioc *ioc;		/* back pointer to ioc */
	u32		type;		/* partition type */
	u8		instance;	/* partition instance */
	u8		rsv[3];
	u32		op_busy;	/*  operation busy flag */
	u32		residue;	/*  residual length */
	u32		offset;		/*  offset */
	enum bfa_status	status;		/*  status */
	u8		*dbuf_kva;	/*  dma buf virtual address */
	u64		dbuf_pa;	/*  dma buf physical address */
	bfa_cb_flash	cbfn;		/*  user callback function */
	void		*cbarg;		/*  user callback arg */
	u8		*ubuf;		/*  user supplied buffer */
	u32		addr_off;	/*  partition address offset */
	struct bfa_mbox_cmd mb;		/*  mailbox */
	struct bfa_ioc_notify ioc_notify; /*  ioc event notify */
};

enum bfa_status bfa_nw_flash_get_attr(struct bfa_flash *flash,
			struct bfa_flash_attr *attr,
			bfa_cb_flash cbfn, void *cbarg);
enum bfa_status bfa_nw_flash_update_part(struct bfa_flash *flash,
			u32 type, u8 instance, void *buf, u32 len, u32 offset,
			bfa_cb_flash cbfn, void *cbarg);
enum bfa_status bfa_nw_flash_read_part(struct bfa_flash *flash,
			u32 type, u8 instance, void *buf, u32 len, u32 offset,
			bfa_cb_flash cbfn, void *cbarg);
u32	bfa_nw_flash_meminfo(void);
void	bfa_nw_flash_attach(struct bfa_flash *flash,
			    struct bfa_ioc *ioc, void *dev);
void	bfa_nw_flash_memclaim(struct bfa_flash *flash, u8 *dm_kva, u64 dm_pa);

#endif /* __BFA_IOC_H__ */
