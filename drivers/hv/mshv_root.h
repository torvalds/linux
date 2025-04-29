/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Microsoft Corporation.
 */

#ifndef _MSHV_ROOT_H_
#define _MSHV_ROOT_H_

#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/srcu.h>
#include <linux/wait.h>
#include <linux/hashtable.h>
#include <linux/dev_printk.h>
#include <linux/build_bug.h>
#include <uapi/linux/mshv.h>

/*
 * Hypervisor must be between these version numbers (inclusive)
 * to guarantee compatibility
 */
#define MSHV_HV_MIN_VERSION		(27744)
#define MSHV_HV_MAX_VERSION		(27751)

static_assert(HV_HYP_PAGE_SIZE == MSHV_HV_PAGE_SIZE);

#define MSHV_MAX_VPS			256

#define MSHV_PARTITIONS_HASH_BITS	9

#define MSHV_PIN_PAGES_BATCH_SIZE	(0x10000000ULL / HV_HYP_PAGE_SIZE)

struct mshv_vp {
	u32 vp_index;
	struct mshv_partition *vp_partition;
	struct mutex vp_mutex;
	struct hv_vp_register_page *vp_register_page;
	struct hv_message *vp_intercept_msg_page;
	void *vp_ghcb_page;
	struct hv_stats_page *vp_stats_pages[2];
	struct {
		atomic64_t vp_signaled_count;
		struct {
			u64 intercept_suspend: 1;
			u64 root_sched_blocked: 1; /* root scheduler only */
			u64 root_sched_dispatched: 1; /* root scheduler only */
			u64 reserved: 61;
		} flags;
		unsigned int kicked_by_hv;
		wait_queue_head_t vp_suspend_queue;
	} run;
};

#define vp_fmt(fmt) "p%lluvp%u: " fmt
#define vp_devprintk(level, v, fmt, ...) \
do { \
	const struct mshv_vp *__vp = (v); \
	const struct mshv_partition *__pt = __vp->vp_partition; \
	dev_##level(__pt->pt_module_dev, vp_fmt(fmt), __pt->pt_id, \
		    __vp->vp_index, ##__VA_ARGS__); \
} while (0)
#define vp_emerg(v, fmt, ...)	vp_devprintk(emerg, v, fmt, ##__VA_ARGS__)
#define vp_crit(v, fmt, ...)	vp_devprintk(crit, v, fmt, ##__VA_ARGS__)
#define vp_alert(v, fmt, ...)	vp_devprintk(alert, v, fmt, ##__VA_ARGS__)
#define vp_err(v, fmt, ...)	vp_devprintk(err, v, fmt, ##__VA_ARGS__)
#define vp_warn(v, fmt, ...)	vp_devprintk(warn, v, fmt, ##__VA_ARGS__)
#define vp_notice(v, fmt, ...)	vp_devprintk(notice, v, fmt, ##__VA_ARGS__)
#define vp_info(v, fmt, ...)	vp_devprintk(info, v, fmt, ##__VA_ARGS__)
#define vp_dbg(v, fmt, ...)	vp_devprintk(dbg, v, fmt, ##__VA_ARGS__)

struct mshv_mem_region {
	struct hlist_node hnode;
	u64 nr_pages;
	u64 start_gfn;
	u64 start_uaddr;
	u32 hv_map_flags;
	struct {
		u64 large_pages:  1; /* 2MiB */
		u64 range_pinned: 1;
		u64 reserved:	 62;
	} flags;
	struct mshv_partition *partition;
	struct page *pages[];
};

struct mshv_irq_ack_notifier {
	struct hlist_node link;
	unsigned int irq_ack_gsi;
	void (*irq_acked)(struct mshv_irq_ack_notifier *mian);
};

struct mshv_partition {
	struct device *pt_module_dev;

	struct hlist_node pt_hnode;
	u64 pt_id;
	refcount_t pt_ref_count;
	struct mutex pt_mutex;
	struct hlist_head pt_mem_regions; // not ordered

	u32 pt_vp_count;
	struct mshv_vp *pt_vp_array[MSHV_MAX_VPS];

	struct mutex pt_irq_lock;
	struct srcu_struct pt_irq_srcu;
	struct hlist_head irq_ack_notifier_list;

	struct hlist_head pt_devices;

	/*
	 * MSHV does not support more than one async hypercall in flight
	 * for a single partition. Thus, it is okay to define per partition
	 * async hypercall status.
	 */
	struct completion async_hypercall;
	u64 async_hypercall_status;

	spinlock_t	  pt_irqfds_lock;
	struct hlist_head pt_irqfds_list;
	struct mutex	  irqfds_resampler_lock;
	struct hlist_head irqfds_resampler_list;

	struct hlist_head ioeventfds_list;

	struct mshv_girq_routing_table __rcu *pt_girq_tbl;
	u64 isolation_type;
	bool import_completed;
	bool pt_initialized;
};

#define pt_fmt(fmt) "p%llu: " fmt
#define pt_devprintk(level, p, fmt, ...) \
do { \
	const struct mshv_partition *__pt = (p); \
	dev_##level(__pt->pt_module_dev, pt_fmt(fmt), __pt->pt_id, \
		    ##__VA_ARGS__); \
} while (0)
#define pt_emerg(p, fmt, ...)	pt_devprintk(emerg, p, fmt, ##__VA_ARGS__)
#define pt_crit(p, fmt, ...)	pt_devprintk(crit, p, fmt, ##__VA_ARGS__)
#define pt_alert(p, fmt, ...)	pt_devprintk(alert, p, fmt, ##__VA_ARGS__)
#define pt_err(p, fmt, ...)	pt_devprintk(err, p, fmt, ##__VA_ARGS__)
#define pt_warn(p, fmt, ...)	pt_devprintk(warn, p, fmt, ##__VA_ARGS__)
#define pt_notice(p, fmt, ...)	pt_devprintk(notice, p, fmt, ##__VA_ARGS__)
#define pt_info(p, fmt, ...)	pt_devprintk(info, p, fmt, ##__VA_ARGS__)
#define pt_dbg(p, fmt, ...)	pt_devprintk(dbg, p, fmt, ##__VA_ARGS__)

struct mshv_lapic_irq {
	u32 lapic_vector;
	u64 lapic_apic_id;
	union hv_interrupt_control lapic_control;
};

#define MSHV_MAX_GUEST_IRQS		4096

/* representation of one guest irq entry, either msi or legacy */
struct mshv_guest_irq_ent {
	u32 girq_entry_valid;	/* vfio looks at this */
	u32 guest_irq_num;	/* a unique number for each irq */
	u32 girq_addr_lo;	/* guest irq msi address info */
	u32 girq_addr_hi;
	u32 girq_irq_data;	/* idt vector in some cases */
};

struct mshv_girq_routing_table {
	u32 num_rt_entries;
	struct mshv_guest_irq_ent mshv_girq_info_tbl[];
};

struct hv_synic_pages {
	struct hv_message_page *synic_message_page;
	struct hv_synic_event_flags_page *synic_event_flags_page;
	struct hv_synic_event_ring_page *synic_event_ring_page;
};

struct mshv_root {
	struct hv_synic_pages __percpu *synic_pages;
	spinlock_t pt_ht_lock;
	DECLARE_HASHTABLE(pt_htable, MSHV_PARTITIONS_HASH_BITS);
};

/*
 * Callback for doorbell events.
 * NOTE: This is called in interrupt context. Callback
 * should defer slow and sleeping logic to later.
 */
typedef void (*doorbell_cb_t) (int doorbell_id, void *);

/*
 * port table information
 */
struct port_table_info {
	struct rcu_head portbl_rcu;
	enum hv_port_type hv_port_type;
	union {
		struct {
			u64 reserved[2];
		} hv_port_message;
		struct {
			u64 reserved[2];
		} hv_port_event;
		struct {
			u64 reserved[2];
		} hv_port_monitor;
		struct {
			doorbell_cb_t doorbell_cb;
			void *data;
		} hv_port_doorbell;
	};
};

int mshv_update_routing_table(struct mshv_partition *partition,
			      const struct mshv_user_irq_entry *entries,
			      unsigned int numents);
void mshv_free_routing_table(struct mshv_partition *partition);

struct mshv_guest_irq_ent mshv_ret_girq_entry(struct mshv_partition *partition,
					      u32 irq_num);

void mshv_copy_girq_info(struct mshv_guest_irq_ent *src_irq,
			 struct mshv_lapic_irq *dest_irq);

void mshv_irqfd_routing_update(struct mshv_partition *partition);

void mshv_port_table_fini(void);
int mshv_portid_alloc(struct port_table_info *info);
int mshv_portid_lookup(int port_id, struct port_table_info *info);
void mshv_portid_free(int port_id);

int mshv_register_doorbell(u64 partition_id, doorbell_cb_t doorbell_cb,
			   void *data, u64 gpa, u64 val, u64 flags);
void mshv_unregister_doorbell(u64 partition_id, int doorbell_portid);

void mshv_isr(void);
int mshv_synic_init(unsigned int cpu);
int mshv_synic_cleanup(unsigned int cpu);

static inline bool mshv_partition_encrypted(struct mshv_partition *partition)
{
	return partition->isolation_type == HV_PARTITION_ISOLATION_TYPE_SNP;
}

struct mshv_partition *mshv_partition_get(struct mshv_partition *partition);
void mshv_partition_put(struct mshv_partition *partition);
struct mshv_partition *mshv_partition_find(u64 partition_id) __must_hold(RCU);

/* hypercalls */

int hv_call_withdraw_memory(u64 count, int node, u64 partition_id);
int hv_call_create_partition(u64 flags,
			     struct hv_partition_creation_properties creation_properties,
			     union hv_partition_isolation_properties isolation_properties,
			     u64 *partition_id);
int hv_call_initialize_partition(u64 partition_id);
int hv_call_finalize_partition(u64 partition_id);
int hv_call_delete_partition(u64 partition_id);
int hv_call_map_mmio_pages(u64 partition_id, u64 gfn, u64 mmio_spa, u64 numpgs);
int hv_call_map_gpa_pages(u64 partition_id, u64 gpa_target, u64 page_count,
			  u32 flags, struct page **pages);
int hv_call_unmap_gpa_pages(u64 partition_id, u64 gpa_target, u64 page_count,
			    u32 flags);
int hv_call_delete_vp(u64 partition_id, u32 vp_index);
int hv_call_assert_virtual_interrupt(u64 partition_id, u32 vector,
				     u64 dest_addr,
				     union hv_interrupt_control control);
int hv_call_clear_virtual_interrupt(u64 partition_id);
int hv_call_get_gpa_access_states(u64 partition_id, u32 count, u64 gpa_base_pfn,
				  union hv_gpa_page_access_state_flags state_flags,
				  int *written_total,
				  union hv_gpa_page_access_state *states);
int hv_call_get_vp_state(u32 vp_index, u64 partition_id,
			 struct hv_vp_state_data state_data,
			 /* Choose between pages and ret_output */
			 u64 page_count, struct page **pages,
			 union hv_output_get_vp_state *ret_output);
int hv_call_set_vp_state(u32 vp_index, u64 partition_id,
			 /* Choose between pages and bytes */
			 struct hv_vp_state_data state_data, u64 page_count,
			 struct page **pages, u32 num_bytes, u8 *bytes);
int hv_call_map_vp_state_page(u64 partition_id, u32 vp_index, u32 type,
			      union hv_input_vtl input_vtl,
			      struct page **state_page);
int hv_call_unmap_vp_state_page(u64 partition_id, u32 vp_index, u32 type,
				union hv_input_vtl input_vtl);
int hv_call_create_port(u64 port_partition_id, union hv_port_id port_id,
			u64 connection_partition_id, struct hv_port_info *port_info,
			u8 port_vtl, u8 min_connection_vtl, int node);
int hv_call_delete_port(u64 port_partition_id, union hv_port_id port_id);
int hv_call_connect_port(u64 port_partition_id, union hv_port_id port_id,
			 u64 connection_partition_id,
			 union hv_connection_id connection_id,
			 struct hv_connection_info *connection_info,
			 u8 connection_vtl, int node);
int hv_call_disconnect_port(u64 connection_partition_id,
			    union hv_connection_id connection_id);
int hv_call_notify_port_ring_empty(u32 sint_index);
int hv_call_map_stat_page(enum hv_stats_object_type type,
			  const union hv_stats_object_identity *identity,
			  void **addr);
int hv_call_unmap_stat_page(enum hv_stats_object_type type,
			    const union hv_stats_object_identity *identity);
int hv_call_modify_spa_host_access(u64 partition_id, struct page **pages,
				   u64 page_struct_count, u32 host_access,
				   u32 flags, u8 acquire);

extern struct mshv_root mshv_root;
extern enum hv_scheduler_type hv_scheduler_type;
extern u8 * __percpu *hv_synic_eventring_tail;

#endif /* _MSHV_ROOT_H_ */
