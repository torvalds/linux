/* arch/sparc64/kernel/sparc64_ksyms.c: Sparc64 specific ksyms support.
 *
 * Copyright (C) 1996, 2007 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 * Copyright (C) 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/cpudata.h>
#include <asm/uaccess.h>
#include <asm/spitfire.h>
#include <asm/oplib.h>
#include <asm/hypervisor.h>

struct poll {
	int fd;
	short events;
	short revents;
};

/* from helpers.S */
EXPORT_SYMBOL(__flushw_user);
EXPORT_SYMBOL_GPL(real_hard_smp_processor_id);

/* from head_64.S */
EXPORT_SYMBOL(__ret_efault);
EXPORT_SYMBOL(tlb_type);
EXPORT_SYMBOL(sun4v_chip_type);
EXPORT_SYMBOL(prom_root_node);

/* from hvcalls.S */
EXPORT_SYMBOL(sun4v_niagara_getperf);
EXPORT_SYMBOL(sun4v_niagara_setperf);
EXPORT_SYMBOL(sun4v_niagara2_getperf);
EXPORT_SYMBOL(sun4v_niagara2_setperf);

#ifdef CONFIG_PCI
/* inline functions in asm/pci_64.h */
EXPORT_SYMBOL(pci_alloc_consistent);
EXPORT_SYMBOL(pci_free_consistent);
EXPORT_SYMBOL(pci_map_single);
EXPORT_SYMBOL(pci_unmap_single);
EXPORT_SYMBOL(pci_map_sg);
EXPORT_SYMBOL(pci_unmap_sg);
EXPORT_SYMBOL(pci_dma_sync_single_for_cpu);
EXPORT_SYMBOL(pci_dma_sync_sg_for_cpu);
#endif

/* Exporting a symbol from /init/main.c */
EXPORT_SYMBOL(saved_command_line);
