/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_AUXR_H_
#define _BNGE_AUXR_H_

#include <linux/auxiliary_bus.h>

#define BNGE_MIN_ROCE_CP_RINGS	2
#define BNGE_MIN_ROCE_STAT_CTXS	1

#define BNGE_MAX_ROCE_MSIX	64

struct hwrm_async_event_cmpl;
struct bnge;

struct bnge_msix_info {
	u32	vector;
	u32	ring_idx;
	u32	db_offset;
};

struct bnge_fw_msg {
	void	*msg;
	int	msg_len;
	void	*resp;
	int	resp_max_len;
	int	timeout;
};

struct bnge_auxr_info {
	void		*handle;
	u16		msix_requested;
};

enum {
	BNGE_ARDEV_ROCEV1_SUPP		= BIT(0),
	BNGE_ARDEV_ROCEV2_SUPP		= BIT(1),
	BNGE_ARDEV_MSIX_ALLOC		= BIT(2),
};

#define BNGE_ARDEV_ROCE_SUPP	(BNGE_ARDEV_ROCEV1_SUPP | \
				 BNGE_ARDEV_ROCEV2_SUPP)

struct bnge_auxr_dev {
	struct net_device	*net;
	struct pci_dev		*pdev;
	void __iomem		*bar0;

	struct bnge_msix_info	msix_info[BNGE_MAX_ROCE_MSIX];

	u32 flags;

	struct bnge_auxr_info	*auxr_info;

	/* Doorbell BAR size in bytes mapped by L2 driver. */
	int	l2_db_size;
	/* Doorbell BAR size in bytes mapped as non-cacheable. */
	int	l2_db_size_nc;
	/* Doorbell offset in bytes within l2_db_size_nc. */
	int	l2_db_offset;

	u16		chip_num;
	u16		hw_ring_stats_size;
	u16		pf_port_id;
	unsigned long	en_state;

	u16	auxr_num_msix_vec;
	u16	auxr_num_ctxs;

	/* serialize auxr operations */
	struct mutex	auxr_dev_lock;
};

void bnge_rdma_aux_device_uninit(struct bnge_dev *bdev);
void bnge_rdma_aux_device_del(struct bnge_dev *bdev);
void bnge_rdma_aux_device_add(struct bnge_dev *bdev);
void bnge_rdma_aux_device_init(struct bnge_dev *bdev);
int bnge_register_dev(struct bnge_auxr_dev *adev,
		      void *handle);
void bnge_unregister_dev(struct bnge_auxr_dev *adev);
int bnge_send_msg(struct bnge_auxr_dev *adev, struct bnge_fw_msg *fw_msg);

#endif /* _BNGE_AUXR_H_ */
