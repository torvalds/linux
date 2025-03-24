/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_RESCTRL_INTERNAL_H
#define _ASM_X86_RESCTRL_INTERNAL_H

#include <linux/resctrl.h>
#include <linux/sched.h>
#include <linux/kernfs.h>
#include <linux/fs_context.h>
#include <linux/jump_label.h>
#include <linux/tick.h>

#include <asm/resctrl.h>

#define L3_QOS_CDP_ENABLE		0x01ULL

#define L2_QOS_CDP_ENABLE		0x01ULL

#define CQM_LIMBOCHECK_INTERVAL	1000

#define MBM_CNTR_WIDTH_BASE		24
#define MBM_OVERFLOW_INTERVAL		1000
#define MAX_MBA_BW			100u
#define MBA_IS_LINEAR			0x4
#define MBM_CNTR_WIDTH_OFFSET_AMD	20

#define RMID_VAL_ERROR			BIT_ULL(63)
#define RMID_VAL_UNAVAIL		BIT_ULL(62)
/*
 * With the above fields in use 62 bits remain in MSR_IA32_QM_CTR for
 * data to be returned. The counter width is discovered from the hardware
 * as an offset from MBM_CNTR_WIDTH_BASE.
 */
#define MBM_CNTR_WIDTH_OFFSET_MAX (62 - MBM_CNTR_WIDTH_BASE)

/* Reads to Local DRAM Memory */
#define READS_TO_LOCAL_MEM		BIT(0)

/* Reads to Remote DRAM Memory */
#define READS_TO_REMOTE_MEM		BIT(1)

/* Non-Temporal Writes to Local Memory */
#define NON_TEMP_WRITE_TO_LOCAL_MEM	BIT(2)

/* Non-Temporal Writes to Remote Memory */
#define NON_TEMP_WRITE_TO_REMOTE_MEM	BIT(3)

/* Reads to Local Memory the system identifies as "Slow Memory" */
#define READS_TO_LOCAL_S_MEM		BIT(4)

/* Reads to Remote Memory the system identifies as "Slow Memory" */
#define READS_TO_REMOTE_S_MEM		BIT(5)

/* Dirty Victims to All Types of Memory */
#define DIRTY_VICTIMS_TO_ALL_MEM	BIT(6)

/* Max event bits supported */
#define MAX_EVT_CONFIG_BITS		GENMASK(6, 0)

/**
 * cpumask_any_housekeeping() - Choose any CPU in @mask, preferring those that
 *			        aren't marked nohz_full
 * @mask:	The mask to pick a CPU from.
 * @exclude_cpu:The CPU to avoid picking.
 *
 * Returns a CPU from @mask, but not @exclude_cpu. If there are housekeeping
 * CPUs that don't use nohz_full, these are preferred. Pass
 * RESCTRL_PICK_ANY_CPU to avoid excluding any CPUs.
 *
 * When a CPU is excluded, returns >= nr_cpu_ids if no CPUs are available.
 */
static inline unsigned int
cpumask_any_housekeeping(const struct cpumask *mask, int exclude_cpu)
{
	unsigned int cpu, hk_cpu;

	if (exclude_cpu == RESCTRL_PICK_ANY_CPU)
		cpu = cpumask_any(mask);
	else
		cpu = cpumask_any_but(mask, exclude_cpu);

	/* Only continue if tick_nohz_full_mask has been initialized. */
	if (!tick_nohz_full_enabled())
		return cpu;

	/* If the CPU picked isn't marked nohz_full nothing more needs doing. */
	if (cpu < nr_cpu_ids && !tick_nohz_full_cpu(cpu))
		return cpu;

	/* Try to find a CPU that isn't nohz_full to use in preference */
	hk_cpu = cpumask_nth_andnot(0, mask, tick_nohz_full_mask);
	if (hk_cpu == exclude_cpu)
		hk_cpu = cpumask_nth_andnot(1, mask, tick_nohz_full_mask);

	if (hk_cpu < nr_cpu_ids)
		cpu = hk_cpu;

	return cpu;
}

struct rdt_fs_context {
	struct kernfs_fs_context	kfc;
	bool				enable_cdpl2;
	bool				enable_cdpl3;
	bool				enable_mba_mbps;
	bool				enable_debug;
};

static inline struct rdt_fs_context *rdt_fc2context(struct fs_context *fc)
{
	struct kernfs_fs_context *kfc = fc->fs_private;

	return container_of(kfc, struct rdt_fs_context, kfc);
}

/**
 * struct mon_evt - Entry in the event list of a resource
 * @evtid:		event id
 * @name:		name of the event
 * @configurable:	true if the event is configurable
 * @list:		entry in &rdt_resource->evt_list
 */
struct mon_evt {
	enum resctrl_event_id	evtid;
	char			*name;
	bool			configurable;
	struct list_head	list;
};

/**
 * union mon_data_bits - Monitoring details for each event file.
 * @priv:              Used to store monitoring event data in @u
 *                     as kernfs private data.
 * @u.rid:             Resource id associated with the event file.
 * @u.evtid:           Event id associated with the event file.
 * @u.sum:             Set when event must be summed across multiple
 *                     domains.
 * @u.domid:           When @u.sum is zero this is the domain to which
 *                     the event file belongs. When @sum is one this
 *                     is the id of the L3 cache that all domains to be
 *                     summed share.
 * @u:                 Name of the bit fields struct.
 */
union mon_data_bits {
	void *priv;
	struct {
		unsigned int rid		: 10;
		enum resctrl_event_id evtid	: 7;
		unsigned int sum		: 1;
		unsigned int domid		: 14;
	} u;
};

/**
 * struct rmid_read - Data passed across smp_call*() to read event count.
 * @rgrp:  Resource group for which the counter is being read. If it is a parent
 *	   resource group then its event count is summed with the count from all
 *	   its child resource groups.
 * @r:	   Resource describing the properties of the event being read.
 * @d:	   Domain that the counter should be read from. If NULL then sum all
 *	   domains in @r sharing L3 @ci.id
 * @evtid: Which monitor event to read.
 * @first: Initialize MBM counter when true.
 * @ci:    Cacheinfo for L3. Only set when @d is NULL. Used when summing domains.
 * @err:   Error encountered when reading counter.
 * @val:   Returned value of event counter. If @rgrp is a parent resource group,
 *	   @val includes the sum of event counts from its child resource groups.
 *	   If @d is NULL, @val includes the sum of all domains in @r sharing @ci.id,
 *	   (summed across child resource groups if @rgrp is a parent resource group).
 * @arch_mon_ctx: Hardware monitor allocated for this read request (MPAM only).
 */
struct rmid_read {
	struct rdtgroup		*rgrp;
	struct rdt_resource	*r;
	struct rdt_mon_domain	*d;
	enum resctrl_event_id	evtid;
	bool			first;
	struct cacheinfo	*ci;
	int			err;
	u64			val;
	void			*arch_mon_ctx;
};

extern unsigned int rdt_mon_features;
extern struct list_head resctrl_schema_all;
extern bool resctrl_mounted;

enum rdt_group_type {
	RDTCTRL_GROUP = 0,
	RDTMON_GROUP,
	RDT_NUM_GROUP,
};

/**
 * enum rdtgrp_mode - Mode of a RDT resource group
 * @RDT_MODE_SHAREABLE: This resource group allows sharing of its allocations
 * @RDT_MODE_EXCLUSIVE: No sharing of this resource group's allocations allowed
 * @RDT_MODE_PSEUDO_LOCKSETUP: Resource group will be used for Pseudo-Locking
 * @RDT_MODE_PSEUDO_LOCKED: No sharing of this resource group's allocations
 *                          allowed AND the allocations are Cache Pseudo-Locked
 * @RDT_NUM_MODES: Total number of modes
 *
 * The mode of a resource group enables control over the allowed overlap
 * between allocations associated with different resource groups (classes
 * of service). User is able to modify the mode of a resource group by
 * writing to the "mode" resctrl file associated with the resource group.
 *
 * The "shareable", "exclusive", and "pseudo-locksetup" modes are set by
 * writing the appropriate text to the "mode" file. A resource group enters
 * "pseudo-locked" mode after the schemata is written while the resource
 * group is in "pseudo-locksetup" mode.
 */
enum rdtgrp_mode {
	RDT_MODE_SHAREABLE = 0,
	RDT_MODE_EXCLUSIVE,
	RDT_MODE_PSEUDO_LOCKSETUP,
	RDT_MODE_PSEUDO_LOCKED,

	/* Must be last */
	RDT_NUM_MODES,
};

/**
 * struct mongroup - store mon group's data in resctrl fs.
 * @mon_data_kn:		kernfs node for the mon_data directory
 * @parent:			parent rdtgrp
 * @crdtgrp_list:		child rdtgroup node list
 * @rmid:			rmid for this rdtgroup
 */
struct mongroup {
	struct kernfs_node	*mon_data_kn;
	struct rdtgroup		*parent;
	struct list_head	crdtgrp_list;
	u32			rmid;
};

/**
 * struct pseudo_lock_region - pseudo-lock region information
 * @s:			Resctrl schema for the resource to which this
 *			pseudo-locked region belongs
 * @d:			RDT domain to which this pseudo-locked region
 *			belongs
 * @cbm:		bitmask of the pseudo-locked region
 * @lock_thread_wq:	waitqueue used to wait on the pseudo-locking thread
 *			completion
 * @thread_done:	variable used by waitqueue to test if pseudo-locking
 *			thread completed
 * @cpu:		core associated with the cache on which the setup code
 *			will be run
 * @line_size:		size of the cache lines
 * @size:		size of pseudo-locked region in bytes
 * @kmem:		the kernel memory associated with pseudo-locked region
 * @minor:		minor number of character device associated with this
 *			region
 * @debugfs_dir:	pointer to this region's directory in the debugfs
 *			filesystem
 * @pm_reqs:		Power management QoS requests related to this region
 */
struct pseudo_lock_region {
	struct resctrl_schema	*s;
	struct rdt_ctrl_domain	*d;
	u32			cbm;
	wait_queue_head_t	lock_thread_wq;
	int			thread_done;
	int			cpu;
	unsigned int		line_size;
	unsigned int		size;
	void			*kmem;
	unsigned int		minor;
	struct dentry		*debugfs_dir;
	struct list_head	pm_reqs;
};

/**
 * struct rdtgroup - store rdtgroup's data in resctrl file system.
 * @kn:				kernfs node
 * @rdtgroup_list:		linked list for all rdtgroups
 * @closid:			closid for this rdtgroup
 * @cpu_mask:			CPUs assigned to this rdtgroup
 * @flags:			status bits
 * @waitcount:			how many cpus expect to find this
 *				group when they acquire rdtgroup_mutex
 * @type:			indicates type of this rdtgroup - either
 *				monitor only or ctrl_mon group
 * @mon:			mongroup related data
 * @mode:			mode of resource group
 * @mba_mbps_event:		input monitoring event id when mba_sc is enabled
 * @plr:			pseudo-locked region
 */
struct rdtgroup {
	struct kernfs_node		*kn;
	struct list_head		rdtgroup_list;
	u32				closid;
	struct cpumask			cpu_mask;
	int				flags;
	atomic_t			waitcount;
	enum rdt_group_type		type;
	struct mongroup			mon;
	enum rdtgrp_mode		mode;
	enum resctrl_event_id		mba_mbps_event;
	struct pseudo_lock_region	*plr;
};

/* rdtgroup.flags */
#define	RDT_DELETED		1

/* rftype.flags */
#define RFTYPE_FLAGS_CPUS_LIST	1

/*
 * Define the file type flags for base and info directories.
 */
#define RFTYPE_INFO			BIT(0)
#define RFTYPE_BASE			BIT(1)
#define RFTYPE_CTRL			BIT(4)
#define RFTYPE_MON			BIT(5)
#define RFTYPE_TOP			BIT(6)
#define RFTYPE_RES_CACHE		BIT(8)
#define RFTYPE_RES_MB			BIT(9)
#define RFTYPE_DEBUG			BIT(10)
#define RFTYPE_CTRL_INFO		(RFTYPE_INFO | RFTYPE_CTRL)
#define RFTYPE_MON_INFO			(RFTYPE_INFO | RFTYPE_MON)
#define RFTYPE_TOP_INFO			(RFTYPE_INFO | RFTYPE_TOP)
#define RFTYPE_CTRL_BASE		(RFTYPE_BASE | RFTYPE_CTRL)
#define RFTYPE_MON_BASE			(RFTYPE_BASE | RFTYPE_MON)

/* List of all resource groups */
extern struct list_head rdt_all_groups;

extern int max_name_width, max_data_width;

int __init rdtgroup_init(void);
void __exit rdtgroup_exit(void);

/**
 * struct rftype - describe each file in the resctrl file system
 * @name:	File name
 * @mode:	Access mode
 * @kf_ops:	File operations
 * @flags:	File specific RFTYPE_FLAGS_* flags
 * @fflags:	File specific RFTYPE_* flags
 * @seq_show:	Show content of the file
 * @write:	Write to the file
 */
struct rftype {
	char			*name;
	umode_t			mode;
	const struct kernfs_ops	*kf_ops;
	unsigned long		flags;
	unsigned long		fflags;

	int (*seq_show)(struct kernfs_open_file *of,
			struct seq_file *sf, void *v);
	/*
	 * write() is the generic write callback which maps directly to
	 * kernfs write operation and overrides all other operations.
	 * Maximum write size is determined by ->max_write_len.
	 */
	ssize_t (*write)(struct kernfs_open_file *of,
			 char *buf, size_t nbytes, loff_t off);
};

/**
 * struct mbm_state - status for each MBM counter in each domain
 * @prev_bw_bytes: Previous bytes value read for bandwidth calculation
 * @prev_bw:	The most recent bandwidth in MBps
 */
struct mbm_state {
	u64	prev_bw_bytes;
	u32	prev_bw;
};

/**
 * struct arch_mbm_state - values used to compute resctrl_arch_rmid_read()s
 *			   return value.
 * @chunks:	Total data moved (multiply by rdt_group.mon_scale to get bytes)
 * @prev_msr:	Value of IA32_QM_CTR last time it was read for the RMID used to
 *		find this struct.
 */
struct arch_mbm_state {
	u64	chunks;
	u64	prev_msr;
};

/**
 * struct rdt_hw_ctrl_domain - Arch private attributes of a set of CPUs that share
 *			       a resource for a control function
 * @d_resctrl:	Properties exposed to the resctrl file system
 * @ctrl_val:	array of cache or mem ctrl values (indexed by CLOSID)
 *
 * Members of this structure are accessed via helpers that provide abstraction.
 */
struct rdt_hw_ctrl_domain {
	struct rdt_ctrl_domain		d_resctrl;
	u32				*ctrl_val;
};

/**
 * struct rdt_hw_mon_domain - Arch private attributes of a set of CPUs that share
 *			      a resource for a monitor function
 * @d_resctrl:	Properties exposed to the resctrl file system
 * @arch_mbm_total:	arch private state for MBM total bandwidth
 * @arch_mbm_local:	arch private state for MBM local bandwidth
 *
 * Members of this structure are accessed via helpers that provide abstraction.
 */
struct rdt_hw_mon_domain {
	struct rdt_mon_domain		d_resctrl;
	struct arch_mbm_state		*arch_mbm_total;
	struct arch_mbm_state		*arch_mbm_local;
};

static inline struct rdt_hw_ctrl_domain *resctrl_to_arch_ctrl_dom(struct rdt_ctrl_domain *r)
{
	return container_of(r, struct rdt_hw_ctrl_domain, d_resctrl);
}

static inline struct rdt_hw_mon_domain *resctrl_to_arch_mon_dom(struct rdt_mon_domain *r)
{
	return container_of(r, struct rdt_hw_mon_domain, d_resctrl);
}

/**
 * struct msr_param - set a range of MSRs from a domain
 * @res:       The resource to use
 * @dom:       The domain to update
 * @low:       Beginning index from base MSR
 * @high:      End index
 */
struct msr_param {
	struct rdt_resource	*res;
	struct rdt_ctrl_domain	*dom;
	u32			low;
	u32			high;
};

static inline bool is_llc_occupancy_enabled(void)
{
	return (rdt_mon_features & (1 << QOS_L3_OCCUP_EVENT_ID));
}

static inline bool is_mbm_total_enabled(void)
{
	return (rdt_mon_features & (1 << QOS_L3_MBM_TOTAL_EVENT_ID));
}

static inline bool is_mbm_local_enabled(void)
{
	return (rdt_mon_features & (1 << QOS_L3_MBM_LOCAL_EVENT_ID));
}

static inline bool is_mbm_enabled(void)
{
	return (is_mbm_total_enabled() || is_mbm_local_enabled());
}

static inline bool is_mbm_event(int e)
{
	return (e >= QOS_L3_MBM_TOTAL_EVENT_ID &&
		e <= QOS_L3_MBM_LOCAL_EVENT_ID);
}

struct rdt_parse_data {
	struct rdtgroup		*rdtgrp;
	char			*buf;
};

/**
 * struct rdt_hw_resource - arch private attributes of a resctrl resource
 * @r_resctrl:		Attributes of the resource used directly by resctrl.
 * @num_closid:		Maximum number of closid this hardware can support,
 *			regardless of CDP. This is exposed via
 *			resctrl_arch_get_num_closid() to avoid confusion
 *			with struct resctrl_schema's property of the same name,
 *			which has been corrected for features like CDP.
 * @msr_base:		Base MSR address for CBMs
 * @msr_update:		Function pointer to update QOS MSRs
 * @mon_scale:		cqm counter * mon_scale = occupancy in bytes
 * @mbm_width:		Monitor width, to detect and correct for overflow.
 * @mbm_cfg_mask:	Bandwidth sources that can be tracked when Bandwidth
 *			Monitoring Event Configuration (BMEC) is supported.
 * @cdp_enabled:	CDP state of this resource
 *
 * Members of this structure are either private to the architecture
 * e.g. mbm_width, or accessed via helpers that provide abstraction. e.g.
 * msr_update and msr_base.
 */
struct rdt_hw_resource {
	struct rdt_resource	r_resctrl;
	u32			num_closid;
	unsigned int		msr_base;
	void			(*msr_update)(struct msr_param *m);
	unsigned int		mon_scale;
	unsigned int		mbm_width;
	unsigned int		mbm_cfg_mask;
	bool			cdp_enabled;
};

static inline struct rdt_hw_resource *resctrl_to_arch_res(struct rdt_resource *r)
{
	return container_of(r, struct rdt_hw_resource, r_resctrl);
}

int parse_cbm(struct rdt_parse_data *data, struct resctrl_schema *s,
	      struct rdt_ctrl_domain *d);
int parse_bw(struct rdt_parse_data *data, struct resctrl_schema *s,
	     struct rdt_ctrl_domain *d);

extern struct mutex rdtgroup_mutex;

extern struct rdt_hw_resource rdt_resources_all[];
extern struct rdtgroup rdtgroup_default;
extern struct dentry *debugfs_resctrl;
extern enum resctrl_event_id mba_mbps_default_event;

enum resctrl_res_level {
	RDT_RESOURCE_L3,
	RDT_RESOURCE_L2,
	RDT_RESOURCE_MBA,
	RDT_RESOURCE_SMBA,

	/* Must be the last */
	RDT_NUM_RESOURCES,
};

static inline struct rdt_resource *resctrl_inc(struct rdt_resource *res)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(res);

	hw_res++;
	return &hw_res->r_resctrl;
}

static inline bool resctrl_arch_get_cdp_enabled(enum resctrl_res_level l)
{
	return rdt_resources_all[l].cdp_enabled;
}

int resctrl_arch_set_cdp_enabled(enum resctrl_res_level l, bool enable);

void arch_mon_domain_online(struct rdt_resource *r, struct rdt_mon_domain *d);

/*
 * To return the common struct rdt_resource, which is contained in struct
 * rdt_hw_resource, walk the resctrl member of struct rdt_hw_resource.
 */
#define for_each_rdt_resource(r)					      \
	for (r = &rdt_resources_all[0].r_resctrl;			      \
	     r <= &rdt_resources_all[RDT_NUM_RESOURCES - 1].r_resctrl;	      \
	     r = resctrl_inc(r))

#define for_each_capable_rdt_resource(r)				      \
	for_each_rdt_resource(r)					      \
		if (r->alloc_capable || r->mon_capable)

#define for_each_alloc_capable_rdt_resource(r)				      \
	for_each_rdt_resource(r)					      \
		if (r->alloc_capable)

#define for_each_mon_capable_rdt_resource(r)				      \
	for_each_rdt_resource(r)					      \
		if (r->mon_capable)

/* CPUID.(EAX=10H, ECX=ResID=1).EAX */
union cpuid_0x10_1_eax {
	struct {
		unsigned int cbm_len:5;
	} split;
	unsigned int full;
};

/* CPUID.(EAX=10H, ECX=ResID=3).EAX */
union cpuid_0x10_3_eax {
	struct {
		unsigned int max_delay:12;
	} split;
	unsigned int full;
};

/* CPUID.(EAX=10H, ECX=ResID).ECX */
union cpuid_0x10_x_ecx {
	struct {
		unsigned int reserved:3;
		unsigned int noncont:1;
	} split;
	unsigned int full;
};

/* CPUID.(EAX=10H, ECX=ResID).EDX */
union cpuid_0x10_x_edx {
	struct {
		unsigned int cos_max:16;
	} split;
	unsigned int full;
};

void rdt_last_cmd_clear(void);
void rdt_last_cmd_puts(const char *s);
__printf(1, 2)
void rdt_last_cmd_printf(const char *fmt, ...);

void rdt_ctrl_update(void *arg);
struct rdtgroup *rdtgroup_kn_lock_live(struct kernfs_node *kn);
void rdtgroup_kn_unlock(struct kernfs_node *kn);
int rdtgroup_kn_mode_restrict(struct rdtgroup *r, const char *name);
int rdtgroup_kn_mode_restore(struct rdtgroup *r, const char *name,
			     umode_t mask);
struct rdt_domain_hdr *rdt_find_domain(struct list_head *h, int id,
				       struct list_head **pos);
ssize_t rdtgroup_schemata_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off);
int rdtgroup_schemata_show(struct kernfs_open_file *of,
			   struct seq_file *s, void *v);
ssize_t rdtgroup_mba_mbps_event_write(struct kernfs_open_file *of,
				      char *buf, size_t nbytes, loff_t off);
int rdtgroup_mba_mbps_event_show(struct kernfs_open_file *of,
				 struct seq_file *s, void *v);
bool rdtgroup_cbm_overlaps(struct resctrl_schema *s, struct rdt_ctrl_domain *d,
			   unsigned long cbm, int closid, bool exclusive);
unsigned int rdtgroup_cbm_to_size(struct rdt_resource *r, struct rdt_ctrl_domain *d,
				  unsigned long cbm);
enum rdtgrp_mode rdtgroup_mode_by_closid(int closid);
int rdtgroup_tasks_assigned(struct rdtgroup *r);
int rdtgroup_locksetup_enter(struct rdtgroup *rdtgrp);
int rdtgroup_locksetup_exit(struct rdtgroup *rdtgrp);
bool rdtgroup_cbm_overlaps_pseudo_locked(struct rdt_ctrl_domain *d, unsigned long cbm);
bool rdtgroup_pseudo_locked_in_hierarchy(struct rdt_ctrl_domain *d);
int rdt_pseudo_lock_init(void);
void rdt_pseudo_lock_release(void);
int rdtgroup_pseudo_lock_create(struct rdtgroup *rdtgrp);
void rdtgroup_pseudo_lock_remove(struct rdtgroup *rdtgrp);
struct rdt_ctrl_domain *get_ctrl_domain_from_cpu(int cpu, struct rdt_resource *r);
struct rdt_mon_domain *get_mon_domain_from_cpu(int cpu, struct rdt_resource *r);
int closids_supported(void);
void closid_free(int closid);
int alloc_rmid(u32 closid);
void free_rmid(u32 closid, u32 rmid);
int rdt_get_mon_l3_config(struct rdt_resource *r);
void __exit rdt_put_mon_l3_config(void);
bool __init rdt_cpu_has(int flag);
void mon_event_count(void *info);
int rdtgroup_mondata_show(struct seq_file *m, void *arg);
void mon_event_read(struct rmid_read *rr, struct rdt_resource *r,
		    struct rdt_mon_domain *d, struct rdtgroup *rdtgrp,
		    cpumask_t *cpumask, int evtid, int first);
void mbm_setup_overflow_handler(struct rdt_mon_domain *dom,
				unsigned long delay_ms,
				int exclude_cpu);
void mbm_handle_overflow(struct work_struct *work);
void __init intel_rdt_mbm_apply_quirk(void);
bool is_mba_sc(struct rdt_resource *r);
void cqm_setup_limbo_handler(struct rdt_mon_domain *dom, unsigned long delay_ms,
			     int exclude_cpu);
void cqm_handle_limbo(struct work_struct *work);
bool has_busy_rmid(struct rdt_mon_domain *d);
void __check_limbo(struct rdt_mon_domain *d, bool force_free);
void rdt_domain_reconfigure_cdp(struct rdt_resource *r);
void resctrl_file_fflags_init(const char *config, unsigned long fflags);
void rdt_staged_configs_clear(void);
bool closid_allocated(unsigned int closid);
int resctrl_find_cleanest_closid(void);
#endif /* _ASM_X86_RESCTRL_INTERNAL_H */
