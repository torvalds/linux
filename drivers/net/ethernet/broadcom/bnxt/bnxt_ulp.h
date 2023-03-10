/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016-2018 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_ULP_H
#define BNXT_ULP_H

#define BNXT_ROCE_ULP	0
#define BNXT_MAX_ULP	1

#define BNXT_MIN_ROCE_CP_RINGS	2
#define BNXT_MIN_ROCE_STAT_CTXS	1
#define BNXT_MAX_ROCE_MSIX	9
#define BNXT_MAX_VF_ROCE_MSIX	2

struct hwrm_async_event_cmpl;
struct bnxt;

struct bnxt_msix_entry {
	u32	vector;
	u32	ring_idx;
	u32	db_offset;
};

struct bnxt_ulp_ops {
	void (*ulp_irq_stop)(void *);
	void (*ulp_irq_restart)(void *, struct bnxt_msix_entry *);
};

struct bnxt_fw_msg {
	void	*msg;
	int	msg_len;
	void	*resp;
	int	resp_max_len;
	int	timeout;
};

struct bnxt_ulp {
	void		*handle;
	struct bnxt_ulp_ops __rcu *ulp_ops;
	unsigned long	*async_events_bmap;
	u16		max_async_event_id;
	u16		msix_requested;
	u16		msix_base;
	atomic_t	ref_count;
};

struct bnxt_en_dev {
	struct net_device *net;
	struct pci_dev *pdev;
	struct bnxt_msix_entry			msix_entries[BNXT_MAX_ROCE_MSIX];
	u32 flags;
	#define BNXT_EN_FLAG_ROCEV1_CAP		0x1
	#define BNXT_EN_FLAG_ROCEV2_CAP		0x2
	#define BNXT_EN_FLAG_ROCE_CAP		(BNXT_EN_FLAG_ROCEV1_CAP | \
						 BNXT_EN_FLAG_ROCEV2_CAP)
	#define BNXT_EN_FLAG_MSIX_REQUESTED	0x4
	#define BNXT_EN_FLAG_ULP_STOPPED	0x8
	#define BNXT_EN_FLAG_VF			0x10
#define BNXT_EN_VF(edev)	((edev)->flags & BNXT_EN_FLAG_VF)

	struct bnxt_ulp			*ulp_tbl;
	int				l2_db_size;	/* Doorbell BAR size in
							 * bytes mapped by L2
							 * driver.
							 */
	int				l2_db_size_nc;	/* Doorbell BAR size in
							 * bytes mapped as non-
							 * cacheable.
							 */
	u16				chip_num;
	u16				hw_ring_stats_size;
	u16				pf_port_id;
	unsigned long			en_state;	/* Could be checked in
							 * RoCE driver suspend
							 * mode only. Will be
							 * updated in resume.
							 */
};

static inline bool bnxt_ulp_registered(struct bnxt_en_dev *edev)
{
	if (edev && edev->ulp_tbl)
		return true;
	return false;
}

int bnxt_get_ulp_msix_num(struct bnxt *bp);
int bnxt_get_ulp_msix_base(struct bnxt *bp);
int bnxt_get_ulp_stat_ctxs(struct bnxt *bp);
void bnxt_ulp_stop(struct bnxt *bp);
void bnxt_ulp_start(struct bnxt *bp, int err);
void bnxt_ulp_sriov_cfg(struct bnxt *bp, int num_vfs);
void bnxt_ulp_irq_stop(struct bnxt *bp);
void bnxt_ulp_irq_restart(struct bnxt *bp, int err);
void bnxt_ulp_async_events(struct bnxt *bp, struct hwrm_async_event_cmpl *cmpl);
void bnxt_rdma_aux_device_uninit(struct bnxt *bp);
void bnxt_rdma_aux_device_init(struct bnxt *bp);
int bnxt_register_dev(struct bnxt_en_dev *edev, struct bnxt_ulp_ops *ulp_ops,
		      void *handle);
void bnxt_unregister_dev(struct bnxt_en_dev *edev);
int bnxt_send_msg(struct bnxt_en_dev *edev, struct bnxt_fw_msg *fw_msg);
int bnxt_register_async_events(struct bnxt_en_dev *edev,
			       unsigned long *events_bmap, u16 max_id);
#endif
