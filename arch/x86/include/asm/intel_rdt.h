#ifndef _ASM_X86_INTEL_RDT_H
#define _ASM_X86_INTEL_RDT_H

#define IA32_L3_CBM_BASE	0xc90
#define IA32_L2_CBM_BASE	0xd10

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

void rdt_cbm_update(void *arg);
#endif /* _ASM_X86_INTEL_RDT_H */
