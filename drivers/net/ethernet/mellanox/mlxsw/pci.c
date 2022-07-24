// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/log2.h>
#include <linux/string.h>

#include "pci_hw.h"
#include "pci.h"
#include "core.h"
#include "cmd.h"
#include "port.h"
#include "resources.h"

#define mlxsw_pci_write32(mlxsw_pci, reg, val) \
	iowrite32be(val, (mlxsw_pci)->hw_addr + (MLXSW_PCI_ ## reg))
#define mlxsw_pci_read32(mlxsw_pci, reg) \
	ioread32be((mlxsw_pci)->hw_addr + (MLXSW_PCI_ ## reg))

enum mlxsw_pci_queue_type {
	MLXSW_PCI_QUEUE_TYPE_SDQ,
	MLXSW_PCI_QUEUE_TYPE_RDQ,
	MLXSW_PCI_QUEUE_TYPE_CQ,
	MLXSW_PCI_QUEUE_TYPE_EQ,
};

#define MLXSW_PCI_QUEUE_TYPE_COUNT	4

static const u16 mlxsw_pci_doorbell_type_offset[] = {
	MLXSW_PCI_DOORBELL_SDQ_OFFSET,	/* for type MLXSW_PCI_QUEUE_TYPE_SDQ */
	MLXSW_PCI_DOORBELL_RDQ_OFFSET,	/* for type MLXSW_PCI_QUEUE_TYPE_RDQ */
	MLXSW_PCI_DOORBELL_CQ_OFFSET,	/* for type MLXSW_PCI_QUEUE_TYPE_CQ */
	MLXSW_PCI_DOORBELL_EQ_OFFSET,	/* for type MLXSW_PCI_QUEUE_TYPE_EQ */
};

static const u16 mlxsw_pci_doorbell_arm_type_offset[] = {
	0, /* unused */
	0, /* unused */
	MLXSW_PCI_DOORBELL_ARM_CQ_OFFSET, /* for type MLXSW_PCI_QUEUE_TYPE_CQ */
	MLXSW_PCI_DOORBELL_ARM_EQ_OFFSET, /* for type MLXSW_PCI_QUEUE_TYPE_EQ */
};

struct mlxsw_pci_mem_item {
	char *buf;
	dma_addr_t mapaddr;
	size_t size;
};

struct mlxsw_pci_queue_elem_info {
	char *elem; /* pointer to actual dma mapped element mem chunk */
	union {
		struct {
			struct sk_buff *skb;
		} sdq;
		struct {
			struct sk_buff *skb;
		} rdq;
	} u;
};

struct mlxsw_pci_queue {
	spinlock_t lock; /* for queue accesses */
	struct mlxsw_pci_mem_item mem_item;
	struct mlxsw_pci_queue_elem_info *elem_info;
	u16 producer_counter;
	u16 consumer_counter;
	u16 count; /* number of elements in queue */
	u8 num; /* queue number */
	u8 elem_size; /* size of one element */
	enum mlxsw_pci_queue_type type;
	struct tasklet_struct tasklet; /* queue processing tasklet */
	struct mlxsw_pci *pci;
	union {
		struct {
			u32 comp_sdq_count;
			u32 comp_rdq_count;
			enum mlxsw_pci_cqe_v v;
		} cq;
		struct {
			u32 ev_cmd_count;
			u32 ev_comp_count;
			u32 ev_other_count;
		} eq;
	} u;
};

struct mlxsw_pci_queue_type_group {
	struct mlxsw_pci_queue *q;
	u8 count; /* number of queues in group */
};

struct mlxsw_pci {
	struct pci_dev *pdev;
	u8 __iomem *hw_addr;
	u64 free_running_clock_offset;
	struct mlxsw_pci_queue_type_group queues[MLXSW_PCI_QUEUE_TYPE_COUNT];
	u32 doorbell_offset;
	struct mlxsw_core *core;
	struct {
		struct mlxsw_pci_mem_item *items;
		unsigned int count;
	} fw_area;
	struct {
		struct mlxsw_pci_mem_item out_mbox;
		struct mlxsw_pci_mem_item in_mbox;
		struct mutex lock; /* Lock access to command registers */
		bool nopoll;
		wait_queue_head_t wait;
		bool wait_done;
		struct {
			u8 status;
			u64 out_param;
		} comp;
	} cmd;
	struct mlxsw_bus_info bus_info;
	const struct pci_device_id *id;
	enum mlxsw_pci_cqe_v max_cqe_ver; /* Maximal supported CQE version */
	u8 num_sdq_cqs; /* Number of CQs used for SDQs */
};

static void mlxsw_pci_queue_tasklet_schedule(struct mlxsw_pci_queue *q)
{
	tasklet_schedule(&q->tasklet);
}

static char *__mlxsw_pci_queue_elem_get(struct mlxsw_pci_queue *q,
					size_t elem_size, int elem_index)
{
	return q->mem_item.buf + (elem_size * elem_index);
}

static struct mlxsw_pci_queue_elem_info *
mlxsw_pci_queue_elem_info_get(struct mlxsw_pci_queue *q, int elem_index)
{
	return &q->elem_info[elem_index];
}

static struct mlxsw_pci_queue_elem_info *
mlxsw_pci_queue_elem_info_producer_get(struct mlxsw_pci_queue *q)
{
	int index = q->producer_counter & (q->count - 1);

	if ((u16) (q->producer_counter - q->consumer_counter) == q->count)
		return NULL;
	return mlxsw_pci_queue_elem_info_get(q, index);
}

static struct mlxsw_pci_queue_elem_info *
mlxsw_pci_queue_elem_info_consumer_get(struct mlxsw_pci_queue *q)
{
	int index = q->consumer_counter & (q->count - 1);

	return mlxsw_pci_queue_elem_info_get(q, index);
}

static char *mlxsw_pci_queue_elem_get(struct mlxsw_pci_queue *q, int elem_index)
{
	return mlxsw_pci_queue_elem_info_get(q, elem_index)->elem;
}

static bool mlxsw_pci_elem_hw_owned(struct mlxsw_pci_queue *q, bool owner_bit)
{
	return owner_bit != !!(q->consumer_counter & q->count);
}

static struct mlxsw_pci_queue_type_group *
mlxsw_pci_queue_type_group_get(struct mlxsw_pci *mlxsw_pci,
			       enum mlxsw_pci_queue_type q_type)
{
	return &mlxsw_pci->queues[q_type];
}

static u8 __mlxsw_pci_queue_count(struct mlxsw_pci *mlxsw_pci,
				  enum mlxsw_pci_queue_type q_type)
{
	struct mlxsw_pci_queue_type_group *queue_group;

	queue_group = mlxsw_pci_queue_type_group_get(mlxsw_pci, q_type);
	return queue_group->count;
}

static u8 mlxsw_pci_sdq_count(struct mlxsw_pci *mlxsw_pci)
{
	return __mlxsw_pci_queue_count(mlxsw_pci, MLXSW_PCI_QUEUE_TYPE_SDQ);
}

static u8 mlxsw_pci_cq_count(struct mlxsw_pci *mlxsw_pci)
{
	return __mlxsw_pci_queue_count(mlxsw_pci, MLXSW_PCI_QUEUE_TYPE_CQ);
}

static struct mlxsw_pci_queue *
__mlxsw_pci_queue_get(struct mlxsw_pci *mlxsw_pci,
		      enum mlxsw_pci_queue_type q_type, u8 q_num)
{
	return &mlxsw_pci->queues[q_type].q[q_num];
}

static struct mlxsw_pci_queue *mlxsw_pci_sdq_get(struct mlxsw_pci *mlxsw_pci,
						 u8 q_num)
{
	return __mlxsw_pci_queue_get(mlxsw_pci,
				     MLXSW_PCI_QUEUE_TYPE_SDQ, q_num);
}

static struct mlxsw_pci_queue *mlxsw_pci_rdq_get(struct mlxsw_pci *mlxsw_pci,
						 u8 q_num)
{
	return __mlxsw_pci_queue_get(mlxsw_pci,
				     MLXSW_PCI_QUEUE_TYPE_RDQ, q_num);
}

static struct mlxsw_pci_queue *mlxsw_pci_cq_get(struct mlxsw_pci *mlxsw_pci,
						u8 q_num)
{
	return __mlxsw_pci_queue_get(mlxsw_pci, MLXSW_PCI_QUEUE_TYPE_CQ, q_num);
}

static struct mlxsw_pci_queue *mlxsw_pci_eq_get(struct mlxsw_pci *mlxsw_pci,
						u8 q_num)
{
	return __mlxsw_pci_queue_get(mlxsw_pci, MLXSW_PCI_QUEUE_TYPE_EQ, q_num);
}

static void __mlxsw_pci_queue_doorbell_set(struct mlxsw_pci *mlxsw_pci,
					   struct mlxsw_pci_queue *q,
					   u16 val)
{
	mlxsw_pci_write32(mlxsw_pci,
			  DOORBELL(mlxsw_pci->doorbell_offset,
				   mlxsw_pci_doorbell_type_offset[q->type],
				   q->num), val);
}

static void __mlxsw_pci_queue_doorbell_arm_set(struct mlxsw_pci *mlxsw_pci,
					       struct mlxsw_pci_queue *q,
					       u16 val)
{
	mlxsw_pci_write32(mlxsw_pci,
			  DOORBELL(mlxsw_pci->doorbell_offset,
				   mlxsw_pci_doorbell_arm_type_offset[q->type],
				   q->num), val);
}

static void mlxsw_pci_queue_doorbell_producer_ring(struct mlxsw_pci *mlxsw_pci,
						   struct mlxsw_pci_queue *q)
{
	wmb(); /* ensure all writes are done before we ring a bell */
	__mlxsw_pci_queue_doorbell_set(mlxsw_pci, q, q->producer_counter);
}

static void mlxsw_pci_queue_doorbell_consumer_ring(struct mlxsw_pci *mlxsw_pci,
						   struct mlxsw_pci_queue *q)
{
	wmb(); /* ensure all writes are done before we ring a bell */
	__mlxsw_pci_queue_doorbell_set(mlxsw_pci, q,
				       q->consumer_counter + q->count);
}

static void
mlxsw_pci_queue_doorbell_arm_consumer_ring(struct mlxsw_pci *mlxsw_pci,
					   struct mlxsw_pci_queue *q)
{
	wmb(); /* ensure all writes are done before we ring a bell */
	__mlxsw_pci_queue_doorbell_arm_set(mlxsw_pci, q, q->consumer_counter);
}

static dma_addr_t __mlxsw_pci_queue_page_get(struct mlxsw_pci_queue *q,
					     int page_index)
{
	return q->mem_item.mapaddr + MLXSW_PCI_PAGE_SIZE * page_index;
}

static int mlxsw_pci_sdq_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
			      struct mlxsw_pci_queue *q)
{
	int tclass;
	int lp;
	int i;
	int err;

	q->producer_counter = 0;
	q->consumer_counter = 0;
	tclass = q->num == MLXSW_PCI_SDQ_EMAD_INDEX ? MLXSW_PCI_SDQ_EMAD_TC :
						      MLXSW_PCI_SDQ_CTL_TC;
	lp = q->num == MLXSW_PCI_SDQ_EMAD_INDEX ? MLXSW_CMD_MBOX_SW2HW_DQ_SDQ_LP_IGNORE_WQE :
						  MLXSW_CMD_MBOX_SW2HW_DQ_SDQ_LP_WQE;

	/* Set CQ of same number of this SDQ. */
	mlxsw_cmd_mbox_sw2hw_dq_cq_set(mbox, q->num);
	mlxsw_cmd_mbox_sw2hw_dq_sdq_lp_set(mbox, lp);
	mlxsw_cmd_mbox_sw2hw_dq_sdq_tclass_set(mbox, tclass);
	mlxsw_cmd_mbox_sw2hw_dq_log2_dq_sz_set(mbox, 3); /* 8 pages */
	for (i = 0; i < MLXSW_PCI_AQ_PAGES; i++) {
		dma_addr_t mapaddr = __mlxsw_pci_queue_page_get(q, i);

		mlxsw_cmd_mbox_sw2hw_dq_pa_set(mbox, i, mapaddr);
	}

	err = mlxsw_cmd_sw2hw_sdq(mlxsw_pci->core, mbox, q->num);
	if (err)
		return err;
	mlxsw_pci_queue_doorbell_producer_ring(mlxsw_pci, q);
	return 0;
}

static void mlxsw_pci_sdq_fini(struct mlxsw_pci *mlxsw_pci,
			       struct mlxsw_pci_queue *q)
{
	mlxsw_cmd_hw2sw_sdq(mlxsw_pci->core, q->num);
}

static int mlxsw_pci_wqe_frag_map(struct mlxsw_pci *mlxsw_pci, char *wqe,
				  int index, char *frag_data, size_t frag_len,
				  int direction)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	dma_addr_t mapaddr;

	mapaddr = dma_map_single(&pdev->dev, frag_data, frag_len, direction);
	if (unlikely(dma_mapping_error(&pdev->dev, mapaddr))) {
		dev_err_ratelimited(&pdev->dev, "failed to dma map tx frag\n");
		return -EIO;
	}
	mlxsw_pci_wqe_address_set(wqe, index, mapaddr);
	mlxsw_pci_wqe_byte_count_set(wqe, index, frag_len);
	return 0;
}

static void mlxsw_pci_wqe_frag_unmap(struct mlxsw_pci *mlxsw_pci, char *wqe,
				     int index, int direction)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	size_t frag_len = mlxsw_pci_wqe_byte_count_get(wqe, index);
	dma_addr_t mapaddr = mlxsw_pci_wqe_address_get(wqe, index);

	if (!frag_len)
		return;
	dma_unmap_single(&pdev->dev, mapaddr, frag_len, direction);
}

static int mlxsw_pci_rdq_skb_alloc(struct mlxsw_pci *mlxsw_pci,
				   struct mlxsw_pci_queue_elem_info *elem_info)
{
	size_t buf_len = MLXSW_PORT_MAX_MTU;
	char *wqe = elem_info->elem;
	struct sk_buff *skb;
	int err;

	skb = netdev_alloc_skb_ip_align(NULL, buf_len);
	if (!skb)
		return -ENOMEM;

	err = mlxsw_pci_wqe_frag_map(mlxsw_pci, wqe, 0, skb->data,
				     buf_len, DMA_FROM_DEVICE);
	if (err)
		goto err_frag_map;

	elem_info->u.rdq.skb = skb;
	return 0;

err_frag_map:
	dev_kfree_skb_any(skb);
	return err;
}

static void mlxsw_pci_rdq_skb_free(struct mlxsw_pci *mlxsw_pci,
				   struct mlxsw_pci_queue_elem_info *elem_info)
{
	struct sk_buff *skb;
	char *wqe;

	skb = elem_info->u.rdq.skb;
	wqe = elem_info->elem;

	mlxsw_pci_wqe_frag_unmap(mlxsw_pci, wqe, 0, DMA_FROM_DEVICE);
	dev_kfree_skb_any(skb);
}

static int mlxsw_pci_rdq_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
			      struct mlxsw_pci_queue *q)
{
	struct mlxsw_pci_queue_elem_info *elem_info;
	u8 sdq_count = mlxsw_pci_sdq_count(mlxsw_pci);
	int i;
	int err;

	q->producer_counter = 0;
	q->consumer_counter = 0;

	/* Set CQ of same number of this RDQ with base
	 * above SDQ count as the lower ones are assigned to SDQs.
	 */
	mlxsw_cmd_mbox_sw2hw_dq_cq_set(mbox, sdq_count + q->num);
	mlxsw_cmd_mbox_sw2hw_dq_log2_dq_sz_set(mbox, 3); /* 8 pages */
	for (i = 0; i < MLXSW_PCI_AQ_PAGES; i++) {
		dma_addr_t mapaddr = __mlxsw_pci_queue_page_get(q, i);

		mlxsw_cmd_mbox_sw2hw_dq_pa_set(mbox, i, mapaddr);
	}

	err = mlxsw_cmd_sw2hw_rdq(mlxsw_pci->core, mbox, q->num);
	if (err)
		return err;

	mlxsw_pci_queue_doorbell_producer_ring(mlxsw_pci, q);

	for (i = 0; i < q->count; i++) {
		elem_info = mlxsw_pci_queue_elem_info_producer_get(q);
		BUG_ON(!elem_info);
		err = mlxsw_pci_rdq_skb_alloc(mlxsw_pci, elem_info);
		if (err)
			goto rollback;
		/* Everything is set up, ring doorbell to pass elem to HW */
		q->producer_counter++;
		mlxsw_pci_queue_doorbell_producer_ring(mlxsw_pci, q);
	}

	return 0;

rollback:
	for (i--; i >= 0; i--) {
		elem_info = mlxsw_pci_queue_elem_info_get(q, i);
		mlxsw_pci_rdq_skb_free(mlxsw_pci, elem_info);
	}
	mlxsw_cmd_hw2sw_rdq(mlxsw_pci->core, q->num);

	return err;
}

static void mlxsw_pci_rdq_fini(struct mlxsw_pci *mlxsw_pci,
			       struct mlxsw_pci_queue *q)
{
	struct mlxsw_pci_queue_elem_info *elem_info;
	int i;

	mlxsw_cmd_hw2sw_rdq(mlxsw_pci->core, q->num);
	for (i = 0; i < q->count; i++) {
		elem_info = mlxsw_pci_queue_elem_info_get(q, i);
		mlxsw_pci_rdq_skb_free(mlxsw_pci, elem_info);
	}
}

static void mlxsw_pci_cq_pre_init(struct mlxsw_pci *mlxsw_pci,
				  struct mlxsw_pci_queue *q)
{
	q->u.cq.v = mlxsw_pci->max_cqe_ver;

	/* For SDQ it is pointless to use CQEv2, so use CQEv1 instead */
	if (q->u.cq.v == MLXSW_PCI_CQE_V2 &&
	    q->num < mlxsw_pci->num_sdq_cqs)
		q->u.cq.v = MLXSW_PCI_CQE_V1;
}

static int mlxsw_pci_cq_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
			     struct mlxsw_pci_queue *q)
{
	int i;
	int err;

	q->consumer_counter = 0;

	for (i = 0; i < q->count; i++) {
		char *elem = mlxsw_pci_queue_elem_get(q, i);

		mlxsw_pci_cqe_owner_set(q->u.cq.v, elem, 1);
	}

	if (q->u.cq.v == MLXSW_PCI_CQE_V1)
		mlxsw_cmd_mbox_sw2hw_cq_cqe_ver_set(mbox,
				MLXSW_CMD_MBOX_SW2HW_CQ_CQE_VER_1);
	else if (q->u.cq.v == MLXSW_PCI_CQE_V2)
		mlxsw_cmd_mbox_sw2hw_cq_cqe_ver_set(mbox,
				MLXSW_CMD_MBOX_SW2HW_CQ_CQE_VER_2);

	mlxsw_cmd_mbox_sw2hw_cq_c_eqn_set(mbox, MLXSW_PCI_EQ_COMP_NUM);
	mlxsw_cmd_mbox_sw2hw_cq_st_set(mbox, 0);
	mlxsw_cmd_mbox_sw2hw_cq_log_cq_size_set(mbox, ilog2(q->count));
	for (i = 0; i < MLXSW_PCI_AQ_PAGES; i++) {
		dma_addr_t mapaddr = __mlxsw_pci_queue_page_get(q, i);

		mlxsw_cmd_mbox_sw2hw_cq_pa_set(mbox, i, mapaddr);
	}
	err = mlxsw_cmd_sw2hw_cq(mlxsw_pci->core, mbox, q->num);
	if (err)
		return err;
	mlxsw_pci_queue_doorbell_consumer_ring(mlxsw_pci, q);
	mlxsw_pci_queue_doorbell_arm_consumer_ring(mlxsw_pci, q);
	return 0;
}

static void mlxsw_pci_cq_fini(struct mlxsw_pci *mlxsw_pci,
			      struct mlxsw_pci_queue *q)
{
	mlxsw_cmd_hw2sw_cq(mlxsw_pci->core, q->num);
}

static unsigned int mlxsw_pci_read32_off(struct mlxsw_pci *mlxsw_pci,
					 ptrdiff_t off)
{
	return ioread32be(mlxsw_pci->hw_addr + off);
}

static void mlxsw_pci_cqe_sdq_handle(struct mlxsw_pci *mlxsw_pci,
				     struct mlxsw_pci_queue *q,
				     u16 consumer_counter_limit,
				     char *cqe)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	struct mlxsw_pci_queue_elem_info *elem_info;
	struct mlxsw_tx_info tx_info;
	char *wqe;
	struct sk_buff *skb;
	int i;

	spin_lock(&q->lock);
	elem_info = mlxsw_pci_queue_elem_info_consumer_get(q);
	tx_info = mlxsw_skb_cb(elem_info->u.sdq.skb)->tx_info;
	skb = elem_info->u.sdq.skb;
	wqe = elem_info->elem;
	for (i = 0; i < MLXSW_PCI_WQE_SG_ENTRIES; i++)
		mlxsw_pci_wqe_frag_unmap(mlxsw_pci, wqe, i, DMA_TO_DEVICE);

	if (unlikely(!tx_info.is_emad &&
		     skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		mlxsw_core_ptp_transmitted(mlxsw_pci->core, skb,
					   tx_info.local_port);
		skb = NULL;
	}

	if (skb)
		dev_kfree_skb_any(skb);
	elem_info->u.sdq.skb = NULL;

	if (q->consumer_counter++ != consumer_counter_limit)
		dev_dbg_ratelimited(&pdev->dev, "Consumer counter does not match limit in SDQ\n");
	spin_unlock(&q->lock);
}

static void mlxsw_pci_cqe_rdq_md_tx_port_init(struct sk_buff *skb,
					      const char *cqe)
{
	struct mlxsw_skb_cb *cb = mlxsw_skb_cb(skb);

	if (mlxsw_pci_cqe2_tx_lag_get(cqe)) {
		cb->rx_md_info.tx_port_is_lag = true;
		cb->rx_md_info.tx_lag_id = mlxsw_pci_cqe2_tx_lag_id_get(cqe);
		cb->rx_md_info.tx_lag_port_index =
			mlxsw_pci_cqe2_tx_lag_subport_get(cqe);
	} else {
		cb->rx_md_info.tx_port_is_lag = false;
		cb->rx_md_info.tx_sys_port =
			mlxsw_pci_cqe2_tx_system_port_get(cqe);
	}

	if (cb->rx_md_info.tx_sys_port != MLXSW_PCI_CQE2_TX_PORT_MULTI_PORT &&
	    cb->rx_md_info.tx_sys_port != MLXSW_PCI_CQE2_TX_PORT_INVALID)
		cb->rx_md_info.tx_port_valid = 1;
	else
		cb->rx_md_info.tx_port_valid = 0;
}

static void mlxsw_pci_cqe_rdq_md_init(struct sk_buff *skb, const char *cqe)
{
	struct mlxsw_skb_cb *cb = mlxsw_skb_cb(skb);

	cb->rx_md_info.tx_congestion = mlxsw_pci_cqe2_mirror_cong_get(cqe);
	if (cb->rx_md_info.tx_congestion != MLXSW_PCI_CQE2_MIRROR_CONG_INVALID)
		cb->rx_md_info.tx_congestion_valid = 1;
	else
		cb->rx_md_info.tx_congestion_valid = 0;
	cb->rx_md_info.tx_congestion <<= MLXSW_PCI_CQE2_MIRROR_CONG_SHIFT;

	cb->rx_md_info.latency = mlxsw_pci_cqe2_mirror_latency_get(cqe);
	if (cb->rx_md_info.latency != MLXSW_PCI_CQE2_MIRROR_LATENCY_INVALID)
		cb->rx_md_info.latency_valid = 1;
	else
		cb->rx_md_info.latency_valid = 0;

	cb->rx_md_info.tx_tc = mlxsw_pci_cqe2_mirror_tclass_get(cqe);
	if (cb->rx_md_info.tx_tc != MLXSW_PCI_CQE2_MIRROR_TCLASS_INVALID)
		cb->rx_md_info.tx_tc_valid = 1;
	else
		cb->rx_md_info.tx_tc_valid = 0;

	mlxsw_pci_cqe_rdq_md_tx_port_init(skb, cqe);
}

static void mlxsw_pci_cqe_rdq_handle(struct mlxsw_pci *mlxsw_pci,
				     struct mlxsw_pci_queue *q,
				     u16 consumer_counter_limit,
				     enum mlxsw_pci_cqe_v cqe_v, char *cqe)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	struct mlxsw_pci_queue_elem_info *elem_info;
	struct mlxsw_rx_info rx_info = {};
	char wqe[MLXSW_PCI_WQE_SIZE];
	struct sk_buff *skb;
	u16 byte_count;
	int err;

	elem_info = mlxsw_pci_queue_elem_info_consumer_get(q);
	skb = elem_info->u.rdq.skb;
	memcpy(wqe, elem_info->elem, MLXSW_PCI_WQE_SIZE);

	if (q->consumer_counter++ != consumer_counter_limit)
		dev_dbg_ratelimited(&pdev->dev, "Consumer counter does not match limit in RDQ\n");

	err = mlxsw_pci_rdq_skb_alloc(mlxsw_pci, elem_info);
	if (err) {
		dev_err_ratelimited(&pdev->dev, "Failed to alloc skb for RDQ\n");
		goto out;
	}

	mlxsw_pci_wqe_frag_unmap(mlxsw_pci, wqe, 0, DMA_FROM_DEVICE);

	if (mlxsw_pci_cqe_lag_get(cqe_v, cqe)) {
		rx_info.is_lag = true;
		rx_info.u.lag_id = mlxsw_pci_cqe_lag_id_get(cqe_v, cqe);
		rx_info.lag_port_index =
			mlxsw_pci_cqe_lag_subport_get(cqe_v, cqe);
	} else {
		rx_info.is_lag = false;
		rx_info.u.sys_port = mlxsw_pci_cqe_system_port_get(cqe);
	}

	rx_info.trap_id = mlxsw_pci_cqe_trap_id_get(cqe);

	if (rx_info.trap_id == MLXSW_TRAP_ID_DISCARD_INGRESS_ACL ||
	    rx_info.trap_id == MLXSW_TRAP_ID_DISCARD_EGRESS_ACL) {
		u32 cookie_index = 0;

		if (mlxsw_pci->max_cqe_ver >= MLXSW_PCI_CQE_V2)
			cookie_index = mlxsw_pci_cqe2_user_def_val_orig_pkt_len_get(cqe);
		mlxsw_skb_cb(skb)->rx_md_info.cookie_index = cookie_index;
	} else if (rx_info.trap_id >= MLXSW_TRAP_ID_MIRROR_SESSION0 &&
		   rx_info.trap_id <= MLXSW_TRAP_ID_MIRROR_SESSION7 &&
		   mlxsw_pci->max_cqe_ver >= MLXSW_PCI_CQE_V2) {
		rx_info.mirror_reason = mlxsw_pci_cqe2_mirror_reason_get(cqe);
		mlxsw_pci_cqe_rdq_md_init(skb, cqe);
	} else if (rx_info.trap_id == MLXSW_TRAP_ID_PKT_SAMPLE &&
		   mlxsw_pci->max_cqe_ver >= MLXSW_PCI_CQE_V2) {
		mlxsw_pci_cqe_rdq_md_tx_port_init(skb, cqe);
	}

	byte_count = mlxsw_pci_cqe_byte_count_get(cqe);
	if (mlxsw_pci_cqe_crc_get(cqe_v, cqe))
		byte_count -= ETH_FCS_LEN;
	skb_put(skb, byte_count);
	mlxsw_core_skb_receive(mlxsw_pci->core, skb, &rx_info);

out:
	/* Everything is set up, ring doorbell to pass elem to HW */
	q->producer_counter++;
	mlxsw_pci_queue_doorbell_producer_ring(mlxsw_pci, q);
	return;
}

static char *mlxsw_pci_cq_sw_cqe_get(struct mlxsw_pci_queue *q)
{
	struct mlxsw_pci_queue_elem_info *elem_info;
	char *elem;
	bool owner_bit;

	elem_info = mlxsw_pci_queue_elem_info_consumer_get(q);
	elem = elem_info->elem;
	owner_bit = mlxsw_pci_cqe_owner_get(q->u.cq.v, elem);
	if (mlxsw_pci_elem_hw_owned(q, owner_bit))
		return NULL;
	q->consumer_counter++;
	rmb(); /* make sure we read owned bit before the rest of elem */
	return elem;
}

static void mlxsw_pci_cq_tasklet(struct tasklet_struct *t)
{
	struct mlxsw_pci_queue *q = from_tasklet(q, t, tasklet);
	struct mlxsw_pci *mlxsw_pci = q->pci;
	char *cqe;
	int items = 0;
	int credits = q->count >> 1;

	while ((cqe = mlxsw_pci_cq_sw_cqe_get(q))) {
		u16 wqe_counter = mlxsw_pci_cqe_wqe_counter_get(cqe);
		u8 sendq = mlxsw_pci_cqe_sr_get(q->u.cq.v, cqe);
		u8 dqn = mlxsw_pci_cqe_dqn_get(q->u.cq.v, cqe);
		char ncqe[MLXSW_PCI_CQE_SIZE_MAX];

		memcpy(ncqe, cqe, q->elem_size);
		mlxsw_pci_queue_doorbell_consumer_ring(mlxsw_pci, q);

		if (sendq) {
			struct mlxsw_pci_queue *sdq;

			sdq = mlxsw_pci_sdq_get(mlxsw_pci, dqn);
			mlxsw_pci_cqe_sdq_handle(mlxsw_pci, sdq,
						 wqe_counter, ncqe);
			q->u.cq.comp_sdq_count++;
		} else {
			struct mlxsw_pci_queue *rdq;

			rdq = mlxsw_pci_rdq_get(mlxsw_pci, dqn);
			mlxsw_pci_cqe_rdq_handle(mlxsw_pci, rdq,
						 wqe_counter, q->u.cq.v, ncqe);
			q->u.cq.comp_rdq_count++;
		}
		if (++items == credits)
			break;
	}
	if (items)
		mlxsw_pci_queue_doorbell_arm_consumer_ring(mlxsw_pci, q);
}

static u16 mlxsw_pci_cq_elem_count(const struct mlxsw_pci_queue *q)
{
	return q->u.cq.v == MLXSW_PCI_CQE_V2 ? MLXSW_PCI_CQE2_COUNT :
					       MLXSW_PCI_CQE01_COUNT;
}

static u8 mlxsw_pci_cq_elem_size(const struct mlxsw_pci_queue *q)
{
	return q->u.cq.v == MLXSW_PCI_CQE_V2 ? MLXSW_PCI_CQE2_SIZE :
					       MLXSW_PCI_CQE01_SIZE;
}

static int mlxsw_pci_eq_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
			     struct mlxsw_pci_queue *q)
{
	int i;
	int err;

	q->consumer_counter = 0;

	for (i = 0; i < q->count; i++) {
		char *elem = mlxsw_pci_queue_elem_get(q, i);

		mlxsw_pci_eqe_owner_set(elem, 1);
	}

	mlxsw_cmd_mbox_sw2hw_eq_int_msix_set(mbox, 1); /* MSI-X used */
	mlxsw_cmd_mbox_sw2hw_eq_st_set(mbox, 1); /* armed */
	mlxsw_cmd_mbox_sw2hw_eq_log_eq_size_set(mbox, ilog2(q->count));
	for (i = 0; i < MLXSW_PCI_AQ_PAGES; i++) {
		dma_addr_t mapaddr = __mlxsw_pci_queue_page_get(q, i);

		mlxsw_cmd_mbox_sw2hw_eq_pa_set(mbox, i, mapaddr);
	}
	err = mlxsw_cmd_sw2hw_eq(mlxsw_pci->core, mbox, q->num);
	if (err)
		return err;
	mlxsw_pci_queue_doorbell_consumer_ring(mlxsw_pci, q);
	mlxsw_pci_queue_doorbell_arm_consumer_ring(mlxsw_pci, q);
	return 0;
}

static void mlxsw_pci_eq_fini(struct mlxsw_pci *mlxsw_pci,
			      struct mlxsw_pci_queue *q)
{
	mlxsw_cmd_hw2sw_eq(mlxsw_pci->core, q->num);
}

static void mlxsw_pci_eq_cmd_event(struct mlxsw_pci *mlxsw_pci, char *eqe)
{
	mlxsw_pci->cmd.comp.status = mlxsw_pci_eqe_cmd_status_get(eqe);
	mlxsw_pci->cmd.comp.out_param =
		((u64) mlxsw_pci_eqe_cmd_out_param_h_get(eqe)) << 32 |
		mlxsw_pci_eqe_cmd_out_param_l_get(eqe);
	mlxsw_pci->cmd.wait_done = true;
	wake_up(&mlxsw_pci->cmd.wait);
}

static char *mlxsw_pci_eq_sw_eqe_get(struct mlxsw_pci_queue *q)
{
	struct mlxsw_pci_queue_elem_info *elem_info;
	char *elem;
	bool owner_bit;

	elem_info = mlxsw_pci_queue_elem_info_consumer_get(q);
	elem = elem_info->elem;
	owner_bit = mlxsw_pci_eqe_owner_get(elem);
	if (mlxsw_pci_elem_hw_owned(q, owner_bit))
		return NULL;
	q->consumer_counter++;
	rmb(); /* make sure we read owned bit before the rest of elem */
	return elem;
}

static void mlxsw_pci_eq_tasklet(struct tasklet_struct *t)
{
	struct mlxsw_pci_queue *q = from_tasklet(q, t, tasklet);
	struct mlxsw_pci *mlxsw_pci = q->pci;
	u8 cq_count = mlxsw_pci_cq_count(mlxsw_pci);
	unsigned long active_cqns[BITS_TO_LONGS(MLXSW_PCI_CQS_MAX)];
	char *eqe;
	u8 cqn;
	bool cq_handle = false;
	int items = 0;
	int credits = q->count >> 1;

	memset(&active_cqns, 0, sizeof(active_cqns));

	while ((eqe = mlxsw_pci_eq_sw_eqe_get(q))) {

		/* Command interface completion events are always received on
		 * queue MLXSW_PCI_EQ_ASYNC_NUM (EQ0) and completion events
		 * are mapped to queue MLXSW_PCI_EQ_COMP_NUM (EQ1).
		 */
		switch (q->num) {
		case MLXSW_PCI_EQ_ASYNC_NUM:
			mlxsw_pci_eq_cmd_event(mlxsw_pci, eqe);
			q->u.eq.ev_cmd_count++;
			break;
		case MLXSW_PCI_EQ_COMP_NUM:
			cqn = mlxsw_pci_eqe_cqn_get(eqe);
			set_bit(cqn, active_cqns);
			cq_handle = true;
			q->u.eq.ev_comp_count++;
			break;
		default:
			q->u.eq.ev_other_count++;
		}
		if (++items == credits)
			break;
	}
	if (items) {
		mlxsw_pci_queue_doorbell_consumer_ring(mlxsw_pci, q);
		mlxsw_pci_queue_doorbell_arm_consumer_ring(mlxsw_pci, q);
	}

	if (!cq_handle)
		return;
	for_each_set_bit(cqn, active_cqns, cq_count) {
		q = mlxsw_pci_cq_get(mlxsw_pci, cqn);
		mlxsw_pci_queue_tasklet_schedule(q);
	}
}

struct mlxsw_pci_queue_ops {
	const char *name;
	enum mlxsw_pci_queue_type type;
	void (*pre_init)(struct mlxsw_pci *mlxsw_pci,
			 struct mlxsw_pci_queue *q);
	int (*init)(struct mlxsw_pci *mlxsw_pci, char *mbox,
		    struct mlxsw_pci_queue *q);
	void (*fini)(struct mlxsw_pci *mlxsw_pci,
		     struct mlxsw_pci_queue *q);
	void (*tasklet)(struct tasklet_struct *t);
	u16 (*elem_count_f)(const struct mlxsw_pci_queue *q);
	u8 (*elem_size_f)(const struct mlxsw_pci_queue *q);
	u16 elem_count;
	u8 elem_size;
};

static const struct mlxsw_pci_queue_ops mlxsw_pci_sdq_ops = {
	.type		= MLXSW_PCI_QUEUE_TYPE_SDQ,
	.init		= mlxsw_pci_sdq_init,
	.fini		= mlxsw_pci_sdq_fini,
	.elem_count	= MLXSW_PCI_WQE_COUNT,
	.elem_size	= MLXSW_PCI_WQE_SIZE,
};

static const struct mlxsw_pci_queue_ops mlxsw_pci_rdq_ops = {
	.type		= MLXSW_PCI_QUEUE_TYPE_RDQ,
	.init		= mlxsw_pci_rdq_init,
	.fini		= mlxsw_pci_rdq_fini,
	.elem_count	= MLXSW_PCI_WQE_COUNT,
	.elem_size	= MLXSW_PCI_WQE_SIZE
};

static const struct mlxsw_pci_queue_ops mlxsw_pci_cq_ops = {
	.type		= MLXSW_PCI_QUEUE_TYPE_CQ,
	.pre_init	= mlxsw_pci_cq_pre_init,
	.init		= mlxsw_pci_cq_init,
	.fini		= mlxsw_pci_cq_fini,
	.tasklet	= mlxsw_pci_cq_tasklet,
	.elem_count_f	= mlxsw_pci_cq_elem_count,
	.elem_size_f	= mlxsw_pci_cq_elem_size
};

static const struct mlxsw_pci_queue_ops mlxsw_pci_eq_ops = {
	.type		= MLXSW_PCI_QUEUE_TYPE_EQ,
	.init		= mlxsw_pci_eq_init,
	.fini		= mlxsw_pci_eq_fini,
	.tasklet	= mlxsw_pci_eq_tasklet,
	.elem_count	= MLXSW_PCI_EQE_COUNT,
	.elem_size	= MLXSW_PCI_EQE_SIZE
};

static int mlxsw_pci_queue_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
				const struct mlxsw_pci_queue_ops *q_ops,
				struct mlxsw_pci_queue *q, u8 q_num)
{
	struct mlxsw_pci_mem_item *mem_item = &q->mem_item;
	int i;
	int err;

	q->num = q_num;
	if (q_ops->pre_init)
		q_ops->pre_init(mlxsw_pci, q);

	spin_lock_init(&q->lock);
	q->count = q_ops->elem_count_f ? q_ops->elem_count_f(q) :
					 q_ops->elem_count;
	q->elem_size = q_ops->elem_size_f ? q_ops->elem_size_f(q) :
					    q_ops->elem_size;
	q->type = q_ops->type;
	q->pci = mlxsw_pci;

	if (q_ops->tasklet)
		tasklet_setup(&q->tasklet, q_ops->tasklet);

	mem_item->size = MLXSW_PCI_AQ_SIZE;
	mem_item->buf = dma_alloc_coherent(&mlxsw_pci->pdev->dev,
					   mem_item->size, &mem_item->mapaddr,
					   GFP_KERNEL);
	if (!mem_item->buf)
		return -ENOMEM;

	q->elem_info = kcalloc(q->count, sizeof(*q->elem_info), GFP_KERNEL);
	if (!q->elem_info) {
		err = -ENOMEM;
		goto err_elem_info_alloc;
	}

	/* Initialize dma mapped elements info elem_info for
	 * future easy access.
	 */
	for (i = 0; i < q->count; i++) {
		struct mlxsw_pci_queue_elem_info *elem_info;

		elem_info = mlxsw_pci_queue_elem_info_get(q, i);
		elem_info->elem =
			__mlxsw_pci_queue_elem_get(q, q->elem_size, i);
	}

	mlxsw_cmd_mbox_zero(mbox);
	err = q_ops->init(mlxsw_pci, mbox, q);
	if (err)
		goto err_q_ops_init;
	return 0;

err_q_ops_init:
	kfree(q->elem_info);
err_elem_info_alloc:
	dma_free_coherent(&mlxsw_pci->pdev->dev, mem_item->size,
			  mem_item->buf, mem_item->mapaddr);
	return err;
}

static void mlxsw_pci_queue_fini(struct mlxsw_pci *mlxsw_pci,
				 const struct mlxsw_pci_queue_ops *q_ops,
				 struct mlxsw_pci_queue *q)
{
	struct mlxsw_pci_mem_item *mem_item = &q->mem_item;

	q_ops->fini(mlxsw_pci, q);
	kfree(q->elem_info);
	dma_free_coherent(&mlxsw_pci->pdev->dev, mem_item->size,
			  mem_item->buf, mem_item->mapaddr);
}

static int mlxsw_pci_queue_group_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
				      const struct mlxsw_pci_queue_ops *q_ops,
				      u8 num_qs)
{
	struct mlxsw_pci_queue_type_group *queue_group;
	int i;
	int err;

	queue_group = mlxsw_pci_queue_type_group_get(mlxsw_pci, q_ops->type);
	queue_group->q = kcalloc(num_qs, sizeof(*queue_group->q), GFP_KERNEL);
	if (!queue_group->q)
		return -ENOMEM;

	for (i = 0; i < num_qs; i++) {
		err = mlxsw_pci_queue_init(mlxsw_pci, mbox, q_ops,
					   &queue_group->q[i], i);
		if (err)
			goto err_queue_init;
	}
	queue_group->count = num_qs;

	return 0;

err_queue_init:
	for (i--; i >= 0; i--)
		mlxsw_pci_queue_fini(mlxsw_pci, q_ops, &queue_group->q[i]);
	kfree(queue_group->q);
	return err;
}

static void mlxsw_pci_queue_group_fini(struct mlxsw_pci *mlxsw_pci,
				       const struct mlxsw_pci_queue_ops *q_ops)
{
	struct mlxsw_pci_queue_type_group *queue_group;
	int i;

	queue_group = mlxsw_pci_queue_type_group_get(mlxsw_pci, q_ops->type);
	for (i = 0; i < queue_group->count; i++)
		mlxsw_pci_queue_fini(mlxsw_pci, q_ops, &queue_group->q[i]);
	kfree(queue_group->q);
}

static int mlxsw_pci_aqs_init(struct mlxsw_pci *mlxsw_pci, char *mbox)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	u8 num_sdqs;
	u8 sdq_log2sz;
	u8 num_rdqs;
	u8 rdq_log2sz;
	u8 num_cqs;
	u8 cq_log2sz;
	u8 cqv2_log2sz;
	u8 num_eqs;
	u8 eq_log2sz;
	int err;

	mlxsw_cmd_mbox_zero(mbox);
	err = mlxsw_cmd_query_aq_cap(mlxsw_pci->core, mbox);
	if (err)
		return err;

	num_sdqs = mlxsw_cmd_mbox_query_aq_cap_max_num_sdqs_get(mbox);
	sdq_log2sz = mlxsw_cmd_mbox_query_aq_cap_log_max_sdq_sz_get(mbox);
	num_rdqs = mlxsw_cmd_mbox_query_aq_cap_max_num_rdqs_get(mbox);
	rdq_log2sz = mlxsw_cmd_mbox_query_aq_cap_log_max_rdq_sz_get(mbox);
	num_cqs = mlxsw_cmd_mbox_query_aq_cap_max_num_cqs_get(mbox);
	cq_log2sz = mlxsw_cmd_mbox_query_aq_cap_log_max_cq_sz_get(mbox);
	cqv2_log2sz = mlxsw_cmd_mbox_query_aq_cap_log_max_cqv2_sz_get(mbox);
	num_eqs = mlxsw_cmd_mbox_query_aq_cap_max_num_eqs_get(mbox);
	eq_log2sz = mlxsw_cmd_mbox_query_aq_cap_log_max_eq_sz_get(mbox);

	if (num_sdqs + num_rdqs > num_cqs ||
	    num_sdqs < MLXSW_PCI_SDQS_MIN ||
	    num_cqs > MLXSW_PCI_CQS_MAX || num_eqs != MLXSW_PCI_EQS_COUNT) {
		dev_err(&pdev->dev, "Unsupported number of queues\n");
		return -EINVAL;
	}

	if ((1 << sdq_log2sz != MLXSW_PCI_WQE_COUNT) ||
	    (1 << rdq_log2sz != MLXSW_PCI_WQE_COUNT) ||
	    (1 << cq_log2sz != MLXSW_PCI_CQE01_COUNT) ||
	    (mlxsw_pci->max_cqe_ver == MLXSW_PCI_CQE_V2 &&
	     (1 << cqv2_log2sz != MLXSW_PCI_CQE2_COUNT)) ||
	    (1 << eq_log2sz != MLXSW_PCI_EQE_COUNT)) {
		dev_err(&pdev->dev, "Unsupported number of async queue descriptors\n");
		return -EINVAL;
	}

	mlxsw_pci->num_sdq_cqs = num_sdqs;

	err = mlxsw_pci_queue_group_init(mlxsw_pci, mbox, &mlxsw_pci_eq_ops,
					 num_eqs);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize event queues\n");
		return err;
	}

	err = mlxsw_pci_queue_group_init(mlxsw_pci, mbox, &mlxsw_pci_cq_ops,
					 num_cqs);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize completion queues\n");
		goto err_cqs_init;
	}

	err = mlxsw_pci_queue_group_init(mlxsw_pci, mbox, &mlxsw_pci_sdq_ops,
					 num_sdqs);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize send descriptor queues\n");
		goto err_sdqs_init;
	}

	err = mlxsw_pci_queue_group_init(mlxsw_pci, mbox, &mlxsw_pci_rdq_ops,
					 num_rdqs);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize receive descriptor queues\n");
		goto err_rdqs_init;
	}

	/* We have to poll in command interface until queues are initialized */
	mlxsw_pci->cmd.nopoll = true;
	return 0;

err_rdqs_init:
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_sdq_ops);
err_sdqs_init:
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_cq_ops);
err_cqs_init:
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_eq_ops);
	return err;
}

static void mlxsw_pci_aqs_fini(struct mlxsw_pci *mlxsw_pci)
{
	mlxsw_pci->cmd.nopoll = false;
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_rdq_ops);
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_sdq_ops);
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_cq_ops);
	mlxsw_pci_queue_group_fini(mlxsw_pci, &mlxsw_pci_eq_ops);
}

static void
mlxsw_pci_config_profile_swid_config(struct mlxsw_pci *mlxsw_pci,
				     char *mbox, int index,
				     const struct mlxsw_swid_config *swid)
{
	u8 mask = 0;

	if (swid->used_type) {
		mlxsw_cmd_mbox_config_profile_swid_config_type_set(
			mbox, index, swid->type);
		mask |= 1;
	}
	if (swid->used_properties) {
		mlxsw_cmd_mbox_config_profile_swid_config_properties_set(
			mbox, index, swid->properties);
		mask |= 2;
	}
	mlxsw_cmd_mbox_config_profile_swid_config_mask_set(mbox, index, mask);
}

static int
mlxsw_pci_profile_get_kvd_sizes(const struct mlxsw_pci *mlxsw_pci,
				const struct mlxsw_config_profile *profile,
				struct mlxsw_res *res)
{
	u64 single_size, double_size, linear_size;
	int err;

	err = mlxsw_core_kvd_sizes_get(mlxsw_pci->core, profile,
				       &single_size, &double_size,
				       &linear_size);
	if (err)
		return err;

	MLXSW_RES_SET(res, KVD_SINGLE_SIZE, single_size);
	MLXSW_RES_SET(res, KVD_DOUBLE_SIZE, double_size);
	MLXSW_RES_SET(res, KVD_LINEAR_SIZE, linear_size);

	return 0;
}

static int mlxsw_pci_config_profile(struct mlxsw_pci *mlxsw_pci, char *mbox,
				    const struct mlxsw_config_profile *profile,
				    struct mlxsw_res *res)
{
	int i;
	int err;

	mlxsw_cmd_mbox_zero(mbox);

	if (profile->used_max_vepa_channels) {
		mlxsw_cmd_mbox_config_profile_set_max_vepa_channels_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_vepa_channels_set(
			mbox, profile->max_vepa_channels);
	}
	if (profile->used_max_mid) {
		mlxsw_cmd_mbox_config_profile_set_max_mid_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_mid_set(
			mbox, profile->max_mid);
	}
	if (profile->used_max_pgt) {
		mlxsw_cmd_mbox_config_profile_set_max_pgt_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_pgt_set(
			mbox, profile->max_pgt);
	}
	if (profile->used_max_system_port) {
		mlxsw_cmd_mbox_config_profile_set_max_system_port_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_system_port_set(
			mbox, profile->max_system_port);
	}
	if (profile->used_max_vlan_groups) {
		mlxsw_cmd_mbox_config_profile_set_max_vlan_groups_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_vlan_groups_set(
			mbox, profile->max_vlan_groups);
	}
	if (profile->used_max_regions) {
		mlxsw_cmd_mbox_config_profile_set_max_regions_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_regions_set(
			mbox, profile->max_regions);
	}
	if (profile->used_flood_tables) {
		mlxsw_cmd_mbox_config_profile_set_flood_tables_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_flood_tables_set(
			mbox, profile->max_flood_tables);
		mlxsw_cmd_mbox_config_profile_max_vid_flood_tables_set(
			mbox, profile->max_vid_flood_tables);
		mlxsw_cmd_mbox_config_profile_max_fid_offset_flood_tables_set(
			mbox, profile->max_fid_offset_flood_tables);
		mlxsw_cmd_mbox_config_profile_fid_offset_flood_table_size_set(
			mbox, profile->fid_offset_flood_table_size);
		mlxsw_cmd_mbox_config_profile_max_fid_flood_tables_set(
			mbox, profile->max_fid_flood_tables);
		mlxsw_cmd_mbox_config_profile_fid_flood_table_size_set(
			mbox, profile->fid_flood_table_size);
	}
	if (profile->used_flood_mode) {
		mlxsw_cmd_mbox_config_profile_set_flood_mode_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_flood_mode_set(
			mbox, profile->flood_mode);
	}
	if (profile->used_max_ib_mc) {
		mlxsw_cmd_mbox_config_profile_set_max_ib_mc_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_ib_mc_set(
			mbox, profile->max_ib_mc);
	}
	if (profile->used_max_pkey) {
		mlxsw_cmd_mbox_config_profile_set_max_pkey_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_max_pkey_set(
			mbox, profile->max_pkey);
	}
	if (profile->used_ar_sec) {
		mlxsw_cmd_mbox_config_profile_set_ar_sec_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_ar_sec_set(
			mbox, profile->ar_sec);
	}
	if (profile->used_adaptive_routing_group_cap) {
		mlxsw_cmd_mbox_config_profile_set_adaptive_routing_group_cap_set(
			mbox, 1);
		mlxsw_cmd_mbox_config_profile_adaptive_routing_group_cap_set(
			mbox, profile->adaptive_routing_group_cap);
	}
	if (profile->used_ubridge) {
		mlxsw_cmd_mbox_config_profile_set_ubridge_set(mbox, 1);
		mlxsw_cmd_mbox_config_profile_ubridge_set(mbox,
							  profile->ubridge);
	}
	if (profile->used_kvd_sizes && MLXSW_RES_VALID(res, KVD_SIZE)) {
		err = mlxsw_pci_profile_get_kvd_sizes(mlxsw_pci, profile, res);
		if (err)
			return err;

		mlxsw_cmd_mbox_config_profile_set_kvd_linear_size_set(mbox, 1);
		mlxsw_cmd_mbox_config_profile_kvd_linear_size_set(mbox,
					MLXSW_RES_GET(res, KVD_LINEAR_SIZE));
		mlxsw_cmd_mbox_config_profile_set_kvd_hash_single_size_set(mbox,
									   1);
		mlxsw_cmd_mbox_config_profile_kvd_hash_single_size_set(mbox,
					MLXSW_RES_GET(res, KVD_SINGLE_SIZE));
		mlxsw_cmd_mbox_config_profile_set_kvd_hash_double_size_set(
								mbox, 1);
		mlxsw_cmd_mbox_config_profile_kvd_hash_double_size_set(mbox,
					MLXSW_RES_GET(res, KVD_DOUBLE_SIZE));
	}

	for (i = 0; i < MLXSW_CONFIG_PROFILE_SWID_COUNT; i++)
		mlxsw_pci_config_profile_swid_config(mlxsw_pci, mbox, i,
						     &profile->swid_config[i]);

	if (mlxsw_pci->max_cqe_ver > MLXSW_PCI_CQE_V0) {
		mlxsw_cmd_mbox_config_profile_set_cqe_version_set(mbox, 1);
		mlxsw_cmd_mbox_config_profile_cqe_version_set(mbox, 1);
	}

	if (profile->used_cqe_time_stamp_type) {
		mlxsw_cmd_mbox_config_profile_set_cqe_time_stamp_type_set(mbox,
									  1);
		mlxsw_cmd_mbox_config_profile_cqe_time_stamp_type_set(mbox,
					profile->cqe_time_stamp_type);
	}

	return mlxsw_cmd_config_profile_set(mlxsw_pci->core, mbox);
}

static int mlxsw_pci_boardinfo(struct mlxsw_pci *mlxsw_pci, char *mbox)
{
	struct mlxsw_bus_info *bus_info = &mlxsw_pci->bus_info;
	int err;

	mlxsw_cmd_mbox_zero(mbox);
	err = mlxsw_cmd_boardinfo(mlxsw_pci->core, mbox);
	if (err)
		return err;
	mlxsw_cmd_mbox_boardinfo_vsd_memcpy_from(mbox, bus_info->vsd);
	mlxsw_cmd_mbox_boardinfo_psid_memcpy_from(mbox, bus_info->psid);
	return 0;
}

static int mlxsw_pci_fw_area_init(struct mlxsw_pci *mlxsw_pci, char *mbox,
				  u16 num_pages)
{
	struct mlxsw_pci_mem_item *mem_item;
	int nent = 0;
	int i;
	int err;

	mlxsw_pci->fw_area.items = kcalloc(num_pages, sizeof(*mem_item),
					   GFP_KERNEL);
	if (!mlxsw_pci->fw_area.items)
		return -ENOMEM;
	mlxsw_pci->fw_area.count = num_pages;

	mlxsw_cmd_mbox_zero(mbox);
	for (i = 0; i < num_pages; i++) {
		mem_item = &mlxsw_pci->fw_area.items[i];

		mem_item->size = MLXSW_PCI_PAGE_SIZE;
		mem_item->buf = dma_alloc_coherent(&mlxsw_pci->pdev->dev,
						   mem_item->size,
						   &mem_item->mapaddr, GFP_KERNEL);
		if (!mem_item->buf) {
			err = -ENOMEM;
			goto err_alloc;
		}
		mlxsw_cmd_mbox_map_fa_pa_set(mbox, nent, mem_item->mapaddr);
		mlxsw_cmd_mbox_map_fa_log2size_set(mbox, nent, 0); /* 1 page */
		if (++nent == MLXSW_CMD_MAP_FA_VPM_ENTRIES_MAX) {
			err = mlxsw_cmd_map_fa(mlxsw_pci->core, mbox, nent);
			if (err)
				goto err_cmd_map_fa;
			nent = 0;
			mlxsw_cmd_mbox_zero(mbox);
		}
	}

	if (nent) {
		err = mlxsw_cmd_map_fa(mlxsw_pci->core, mbox, nent);
		if (err)
			goto err_cmd_map_fa;
	}

	return 0;

err_cmd_map_fa:
err_alloc:
	for (i--; i >= 0; i--) {
		mem_item = &mlxsw_pci->fw_area.items[i];

		dma_free_coherent(&mlxsw_pci->pdev->dev, mem_item->size,
				  mem_item->buf, mem_item->mapaddr);
	}
	kfree(mlxsw_pci->fw_area.items);
	return err;
}

static void mlxsw_pci_fw_area_fini(struct mlxsw_pci *mlxsw_pci)
{
	struct mlxsw_pci_mem_item *mem_item;
	int i;

	mlxsw_cmd_unmap_fa(mlxsw_pci->core);

	for (i = 0; i < mlxsw_pci->fw_area.count; i++) {
		mem_item = &mlxsw_pci->fw_area.items[i];

		dma_free_coherent(&mlxsw_pci->pdev->dev, mem_item->size,
				  mem_item->buf, mem_item->mapaddr);
	}
	kfree(mlxsw_pci->fw_area.items);
}

static irqreturn_t mlxsw_pci_eq_irq_handler(int irq, void *dev_id)
{
	struct mlxsw_pci *mlxsw_pci = dev_id;
	struct mlxsw_pci_queue *q;
	int i;

	for (i = 0; i < MLXSW_PCI_EQS_COUNT; i++) {
		q = mlxsw_pci_eq_get(mlxsw_pci, i);
		mlxsw_pci_queue_tasklet_schedule(q);
	}
	return IRQ_HANDLED;
}

static int mlxsw_pci_mbox_alloc(struct mlxsw_pci *mlxsw_pci,
				struct mlxsw_pci_mem_item *mbox)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	int err = 0;

	mbox->size = MLXSW_CMD_MBOX_SIZE;
	mbox->buf = dma_alloc_coherent(&pdev->dev, MLXSW_CMD_MBOX_SIZE,
				       &mbox->mapaddr, GFP_KERNEL);
	if (!mbox->buf) {
		dev_err(&pdev->dev, "Failed allocating memory for mailbox\n");
		err = -ENOMEM;
	}

	return err;
}

static void mlxsw_pci_mbox_free(struct mlxsw_pci *mlxsw_pci,
				struct mlxsw_pci_mem_item *mbox)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;

	dma_free_coherent(&pdev->dev, MLXSW_CMD_MBOX_SIZE, mbox->buf,
			  mbox->mapaddr);
}

static int mlxsw_pci_sys_ready_wait(struct mlxsw_pci *mlxsw_pci,
				    const struct pci_device_id *id,
				    u32 *p_sys_status)
{
	unsigned long end;
	u32 val;

	/* We must wait for the HW to become responsive. */
	msleep(MLXSW_PCI_SW_RESET_WAIT_MSECS);

	end = jiffies + msecs_to_jiffies(MLXSW_PCI_SW_RESET_TIMEOUT_MSECS);
	do {
		val = mlxsw_pci_read32(mlxsw_pci, FW_READY);
		if ((val & MLXSW_PCI_FW_READY_MASK) == MLXSW_PCI_FW_READY_MAGIC)
			return 0;
		cond_resched();
	} while (time_before(jiffies, end));

	*p_sys_status = val & MLXSW_PCI_FW_READY_MASK;

	return -EBUSY;
}

static int mlxsw_pci_sw_reset(struct mlxsw_pci *mlxsw_pci,
			      const struct pci_device_id *id)
{
	struct pci_dev *pdev = mlxsw_pci->pdev;
	char mrsr_pl[MLXSW_REG_MRSR_LEN];
	u32 sys_status;
	int err;

	err = mlxsw_pci_sys_ready_wait(mlxsw_pci, id, &sys_status);
	if (err) {
		dev_err(&pdev->dev, "Failed to reach system ready status before reset. Status is 0x%x\n",
			sys_status);
		return err;
	}

	mlxsw_reg_mrsr_pack(mrsr_pl);
	err = mlxsw_reg_write(mlxsw_pci->core, MLXSW_REG(mrsr), mrsr_pl);
	if (err)
		return err;

	err = mlxsw_pci_sys_ready_wait(mlxsw_pci, id, &sys_status);
	if (err) {
		dev_err(&pdev->dev, "Failed to reach system ready status after reset. Status is 0x%x\n",
			sys_status);
		return err;
	}

	return 0;
}

static int mlxsw_pci_alloc_irq_vectors(struct mlxsw_pci *mlxsw_pci)
{
	int err;

	err = pci_alloc_irq_vectors(mlxsw_pci->pdev, 1, 1, PCI_IRQ_MSIX);
	if (err < 0)
		dev_err(&mlxsw_pci->pdev->dev, "MSI-X init failed\n");
	return err;
}

static void mlxsw_pci_free_irq_vectors(struct mlxsw_pci *mlxsw_pci)
{
	pci_free_irq_vectors(mlxsw_pci->pdev);
}

static int mlxsw_pci_init(void *bus_priv, struct mlxsw_core *mlxsw_core,
			  const struct mlxsw_config_profile *profile,
			  struct mlxsw_res *res)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;
	struct pci_dev *pdev = mlxsw_pci->pdev;
	char *mbox;
	u16 num_pages;
	int err;

	mlxsw_pci->core = mlxsw_core;

	mbox = mlxsw_cmd_mbox_alloc();
	if (!mbox)
		return -ENOMEM;

	err = mlxsw_pci_sw_reset(mlxsw_pci, mlxsw_pci->id);
	if (err)
		goto err_sw_reset;

	err = mlxsw_pci_alloc_irq_vectors(mlxsw_pci);
	if (err < 0) {
		dev_err(&pdev->dev, "MSI-X init failed\n");
		goto err_alloc_irq;
	}

	err = mlxsw_cmd_query_fw(mlxsw_core, mbox);
	if (err)
		goto err_query_fw;

	mlxsw_pci->bus_info.fw_rev.major =
		mlxsw_cmd_mbox_query_fw_fw_rev_major_get(mbox);
	mlxsw_pci->bus_info.fw_rev.minor =
		mlxsw_cmd_mbox_query_fw_fw_rev_minor_get(mbox);
	mlxsw_pci->bus_info.fw_rev.subminor =
		mlxsw_cmd_mbox_query_fw_fw_rev_subminor_get(mbox);

	if (mlxsw_cmd_mbox_query_fw_cmd_interface_rev_get(mbox) != 1) {
		dev_err(&pdev->dev, "Unsupported cmd interface revision ID queried from hw\n");
		err = -EINVAL;
		goto err_iface_rev;
	}
	if (mlxsw_cmd_mbox_query_fw_doorbell_page_bar_get(mbox) != 0) {
		dev_err(&pdev->dev, "Unsupported doorbell page bar queried from hw\n");
		err = -EINVAL;
		goto err_doorbell_page_bar;
	}

	mlxsw_pci->doorbell_offset =
		mlxsw_cmd_mbox_query_fw_doorbell_page_offset_get(mbox);

	if (mlxsw_cmd_mbox_query_fw_fr_rn_clk_bar_get(mbox) != 0) {
		dev_err(&pdev->dev, "Unsupported free running clock BAR queried from hw\n");
		err = -EINVAL;
		goto err_fr_rn_clk_bar;
	}

	mlxsw_pci->free_running_clock_offset =
		mlxsw_cmd_mbox_query_fw_free_running_clock_offset_get(mbox);

	num_pages = mlxsw_cmd_mbox_query_fw_fw_pages_get(mbox);
	err = mlxsw_pci_fw_area_init(mlxsw_pci, mbox, num_pages);
	if (err)
		goto err_fw_area_init;

	err = mlxsw_pci_boardinfo(mlxsw_pci, mbox);
	if (err)
		goto err_boardinfo;

	err = mlxsw_core_resources_query(mlxsw_core, mbox, res);
	if (err)
		goto err_query_resources;

	if (MLXSW_CORE_RES_VALID(mlxsw_core, CQE_V2) &&
	    MLXSW_CORE_RES_GET(mlxsw_core, CQE_V2))
		mlxsw_pci->max_cqe_ver = MLXSW_PCI_CQE_V2;
	else if (MLXSW_CORE_RES_VALID(mlxsw_core, CQE_V1) &&
		 MLXSW_CORE_RES_GET(mlxsw_core, CQE_V1))
		mlxsw_pci->max_cqe_ver = MLXSW_PCI_CQE_V1;
	else if ((MLXSW_CORE_RES_VALID(mlxsw_core, CQE_V0) &&
		  MLXSW_CORE_RES_GET(mlxsw_core, CQE_V0)) ||
		 !MLXSW_CORE_RES_VALID(mlxsw_core, CQE_V0)) {
		mlxsw_pci->max_cqe_ver = MLXSW_PCI_CQE_V0;
	} else {
		dev_err(&pdev->dev, "Invalid supported CQE version combination reported\n");
		goto err_cqe_v_check;
	}

	err = mlxsw_pci_config_profile(mlxsw_pci, mbox, profile, res);
	if (err)
		goto err_config_profile;

	/* Some resources depend on unified bridge model, which is configured
	 * as part of config_profile. Query the resources again to get correct
	 * values.
	 */
	err = mlxsw_core_resources_query(mlxsw_core, mbox, res);
	if (err)
		goto err_requery_resources;

	err = mlxsw_pci_aqs_init(mlxsw_pci, mbox);
	if (err)
		goto err_aqs_init;

	err = request_irq(pci_irq_vector(pdev, 0),
			  mlxsw_pci_eq_irq_handler, 0,
			  mlxsw_pci->bus_info.device_kind, mlxsw_pci);
	if (err) {
		dev_err(&pdev->dev, "IRQ request failed\n");
		goto err_request_eq_irq;
	}

	goto mbox_put;

err_request_eq_irq:
	mlxsw_pci_aqs_fini(mlxsw_pci);
err_aqs_init:
err_requery_resources:
err_config_profile:
err_cqe_v_check:
err_query_resources:
err_boardinfo:
	mlxsw_pci_fw_area_fini(mlxsw_pci);
err_fw_area_init:
err_fr_rn_clk_bar:
err_doorbell_page_bar:
err_iface_rev:
err_query_fw:
	mlxsw_pci_free_irq_vectors(mlxsw_pci);
err_alloc_irq:
err_sw_reset:
mbox_put:
	mlxsw_cmd_mbox_free(mbox);
	return err;
}

static void mlxsw_pci_fini(void *bus_priv)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;

	free_irq(pci_irq_vector(mlxsw_pci->pdev, 0), mlxsw_pci);
	mlxsw_pci_aqs_fini(mlxsw_pci);
	mlxsw_pci_fw_area_fini(mlxsw_pci);
	mlxsw_pci_free_irq_vectors(mlxsw_pci);
}

static struct mlxsw_pci_queue *
mlxsw_pci_sdq_pick(struct mlxsw_pci *mlxsw_pci,
		   const struct mlxsw_tx_info *tx_info)
{
	u8 ctl_sdq_count = mlxsw_pci_sdq_count(mlxsw_pci) - 1;
	u8 sdqn;

	if (tx_info->is_emad) {
		sdqn = MLXSW_PCI_SDQ_EMAD_INDEX;
	} else {
		BUILD_BUG_ON(MLXSW_PCI_SDQ_EMAD_INDEX != 0);
		sdqn = 1 + (tx_info->local_port % ctl_sdq_count);
	}

	return mlxsw_pci_sdq_get(mlxsw_pci, sdqn);
}

static bool mlxsw_pci_skb_transmit_busy(void *bus_priv,
					const struct mlxsw_tx_info *tx_info)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;
	struct mlxsw_pci_queue *q = mlxsw_pci_sdq_pick(mlxsw_pci, tx_info);

	return !mlxsw_pci_queue_elem_info_producer_get(q);
}

static int mlxsw_pci_skb_transmit(void *bus_priv, struct sk_buff *skb,
				  const struct mlxsw_tx_info *tx_info)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;
	struct mlxsw_pci_queue *q;
	struct mlxsw_pci_queue_elem_info *elem_info;
	char *wqe;
	int i;
	int err;

	if (skb_shinfo(skb)->nr_frags > MLXSW_PCI_WQE_SG_ENTRIES - 1) {
		err = skb_linearize(skb);
		if (err)
			return err;
	}

	q = mlxsw_pci_sdq_pick(mlxsw_pci, tx_info);
	spin_lock_bh(&q->lock);
	elem_info = mlxsw_pci_queue_elem_info_producer_get(q);
	if (!elem_info) {
		/* queue is full */
		err = -EAGAIN;
		goto unlock;
	}
	mlxsw_skb_cb(skb)->tx_info = *tx_info;
	elem_info->u.sdq.skb = skb;

	wqe = elem_info->elem;
	mlxsw_pci_wqe_c_set(wqe, 1); /* always report completion */
	mlxsw_pci_wqe_lp_set(wqe, 0);
	mlxsw_pci_wqe_type_set(wqe, MLXSW_PCI_WQE_TYPE_ETHERNET);

	err = mlxsw_pci_wqe_frag_map(mlxsw_pci, wqe, 0, skb->data,
				     skb_headlen(skb), DMA_TO_DEVICE);
	if (err)
		goto unlock;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		err = mlxsw_pci_wqe_frag_map(mlxsw_pci, wqe, i + 1,
					     skb_frag_address(frag),
					     skb_frag_size(frag),
					     DMA_TO_DEVICE);
		if (err)
			goto unmap_frags;
	}

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	/* Set unused sq entries byte count to zero. */
	for (i++; i < MLXSW_PCI_WQE_SG_ENTRIES; i++)
		mlxsw_pci_wqe_byte_count_set(wqe, i, 0);

	/* Everything is set up, ring producer doorbell to get HW going */
	q->producer_counter++;
	mlxsw_pci_queue_doorbell_producer_ring(mlxsw_pci, q);

	goto unlock;

unmap_frags:
	for (; i >= 0; i--)
		mlxsw_pci_wqe_frag_unmap(mlxsw_pci, wqe, i, DMA_TO_DEVICE);
unlock:
	spin_unlock_bh(&q->lock);
	return err;
}

static int mlxsw_pci_cmd_exec(void *bus_priv, u16 opcode, u8 opcode_mod,
			      u32 in_mod, bool out_mbox_direct,
			      char *in_mbox, size_t in_mbox_size,
			      char *out_mbox, size_t out_mbox_size,
			      u8 *p_status)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;
	dma_addr_t in_mapaddr = 0, out_mapaddr = 0;
	bool evreq = mlxsw_pci->cmd.nopoll;
	unsigned long timeout = msecs_to_jiffies(MLXSW_PCI_CIR_TIMEOUT_MSECS);
	bool *p_wait_done = &mlxsw_pci->cmd.wait_done;
	int err;

	*p_status = MLXSW_CMD_STATUS_OK;

	err = mutex_lock_interruptible(&mlxsw_pci->cmd.lock);
	if (err)
		return err;

	if (in_mbox) {
		memcpy(mlxsw_pci->cmd.in_mbox.buf, in_mbox, in_mbox_size);
		in_mapaddr = mlxsw_pci->cmd.in_mbox.mapaddr;
	}
	mlxsw_pci_write32(mlxsw_pci, CIR_IN_PARAM_HI, upper_32_bits(in_mapaddr));
	mlxsw_pci_write32(mlxsw_pci, CIR_IN_PARAM_LO, lower_32_bits(in_mapaddr));

	if (out_mbox)
		out_mapaddr = mlxsw_pci->cmd.out_mbox.mapaddr;
	mlxsw_pci_write32(mlxsw_pci, CIR_OUT_PARAM_HI, upper_32_bits(out_mapaddr));
	mlxsw_pci_write32(mlxsw_pci, CIR_OUT_PARAM_LO, lower_32_bits(out_mapaddr));

	mlxsw_pci_write32(mlxsw_pci, CIR_IN_MODIFIER, in_mod);
	mlxsw_pci_write32(mlxsw_pci, CIR_TOKEN, 0);

	*p_wait_done = false;

	wmb(); /* all needs to be written before we write control register */
	mlxsw_pci_write32(mlxsw_pci, CIR_CTRL,
			  MLXSW_PCI_CIR_CTRL_GO_BIT |
			  (evreq ? MLXSW_PCI_CIR_CTRL_EVREQ_BIT : 0) |
			  (opcode_mod << MLXSW_PCI_CIR_CTRL_OPCODE_MOD_SHIFT) |
			  opcode);

	if (!evreq) {
		unsigned long end;

		end = jiffies + timeout;
		do {
			u32 ctrl = mlxsw_pci_read32(mlxsw_pci, CIR_CTRL);

			if (!(ctrl & MLXSW_PCI_CIR_CTRL_GO_BIT)) {
				*p_wait_done = true;
				*p_status = ctrl >> MLXSW_PCI_CIR_CTRL_STATUS_SHIFT;
				break;
			}
			cond_resched();
		} while (time_before(jiffies, end));
	} else {
		wait_event_timeout(mlxsw_pci->cmd.wait, *p_wait_done, timeout);
		*p_status = mlxsw_pci->cmd.comp.status;
	}

	err = 0;
	if (*p_wait_done) {
		if (*p_status)
			err = -EIO;
	} else {
		err = -ETIMEDOUT;
	}

	if (!err && out_mbox && out_mbox_direct) {
		/* Some commands don't use output param as address to mailbox
		 * but they store output directly into registers. In that case,
		 * copy registers into mbox buffer.
		 */
		__be32 tmp;

		if (!evreq) {
			tmp = cpu_to_be32(mlxsw_pci_read32(mlxsw_pci,
							   CIR_OUT_PARAM_HI));
			memcpy(out_mbox, &tmp, sizeof(tmp));
			tmp = cpu_to_be32(mlxsw_pci_read32(mlxsw_pci,
							   CIR_OUT_PARAM_LO));
			memcpy(out_mbox + sizeof(tmp), &tmp, sizeof(tmp));
		}
	} else if (!err && out_mbox) {
		memcpy(out_mbox, mlxsw_pci->cmd.out_mbox.buf, out_mbox_size);
	}

	mutex_unlock(&mlxsw_pci->cmd.lock);

	return err;
}

static u32 mlxsw_pci_read_frc_h(void *bus_priv)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;
	u64 frc_offset_h;

	frc_offset_h = mlxsw_pci->free_running_clock_offset;
	return mlxsw_pci_read32_off(mlxsw_pci, frc_offset_h);
}

static u32 mlxsw_pci_read_frc_l(void *bus_priv)
{
	struct mlxsw_pci *mlxsw_pci = bus_priv;
	u64 frc_offset_l;

	frc_offset_l = mlxsw_pci->free_running_clock_offset + 4;
	return mlxsw_pci_read32_off(mlxsw_pci, frc_offset_l);
}

static const struct mlxsw_bus mlxsw_pci_bus = {
	.kind			= "pci",
	.init			= mlxsw_pci_init,
	.fini			= mlxsw_pci_fini,
	.skb_transmit_busy	= mlxsw_pci_skb_transmit_busy,
	.skb_transmit		= mlxsw_pci_skb_transmit,
	.cmd_exec		= mlxsw_pci_cmd_exec,
	.read_frc_h		= mlxsw_pci_read_frc_h,
	.read_frc_l		= mlxsw_pci_read_frc_l,
	.features		= MLXSW_BUS_F_TXRX | MLXSW_BUS_F_RESET,
};

static int mlxsw_pci_cmd_init(struct mlxsw_pci *mlxsw_pci)
{
	int err;

	mutex_init(&mlxsw_pci->cmd.lock);
	init_waitqueue_head(&mlxsw_pci->cmd.wait);

	err = mlxsw_pci_mbox_alloc(mlxsw_pci, &mlxsw_pci->cmd.in_mbox);
	if (err)
		goto err_in_mbox_alloc;

	err = mlxsw_pci_mbox_alloc(mlxsw_pci, &mlxsw_pci->cmd.out_mbox);
	if (err)
		goto err_out_mbox_alloc;

	return 0;

err_out_mbox_alloc:
	mlxsw_pci_mbox_free(mlxsw_pci, &mlxsw_pci->cmd.in_mbox);
err_in_mbox_alloc:
	mutex_destroy(&mlxsw_pci->cmd.lock);
	return err;
}

static void mlxsw_pci_cmd_fini(struct mlxsw_pci *mlxsw_pci)
{
	mlxsw_pci_mbox_free(mlxsw_pci, &mlxsw_pci->cmd.out_mbox);
	mlxsw_pci_mbox_free(mlxsw_pci, &mlxsw_pci->cmd.in_mbox);
	mutex_destroy(&mlxsw_pci->cmd.lock);
}

static int mlxsw_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	const char *driver_name = dev_driver_string(&pdev->dev);
	struct mlxsw_pci *mlxsw_pci;
	int err;

	mlxsw_pci = kzalloc(sizeof(*mlxsw_pci), GFP_KERNEL);
	if (!mlxsw_pci)
		return -ENOMEM;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		goto err_pci_enable_device;
	}

	err = pci_request_regions(pdev, driver_name);
	if (err) {
		dev_err(&pdev->dev, "pci_request_regions failed\n");
		goto err_pci_request_regions;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "dma_set_mask failed\n");
			goto err_pci_set_dma_mask;
		}
	}

	if (pci_resource_len(pdev, 0) < MLXSW_PCI_BAR0_SIZE) {
		dev_err(&pdev->dev, "invalid PCI region size\n");
		err = -EINVAL;
		goto err_pci_resource_len_check;
	}

	mlxsw_pci->hw_addr = ioremap(pci_resource_start(pdev, 0),
				     pci_resource_len(pdev, 0));
	if (!mlxsw_pci->hw_addr) {
		dev_err(&pdev->dev, "ioremap failed\n");
		err = -EIO;
		goto err_ioremap;
	}
	pci_set_master(pdev);

	mlxsw_pci->pdev = pdev;
	pci_set_drvdata(pdev, mlxsw_pci);

	err = mlxsw_pci_cmd_init(mlxsw_pci);
	if (err)
		goto err_pci_cmd_init;

	mlxsw_pci->bus_info.device_kind = driver_name;
	mlxsw_pci->bus_info.device_name = pci_name(mlxsw_pci->pdev);
	mlxsw_pci->bus_info.dev = &pdev->dev;
	mlxsw_pci->bus_info.read_clock_capable = true;
	mlxsw_pci->id = id;

	err = mlxsw_core_bus_device_register(&mlxsw_pci->bus_info,
					     &mlxsw_pci_bus, mlxsw_pci, false,
					     NULL, NULL);
	if (err) {
		dev_err(&pdev->dev, "cannot register bus device\n");
		goto err_bus_device_register;
	}

	return 0;

err_bus_device_register:
	mlxsw_pci_cmd_fini(mlxsw_pci);
err_pci_cmd_init:
	iounmap(mlxsw_pci->hw_addr);
err_ioremap:
err_pci_resource_len_check:
err_pci_set_dma_mask:
	pci_release_regions(pdev);
err_pci_request_regions:
	pci_disable_device(pdev);
err_pci_enable_device:
	kfree(mlxsw_pci);
	return err;
}

static void mlxsw_pci_remove(struct pci_dev *pdev)
{
	struct mlxsw_pci *mlxsw_pci = pci_get_drvdata(pdev);

	mlxsw_core_bus_device_unregister(mlxsw_pci->core, false);
	mlxsw_pci_cmd_fini(mlxsw_pci);
	iounmap(mlxsw_pci->hw_addr);
	pci_release_regions(mlxsw_pci->pdev);
	pci_disable_device(mlxsw_pci->pdev);
	kfree(mlxsw_pci);
}

int mlxsw_pci_driver_register(struct pci_driver *pci_driver)
{
	pci_driver->probe = mlxsw_pci_probe;
	pci_driver->remove = mlxsw_pci_remove;
	pci_driver->shutdown = mlxsw_pci_remove;
	return pci_register_driver(pci_driver);
}
EXPORT_SYMBOL(mlxsw_pci_driver_register);

void mlxsw_pci_driver_unregister(struct pci_driver *pci_driver)
{
	pci_unregister_driver(pci_driver);
}
EXPORT_SYMBOL(mlxsw_pci_driver_unregister);

static int __init mlxsw_pci_module_init(void)
{
	return 0;
}

static void __exit mlxsw_pci_module_exit(void)
{
}

module_init(mlxsw_pci_module_init);
module_exit(mlxsw_pci_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Mellanox switch PCI interface driver");
