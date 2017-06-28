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

/* List of all resource groups */
extern struct list_head rdt_all_groups;

int __init rdtgroup_init(void);

/**
 * struct rftype - describe each file in the resctrl file system
 * @name: file name
 * @mode: access mode
 * @kf_ops: operations
 * @seq_show: show content of the file
 * @write: write to the file
 */
struct rftype {
	char			*name;
	umode_t			mode;
	struct kernfs_ops	*kf_ops;

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
 * struct rdt_resource - attributes of an RDT resource
 * @enabled:			Is this feature enabled on this machine
 * @capable:			Is this feature available on this machine
 * @name:			Name to use in "schemata" file
 * @num_closid:			Number of CLOSIDs available
 * @max_cbm:			Largest Cache Bit Mask allowed
 * @min_cbm_bits:		Minimum number of consecutive bits to be set
 *				in a cache bit mask
 * @domains:			All domains for this resource
 * @num_domains:		Number of domains active
 * @msr_base:			Base MSR address for CBMs
 * @tmp_cbms:			Scratch space when updating schemata
 * @num_tmp_cbms:		Number of CBMs in tmp_cbms
 * @cache_level:		Which cache level defines scope of this domain
 * @cbm_idx_multi:		Multiplier of CBM index
 * @cbm_idx_offset:		Offset of CBM index. CBM index is computed by:
 *				closid * cbm_idx_multi + cbm_idx_offset
 */
struct rdt_resource {
	bool			enabled;
	bool			capable;
	char			*name;
	int			num_closid;
	int			cbm_len;
	int			min_cbm_bits;
	u32			max_cbm;
	struct list_head	domains;
	int			num_domains;
	int			msr_base;
	u32			*tmp_cbms;
	int			num_tmp_cbms;
	int			cache_level;
	int			cbm_idx_multi;
	int			cbm_idx_offset;
};

/**
 * struct rdt_domain - group of cpus sharing an RDT resource
 * @list:	all instances of this resource
 * @id:		unique id for this instance
 * @cpu_mask:	which cpus share this resource
 * @cbm:	array of cache bit masks (indexed by CLOSID)
 */
struct rdt_domain {
	struct list_head	list;
	int			id;
	struct cpumask		cpu_mask;
	u32			*cbm;
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

	/* Must be the last */
	RDT_NUM_RESOURCES,
};

#define for_each_capable_rdt_resource(r)				      \
	for (r = rdt_resources_all; r < rdt_resources_all + RDT_NUM_RESOURCES;\
	     r++) 							      \
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

/* CPUID.(EAX=10H, ECX=ResID=1).EDX */
union cpuid_0x10_1_edx {
	struct {
		unsigned int cos_max:16;
	} split;
	unsigned int full;
};

DECLARE_PER_CPU_READ_MOSTLY(int, cpu_closid);

void rdt_cbm_update(void *arg);
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
