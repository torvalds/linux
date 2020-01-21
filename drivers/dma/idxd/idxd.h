/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#ifndef _IDXD_H_
#define _IDXD_H_

#include <linux/sbitmap.h>
#include <linux/dmaengine.h>
#include <linux/percpu-rwsem.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include "registers.h"

#define IDXD_DRIVER_VERSION	"1.00"

extern struct kmem_cache *idxd_desc_pool;

#define IDXD_REG_TIMEOUT	50
#define IDXD_DRAIN_TIMEOUT	5000

enum idxd_type {
	IDXD_TYPE_UNKNOWN = -1,
	IDXD_TYPE_DSA = 0,
	IDXD_TYPE_MAX
};

#define IDXD_NAME_SIZE		128

struct idxd_device_driver {
	struct device_driver drv;
};

struct idxd_irq_entry {
	struct idxd_device *idxd;
	int id;
	struct llist_head pending_llist;
	struct list_head work_list;
};

struct idxd_group {
	struct device conf_dev;
	struct idxd_device *idxd;
	struct grpcfg grpcfg;
	int id;
	int num_engines;
	int num_wqs;
	bool use_token_limit;
	u8 tokens_allowed;
	u8 tokens_reserved;
	int tc_a;
	int tc_b;
};

#define IDXD_MAX_PRIORITY	0xf

enum idxd_wq_state {
	IDXD_WQ_DISABLED = 0,
	IDXD_WQ_ENABLED,
};

enum idxd_wq_flag {
	WQ_FLAG_DEDICATED = 0,
};

enum idxd_wq_type {
	IDXD_WQT_NONE = 0,
	IDXD_WQT_KERNEL,
	IDXD_WQT_USER,
};

struct idxd_cdev {
	struct cdev cdev;
	struct device *dev;
	int minor;
	struct wait_queue_head err_queue;
};

#define IDXD_ALLOCATED_BATCH_SIZE	128U
#define WQ_NAME_SIZE   1024
#define WQ_TYPE_SIZE   10

enum idxd_op_type {
	IDXD_OP_BLOCK = 0,
	IDXD_OP_NONBLOCK = 1,
};

enum idxd_complete_type {
	IDXD_COMPLETE_NORMAL = 0,
	IDXD_COMPLETE_ABORT,
};

struct idxd_wq {
	void __iomem *dportal;
	struct device conf_dev;
	struct idxd_cdev idxd_cdev;
	struct idxd_device *idxd;
	int id;
	enum idxd_wq_type type;
	struct idxd_group *group;
	int client_count;
	struct mutex wq_lock;	/* mutex for workqueue */
	u32 size;
	u32 threshold;
	u32 priority;
	enum idxd_wq_state state;
	unsigned long flags;
	union wqcfg wqcfg;
	atomic_t dq_count;	/* dedicated queue flow control */
	u32 vec_ptr;		/* interrupt steering */
	struct dsa_hw_desc **hw_descs;
	int num_descs;
	struct dsa_completion_record *compls;
	dma_addr_t compls_addr;
	int compls_size;
	struct idxd_desc **descs;
	struct sbitmap sbmap;
	struct dma_chan dma_chan;
	struct percpu_rw_semaphore submit_lock;
	wait_queue_head_t submit_waitq;
	char name[WQ_NAME_SIZE + 1];
};

struct idxd_engine {
	struct device conf_dev;
	int id;
	struct idxd_group *group;
	struct idxd_device *idxd;
};

/* shadow registers */
struct idxd_hw {
	u32 version;
	union gen_cap_reg gen_cap;
	union wq_cap_reg wq_cap;
	union group_cap_reg group_cap;
	union engine_cap_reg engine_cap;
	struct opcap opcap;
};

enum idxd_device_state {
	IDXD_DEV_HALTED = -1,
	IDXD_DEV_DISABLED = 0,
	IDXD_DEV_CONF_READY,
	IDXD_DEV_ENABLED,
};

enum idxd_device_flag {
	IDXD_FLAG_CONFIGURABLE = 0,
};

struct idxd_device {
	enum idxd_type type;
	struct device conf_dev;
	struct list_head list;
	struct idxd_hw hw;
	enum idxd_device_state state;
	unsigned long flags;
	int id;
	int major;

	struct pci_dev *pdev;
	void __iomem *reg_base;

	spinlock_t dev_lock;	/* spinlock for device */
	struct idxd_group *groups;
	struct idxd_wq *wqs;
	struct idxd_engine *engines;

	int num_groups;

	u32 msix_perm_offset;
	u32 wqcfg_offset;
	u32 grpcfg_offset;
	u32 perfmon_offset;

	u64 max_xfer_bytes;
	u32 max_batch_size;
	int max_groups;
	int max_engines;
	int max_tokens;
	int max_wqs;
	int max_wq_size;
	int token_limit;
	int nr_tokens;		/* non-reserved tokens */

	union sw_err_reg sw_err;

	struct msix_entry *msix_entries;
	int num_wq_irqs;
	struct idxd_irq_entry *irq_entries;

	struct dma_device dma_dev;
};

/* IDXD software descriptor */
struct idxd_desc {
	struct dsa_hw_desc *hw;
	dma_addr_t desc_dma;
	struct dsa_completion_record *completion;
	dma_addr_t compl_dma;
	struct dma_async_tx_descriptor txd;
	struct llist_node llnode;
	struct list_head list;
	int id;
	struct idxd_wq *wq;
};

#define confdev_to_idxd(dev) container_of(dev, struct idxd_device, conf_dev)
#define confdev_to_wq(dev) container_of(dev, struct idxd_wq, conf_dev)

extern struct bus_type dsa_bus_type;

static inline bool wq_dedicated(struct idxd_wq *wq)
{
	return test_bit(WQ_FLAG_DEDICATED, &wq->flags);
}

enum idxd_portal_prot {
	IDXD_PORTAL_UNLIMITED = 0,
	IDXD_PORTAL_LIMITED,
};

static inline int idxd_get_wq_portal_offset(enum idxd_portal_prot prot)
{
	return prot * 0x1000;
}

static inline int idxd_get_wq_portal_full_offset(int wq_id,
						 enum idxd_portal_prot prot)
{
	return ((wq_id * 4) << PAGE_SHIFT) + idxd_get_wq_portal_offset(prot);
}

static inline void idxd_set_type(struct idxd_device *idxd)
{
	struct pci_dev *pdev = idxd->pdev;

	if (pdev->device == PCI_DEVICE_ID_INTEL_DSA_SPR0)
		idxd->type = IDXD_TYPE_DSA;
	else
		idxd->type = IDXD_TYPE_UNKNOWN;
}

static inline void idxd_wq_get(struct idxd_wq *wq)
{
	wq->client_count++;
}

static inline void idxd_wq_put(struct idxd_wq *wq)
{
	wq->client_count--;
}

static inline int idxd_wq_refcount(struct idxd_wq *wq)
{
	return wq->client_count;
};

const char *idxd_get_dev_name(struct idxd_device *idxd);
int idxd_register_bus_type(void);
void idxd_unregister_bus_type(void);
int idxd_setup_sysfs(struct idxd_device *idxd);
void idxd_cleanup_sysfs(struct idxd_device *idxd);
int idxd_register_driver(void);
void idxd_unregister_driver(void);
struct bus_type *idxd_get_bus_type(struct idxd_device *idxd);

/* device interrupt control */
irqreturn_t idxd_irq_handler(int vec, void *data);
irqreturn_t idxd_misc_thread(int vec, void *data);
irqreturn_t idxd_wq_thread(int irq, void *data);
void idxd_mask_error_interrupts(struct idxd_device *idxd);
void idxd_unmask_error_interrupts(struct idxd_device *idxd);
void idxd_mask_msix_vectors(struct idxd_device *idxd);
int idxd_mask_msix_vector(struct idxd_device *idxd, int vec_id);
int idxd_unmask_msix_vector(struct idxd_device *idxd, int vec_id);

/* device control */
int idxd_device_enable(struct idxd_device *idxd);
int idxd_device_disable(struct idxd_device *idxd);
int idxd_device_reset(struct idxd_device *idxd);
int __idxd_device_reset(struct idxd_device *idxd);
void idxd_device_cleanup(struct idxd_device *idxd);
int idxd_device_config(struct idxd_device *idxd);
void idxd_device_wqs_clear_state(struct idxd_device *idxd);

/* work queue control */
int idxd_wq_alloc_resources(struct idxd_wq *wq);
void idxd_wq_free_resources(struct idxd_wq *wq);
int idxd_wq_enable(struct idxd_wq *wq);
int idxd_wq_disable(struct idxd_wq *wq);
int idxd_wq_map_portal(struct idxd_wq *wq);
void idxd_wq_unmap_portal(struct idxd_wq *wq);

/* submission */
int idxd_submit_desc(struct idxd_wq *wq, struct idxd_desc *desc);
struct idxd_desc *idxd_alloc_desc(struct idxd_wq *wq, enum idxd_op_type optype);
void idxd_free_desc(struct idxd_wq *wq, struct idxd_desc *desc);

/* dmaengine */
int idxd_register_dma_device(struct idxd_device *idxd);
void idxd_unregister_dma_device(struct idxd_device *idxd);
int idxd_register_dma_channel(struct idxd_wq *wq);
void idxd_unregister_dma_channel(struct idxd_wq *wq);
void idxd_parse_completion_status(u8 status, enum dmaengine_tx_result *res);
void idxd_dma_complete_txd(struct idxd_desc *desc,
			   enum idxd_complete_type comp_type);
dma_cookie_t idxd_dma_tx_submit(struct dma_async_tx_descriptor *tx);

/* cdev */
int idxd_cdev_register(void);
void idxd_cdev_remove(void);
int idxd_cdev_get_major(struct idxd_device *idxd);
int idxd_wq_add_cdev(struct idxd_wq *wq);
void idxd_wq_del_cdev(struct idxd_wq *wq);

#endif
