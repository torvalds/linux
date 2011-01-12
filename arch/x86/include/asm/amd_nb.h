#ifndef _ASM_X86_AMD_NB_H
#define _ASM_X86_AMD_NB_H

#include <linux/pci.h>

struct amd_nb_bus_dev_range {
	u8 bus;
	u8 dev_base;
	u8 dev_limit;
};

extern struct pci_device_id amd_nb_misc_ids[];
extern const struct amd_nb_bus_dev_range amd_nb_bus_dev_ranges[];
struct bootnode;

extern int early_is_amd_nb(u32 value);
extern int amd_cache_northbridges(void);
extern void amd_flush_garts(void);
extern int amd_numa_init(unsigned long start_pfn, unsigned long end_pfn);
extern int amd_scan_nodes(void);

#ifdef CONFIG_NUMA_EMU
extern void amd_fake_nodes(const struct bootnode *nodes, int nr_nodes);
extern void amd_get_nodes(struct bootnode *nodes);
#endif

struct amd_northbridge {
	struct pci_dev *misc;
};

struct amd_northbridge_info {
	u16 num;
	u64 flags;
	struct amd_northbridge *nb;
};
extern struct amd_northbridge_info amd_northbridges;

#define AMD_NB_GART			0x1
#define AMD_NB_L3_INDEX_DISABLE		0x2

#ifdef CONFIG_AMD_NB

static inline int amd_nb_num(void)
{
	return amd_northbridges.num;
}

static inline int amd_nb_has_feature(int feature)
{
	return ((amd_northbridges.flags & feature) == feature);
}

static inline struct amd_northbridge *node_to_amd_nb(int node)
{
	return (node < amd_northbridges.num) ? &amd_northbridges.nb[node] : NULL;
}

#else

#define amd_nb_num(x)		0
#define amd_nb_has_feature(x)	false
#define node_to_amd_nb(x)	NULL

#endif


#endif /* _ASM_X86_AMD_NB_H */
