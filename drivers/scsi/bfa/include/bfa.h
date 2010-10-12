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
#ifndef __BFA_H__
#define __BFA_H__

#include <bfa_os_inc.h>
#include <cs/bfa_debug.h>
#include <cs/bfa_q.h>
#include <cs/bfa_trc.h>
#include <cs/bfa_log.h>
#include <cs/bfa_plog.h>
#include <defs/bfa_defs_status.h>
#include <defs/bfa_defs_ioc.h>
#include <defs/bfa_defs_iocfc.h>
#include <aen/bfa_aen.h>
#include <bfi/bfi.h>

struct bfa_s;
#include <bfa_intr_priv.h>

struct bfa_pcidev_s;

/**
 * PCI devices supported by the current BFA
 */
struct bfa_pciid_s {
	u16        device_id;
	u16        vendor_id;
};

extern char     bfa_version[];

/**
 * BFA Power Mgmt Commands
 */
enum bfa_pm_cmd {
	BFA_PM_CTL_D0 = 0,
	BFA_PM_CTL_D1 = 1,
	BFA_PM_CTL_D2 = 2,
	BFA_PM_CTL_D3 = 3,
};

/**
 * BFA memory resources
 */
enum bfa_mem_type {
	BFA_MEM_TYPE_KVA = 1,	/*! Kernel Virtual Memory *(non-dma-able) */
	BFA_MEM_TYPE_DMA = 2,	/*! DMA-able memory */
	BFA_MEM_TYPE_MAX = BFA_MEM_TYPE_DMA,
};

struct bfa_mem_elem_s {
	enum bfa_mem_type mem_type;	/*  see enum bfa_mem_type 	*/
	u32        mem_len;	/*  Total Length in Bytes	*/
	u8       	*kva;		/*  kernel virtual address	*/
	u64        dma;		/*  dma address if DMA memory	*/
	u8       	*kva_curp;	/*  kva allocation cursor	*/
	u64        dma_curp;	/*  dma allocation cursor	*/
};

struct bfa_meminfo_s {
	struct bfa_mem_elem_s meminfo[BFA_MEM_TYPE_MAX];
};
#define bfa_meminfo_kva(_m)	\
	((_m)->meminfo[BFA_MEM_TYPE_KVA - 1].kva_curp)
#define bfa_meminfo_dma_virt(_m)	\
	((_m)->meminfo[BFA_MEM_TYPE_DMA - 1].kva_curp)
#define bfa_meminfo_dma_phys(_m)	\
	((_m)->meminfo[BFA_MEM_TYPE_DMA - 1].dma_curp)

/**
 * Generic Scatter Gather Element used by driver
 */
struct bfa_sge_s {
	u32        sg_len;
	void           *sg_addr;
};

#define bfa_sge_to_be(__sge) do {                                          \
	((u32 *)(__sge))[0] = bfa_os_htonl(((u32 *)(__sge))[0]);      \
	((u32 *)(__sge))[1] = bfa_os_htonl(((u32 *)(__sge))[1]);      \
	((u32 *)(__sge))[2] = bfa_os_htonl(((u32 *)(__sge))[2]);      \
} while (0)


/*
 * bfa stats interfaces
 */
#define bfa_stats(_mod, _stats)	((_mod)->stats._stats++)

#define bfa_ioc_get_stats(__bfa, __ioc_stats)	\
	bfa_ioc_fetch_stats(&(__bfa)->ioc, __ioc_stats)
#define bfa_ioc_clear_stats(__bfa)	\
	bfa_ioc_clr_stats(&(__bfa)->ioc)
#define bfa_get_nports(__bfa)   \
	bfa_ioc_get_nports(&(__bfa)->ioc)
#define bfa_get_adapter_manufacturer(__bfa, __manufacturer) \
	bfa_ioc_get_adapter_manufacturer(&(__bfa)->ioc, __manufacturer)
#define bfa_get_adapter_model(__bfa, __model)   \
	bfa_ioc_get_adapter_model(&(__bfa)->ioc, __model)
#define bfa_get_adapter_serial_num(__bfa, __serial_num) \
	bfa_ioc_get_adapter_serial_num(&(__bfa)->ioc, __serial_num)
#define bfa_get_adapter_fw_ver(__bfa, __fw_ver) \
	bfa_ioc_get_adapter_fw_ver(&(__bfa)->ioc, __fw_ver)
#define bfa_get_adapter_optrom_ver(__bfa, __optrom_ver) \
	bfa_ioc_get_adapter_optrom_ver(&(__bfa)->ioc, __optrom_ver)
#define bfa_get_pci_chip_rev(__bfa, __chip_rev) \
	bfa_ioc_get_pci_chip_rev(&(__bfa)->ioc, __chip_rev)
#define bfa_get_ioc_state(__bfa)    \
	bfa_ioc_get_state(&(__bfa)->ioc)
#define bfa_get_type(__bfa) \
	bfa_ioc_get_type(&(__bfa)->ioc)
#define bfa_get_mac(__bfa)  \
	bfa_ioc_get_mac(&(__bfa)->ioc)
#define bfa_get_mfg_mac(__bfa)  \
	bfa_ioc_get_mfg_mac(&(__bfa)->ioc)
#define bfa_get_fw_clock_res(__bfa)    \
	((__bfa)->iocfc.cfgrsp->fwcfg.fw_tick_res)

/*
 * bfa API functions
 */
void bfa_get_pciids(struct bfa_pciid_s **pciids, int *npciids);
void bfa_cfg_get_default(struct bfa_iocfc_cfg_s *cfg);
void bfa_cfg_get_min(struct bfa_iocfc_cfg_s *cfg);
void bfa_cfg_get_meminfo(struct bfa_iocfc_cfg_s *cfg,
			struct bfa_meminfo_s *meminfo);
void bfa_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
			struct bfa_meminfo_s *meminfo,
			struct bfa_pcidev_s *pcidev);
void bfa_init_trc(struct bfa_s *bfa, struct bfa_trc_mod_s *trcmod);
void bfa_init_log(struct bfa_s *bfa, struct bfa_log_mod_s *logmod);
void bfa_init_aen(struct bfa_s *bfa, struct bfa_aen_s *aen);
void bfa_init_plog(struct bfa_s *bfa, struct bfa_plog_s *plog);
void bfa_detach(struct bfa_s *bfa);
void bfa_init(struct bfa_s *bfa);
void bfa_start(struct bfa_s *bfa);
void bfa_stop(struct bfa_s *bfa);
void bfa_attach_fcs(struct bfa_s *bfa);
void bfa_cb_init(void *bfad, bfa_status_t status);
void bfa_cb_stop(void *bfad, bfa_status_t status);
void bfa_cb_updateq(void *bfad, bfa_status_t status);

bfa_boolean_t bfa_intx(struct bfa_s *bfa);
void bfa_isr_enable(struct bfa_s *bfa);
void bfa_isr_disable(struct bfa_s *bfa);
void bfa_msix_getvecs(struct bfa_s *bfa, u32 *msix_vecs_bmap,
			u32 *num_vecs, u32 *max_vec_bit);
#define bfa_msix(__bfa, __vec) ((__bfa)->msix.handler[__vec](__bfa, __vec))

void bfa_comp_deq(struct bfa_s *bfa, struct list_head *comp_q);
void bfa_comp_process(struct bfa_s *bfa, struct list_head *comp_q);
void bfa_comp_free(struct bfa_s *bfa, struct list_head *comp_q);

typedef void (*bfa_cb_ioc_t) (void *cbarg, enum bfa_status status);
void bfa_iocfc_get_attr(struct bfa_s *bfa, struct bfa_iocfc_attr_s *attr);
bfa_status_t bfa_iocfc_get_stats(struct bfa_s *bfa,
			struct bfa_iocfc_stats_s *stats,
			bfa_cb_ioc_t cbfn, void *cbarg);
bfa_status_t bfa_iocfc_clear_stats(struct bfa_s *bfa,
			bfa_cb_ioc_t cbfn, void *cbarg);
void bfa_get_attr(struct bfa_s *bfa, struct bfa_ioc_attr_s *ioc_attr);

void bfa_adapter_get_attr(struct bfa_s *bfa,
			struct bfa_adapter_attr_s *ad_attr);
u64 bfa_adapter_get_id(struct bfa_s *bfa);

bfa_status_t bfa_iocfc_israttr_set(struct bfa_s *bfa,
			struct bfa_iocfc_intr_attr_s *attr);

void bfa_iocfc_enable(struct bfa_s *bfa);
void bfa_iocfc_disable(struct bfa_s *bfa);
void bfa_ioc_auto_recover(bfa_boolean_t auto_recover);
void bfa_chip_reset(struct bfa_s *bfa);
void bfa_cb_ioc_disable(void *bfad);
void bfa_timer_tick(struct bfa_s *bfa);
#define bfa_timer_start(_bfa, _timer, _timercb, _arg, _timeout)	\
	bfa_timer_begin(&(_bfa)->timer_mod, _timer, _timercb, _arg, _timeout)

/*
 * BFA debug API functions
 */
bfa_status_t bfa_debug_fwtrc(struct bfa_s *bfa, void *trcdata, int *trclen);
bfa_status_t bfa_debug_fwsave(struct bfa_s *bfa, void *trcdata, int *trclen);
void bfa_debug_fwsave_clear(struct bfa_s *bfa);

#include "bfa_priv.h"

#endif /* __BFA_H__ */
