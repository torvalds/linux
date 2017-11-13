/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_INTEL_RDT_H
#define _ASM_X86_INTEL_RDT_H

#include <linux/sched.h>
#include <linux/kernfs.h>
#include <linux/jump_label.h>

#define IA32_L3_QOS_CFG		0xc81
#define IA32_L3_CBM_BASE	0xc90
#define IA32_L2_CBM_BASE	0xd10
#define IA32_MBA_THRTL_BASE	0xd50

#define L3_QOS_CDP_ENABLE	0x01ULL

/*
 * Event IDs are used to program IA32_QM_EVTSEL before reading event
 * counter from IA32_QM_CTR
 */
#define QOS_L3_OCCUP_EVENT_ID		0x01
#define QOS_L3_MBM_TOTAL_EVENT_ID	0x02
#define QOS_L3_MBM_LOCAL_EVENT_ID	0x03

#define CQM_LIMBOCHECK_INTERVAL	1000

#define MBM_CNTR_WIDTH			24
#define MBM_OVERFLOW_INTERVAL		1000

#define RMID_VAL_ERROR			BIT_ULL(63)
#define RMID_VAL_UNAVAIL		BIT_ULL(62)

DECLARE_STATIC_KEY_FALSE(rdt_enable_key);

/**
 * struct mon_evt - Entry in the event list of a resource
 * @evtid:		event id
 * @name:		name of the event
 */
struct mon_evt {
	u32			evtid;
	char			*name;
	struct list_head	list;
};

/**
 * struct mon_data_bits - Monitoring details for each event file
 * @rid:               Resource id associated with the event file.
 * @evtid:             Event id associated with the event file
 * @domid:             The domain to which the event file belongs
 */
union mon_data_bits {
	void *priv;
	struct {
		unsigned int rid	: 10;
		unsigned int evtid	: 8;
		unsigned int domid	: 14;
	} u;
};

struct rmid_read {
	struct rdtgroup		*rgrp;
	struct rdt_domain	*d;
	int			evtid;
	bool			first;
	u64			val;
};

extern unsigned int intel_cqm_threshold;
extern bool rdt_alloc_capable;
extern bool rdt_mon_capable;
extern unsigned int rdt_mon_features;

enum rdt_group_type {
	RDTCTRL_GROUP = 0,
	RDTMON_GROUP,
	RDT_NUM_GROUP,
};

/**
 * struct mongroup - store mon group's data in resctrl fs.
 * @mon_data_kn		kernlfs node for the mon_data directory
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
 */
struct rdtgroup {
	struct kernfs_node	*kn;
	struct list_head	rdtgroup_list;
	u32			closid;
	struct cpumask		cpu_mask;
	int			flags;
	atomic_t		waitcount;
	enum rdt_group_type	type;
	struct mongroup		mon;
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
#define RF_CTRLSHIFT			4
#define RF_MONSHIFT			5
#define RFTYPE_CTRL			BIT(RF_CTRLSHIFT)
#define RFTYPE_MON			BIT(RF_MONSHIFT)
#define RFTYPE_RES_CACHE		BIT(8)
#define RFTYPE_RES_MB			BIT(9)
#define RF_CTRL_INFO			(RFTYPE_INFO | RFTYPE_CTRL)
#define RF_MON_INFO			(RFTYPE_INFO | RFTYPE_MON)
#define RF_CTRL_BASE			(RFTYPE_BASE | RFTYPE_CTRL)

/* List of all resource groups */
extern struct list_head rdt_all_groups;

extern int max_name_width, max_data_width;

int __init rdtgroup_init(void);

/**
 * struct rftype - describe each file in the resctrl file system
 * @name:	File name
 * @mode:	Access mode
 * @kf_ops:	File operations
 * @flags:	File specific RFTYPE_FLAGS_* flags
 * @fflags:	File specific RF_* or RFTYPE_* flags
 * @seq_show:	Show content of the file
 * @write:	Write to the file
 */
struct rftype {
	char			*name;
	umode_t			mode;
	struct kernfs_ops	*kf_ops;
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
 * @chunks:	Total data moved (multiply by rdt_group.mon_scale to get bytes)
 * @prev_msr	Value of IA32_QM_CTR for this RMID last time we read it
 */
struct mbm_state {
	u64	chunks;
	u64	prev_msr;
};

/**
 * struct rdt_domain - group of cpus sharing an RDT resource
 * @list:	all instances of this resource
 * @id:		unique id for this instance
 * @cpu_mask:	which cpus share this resource
 * @rmid_busy_llc:
 *		bitmap of which limbo RMIDs are above threshold
 * @mbm_total:	saved state for MBM total bandwidth
 * @mbm_local:	saved state for MBM local bandwidth
 * @mbm_over:	worker to periodically read MBM h/w counters
 * @cqm_limbo:	worker to periodically read CQM h/w counters
 * @mbm_work_cpu:
 *		worker cpu for MBM h/w counters
 * @cqm_work_cpu:
 *		worker cpu for CQM h/w counters
 * @ctrl_val:	array of cache or mem ctrl values (indexed by CLOSID)
 * @new_ctrl:	new ctrl value to be loaded
 * @have_new_ctrl: did user provide new_ctrl for this domain
 */
struct rdt_domain {
	struct list_head	list;
	int			id;
	struct cpumask		cpu_mask;
	unsigned long		*rmid_busy_llc;
	struct mbm_state	*mbm_total;
	struct mbm_state	*mbm_local;
	struct delayed_work	mbm_over;
	struct delayed_work	cqm_limbo;
	int			mbm_work_cpu;
	int			cqm_work_cpu;
	u32			*ctrl_val;
	u32			new_ctrl;
	bool			have_new_ctrl;
};

/**
 * struct msr_param - set a range of MSRs from a domain
 * @res:       The resource to use
 * @low:       Beginning index from base MSR
 * @high:      End index
 */
struct msr_param {
	struct rdt_resource	*res;
	int			low;
	int			high;
};

/**
 * struct rdt_cache - Cache allocation related data
 * @cbm_len:		Length of the cache bit mask
 * @min_cbm_bits:	Minimum number of consecutive bits to be set
 * @cbm_idx_mult:	Multiplier of CBM index
 * @cbm_idx_offset:	Offset of CBM index. CBM index is computed by:
 *			closid * cbm_idx_multi + cbm_idx_offset
 *			in a cache bit mask
 * @shareable_bits:	Bitmask of shareable resource with other
 *			executing entities
 */
struct rdt_cache {
	unsigned int	cbm_len;
	unsigned int	min_cbm_bits;
	unsigned int	cbm_idx_mult;
	unsigned int	cbm_idx_offset;
	unsigned int	shareable_bits;
};

/**
 * struct rdt_membw - Memory bandwidth allocation related data
 * @max_delay:		Max throttle delay. Delay is the hardware
 *			representation for memory bandwidth.
 * @min_bw:		Minimum memory bandwidth percentage user can request
 * @bw_gran:		Granularity at which the memory bandwidth is allocated
 * @delay_linear:	True if memory B/W delay is in linear scale
 * @mb_map:		Mapping of memory B/W percentage to memory B/W delay
 */
struct rdt_membw {
	u32		max_delay;
	u32		min_bw;
	u32		bw_gran;
	u32		delay_linear;
	u32		*mb_map;
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

/**
 * struct rdt_resource - attributes of an RDT resource
 * @rid:		The index of the resource
 * @alloc_enabled:	Is allocation enabled on this machine
 * @mon_enabled:		Is monitoring enabled for this feature
 * @alloc_capable:	Is allocation available on this machine
 * @mon_capable:		Is monitor feature available on this machine
 * @name:		Name to use in "schemata" file
 * @num_closid:		Number of CLOSIDs available
 * @cache_level:	Which cache level defines scope of this resource
 * @default_ctrl:	Specifies default cache cbm or memory B/W percent.
 * @msr_base:		Base MSR address for CBMs
 * @msr_update:		Function pointer to update QOS MSRs
 * @data_width:		Character width of data when displaying
 * @domains:		All domains for this resource
 * @cache:		Cache allocation related data
 * @format_str:		Per resource format string to show domain value
 * @parse_ctrlval:	Per resource function pointer to parse control values
 * @evt_list:			List of monitoring events
 * @num_rmid:			Number of RMIDs available
 * @mon_scale:			cqm counter * mon_scale = occupancy in bytes
 * @fflags:			flags to choose base and info files
 */
struct rdt_resource {
	int			rid;
	bool			alloc_enabled;
	bool			mon_enabled;
	bool			alloc_capable;
	bool			mon_capable;
	char			*name;
	int			num_closid;
	int			cache_level;
	u32			default_ctrl;
	unsigned int		msr_base;
	void (*msr_update)	(struct rdt_domain *d, struct msr_param *m,
				 struct rdt_resource *r);
	int			data_width;
	struct list_head	domains;
	struct rdt_cache	cache;
	struct rdt_membw	membw;
	const char		*format_str;
	int (*parse_ctrlval)	(char *buf, struct rdt_resource *r,
				 struct rdt_domain *d);
	struct list_head	evt_list;
	int			num_rmid;
	unsigned int		mon_scale;
	unsigned long		fflags;
};

int parse_cbm(char *buf, struct rdt_resource *r, struct rdt_domain *d);
int parse_bw(char *buf, struct rdt_resource *r,  struct rdt_domain *d);

extern struct mutex rdtgroup_mutex;

extern struct rdt_resource rdt_resources_all[];
extern struct rdtgroup rdtgroup_default;
DECLARE_STATIC_KEY_FALSE(rdt_alloc_enable_key);

int __init rdtgroup_init(void);

enum {
	RDT_RESOURCE_L3,
	RDT_RESOURCE_L3DATA,
	RDT_RESOURCE_L3CODE,
	RDT_RESOURCE_L2,
	RDT_RESOURCE_MBA,

	/* Must be the last */
	RDT_NUM_RESOURCES,
};

#define for_each_capable_rdt_resource(r)				      \
	for (r = rdt_resources_all; r < rdt_resources_all + RDT_NUM_RESOURCES;\
	     r++)							      \
		if (r->alloc_capable || r->mon_capable)

#define for_each_alloc_capable_rdt_resource(r)				      \
	for (r = rdt_resources_all; r < rdt_resources_all + RDT_NUM_RESOURCES;\
	     r++)							      \
		if (r->alloc_capable)

#define for_each_mon_capable_rdt_resource(r)				      \
	for (r = rdt_resources_all; r < rdt_resources_all + RDT_NUM_RESOURCES;\
	     r++)							      \
		if (r->mon_capable)

#define for_each_alloc_enabled_rdt_resource(r)				      \
	for (r = rdt_resources_all; r < rdt_resources_all + RDT_NUM_RESOURCES;\
	     r++)							      \
		if (r->alloc_enabled)

#define for_each_mon_enabled_rdt_resource(r)				      \
	for (r = rdt_resources_all; r < rdt_resources_all + RDT_NUM_RESOURCES;\
	     r++)							      \
		if (r->mon_enabled)

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

/* CPUID.(EAX=10H, ECX=ResID).EDX */
union cpuid_0x10_x_edx {
	struct {
		unsigned int cos_max:16;
	} split;
	unsigned int full;
};

void rdt_ctrl_update(void *arg);
struct rdtgroup *rdtgroup_kn_lock_live(struct kernfs_node *kn);
void rdtgroup_kn_unlock(struct kernfs_node *kn);
struct rdt_domain *rdt_find_domain(struct rdt_resource *r, int id,
				   struct list_head **pos);
ssize_t rdtgroup_schemata_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off);
int rdtgroup_schemata_show(struct kernfs_open_file *of,
			   struct seq_file *s, void *v);
struct rdt_domain *get_domain_from_cpu(int cpu, struct rdt_resource *r);
int alloc_rmid(void);
void free_rmid(u32 rmid);
int rdt_get_mon_l3_config(struct rdt_resource *r);
void mon_event_count(void *info);
int rdtgroup_mondata_show(struct seq_file *m, void *arg);
void rmdir_mondata_subdir_allrdtgrp(struct rdt_resource *r,
				    unsigned int dom_id);
void mkdir_mondata_subdir_allrdtgrp(struct rdt_resource *r,
				    struct rdt_domain *d);
void mon_event_read(struct rmid_read *rr, struct rdt_domain *d,
		    struct rdtgroup *rdtgrp, int evtid, int first);
void mbm_setup_overflow_handler(struct rdt_domain *dom,
				unsigned long delay_ms);
void mbm_handle_overflow(struct work_struct *work);
void cqm_setup_limbo_handler(struct rdt_domain *dom, unsigned long delay_ms);
void cqm_handle_limbo(struct work_struct *work);
bool has_busy_rmid(struct rdt_resource *r, struct rdt_domain *d);
void __check_limbo(struct rdt_domain *d, bool force_free);

#endif /* _ASM_X86_INTEL_RDT_H */
