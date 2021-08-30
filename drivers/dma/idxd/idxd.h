/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#ifndef _IDXD_H_
#define _IDXD_H_

#include <linux/sbitmap.h>
#include <linux/dmaengine.h>
#include <linux/percpu-rwsem.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/idr.h>
#include <linux/pci.h>
#include <linux/perf_event.h>
#include "registers.h"

#define IDXD_DRIVER_VERSION	"1.00"

extern struct kmem_cache *idxd_desc_pool;

struct idxd_device;
struct idxd_wq;

#define IDXD_REG_TIMEOUT	50
#define IDXD_DRAIN_TIMEOUT	5000

enum idxd_type {
	IDXD_TYPE_UNKNOWN = -1,
	IDXD_TYPE_DSA = 0,
	IDXD_TYPE_IAX,
	IDXD_TYPE_MAX,
};

#define IDXD_NAME_SIZE		128
#define IDXD_PMU_EVENT_MAX	64

struct idxd_device_driver {
	struct device_driver drv;
};

struct idxd_irq_entry {
	struct idxd_device *idxd;
	int id;
	int vector;
	struct llist_head pending_llist;
	struct list_head work_list;
	/*
	 * Lock to protect access between irq thread process descriptor
	 * and irq thread processing error descriptor.
	 */
	spinlock_t list_lock;
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

struct idxd_pmu {
	struct idxd_device *idxd;

	struct perf_event *event_list[IDXD_PMU_EVENT_MAX];
	int n_events;

	DECLARE_BITMAP(used_mask, IDXD_PMU_EVENT_MAX);

	struct pmu pmu;
	char name[IDXD_NAME_SIZE];
	int cpu;

	int n_counters;
	int counter_width;
	int n_event_categories;

	bool per_counter_caps_supported;
	unsigned long supported_event_categories;

	unsigned long supported_filters;
	int n_filters;

	struct hlist_node cpuhp_node;
};

#define IDXD_MAX_PRIORITY	0xf

enum idxd_wq_state {
	IDXD_WQ_DISABLED = 0,
	IDXD_WQ_ENABLED,
};

enum idxd_wq_flag {
	WQ_FLAG_DEDICATED = 0,
	WQ_FLAG_BLOCK_ON_FAULT,
};

enum idxd_wq_type {
	IDXD_WQT_NONE = 0,
	IDXD_WQT_KERNEL,
	IDXD_WQT_USER,
};

struct idxd_cdev {
	struct idxd_wq *wq;
	struct cdev cdev;
	struct device dev;
	int minor;
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
	IDXD_COMPLETE_DEV_FAIL,
};

struct idxd_dma_chan {
	struct dma_chan chan;
	struct idxd_wq *wq;
};

struct idxd_wq {
	void __iomem *portal;
	struct percpu_ref wq_active;
	struct completion wq_dead;
	struct device conf_dev;
	struct idxd_cdev *idxd_cdev;
	struct wait_queue_head err_queue;
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
	union wqcfg *wqcfg;
	u32 vec_ptr;		/* interrupt steering */
	struct dsa_hw_desc **hw_descs;
	int num_descs;
	union {
		struct dsa_completion_record *compls;
		struct iax_completion_record *iax_compls;
	};
	void *compls_raw;
	dma_addr_t compls_addr;
	dma_addr_t compls_addr_raw;
	int compls_size;
	struct idxd_desc **descs;
	struct sbitmap_queue sbq;
	struct idxd_dma_chan *idxd_chan;
	char name[WQ_NAME_SIZE + 1];
	u64 max_xfer_bytes;
	u32 max_batch_size;
	bool ats_dis;
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
	u32 cmd_cap;
};

enum idxd_device_state {
	IDXD_DEV_HALTED = -1,
	IDXD_DEV_DISABLED = 0,
	IDXD_DEV_CONF_READY,
	IDXD_DEV_ENABLED,
};

enum idxd_device_flag {
	IDXD_FLAG_CONFIGURABLE = 0,
	IDXD_FLAG_CMD_RUNNING,
	IDXD_FLAG_PASID_ENABLED,
};

struct idxd_dma_dev {
	struct idxd_device *idxd;
	struct dma_device dma;
};

struct idxd_driver_data {
	const char *name_prefix;
	enum idxd_type type;
	struct device_type *dev_type;
	int compl_size;
	int align;
};

struct idxd_device {
	struct device conf_dev;
	struct idxd_driver_data *data;
	struct list_head list;
	struct idxd_hw hw;
	enum idxd_device_state state;
	unsigned long flags;
	int id;
	int major;
	u8 cmd_status;

	struct pci_dev *pdev;
	void __iomem *reg_base;

	spinlock_t dev_lock;	/* spinlock for device */
	spinlock_t cmd_lock;	/* spinlock for device commands */
	struct completion *cmd_done;
	struct idxd_group **groups;
	struct idxd_wq **wqs;
	struct idxd_engine **engines;

	struct iommu_sva *sva;
	unsigned int pasid;

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
	unsigned int wqcfg_size;

	union sw_err_reg sw_err;
	wait_queue_head_t cmd_waitq;
	int num_wq_irqs;
	struct idxd_irq_entry *irq_entries;

	struct idxd_dma_dev *idxd_dma;
	struct workqueue_struct *wq;
	struct work_struct work;

	int *int_handles;

	struct idxd_pmu *idxd_pmu;
};

/* IDXD software descriptor */
struct idxd_desc {
	union {
		struct dsa_hw_desc *hw;
		struct iax_hw_desc *iax_hw;
	};
	dma_addr_t desc_dma;
	union {
		struct dsa_completion_record *completion;
		struct iax_completion_record *iax_completion;
	};
	dma_addr_t compl_dma;
	struct dma_async_tx_descriptor txd;
	struct llist_node llnode;
	struct list_head list;
	int id;
	int cpu;
	unsigned int vector;
	struct idxd_wq *wq;
};

/*
 * This is software defined error for the completion status. We overload the error code
 * that will never appear in completion status and only SWERR register.
 */
enum idxd_completion_status {
	IDXD_COMP_DESC_ABORT = 0xff,
};

#define confdev_to_idxd(dev) container_of(dev, struct idxd_device, conf_dev)
#define confdev_to_wq(dev) container_of(dev, struct idxd_wq, conf_dev)

extern struct bus_type dsa_bus_type;
extern struct bus_type iax_bus_type;

extern bool support_enqcmd;
extern struct ida idxd_ida;
extern struct device_type dsa_device_type;
extern struct device_type iax_device_type;
extern struct device_type idxd_wq_device_type;
extern struct device_type idxd_engine_device_type;
extern struct device_type idxd_group_device_type;

static inline bool is_dsa_dev(struct device *dev)
{
	return dev->type == &dsa_device_type;
}

static inline bool is_iax_dev(struct device *dev)
{
	return dev->type == &iax_device_type;
}

static inline bool is_idxd_dev(struct device *dev)
{
	return is_dsa_dev(dev) || is_iax_dev(dev);
}

static inline bool is_idxd_wq_dev(struct device *dev)
{
	return dev->type == &idxd_wq_device_type;
}

static inline bool is_idxd_wq_dmaengine(struct idxd_wq *wq)
{
	if (wq->type == IDXD_WQT_KERNEL && strcmp(wq->name, "dmaengine") == 0)
		return true;
	return false;
}

static inline bool is_idxd_wq_cdev(struct idxd_wq *wq)
{
	return wq->type == IDXD_WQT_USER;
}

static inline bool wq_dedicated(struct idxd_wq *wq)
{
	return test_bit(WQ_FLAG_DEDICATED, &wq->flags);
}

static inline bool wq_shared(struct idxd_wq *wq)
{
	return !test_bit(WQ_FLAG_DEDICATED, &wq->flags);
}

static inline bool device_pasid_enabled(struct idxd_device *idxd)
{
	return test_bit(IDXD_FLAG_PASID_ENABLED, &idxd->flags);
}

static inline bool device_swq_supported(struct idxd_device *idxd)
{
	return (support_enqcmd && device_pasid_enabled(idxd));
}

enum idxd_portal_prot {
	IDXD_PORTAL_UNLIMITED = 0,
	IDXD_PORTAL_LIMITED,
};

enum idxd_interrupt_type {
	IDXD_IRQ_MSIX = 0,
	IDXD_IRQ_IMS,
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

int idxd_register_bus_type(void);
void idxd_unregister_bus_type(void);
int idxd_register_devices(struct idxd_device *idxd);
void idxd_unregister_devices(struct idxd_device *idxd);
int idxd_register_driver(void);
void idxd_unregister_driver(void);
void idxd_wqs_quiesce(struct idxd_device *idxd);

/* device interrupt control */
void idxd_msix_perm_setup(struct idxd_device *idxd);
void idxd_msix_perm_clear(struct idxd_device *idxd);
irqreturn_t idxd_misc_thread(int vec, void *data);
irqreturn_t idxd_wq_thread(int irq, void *data);
void idxd_mask_error_interrupts(struct idxd_device *idxd);
void idxd_unmask_error_interrupts(struct idxd_device *idxd);
void idxd_mask_msix_vectors(struct idxd_device *idxd);
void idxd_mask_msix_vector(struct idxd_device *idxd, int vec_id);
void idxd_unmask_msix_vector(struct idxd_device *idxd, int vec_id);

/* device control */
int idxd_device_init_reset(struct idxd_device *idxd);
int idxd_device_enable(struct idxd_device *idxd);
int idxd_device_disable(struct idxd_device *idxd);
void idxd_device_reset(struct idxd_device *idxd);
void idxd_device_cleanup(struct idxd_device *idxd);
int idxd_device_config(struct idxd_device *idxd);
void idxd_device_wqs_clear_state(struct idxd_device *idxd);
void idxd_device_drain_pasid(struct idxd_device *idxd, int pasid);
int idxd_device_load_config(struct idxd_device *idxd);
int idxd_device_request_int_handle(struct idxd_device *idxd, int idx, int *handle,
				   enum idxd_interrupt_type irq_type);
int idxd_device_release_int_handle(struct idxd_device *idxd, int handle,
				   enum idxd_interrupt_type irq_type);

/* work queue control */
void idxd_wqs_unmap_portal(struct idxd_device *idxd);
int idxd_wq_alloc_resources(struct idxd_wq *wq);
void idxd_wq_free_resources(struct idxd_wq *wq);
int idxd_wq_enable(struct idxd_wq *wq);
int idxd_wq_disable(struct idxd_wq *wq);
void idxd_wq_drain(struct idxd_wq *wq);
void idxd_wq_reset(struct idxd_wq *wq);
int idxd_wq_map_portal(struct idxd_wq *wq);
void idxd_wq_unmap_portal(struct idxd_wq *wq);
void idxd_wq_disable_cleanup(struct idxd_wq *wq);
int idxd_wq_set_pasid(struct idxd_wq *wq, int pasid);
int idxd_wq_disable_pasid(struct idxd_wq *wq);
void idxd_wq_quiesce(struct idxd_wq *wq);
int idxd_wq_init_percpu_ref(struct idxd_wq *wq);

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

/* cdev */
int idxd_cdev_register(void);
void idxd_cdev_remove(void);
int idxd_cdev_get_major(struct idxd_device *idxd);
int idxd_wq_add_cdev(struct idxd_wq *wq);
void idxd_wq_del_cdev(struct idxd_wq *wq);

/* perfmon */
#if IS_ENABLED(CONFIG_INTEL_IDXD_PERFMON)
int perfmon_pmu_init(struct idxd_device *idxd);
void perfmon_pmu_remove(struct idxd_device *idxd);
void perfmon_counter_overflow(struct idxd_device *idxd);
void perfmon_init(void);
void perfmon_exit(void);
#else
static inline int perfmon_pmu_init(struct idxd_device *idxd) { return 0; }
static inline void perfmon_pmu_remove(struct idxd_device *idxd) {}
static inline void perfmon_counter_overflow(struct idxd_device *idxd) {}
static inline void perfmon_init(void) {}
static inline void perfmon_exit(void) {}
#endif

static inline void complete_desc(struct idxd_desc *desc, enum idxd_complete_type reason)
{
	idxd_dma_complete_txd(desc, reason);
	idxd_free_desc(desc->wq, desc);
}

#endif
