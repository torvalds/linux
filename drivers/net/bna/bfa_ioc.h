/*
 * Linux network driver for Brocade Converged Network Adapter.
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
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */

#ifndef __BFA_IOC_H__
#define __BFA_IOC_H__

#include "bfa_sm.h"
#include "bfi.h"
#include "cna.h"

#define BFA_IOC_TOV		3000	/* msecs */
#define BFA_IOC_HWSEM_TOV	500	/* msecs */
#define BFA_IOC_HB_TOV		500	/* msecs */
#define BFA_IOC_HWINIT_MAX	5

/**
 * PCI device information required by IOC
 */
struct bfa_pcidev {
	int	pci_slot;
	u8	pci_func;
	u16	device_id;
	void	__iomem *pci_bar_kva;
};

/**
 * Structure used to remember the DMA-able memory block's KVA and Physical
 * Address
 */
struct bfa_dma {
	void	*kva;	/* ! Kernel virtual address	*/
	u64	pa;	/* ! Physical address		*/
};

#define BFA_DMA_ALIGN_SZ	256

/**
 * smem size for Crossbow and Catapult
 */
#define BFI_SMEM_CB_SIZE	0x200000U	/* ! 2MB for crossbow	*/
#define BFI_SMEM_CT_SIZE	0x280000U	/* ! 2.5MB for catapult	*/

/**
 * @brief BFA dma address assignment macro. (big endian format)
 */
#define bfa_dma_be_addr_set(dma_addr, pa)	\
		__bfa_dma_be_addr_set(&dma_addr, (u64)pa)
static inline void
__bfa_dma_be_addr_set(union bfi_addr_u *dma_addr, u64 pa)
{
	dma_addr->a32.addr_lo = (u32) htonl(pa);
	dma_addr->a32.addr_hi = (u32) htonl(upper_32_bits(pa));
}

struct bfa_ioc_regs {
	void __iomem *hfn_mbox_cmd;
	void __iomem *hfn_mbox;
	void __iomem *lpu_mbox_cmd;
	void __iomem *lpu_mbox;
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

/**
 * IOC Mailbox structures
 */
struct bfa_mbox_cmd {
	struct list_head	qe;
	u32			msg[BFI_IOC_MSGSZ];
};

/**
 * IOC mailbox module
 */
typedef void (*bfa_ioc_mbox_mcfunc_t)(void *cbarg, struct bfi_mbmsg *m);
struct bfa_ioc_mbox_mod {
	struct list_head	cmd_q;		/*!< pending mbox queue	*/
	int			nmclass;	/*!< number of handlers */
	struct {
		bfa_ioc_mbox_mcfunc_t	cbfn;	/*!< message handlers	*/
		void			*cbarg;
	} mbhdlr[BFI_MC_MAX];
};

/**
 * IOC callback function interfaces
 */
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

/**
 * Heartbeat failure notification queue element.
 */
struct bfa_ioc_hbfail_notify {
	struct list_head	qe;
	bfa_ioc_hbfail_cbfn_t	cbfn;
	void			*cbarg;
};

/**
 * Initialize a heartbeat failure notification structure
 */
#define bfa_ioc_hbfail_init(__notify, __cbfn, __cbarg) do {	\
	(__notify)->cbfn = (__cbfn);				\
	(__notify)->cbarg = (__cbarg);				\
} while (0)

struct bfa_iocpf {
	bfa_fsm_t		fsm;
	struct bfa_ioc		*ioc;
	u32			retry_count;
	bool			auto_recover;
};

struct bfa_ioc {
	bfa_fsm_t		fsm;
	struct bfa 		*bfa;
	struct bfa_pcidev 	pcidev;
	struct timer_list 	ioc_timer;
	struct timer_list 	iocpf_timer;
	struct timer_list 	sem_timer;
	struct timer_list	hb_timer;
	u32			hb_count;
	struct list_head	hb_notify_q;
	void			*dbg_fwsave;
	int			dbg_fwsave_len;
	bool			dbg_fwsave_once;
	enum bfi_mclass		ioc_mc;
	struct bfa_ioc_regs 	ioc_regs;
	struct bfa_ioc_drv_stats stats;
	bool			fcmode;
	bool			ctdev;
	bool			cna;
	bool			pllinit;
	bool   			stats_busy;	/*!< outstanding stats */
	u8			port_id;

	struct bfa_dma		attr_dma;
	struct bfi_ioc_attr	*attr;
	struct bfa_ioc_cbfn	*cbfn;
	struct bfa_ioc_mbox_mod	mbox_mod;
	struct bfa_ioc_hwif	*ioc_hwif;
	struct bfa_iocpf	iocpf;
};

struct bfa_ioc_hwif {
	enum bfa_status (*ioc_pll_init) (void __iomem *rb, bool fcmode);
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
};

#define bfa_ioc_pcifn(__ioc)		((__ioc)->pcidev.pci_func)
#define bfa_ioc_devid(__ioc)		((__ioc)->pcidev.device_id)
#define bfa_ioc_bar0(__ioc)		((__ioc)->pcidev.pci_bar_kva)
#define bfa_ioc_portid(__ioc)		((__ioc)->port_id)
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
#define BFA_IOC_FWIMG_TYPE(__ioc)					\
	(((__ioc)->ctdev) ? 						\
	 (((__ioc)->fcmode) ? BFI_IMAGE_CT_FC : BFI_IMAGE_CT_CNA) :	\
	 BFI_IMAGE_CB_FC)
#define BFA_IOC_FW_SMEM_SIZE(__ioc)					\
	(((__ioc)->ctdev) ? BFI_SMEM_CT_SIZE : BFI_SMEM_CB_SIZE)
#define BFA_IOC_FLASH_CHUNK_NO(off)		(off / BFI_FLASH_CHUNK_SZ_WORDS)
#define BFA_IOC_FLASH_OFFSET_IN_CHUNK(off)	(off % BFI_FLASH_CHUNK_SZ_WORDS)
#define BFA_IOC_FLASH_CHUNK_ADDR(chunkno)  (chunkno * BFI_FLASH_CHUNK_SZ_WORDS)

/**
 * IOC mailbox interface
 */
void bfa_nw_ioc_mbox_queue(struct bfa_ioc *ioc, struct bfa_mbox_cmd *cmd);
void bfa_nw_ioc_mbox_isr(struct bfa_ioc *ioc);
void bfa_nw_ioc_mbox_regisr(struct bfa_ioc *ioc, enum bfi_mclass mc,
		bfa_ioc_mbox_mcfunc_t cbfn, void *cbarg);

/**
 * IOC interfaces
 */

#define bfa_ioc_pll_init_asic(__ioc) \
	((__ioc)->ioc_hwif->ioc_pll_init((__ioc)->pcidev.pci_bar_kva, \
			   (__ioc)->fcmode))

#define	bfa_ioc_isr_mode_set(__ioc, __msix)			\
			((__ioc)->ioc_hwif->ioc_isr_mode_set(__ioc, __msix))
#define	bfa_ioc_ownership_reset(__ioc)				\
			((__ioc)->ioc_hwif->ioc_ownership_reset(__ioc))

void bfa_nw_ioc_set_ct_hwif(struct bfa_ioc *ioc);

void bfa_nw_ioc_attach(struct bfa_ioc *ioc, void *bfa,
		struct bfa_ioc_cbfn *cbfn);
void bfa_nw_ioc_auto_recover(bool auto_recover);
void bfa_nw_ioc_detach(struct bfa_ioc *ioc);
void bfa_nw_ioc_pci_init(struct bfa_ioc *ioc, struct bfa_pcidev *pcidev,
		enum bfi_mclass mc);
u32 bfa_nw_ioc_meminfo(void);
void bfa_nw_ioc_mem_claim(struct bfa_ioc *ioc,  u8 *dm_kva, u64 dm_pa);
void bfa_nw_ioc_enable(struct bfa_ioc *ioc);
void bfa_nw_ioc_disable(struct bfa_ioc *ioc);

void bfa_nw_ioc_error_isr(struct bfa_ioc *ioc);
void bfa_nw_ioc_get_attr(struct bfa_ioc *ioc, struct bfa_ioc_attr *ioc_attr);
void bfa_nw_ioc_hbfail_register(struct bfa_ioc *ioc,
	struct bfa_ioc_hbfail_notify *notify);
bool bfa_nw_ioc_sem_get(void __iomem *sem_reg);
void bfa_nw_ioc_sem_release(void __iomem *sem_reg);
void bfa_nw_ioc_hw_sem_release(struct bfa_ioc *ioc);
void bfa_nw_ioc_fwver_get(struct bfa_ioc *ioc,
			struct bfi_ioc_image_hdr *fwhdr);
bool bfa_nw_ioc_fwver_cmp(struct bfa_ioc *ioc,
			struct bfi_ioc_image_hdr *fwhdr);
mac_t bfa_nw_ioc_get_mac(struct bfa_ioc *ioc);

/*
 * Timeout APIs
 */
void bfa_nw_ioc_timeout(void *ioc);
void bfa_nw_ioc_hb_check(void *ioc);
void bfa_nw_iocpf_timeout(void *ioc);
void bfa_nw_iocpf_sem_timeout(void *ioc);

/*
 * F/W Image Size & Chunk
 */
u32 *bfa_cb_image_get_chunk(int type, u32 off);
u32 bfa_cb_image_get_size(int type);

#endif /* __BFA_IOC_H__ */
