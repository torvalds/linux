#ifndef _ASM_X86_INTEL_RDT_H
#define _ASM_X86_INTEL_RDT_H

#ifdef CONFIG_INTEL_RDT_A

#include <linux/sched.h>
#include <linux/kernfs.h>
#include <linux/jump_label.h>

#include <asm/intel_rdt_common.h>

#define IA32_L3_QOS_CFG		0xc81
#define IA32_L3_CBM_BASE	0xc90
#define IA32_L2_CBM_BASE	0xd10
#define IA32_MBA_THRTL_BASE	0xd50

#define L3_QOS_CDP_ENABLE	0x01ULL

/**
 * struct rdtgroup - store rdtgroup's data in resctrl file system.
 * @kn:				kernfs node
 * @rdtgroup_list:		linked list for all rdtgroups
 * @closid:			closid for this rdtgroup
 * @cpu_mask:			CPUs assigned to this rdtgroup
 * @flags:			status bits
 * @waitcount:			how many cpus expect to find this
 *				group when they acquire rdtgroup_mutex
 */
struct rdtgroup {
	struct kernfs_node	*kn;
	struct list_head	rdtgroup_list;
	int			closid;
	struct cpumask		cpu_mask;
	int			flags;
	atomic_t		waitcount;
};

/* rdtgroup.flags */
#define	RDT_DELETED		1

/* rftype.flags */
#define RFTYPE_FLAGS_CPUS_LIST	1

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
 * @seq_show:	Show content of the file
 * @write:	Write to the file
 */
struct rftype {
	char			*name;
	umode_t			mode;
	struct kernfs_ops	*kf_ops;
	unsigned long		flags;

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
 * struct rdt_domain - group of cpus sharing an RDT resource
 * @list:	all instances of this resource
 * @id:		unique id for this instance
 * @cpu_mask:	which cpus share this resource
 * @ctrl_val:	array of cache or mem ctrl values (indexed by CLOSID)
 * @new_ctrl:	new ctrl value to be loaded
 * @have_new_ctrl: did user provide new_ctrl for this domain
 */
struct rdt_domain {
	struct list_head	list;
	int			id;
	struct cpumask		cpu_mask;
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
 */
struct rdt_cache {
	unsigned int	cbm_len;
	unsigned int	min_cbm_bits;
	unsigned int	cbm_idx_mult;
	unsigned int	cbm_idx_offset;
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

/**
 * struct rdt_resource - attributes of an RDT resource
 * @enabled:		Is this feature enabled on this machine
 * @capable:		Is this feature available on this machine
 * @name:		Name to use in "schemata" file
 * @num_closid:		Number of CLOSIDs available
 * @cache_level:	Which cache level defines scope of this resource
 * @default_ctrl:	Specifies default cache cbm or memory B/W percent.
 * @msr_base:		Base MSR address for CBMs
 * @msr_update:		Function pointer to update QOS MSRs
 * @data_width:		Character width of data when displaying
 * @domains:		All domains for this resource
 * @cache:		Cache allocation related data
 * @info_files:		resctrl info files for the resource
 * @nr_info_files:	Number of info files
 * @format_str:		Per resource format string to show domain value
 * @parse_ctrlval:	Per resource function pointer to parse control values
 */
struct rdt_resource {
	bool			enabled;
	bool			capable;
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
	struct rftype		*info_files;
	int			nr_info_files;
	const char		*format_str;
	int (*parse_ctrlval)	(char *buf, struct rdt_resource *r,
				 struct rdt_domain *d);
};

void rdt_get_cache_infofile(struct rdt_resource *r);
void rdt_get_mba_infofile(struct rdt_resource *r);
int parse_cbm(char *buf, struct rdt_resource *r, struct rdt_domain *d);
int parse_bw(char *buf, struct rdt_resource *r,  struct rdt_domain *d);

extern struct mutex rdtgroup_mutex;

extern struct rdt_resource rdt_resources_all[];
extern struct rdtgroup rdtgroup_default;
DECLARE_STATIC_KEY_FALSE(rdt_enable_key);

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
		if (r->capable)

#define for_each_enabled_rdt_resource(r)				      \
	for (r = rdt_resources_all; r < rdt_resources_all + RDT_NUM_RESOURCES;\
	     r++)							      \
		if (r->enabled)

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

DECLARE_PER_CPU_READ_MOSTLY(int, cpu_closid);

void rdt_ctrl_update(void *arg);
struct rdtgroup *rdtgroup_kn_lock_live(struct kernfs_node *kn);
void rdtgroup_kn_unlock(struct kernfs_node *kn);
ssize_t rdtgroup_schemata_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off);
int rdtgroup_schemata_show(struct kernfs_open_file *of,
			   struct seq_file *s, void *v);

/*
 * intel_rdt_sched_in() - Writes the task's CLOSid to IA32_PQR_MSR
 *
 * Following considerations are made so that this has minimal impact
 * on scheduler hot path:
 * - This will stay as no-op unless we are running on an Intel SKU
 *   which supports resource control and we enable by mounting the
 *   resctrl file system.
 * - Caches the per cpu CLOSid values and does the MSR write only
 *   when a task with a different CLOSid is scheduled in.
 *
 * Must be called with preemption disabled.
 */
static inline void intel_rdt_sched_in(void)
{
	if (static_branch_likely(&rdt_enable_key)) {
		struct intel_pqr_state *state = this_cpu_ptr(&pqr_state);
		int closid;

		/*
		 * If this task has a closid assigned, use it.
		 * Else use the closid assigned to this cpu.
		 */
		closid = current->closid;
		if (closid == 0)
			closid = this_cpu_read(cpu_closid);

		if (closid != state->closid) {
			state->closid = closid;
			wrmsr(MSR_IA32_PQR_ASSOC, state->rmid, closid);
		}
	}
}

#else

static inline void intel_rdt_sched_in(void) {}

#endif /* CONFIG_INTEL_RDT_A */
#endif /* _ASM_X86_INTEL_RDT_H */
