#ifndef _ASM_X86_AMD_NB_H
#define _ASM_X86_AMD_NB_H

#include <linux/pci.h>

extern struct pci_device_id amd_nb_ids[];
struct bootnode;

extern int early_is_amd_nb(u32 value);
extern int cache_amd_northbridges(void);
extern void amd_flush_garts(void);
extern int amd_get_nodes(struct bootnode *nodes);
extern int amd_numa_init(unsigned long start_pfn, unsigned long end_pfn);
extern int amd_scan_nodes(void);

struct amd_northbridge_info {
	u16 num;
	u8 gart_supported;
	struct pci_dev **nb_misc;
};
extern struct amd_northbridge_info amd_northbridges;

#ifdef CONFIG_AMD_NB

static inline struct pci_dev *node_to_amd_nb_misc(int node)
{
	return (node < amd_northbridges.num) ? amd_northbridges.nb_misc[node] : NULL;
}

#else

static inline struct pci_dev *node_to_amd_nb_misc(int node)
{
	return NULL;
}
#endif


#endif /* _ASM_X86_AMD_NB_H */
