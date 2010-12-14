#ifndef _ASM_X86_AMD_NB_H
#define _ASM_X86_AMD_NB_H

#include <linux/pci.h>

extern struct pci_device_id k8_nb_ids[];
struct bootnode;

extern int early_is_k8_nb(u32 value);
extern int cache_k8_northbridges(void);
extern void k8_flush_garts(void);
extern int k8_get_nodes(struct bootnode *nodes);
extern int k8_numa_init(unsigned long start_pfn, unsigned long end_pfn);
extern int k8_scan_nodes(void);

struct k8_northbridge_info {
	u16 num;
	u8 gart_supported;
	struct pci_dev **nb_misc;
};
extern struct k8_northbridge_info k8_northbridges;

#ifdef CONFIG_AMD_NB

static inline struct pci_dev *node_to_k8_nb_misc(int node)
{
	return (node < k8_northbridges.num) ? k8_northbridges.nb_misc[node] : NULL;
}

#else

static inline struct pci_dev *node_to_k8_nb_misc(int node)
{
	return NULL;
}
#endif


#endif /* _ASM_X86_AMD_NB_H */
