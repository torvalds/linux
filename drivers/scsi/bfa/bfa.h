/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
 */
#ifndef __BFA_H__
#define __BFA_H__

#include "bfad_drv.h"
#include "bfa_cs.h"
#include "bfa_plog.h"
#include "bfa_defs_svc.h"
#include "bfi.h"
#include "bfa_ioc.h"

struct bfa_s;

typedef void (*bfa_isr_func_t) (struct bfa_s *bfa, struct bfi_msg_s *m);

/*
 * Interrupt message handlers
 */
void bfa_isr_unhandled(struct bfa_s *bfa, struct bfi_msg_s *m);

/*
 * Request and response queue related defines
 */
#define BFA_REQQ_NELEMS_MIN	(4)
#define BFA_RSPQ_NELEMS_MIN	(4)

#define bfa_reqq_pi(__bfa, __reqq)	((__bfa)->iocfc.req_cq_pi[__reqq])
#define bfa_reqq_ci(__bfa, __reqq)					\
	(*(u32 *)((__bfa)->iocfc.req_cq_shadow_ci[__reqq].kva))

#define bfa_reqq_full(__bfa, __reqq)				\
	(((bfa_reqq_pi(__bfa, __reqq) + 1) &			\
	  ((__bfa)->iocfc.cfg.drvcfg.num_reqq_elems - 1)) ==	\
	 bfa_reqq_ci(__bfa, __reqq))

#define bfa_reqq_next(__bfa, __reqq)					\
	(bfa_reqq_full(__bfa, __reqq) ? NULL :				\
	 ((void *)((struct bfi_msg_s *)((__bfa)->iocfc.req_cq_ba[__reqq].kva) \
		   + bfa_reqq_pi((__bfa), (__reqq)))))

#define bfa_reqq_produce(__bfa, __reqq, __mh)  do {			\
		(__mh).mtag.h2i.qid     = (__bfa)->iocfc.hw_qid[__reqq];\
		(__bfa)->iocfc.req_cq_pi[__reqq]++;			\
		(__bfa)->iocfc.req_cq_pi[__reqq] &=			\
			((__bfa)->iocfc.cfg.drvcfg.num_reqq_elems - 1); \
		writel((__bfa)->iocfc.req_cq_pi[__reqq],		\
			(__bfa)->iocfc.bfa_regs.cpe_q_pi[__reqq]);	\
		} while (0)

#define bfa_rspq_pi(__bfa, __rspq)					\
	(*(u32 *)((__bfa)->iocfc.rsp_cq_shadow_pi[__rspq].kva))

#define bfa_rspq_ci(__bfa, __rspq)	((__bfa)->iocfc.rsp_cq_ci[__rspq])
#define bfa_rspq_elem(__bfa, __rspq, __ci)				\
	(&((struct bfi_msg_s *)((__bfa)->iocfc.rsp_cq_ba[__rspq].kva))[__ci])

#define CQ_INCR(__index, __size) do {			\
	(__index)++;					\
	(__index) &= ((__size) - 1);			\
} while (0)

/*
 * Circular queue usage assignments
 */
enum {
	BFA_REQQ_IOC	= 0,	/*  all low-priority IOC msgs	*/
	BFA_REQQ_FCXP	= 0,	/*  all FCXP messages		*/
	BFA_REQQ_LPS	= 0,	/*  all lport service msgs	*/
	BFA_REQQ_PORT	= 0,	/*  all port messages		*/
	BFA_REQQ_FLASH	= 0,	/*  for flash module		*/
	BFA_REQQ_DIAG	= 0,	/*  for diag module		*/
	BFA_REQQ_RPORT	= 0,	/*  all port messages		*/
	BFA_REQQ_SBOOT	= 0,	/*  all san boot messages	*/
	BFA_REQQ_QOS_LO	= 1,	/*  all low priority IO	*/
	BFA_REQQ_QOS_MD	= 2,	/*  all medium priority IO	*/
	BFA_REQQ_QOS_HI	= 3,	/*  all high priority IO	*/
};

static inline void
bfa_reqq_winit(struct bfa_reqq_wait_s *wqe, void (*qresume) (void *cbarg),
	       void *cbarg)
{
	wqe->qresume = qresume;
	wqe->cbarg = cbarg;
}

#define bfa_reqq(__bfa, __reqq)	(&(__bfa)->reqq_waitq[__reqq])

/*
 * static inline void
 * bfa_reqq_wait(struct bfa_s *bfa, int reqq, struct bfa_reqq_wait_s *wqe)
 */
#define bfa_reqq_wait(__bfa, __reqq, __wqe) do {			\
									\
		struct list_head *waitq = bfa_reqq(__bfa, __reqq);      \
									\
		WARN_ON(((__reqq) >= BFI_IOC_MAX_CQS));			\
		WARN_ON(!((__wqe)->qresume && (__wqe)->cbarg));		\
									\
		list_add_tail(&(__wqe)->qe, waitq);      \
	} while (0)

#define bfa_reqq_wcancel(__wqe)	list_del(&(__wqe)->qe)

#define bfa_cb_queue(__bfa, __hcb_qe, __cbfn, __cbarg) do {	\
		(__hcb_qe)->cbfn  = (__cbfn);      \
		(__hcb_qe)->cbarg = (__cbarg);      \
		(__hcb_qe)->pre_rmv = BFA_FALSE;		\
		list_add_tail(&(__hcb_qe)->qe, &(__bfa)->comp_q);      \
	} while (0)

#define bfa_cb_dequeue(__hcb_qe)	list_del(&(__hcb_qe)->qe)

#define bfa_cb_queue_once(__bfa, __hcb_qe, __cbfn, __cbarg) do {	\
		(__hcb_qe)->cbfn  = (__cbfn);      \
		(__hcb_qe)->cbarg = (__cbarg);      \
		if (!(__hcb_qe)->once) {      \
			list_add_tail(&(__hcb_qe)->qe, &(__bfa)->comp_q);      \
			(__hcb_qe)->once = BFA_TRUE;			\
		}							\
	} while (0)

#define bfa_cb_queue_status(__bfa, __hcb_qe, __status) do {		\
		(__hcb_qe)->fw_status = (__status);			\
		list_add_tail(&(__hcb_qe)->qe, &(__bfa)->comp_q);	\
} while (0)

#define bfa_cb_queue_done(__hcb_qe) do {	\
		(__hcb_qe)->once = BFA_FALSE;	\
	} while (0)


extern char     bfa_version[];

struct bfa_iocfc_regs_s {
	void __iomem	*intr_status;
	void __iomem	*intr_mask;
	void __iomem	*cpe_q_pi[BFI_IOC_MAX_CQS];
	void __iomem	*cpe_q_ci[BFI_IOC_MAX_CQS];
	void __iomem	*cpe_q_ctrl[BFI_IOC_MAX_CQS];
	void __iomem	*rme_q_ci[BFI_IOC_MAX_CQS];
	void __iomem	*rme_q_pi[BFI_IOC_MAX_CQS];
	void __iomem	*rme_q_ctrl[BFI_IOC_MAX_CQS];
};

/*
 * MSIX vector handlers
 */
#define BFA_MSIX_MAX_VECTORS	22
typedef void (*bfa_msix_handler_t)(struct bfa_s *bfa, int vec);
struct bfa_msix_s {
	int	nvecs;
	bfa_msix_handler_t handler[BFA_MSIX_MAX_VECTORS];
};

/*
 * Chip specific interfaces
 */
struct bfa_hwif_s {
	void (*hw_reginit)(struct bfa_s *bfa);
	void (*hw_reqq_ack)(struct bfa_s *bfa, int reqq);
	void (*hw_rspq_ack)(struct bfa_s *bfa, int rspq, u32 ci);
	void (*hw_msix_init)(struct bfa_s *bfa, int nvecs);
	void (*hw_msix_ctrl_install)(struct bfa_s *bfa);
	void (*hw_msix_queue_install)(struct bfa_s *bfa);
	void (*hw_msix_uninstall)(struct bfa_s *bfa);
	void (*hw_isr_mode_set)(struct bfa_s *bfa, bfa_boolean_t msix);
	void (*hw_msix_getvecs)(struct bfa_s *bfa, u32 *vecmap,
				u32 *nvecs, u32 *maxvec);
	void (*hw_msix_get_rme_range) (struct bfa_s *bfa, u32 *start,
				       u32 *end);
	int	cpe_vec_q0;
	int	rme_vec_q0;
};
typedef void (*bfa_cb_iocfc_t) (void *cbarg, enum bfa_status status);

struct bfa_faa_cbfn_s {
	bfa_cb_iocfc_t	faa_cbfn;
	void		*faa_cbarg;
};

#define BFA_FAA_ENABLED		1
#define BFA_FAA_DISABLED	2

/*
 *	FAA attributes
 */
struct bfa_faa_attr_s {
	wwn_t	faa;
	u8	faa_state;
	u8	pwwn_source;
	u8	rsvd[6];
};

struct bfa_faa_args_s {
	struct bfa_faa_attr_s	*faa_attr;
	struct bfa_faa_cbfn_s	faa_cb;
	u8			faa_state;
	bfa_boolean_t		busy;
};

/*
 * IOCFC state machine definitions/declarations
 */
enum iocfc_event {
	IOCFC_E_INIT		= 1,	/* IOCFC init request		*/
	IOCFC_E_START		= 2,	/* IOCFC mod start request	*/
	IOCFC_E_STOP		= 3,	/* IOCFC stop request		*/
	IOCFC_E_ENABLE		= 4,	/* IOCFC enable request		*/
	IOCFC_E_DISABLE		= 5,	/* IOCFC disable request	*/
	IOCFC_E_IOC_ENABLED	= 6,	/* IOC enabled message		*/
	IOCFC_E_IOC_DISABLED	= 7,	/* IOC disabled message		*/
	IOCFC_E_IOC_FAILED	= 8,	/* failure notice by IOC sm	*/
	IOCFC_E_DCONF_DONE	= 9,	/* dconf read/write done	*/
	IOCFC_E_CFG_DONE	= 10,	/* IOCFC config complete	*/
};

struct bfa_iocfc_s;
typedef void (*bfa_iocfs_fsm_t)(struct bfa_iocfc_s *, enum iocfc_event);

struct bfa_iocfc_s {
	bfa_iocfs_fsm_t		fsm;
	struct bfa_s		*bfa;
	struct bfa_iocfc_cfg_s	cfg;
	u32		req_cq_pi[BFI_IOC_MAX_CQS];
	u32		rsp_cq_ci[BFI_IOC_MAX_CQS];
	u8		hw_qid[BFI_IOC_MAX_CQS];
	struct bfa_cb_qe_s	init_hcb_qe;
	struct bfa_cb_qe_s	stop_hcb_qe;
	struct bfa_cb_qe_s	dis_hcb_qe;
	struct bfa_cb_qe_s	en_hcb_qe;
	struct bfa_cb_qe_s	stats_hcb_qe;
	bfa_boolean_t		submod_enabled;
	bfa_boolean_t		cb_reqd;	/* Driver call back reqd */
	bfa_status_t		op_status;	/* Status of bfa iocfc op */

	struct bfa_dma_s	cfg_info;
	struct bfi_iocfc_cfg_s *cfginfo;
	struct bfa_dma_s	cfgrsp_dma;
	struct bfi_iocfc_cfgrsp_s *cfgrsp;
	struct bfa_dma_s	req_cq_ba[BFI_IOC_MAX_CQS];
	struct bfa_dma_s	req_cq_shadow_ci[BFI_IOC_MAX_CQS];
	struct bfa_dma_s	rsp_cq_ba[BFI_IOC_MAX_CQS];
	struct bfa_dma_s	rsp_cq_shadow_pi[BFI_IOC_MAX_CQS];
	struct bfa_iocfc_regs_s	bfa_regs;	/*  BFA device registers */
	struct bfa_hwif_s	hwif;
	bfa_cb_iocfc_t		updateq_cbfn; /*  bios callback function */
	void			*updateq_cbarg;	/*  bios callback arg */
	u32	intr_mask;
	struct bfa_faa_args_s	faa_args;
	struct bfa_mem_dma_s	ioc_dma;
	struct bfa_mem_dma_s	iocfc_dma;
	struct bfa_mem_dma_s	reqq_dma[BFI_IOC_MAX_CQS];
	struct bfa_mem_dma_s	rspq_dma[BFI_IOC_MAX_CQS];
	struct bfa_mem_kva_s	kva_seg;
};

#define BFA_MEM_IOC_DMA(_bfa)		(&((_bfa)->iocfc.ioc_dma))
#define BFA_MEM_IOCFC_DMA(_bfa)		(&((_bfa)->iocfc.iocfc_dma))
#define BFA_MEM_REQQ_DMA(_bfa, _qno)	(&((_bfa)->iocfc.reqq_dma[(_qno)]))
#define BFA_MEM_RSPQ_DMA(_bfa, _qno)	(&((_bfa)->iocfc.rspq_dma[(_qno)]))
#define BFA_MEM_IOCFC_KVA(_bfa)		(&((_bfa)->iocfc.kva_seg))

#define bfa_fn_lpu(__bfa)	\
	bfi_fn_lpu(bfa_ioc_pcifn(&(__bfa)->ioc), bfa_ioc_portid(&(__bfa)->ioc))
#define bfa_msix_init(__bfa, __nvecs)					\
	((__bfa)->iocfc.hwif.hw_msix_init(__bfa, __nvecs))
#define bfa_msix_ctrl_install(__bfa)					\
	((__bfa)->iocfc.hwif.hw_msix_ctrl_install(__bfa))
#define bfa_msix_queue_install(__bfa)					\
	((__bfa)->iocfc.hwif.hw_msix_queue_install(__bfa))
#define bfa_msix_uninstall(__bfa)					\
	((__bfa)->iocfc.hwif.hw_msix_uninstall(__bfa))
#define bfa_isr_rspq_ack(__bfa, __queue, __ci)				\
	((__bfa)->iocfc.hwif.hw_rspq_ack(__bfa, __queue, __ci))
#define bfa_isr_reqq_ack(__bfa, __queue) do {				\
	if ((__bfa)->iocfc.hwif.hw_reqq_ack)				\
		(__bfa)->iocfc.hwif.hw_reqq_ack(__bfa, __queue);	\
} while (0)
#define bfa_isr_mode_set(__bfa, __msix) do {				\
	if ((__bfa)->iocfc.hwif.hw_isr_mode_set)			\
		(__bfa)->iocfc.hwif.hw_isr_mode_set(__bfa, __msix);	\
} while (0)
#define bfa_msix_getvecs(__bfa, __vecmap, __nvecs, __maxvec)		\
	((__bfa)->iocfc.hwif.hw_msix_getvecs(__bfa, __vecmap,		\
					__nvecs, __maxvec))
#define bfa_msix_get_rme_range(__bfa, __start, __end)			\
	((__bfa)->iocfc.hwif.hw_msix_get_rme_range(__bfa, __start, __end))
#define bfa_msix(__bfa, __vec)						\
	((__bfa)->msix.handler[__vec](__bfa, __vec))

/*
 * FC specific IOC functions.
 */
void bfa_iocfc_meminfo(struct bfa_iocfc_cfg_s *cfg,
			struct bfa_meminfo_s *meminfo,
			struct bfa_s *bfa);
void bfa_iocfc_attach(struct bfa_s *bfa, void *bfad,
		      struct bfa_iocfc_cfg_s *cfg,
		      struct bfa_pcidev_s *pcidev);
void bfa_iocfc_init(struct bfa_s *bfa);
void bfa_iocfc_start(struct bfa_s *bfa);
void bfa_iocfc_stop(struct bfa_s *bfa);
void bfa_iocfc_isr(void *bfa, struct bfi_mbmsg_s *msg);
void bfa_iocfc_set_snsbase(struct bfa_s *bfa, int seg_no, u64 snsbase_pa);
bfa_boolean_t bfa_iocfc_is_operational(struct bfa_s *bfa);
void bfa_iocfc_reset_queues(struct bfa_s *bfa);

void bfa_msix_all(struct bfa_s *bfa, int vec);
void bfa_msix_reqq(struct bfa_s *bfa, int vec);
void bfa_msix_rspq(struct bfa_s *bfa, int vec);
void bfa_msix_lpu_err(struct bfa_s *bfa, int vec);

void bfa_hwcb_reginit(struct bfa_s *bfa);
void bfa_hwcb_rspq_ack(struct bfa_s *bfa, int rspq, u32 ci);
void bfa_hwcb_msix_init(struct bfa_s *bfa, int nvecs);
void bfa_hwcb_msix_ctrl_install(struct bfa_s *bfa);
void bfa_hwcb_msix_queue_install(struct bfa_s *bfa);
void bfa_hwcb_msix_uninstall(struct bfa_s *bfa);
void bfa_hwcb_isr_mode_set(struct bfa_s *bfa, bfa_boolean_t msix);
void bfa_hwcb_msix_getvecs(struct bfa_s *bfa, u32 *vecmap, u32 *nvecs,
			   u32 *maxvec);
void bfa_hwcb_msix_get_rme_range(struct bfa_s *bfa, u32 *start,
				 u32 *end);
void bfa_hwct_reginit(struct bfa_s *bfa);
void bfa_hwct2_reginit(struct bfa_s *bfa);
void bfa_hwct_reqq_ack(struct bfa_s *bfa, int rspq);
void bfa_hwct_rspq_ack(struct bfa_s *bfa, int rspq, u32 ci);
void bfa_hwct2_rspq_ack(struct bfa_s *bfa, int rspq, u32 ci);
void bfa_hwct_msix_init(struct bfa_s *bfa, int nvecs);
void bfa_hwct_msix_ctrl_install(struct bfa_s *bfa);
void bfa_hwct_msix_queue_install(struct bfa_s *bfa);
void bfa_hwct_msix_uninstall(struct bfa_s *bfa);
void bfa_hwct_isr_mode_set(struct bfa_s *bfa, bfa_boolean_t msix);
void bfa_hwct_msix_getvecs(struct bfa_s *bfa, u32 *vecmap, u32 *nvecs,
			   u32 *maxvec);
void bfa_hwct_msix_get_rme_range(struct bfa_s *bfa, u32 *start,
				 u32 *end);
void bfa_iocfc_get_bootwwns(struct bfa_s *bfa, u8 *nwwns, wwn_t *wwns);
int bfa_iocfc_get_pbc_vports(struct bfa_s *bfa,
				struct bfi_pbc_vport_s *pbc_vport);


/*
 *----------------------------------------------------------------------
 *		BFA public interfaces
 *----------------------------------------------------------------------
 */
#define bfa_stats(_mod, _stats)	((_mod)->stats._stats++)
#define bfa_ioc_get_stats(__bfa, __ioc_stats)		\
	bfa_ioc_fetch_stats(&(__bfa)->ioc, __ioc_stats)
#define bfa_ioc_clear_stats(__bfa)		\
	bfa_ioc_clr_stats(&(__bfa)->ioc)
#define bfa_get_nports(__bfa)			\
	bfa_ioc_get_nports(&(__bfa)->ioc)
#define bfa_get_adapter_manufacturer(__bfa, __manufacturer)		\
	bfa_ioc_get_adapter_manufacturer(&(__bfa)->ioc, __manufacturer)
#define bfa_get_adapter_model(__bfa, __model)			\
	bfa_ioc_get_adapter_model(&(__bfa)->ioc, __model)
#define bfa_get_adapter_serial_num(__bfa, __serial_num)			\
	bfa_ioc_get_adapter_serial_num(&(__bfa)->ioc, __serial_num)
#define bfa_get_adapter_fw_ver(__bfa, __fw_ver)			\
	bfa_ioc_get_adapter_fw_ver(&(__bfa)->ioc, __fw_ver)
#define bfa_get_adapter_optrom_ver(__bfa, __optrom_ver)			\
	bfa_ioc_get_adapter_optrom_ver(&(__bfa)->ioc, __optrom_ver)
#define bfa_get_pci_chip_rev(__bfa, __chip_rev)			\
	bfa_ioc_get_pci_chip_rev(&(__bfa)->ioc, __chip_rev)
#define bfa_get_ioc_state(__bfa)		\
	bfa_ioc_get_state(&(__bfa)->ioc)
#define bfa_get_type(__bfa)			\
	bfa_ioc_get_type(&(__bfa)->ioc)
#define bfa_get_mac(__bfa)			\
	bfa_ioc_get_mac(&(__bfa)->ioc)
#define bfa_get_mfg_mac(__bfa)			\
	bfa_ioc_get_mfg_mac(&(__bfa)->ioc)
#define bfa_get_fw_clock_res(__bfa)		\
	((__bfa)->iocfc.cfgrsp->fwcfg.fw_tick_res)

/*
 * lun mask macros return NULL when min cfg is enabled and there is
 * no memory allocated for lunmask.
 */
#define bfa_get_lun_mask(__bfa)					\
	((&(__bfa)->modules.dconf_mod)->min_cfg) ? NULL :	\
	 (&(BFA_DCONF_MOD(__bfa)->dconf->lun_mask))

#define bfa_get_lun_mask_list(_bfa)				\
	((&(_bfa)->modules.dconf_mod)->min_cfg) ? NULL :	\
	 (bfa_get_lun_mask(_bfa)->lun_list)

#define bfa_get_lun_mask_status(_bfa)				\
	(((&(_bfa)->modules.dconf_mod)->min_cfg)		\
	 ? BFA_LUNMASK_MINCFG : ((bfa_get_lun_mask(_bfa))->status))

void bfa_cfg_get_default(struct bfa_iocfc_cfg_s *cfg);
void bfa_cfg_get_meminfo(struct bfa_iocfc_cfg_s *cfg,
			struct bfa_meminfo_s *meminfo,
			struct bfa_s *bfa);
void bfa_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		struct bfa_meminfo_s *meminfo,
		struct bfa_pcidev_s *pcidev);
void bfa_detach(struct bfa_s *bfa);
void bfa_cb_init(void *bfad, bfa_status_t status);
void bfa_cb_updateq(void *bfad, bfa_status_t status);

bfa_boolean_t bfa_intx(struct bfa_s *bfa);
void bfa_isr_enable(struct bfa_s *bfa);
void bfa_isr_disable(struct bfa_s *bfa);

void bfa_comp_deq(struct bfa_s *bfa, struct list_head *comp_q);
void bfa_comp_process(struct bfa_s *bfa, struct list_head *comp_q);
void bfa_comp_free(struct bfa_s *bfa, struct list_head *comp_q);

typedef void (*bfa_cb_ioc_t) (void *cbarg, enum bfa_status status);
void bfa_iocfc_get_attr(struct bfa_s *bfa, struct bfa_iocfc_attr_s *attr);


bfa_status_t bfa_iocfc_israttr_set(struct bfa_s *bfa,
				   struct bfa_iocfc_intr_attr_s *attr);

void bfa_iocfc_enable(struct bfa_s *bfa);
void bfa_iocfc_disable(struct bfa_s *bfa);
#define bfa_timer_start(_bfa, _timer, _timercb, _arg, _timeout)		\
	bfa_timer_begin(&(_bfa)->timer_mod, _timer, _timercb, _arg, _timeout)

struct bfa_cb_pending_q_s {
	struct bfa_cb_qe_s	hcb_qe;
	void			*data;  /* Driver buffer */
};

/* Common macros to operate on pending stats/attr apis */
#define bfa_pending_q_init(__qe, __cbfn, __cbarg, __data) do {	\
	bfa_q_qe_init(&((__qe)->hcb_qe.qe));			\
	(__qe)->hcb_qe.cbfn = (__cbfn);				\
	(__qe)->hcb_qe.cbarg = (__cbarg);			\
	(__qe)->hcb_qe.pre_rmv = BFA_TRUE;			\
	(__qe)->data = (__data);				\
} while (0)

#define bfa_pending_q_init_status(__qe, __cbfn, __cbarg, __data) do {	\
	bfa_q_qe_init(&((__qe)->hcb_qe.qe));			\
	(__qe)->hcb_qe.cbfn_status = (__cbfn);			\
	(__qe)->hcb_qe.cbarg = (__cbarg);			\
	(__qe)->hcb_qe.pre_rmv = BFA_TRUE;			\
	(__qe)->data = (__data);				\
} while (0)

#endif /* __BFA_H__ */
