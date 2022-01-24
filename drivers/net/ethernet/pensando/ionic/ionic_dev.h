/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_DEV_H_
#define _IONIC_DEV_H_

#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "ionic_if.h"
#include "ionic_regs.h"

#define IONIC_MAX_TX_DESC		8192
#define IONIC_MAX_RX_DESC		16384
#define IONIC_MIN_TXRX_DESC		64
#define IONIC_DEF_TXRX_DESC		4096
#define IONIC_RX_FILL_THRESHOLD		16
#define IONIC_RX_FILL_DIV		8
#define IONIC_LIFS_MAX			1024
#define IONIC_WATCHDOG_SECS		5
#define IONIC_ITR_COAL_USEC_DEFAULT	64

#define IONIC_DEV_CMD_REG_VERSION	1
#define IONIC_DEV_INFO_REG_COUNT	32
#define IONIC_DEV_CMD_REG_COUNT		32

struct ionic_dev_bar {
	void __iomem *vaddr;
	phys_addr_t bus_addr;
	unsigned long len;
	int res_index;
};

#ifndef __CHECKER__
/* Registers */
static_assert(sizeof(struct ionic_intr) == 32);

static_assert(sizeof(struct ionic_doorbell) == 8);
static_assert(sizeof(struct ionic_intr_status) == 8);
static_assert(sizeof(union ionic_dev_regs) == 4096);
static_assert(sizeof(union ionic_dev_info_regs) == 2048);
static_assert(sizeof(union ionic_dev_cmd_regs) == 2048);
static_assert(sizeof(struct ionic_lif_stats) == 1024);

static_assert(sizeof(struct ionic_admin_cmd) == 64);
static_assert(sizeof(struct ionic_admin_comp) == 16);
static_assert(sizeof(struct ionic_nop_cmd) == 64);
static_assert(sizeof(struct ionic_nop_comp) == 16);

/* Device commands */
static_assert(sizeof(struct ionic_dev_identify_cmd) == 64);
static_assert(sizeof(struct ionic_dev_identify_comp) == 16);
static_assert(sizeof(struct ionic_dev_init_cmd) == 64);
static_assert(sizeof(struct ionic_dev_init_comp) == 16);
static_assert(sizeof(struct ionic_dev_reset_cmd) == 64);
static_assert(sizeof(struct ionic_dev_reset_comp) == 16);
static_assert(sizeof(struct ionic_dev_getattr_cmd) == 64);
static_assert(sizeof(struct ionic_dev_getattr_comp) == 16);
static_assert(sizeof(struct ionic_dev_setattr_cmd) == 64);
static_assert(sizeof(struct ionic_dev_setattr_comp) == 16);
static_assert(sizeof(struct ionic_lif_setphc_cmd) == 64);

/* Port commands */
static_assert(sizeof(struct ionic_port_identify_cmd) == 64);
static_assert(sizeof(struct ionic_port_identify_comp) == 16);
static_assert(sizeof(struct ionic_port_init_cmd) == 64);
static_assert(sizeof(struct ionic_port_init_comp) == 16);
static_assert(sizeof(struct ionic_port_reset_cmd) == 64);
static_assert(sizeof(struct ionic_port_reset_comp) == 16);
static_assert(sizeof(struct ionic_port_getattr_cmd) == 64);
static_assert(sizeof(struct ionic_port_getattr_comp) == 16);
static_assert(sizeof(struct ionic_port_setattr_cmd) == 64);
static_assert(sizeof(struct ionic_port_setattr_comp) == 16);

/* LIF commands */
static_assert(sizeof(struct ionic_lif_init_cmd) == 64);
static_assert(sizeof(struct ionic_lif_init_comp) == 16);
static_assert(sizeof(struct ionic_lif_reset_cmd) == 64);
static_assert(sizeof(ionic_lif_reset_comp) == 16);
static_assert(sizeof(struct ionic_lif_getattr_cmd) == 64);
static_assert(sizeof(struct ionic_lif_getattr_comp) == 16);
static_assert(sizeof(struct ionic_lif_setattr_cmd) == 64);
static_assert(sizeof(struct ionic_lif_setattr_comp) == 16);

static_assert(sizeof(struct ionic_q_init_cmd) == 64);
static_assert(sizeof(struct ionic_q_init_comp) == 16);
static_assert(sizeof(struct ionic_q_control_cmd) == 64);
static_assert(sizeof(ionic_q_control_comp) == 16);
static_assert(sizeof(struct ionic_q_identify_cmd) == 64);
static_assert(sizeof(struct ionic_q_identify_comp) == 16);

static_assert(sizeof(struct ionic_rx_mode_set_cmd) == 64);
static_assert(sizeof(ionic_rx_mode_set_comp) == 16);
static_assert(sizeof(struct ionic_rx_filter_add_cmd) == 64);
static_assert(sizeof(struct ionic_rx_filter_add_comp) == 16);
static_assert(sizeof(struct ionic_rx_filter_del_cmd) == 64);
static_assert(sizeof(ionic_rx_filter_del_comp) == 16);

/* RDMA commands */
static_assert(sizeof(struct ionic_rdma_reset_cmd) == 64);
static_assert(sizeof(struct ionic_rdma_queue_cmd) == 64);

/* Events */
static_assert(sizeof(struct ionic_notifyq_cmd) == 4);
static_assert(sizeof(union ionic_notifyq_comp) == 64);
static_assert(sizeof(struct ionic_notifyq_event) == 64);
static_assert(sizeof(struct ionic_link_change_event) == 64);
static_assert(sizeof(struct ionic_reset_event) == 64);
static_assert(sizeof(struct ionic_heartbeat_event) == 64);
static_assert(sizeof(struct ionic_log_event) == 64);

/* I/O */
static_assert(sizeof(struct ionic_txq_desc) == 16);
static_assert(sizeof(struct ionic_txq_sg_desc) == 128);
static_assert(sizeof(struct ionic_txq_comp) == 16);

static_assert(sizeof(struct ionic_rxq_desc) == 16);
static_assert(sizeof(struct ionic_rxq_sg_desc) == 128);
static_assert(sizeof(struct ionic_rxq_comp) == 16);

/* SR/IOV */
static_assert(sizeof(struct ionic_vf_setattr_cmd) == 64);
static_assert(sizeof(struct ionic_vf_setattr_comp) == 16);
static_assert(sizeof(struct ionic_vf_getattr_cmd) == 64);
static_assert(sizeof(struct ionic_vf_getattr_comp) == 16);
#endif /* __CHECKER__ */

struct ionic_devinfo {
	u8 asic_type;
	u8 asic_rev;
	char fw_version[IONIC_DEVINFO_FWVERS_BUFLEN + 1];
	char serial_num[IONIC_DEVINFO_SERIAL_BUFLEN + 1];
};

struct ionic_dev {
	union ionic_dev_info_regs __iomem *dev_info_regs;
	union ionic_dev_cmd_regs __iomem *dev_cmd_regs;
	struct ionic_hwstamp_regs __iomem *hwstamp_regs;

	atomic_long_t last_check_time;
	unsigned long last_hb_time;
	u32 last_fw_hb;
	bool fw_hb_ready;
	bool fw_status_ready;
	u8 fw_generation;

	u64 __iomem *db_pages;
	dma_addr_t phy_db_pages;

	struct ionic_intr __iomem *intr_ctrl;
	u64 __iomem *intr_status;

	u32 port_info_sz;
	struct ionic_port_info *port_info;
	dma_addr_t port_info_pa;

	struct ionic_devinfo dev_info;
};

struct ionic_cq_info {
	union {
		void *cq_desc;
		struct ionic_admin_comp *admincq;
		struct ionic_notifyq_event *notifyq;
	};
};

struct ionic_queue;
struct ionic_qcq;
struct ionic_desc_info;

typedef void (*ionic_desc_cb)(struct ionic_queue *q,
			      struct ionic_desc_info *desc_info,
			      struct ionic_cq_info *cq_info, void *cb_arg);

#define IONIC_PAGE_SIZE				PAGE_SIZE
#define IONIC_PAGE_SPLIT_SZ			(PAGE_SIZE / 2)
#define IONIC_PAGE_GFP_MASK			(GFP_ATOMIC | __GFP_NOWARN |\
						 __GFP_COMP | __GFP_MEMALLOC)

struct ionic_buf_info {
	struct page *page;
	dma_addr_t dma_addr;
	u32 page_offset;
	u32 len;
};

#define IONIC_MAX_FRAGS			(1 + IONIC_TX_MAX_SG_ELEMS_V1)

struct ionic_desc_info {
	union {
		void *desc;
		struct ionic_txq_desc *txq_desc;
		struct ionic_rxq_desc *rxq_desc;
		struct ionic_admin_cmd *adminq_desc;
	};
	union {
		void *sg_desc;
		struct ionic_txq_sg_desc *txq_sg_desc;
		struct ionic_rxq_sg_desc *rxq_sgl_desc;
	};
	unsigned int bytes;
	unsigned int nbufs;
	struct ionic_buf_info bufs[IONIC_MAX_FRAGS];
	ionic_desc_cb cb;
	void *cb_arg;
};

#define IONIC_QUEUE_NAME_MAX_SZ		32

struct ionic_queue {
	struct device *dev;
	struct ionic_lif *lif;
	struct ionic_desc_info *info;
	u64 dbval;
	u16 head_idx;
	u16 tail_idx;
	unsigned int index;
	unsigned int num_descs;
	unsigned int max_sg_elems;
	u64 features;
	u64 drop;
	struct ionic_dev *idev;
	unsigned int type;
	unsigned int hw_index;
	unsigned int hw_type;
	union {
		void *base;
		struct ionic_txq_desc *txq;
		struct ionic_rxq_desc *rxq;
		struct ionic_admin_cmd *adminq;
	};
	union {
		void *sg_base;
		struct ionic_txq_sg_desc *txq_sgl;
		struct ionic_rxq_sg_desc *rxq_sgl;
	};
	dma_addr_t base_pa;
	dma_addr_t sg_base_pa;
	unsigned int desc_size;
	unsigned int sg_desc_size;
	unsigned int pid;
	char name[IONIC_QUEUE_NAME_MAX_SZ];
} ____cacheline_aligned_in_smp;

#define IONIC_INTR_INDEX_NOT_ASSIGNED	-1
#define IONIC_INTR_NAME_MAX_SZ		32

struct ionic_intr_info {
	char name[IONIC_INTR_NAME_MAX_SZ];
	unsigned int index;
	unsigned int vector;
	u64 rearm_count;
	unsigned int cpu;
	cpumask_t affinity_mask;
	u32 dim_coal_hw;
};

struct ionic_cq {
	struct ionic_lif *lif;
	struct ionic_cq_info *info;
	struct ionic_queue *bound_q;
	struct ionic_intr_info *bound_intr;
	u16 tail_idx;
	bool done_color;
	unsigned int num_descs;
	unsigned int desc_size;
	void *base;
	dma_addr_t base_pa;
} ____cacheline_aligned_in_smp;

struct ionic;

static inline void ionic_intr_init(struct ionic_dev *idev,
				   struct ionic_intr_info *intr,
				   unsigned long index)
{
	ionic_intr_clean(idev->intr_ctrl, index);
	intr->index = index;
}

static inline unsigned int ionic_q_space_avail(struct ionic_queue *q)
{
	unsigned int avail = q->tail_idx;

	if (q->head_idx >= avail)
		avail += q->num_descs - q->head_idx - 1;
	else
		avail -= q->head_idx + 1;

	return avail;
}

static inline bool ionic_q_has_space(struct ionic_queue *q, unsigned int want)
{
	return ionic_q_space_avail(q) >= want;
}

void ionic_init_devinfo(struct ionic *ionic);
int ionic_dev_setup(struct ionic *ionic);

void ionic_dev_cmd_go(struct ionic_dev *idev, union ionic_dev_cmd *cmd);
u8 ionic_dev_cmd_status(struct ionic_dev *idev);
bool ionic_dev_cmd_done(struct ionic_dev *idev);
void ionic_dev_cmd_comp(struct ionic_dev *idev, union ionic_dev_cmd_comp *comp);

void ionic_dev_cmd_identify(struct ionic_dev *idev, u8 ver);
void ionic_dev_cmd_init(struct ionic_dev *idev);
void ionic_dev_cmd_reset(struct ionic_dev *idev);

void ionic_dev_cmd_port_identify(struct ionic_dev *idev);
void ionic_dev_cmd_port_init(struct ionic_dev *idev);
void ionic_dev_cmd_port_reset(struct ionic_dev *idev);
void ionic_dev_cmd_port_state(struct ionic_dev *idev, u8 state);
void ionic_dev_cmd_port_speed(struct ionic_dev *idev, u32 speed);
void ionic_dev_cmd_port_autoneg(struct ionic_dev *idev, u8 an_enable);
void ionic_dev_cmd_port_fec(struct ionic_dev *idev, u8 fec_type);
void ionic_dev_cmd_port_pause(struct ionic_dev *idev, u8 pause_type);

int ionic_set_vf_config(struct ionic *ionic, int vf, u8 attr, u8 *data);
void ionic_dev_cmd_queue_identify(struct ionic_dev *idev,
				  u16 lif_type, u8 qtype, u8 qver);
void ionic_dev_cmd_lif_identify(struct ionic_dev *idev, u8 type, u8 ver);
void ionic_dev_cmd_lif_init(struct ionic_dev *idev, u16 lif_index,
			    dma_addr_t addr);
void ionic_dev_cmd_lif_reset(struct ionic_dev *idev, u16 lif_index);
void ionic_dev_cmd_adminq_init(struct ionic_dev *idev, struct ionic_qcq *qcq,
			       u16 lif_index, u16 intr_index);

int ionic_db_page_num(struct ionic_lif *lif, int pid);

int ionic_cq_init(struct ionic_lif *lif, struct ionic_cq *cq,
		  struct ionic_intr_info *intr,
		  unsigned int num_descs, size_t desc_size);
void ionic_cq_map(struct ionic_cq *cq, void *base, dma_addr_t base_pa);
void ionic_cq_bind(struct ionic_cq *cq, struct ionic_queue *q);
typedef bool (*ionic_cq_cb)(struct ionic_cq *cq, struct ionic_cq_info *cq_info);
typedef void (*ionic_cq_done_cb)(void *done_arg);
unsigned int ionic_cq_service(struct ionic_cq *cq, unsigned int work_to_do,
			      ionic_cq_cb cb, ionic_cq_done_cb done_cb,
			      void *done_arg);

int ionic_q_init(struct ionic_lif *lif, struct ionic_dev *idev,
		 struct ionic_queue *q, unsigned int index, const char *name,
		 unsigned int num_descs, size_t desc_size,
		 size_t sg_desc_size, unsigned int pid);
void ionic_q_map(struct ionic_queue *q, void *base, dma_addr_t base_pa);
void ionic_q_sg_map(struct ionic_queue *q, void *base, dma_addr_t base_pa);
void ionic_q_post(struct ionic_queue *q, bool ring_doorbell, ionic_desc_cb cb,
		  void *cb_arg);
void ionic_q_rewind(struct ionic_queue *q, struct ionic_desc_info *start);
void ionic_q_service(struct ionic_queue *q, struct ionic_cq_info *cq_info,
		     unsigned int stop_index);
int ionic_heartbeat_check(struct ionic *ionic);
bool ionic_is_fw_running(struct ionic_dev *idev);

#endif /* _IONIC_DEV_H_ */
