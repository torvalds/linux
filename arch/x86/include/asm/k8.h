#ifndef _ASM_X86_K8_H
#define _ASM_X86_K8_H

#include <linux/pci.h>

extern struct pci_device_id k8_nb_ids[];

extern int early_is_k8_nb(u32 value);
extern struct pci_dev **k8_northbridges;
extern int num_k8_northbridges;
extern int cache_k8_northbridges(void);
extern void k8_flush_garts(void);
extern int k8_scan_nodes(unsigned long start, unsigned long end);

#endif /* _ASM_X86_K8_H */
