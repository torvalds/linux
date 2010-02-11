/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
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

#ifndef __BFA_IOCFC_H__
#define __BFA_IOCFC_H__

#include <bfa_ioc.h>
#include <bfa.h>
#include <bfi/bfi_iocfc.h>
#include <bfa_callback_priv.h>

#define BFA_REQQ_NELEMS_MIN	(4)
#define BFA_RSPQ_NELEMS_MIN	(4)

struct bfa_iocfc_regs_s {
	bfa_os_addr_t   intr_status;
	bfa_os_addr_t   intr_mask;
	bfa_os_addr_t   cpe_q_pi[BFI_IOC_MAX_CQS];
	bfa_os_addr_t   cpe_q_ci[BFI_IOC_MAX_CQS];
	bfa_os_addr_t   cpe_q_depth[BFI_IOC_MAX_CQS];
	bfa_os_addr_t   cpe_q_ctrl[BFI_IOC_MAX_CQS];
	bfa_os_addr_t   rme_q_ci[BFI_IOC_MAX_CQS];
	bfa_os_addr_t   rme_q_pi[BFI_IOC_MAX_CQS];
	bfa_os_addr_t   rme_q_depth[BFI_IOC_MAX_CQS];
	bfa_os_addr_t   rme_q_ctrl[BFI_IOC_MAX_CQS];
};

/**
 * MSIX vector handlers
 */
#define BFA_MSIX_MAX_VECTORS	22
typedef void (*bfa_msix_handler_t)(struct bfa_s *bfa, int vec);
struct bfa_msix_s {
	int	nvecs;
	bfa_msix_handler_t handler[BFA_MSIX_MAX_VECTORS];
};

/**
 * Chip specific interfaces
 */
struct bfa_hwif_s {
	void (*hw_reginit)(struct bfa_s *bfa);
	void (*hw_rspq_ack)(struct bfa_s *bfa, int rspq);
	void (*hw_msix_init)(struct bfa_s *bfa, int nvecs);
	void (*hw_msix_install)(struct bfa_s *bfa);
	void (*hw_msix_uninstall)(struct bfa_s *bfa);
	void (*hw_isr_mode_set)(struct bfa_s *bfa, bfa_boolean_t msix);
	void (*hw_msix_getvecs)(struct bfa_s *bfa, u32 *vecmap,
			u32 *nvecs, u32 *maxvec);
};
typedef void (*bfa_cb_iocfc_t) (void *cbarg, enum bfa_status status);

struct bfa_iocfc_s {
	struct bfa_s 		*bfa;
	struct bfa_iocfc_cfg_s 	cfg;
	int			action;

	u32        	req_cq_pi[BFI_IOC_MAX_CQS];
	u32        	rsp_cq_ci[BFI_IOC_MAX_CQS];

	struct bfa_cb_qe_s	init_hcb_qe;
	struct bfa_cb_qe_s	stop_hcb_qe;
	struct bfa_cb_qe_s	dis_hcb_qe;
	struct bfa_cb_qe_s	stats_hcb_qe;
	bfa_boolean_t		cfgdone;

	struct bfa_dma_s	cfg_info;
	struct bfi_iocfc_cfg_s *cfginfo;
	struct bfa_dma_s	cfgrsp_dma;
	struct bfi_iocfc_cfgrsp_s *cfgrsp;
	struct bfi_iocfc_cfg_reply_s *cfg_reply;

	u8			*stats_kva;
	u64		stats_pa;
	struct bfa_fw_stats_s 	*fw_stats;
	struct bfa_timer_s 	stats_timer;	/*  timer */
	struct bfa_iocfc_stats_s *stats_ret;	/*  driver stats location */
	bfa_status_t		stats_status;	/*  stats/statsclr status */
	bfa_boolean_t   	stats_busy;	/*  outstanding stats */
	bfa_cb_ioc_t		stats_cbfn;	/*  driver callback function */
	void           		*stats_cbarg;	/*  user callback arg */

	struct bfa_dma_s   	req_cq_ba[BFI_IOC_MAX_CQS];
	struct bfa_dma_s   	req_cq_shadow_ci[BFI_IOC_MAX_CQS];
	struct bfa_dma_s   	rsp_cq_ba[BFI_IOC_MAX_CQS];
	struct bfa_dma_s   	rsp_cq_shadow_pi[BFI_IOC_MAX_CQS];
	struct bfa_iocfc_regs_s	bfa_regs;	/*  BFA device registers */
	struct bfa_hwif_s	hwif;

	bfa_cb_iocfc_t		updateq_cbfn; /*  bios callback function */
	void				*updateq_cbarg;	/*  bios callback arg */
};

#define bfa_lpuid(__bfa)		bfa_ioc_portid(&(__bfa)->ioc)
#define bfa_msix_init(__bfa, __nvecs)	\
	((__bfa)->iocfc.hwif.hw_msix_init(__bfa, __nvecs))
#define bfa_msix_install(__bfa)	\
	((__bfa)->iocfc.hwif.hw_msix_install(__bfa))
#define bfa_msix_uninstall(__bfa)	\
	((__bfa)->iocfc.hwif.hw_msix_uninstall(__bfa))
#define bfa_isr_mode_set(__bfa, __msix)	\
	((__bfa)->iocfc.hwif.hw_isr_mode_set(__bfa, __msix))
#define bfa_msix_getvecs(__bfa, __vecmap, __nvecs, __maxvec)	\
	(__bfa)->iocfc.hwif.hw_msix_getvecs(__bfa, __vecmap, __nvecs, __maxvec)

/*
 * FC specific IOC functions.
 */
void bfa_iocfc_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *km_len,
		u32 *dm_len);
void bfa_iocfc_attach(struct bfa_s *bfa, void *bfad,
		struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *meminfo,
		struct bfa_pcidev_s *pcidev);
void bfa_iocfc_detach(struct bfa_s *bfa);
void bfa_iocfc_init(struct bfa_s *bfa);
void bfa_iocfc_start(struct bfa_s *bfa);
void bfa_iocfc_stop(struct bfa_s *bfa);
void bfa_iocfc_isr(void *bfa, struct bfi_mbmsg_s *msg);
void bfa_iocfc_set_snsbase(struct bfa_s *bfa, u64 snsbase_pa);
bfa_boolean_t bfa_iocfc_is_operational(struct bfa_s *bfa);
void bfa_iocfc_reset_queues(struct bfa_s *bfa);
void bfa_iocfc_updateq(struct bfa_s *bfa, u32 reqq_ba, u32 rspq_ba,
			u32 reqq_sci, u32 rspq_spi,
			bfa_cb_iocfc_t cbfn, void *cbarg);

void bfa_msix_all(struct bfa_s *bfa, int vec);
void bfa_msix_reqq(struct bfa_s *bfa, int vec);
void bfa_msix_rspq(struct bfa_s *bfa, int vec);
void bfa_msix_lpu_err(struct bfa_s *bfa, int vec);

void bfa_hwcb_reginit(struct bfa_s *bfa);
void bfa_hwcb_rspq_ack(struct bfa_s *bfa, int rspq);
void bfa_hwcb_msix_init(struct bfa_s *bfa, int nvecs);
void bfa_hwcb_msix_install(struct bfa_s *bfa);
void bfa_hwcb_msix_uninstall(struct bfa_s *bfa);
void bfa_hwcb_isr_mode_set(struct bfa_s *bfa, bfa_boolean_t msix);
void bfa_hwcb_msix_getvecs(struct bfa_s *bfa, u32 *vecmap,
			u32 *nvecs, u32 *maxvec);
void bfa_hwct_reginit(struct bfa_s *bfa);
void bfa_hwct_rspq_ack(struct bfa_s *bfa, int rspq);
void bfa_hwct_msix_init(struct bfa_s *bfa, int nvecs);
void bfa_hwct_msix_install(struct bfa_s *bfa);
void bfa_hwct_msix_uninstall(struct bfa_s *bfa);
void bfa_hwct_isr_mode_set(struct bfa_s *bfa, bfa_boolean_t msix);
void bfa_hwct_msix_getvecs(struct bfa_s *bfa, u32 *vecmap,
			u32 *nvecs, u32 *maxvec);

void bfa_com_meminfo(bfa_boolean_t mincfg, u32 *dm_len);
void bfa_com_attach(struct bfa_s *bfa, struct bfa_meminfo_s *mi,
		bfa_boolean_t mincfg);
void bfa_iocfc_get_bootwwns(struct bfa_s *bfa, u8 *nwwns, wwn_t **wwns);

#endif /* __BFA_IOCFC_H__ */

