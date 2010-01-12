#ifndef _ASM_X86_K8_H
#define _ASM_X86_K8_H

#include <linux/pci.h>

extern struct pci_device_id k8_nb_ids[];
struct bootnode;

extern int early_is_k8_nb(u32 value);
extern struct pci_dev **k8_northbridges;
extern int num_k8_northbridges;
extern int cache_k8_northbridges(void);
extern void k8_flush_garts(void);
extern int k8_get_nodes(struct bootnode *nodes);
extern int k8_numa_init(unsigned long start_pfn, unsigned long end_pfn);
extern int k8_scan_nodes(void);

#ifdef CONFIG_K8_NB
static inline struct pci_dev *node_to_k8_nb_misc(int node)
{
	return (node < num_k8_northbridges) ? k8_northbridges[node] : NULL;
}
#else
static inline struct pci_dev *node_to_k8_nb_misc(int node)
{
	return NULL;
}
#endif


#endif /* _ASM_X86_K8_H */
