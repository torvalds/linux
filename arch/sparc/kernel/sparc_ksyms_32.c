/*
 * arch/sparc/kernel/ksyms.c: Sparc specific ksyms support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 */

#define PROMLIB_INTERNAL

#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/in6.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/syscalls.h>
#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif
#include <linux/pm.h>
#ifdef CONFIG_HIGHMEM
#include <linux/highmem.h>
#endif

#include <asm/oplib.h>
#include <asm/delay.h>
#include <asm/system.h>
#include <asm/auxio.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/idprom.h>
#include <asm/head.h>
#include <asm/smp.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#ifdef CONFIG_SBUS
#include <asm/dma.h>
#endif
#include <asm/io-unit.h>
#include <asm/bug.h>

extern spinlock_t rtc_lock;

struct poll {
	int fd;
	short events;
	short revents;
};

/* used by various drivers */
EXPORT_SYMBOL(sparc_cpu_model);
EXPORT_SYMBOL(kernel_thread);

EXPORT_SYMBOL(sparc_valid_addr_bitmap);
EXPORT_SYMBOL(phys_base);
EXPORT_SYMBOL(pfn_base);

/* Per-CPU information table */
EXPORT_PER_CPU_SYMBOL(__cpu_data);

#ifdef CONFIG_SMP
/* IRQ implementation. */
EXPORT_SYMBOL(synchronize_irq);
#endif

EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__ndelay);
EXPORT_SYMBOL(rtc_lock);
EXPORT_SYMBOL(set_auxio);
EXPORT_SYMBOL(get_auxio);
EXPORT_SYMBOL(io_remap_pfn_range);

#ifndef CONFIG_SMP
EXPORT_SYMBOL(BTFIXUP_CALL(___xchg32));
#else
EXPORT_SYMBOL(BTFIXUP_CALL(__hard_smp_processor_id));
#endif
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_unlockarea));
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_lockarea));
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_get_scsi_sgl));
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_get_scsi_one));
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_release_scsi_sgl));
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_release_scsi_one));

EXPORT_SYMBOL(BTFIXUP_CALL(pgprot_noncached));

#ifdef CONFIG_SBUS
EXPORT_SYMBOL(sbus_set_sbus64);
#endif
#ifdef CONFIG_PCI
EXPORT_SYMBOL(insb);
EXPORT_SYMBOL(outsb);
EXPORT_SYMBOL(insw);
EXPORT_SYMBOL(outsw);
EXPORT_SYMBOL(insl);
EXPORT_SYMBOL(outsl);
EXPORT_SYMBOL(pci_alloc_consistent);
EXPORT_SYMBOL(pci_free_consistent);
EXPORT_SYMBOL(pci_map_single);
EXPORT_SYMBOL(pci_unmap_single);
EXPORT_SYMBOL(pci_dma_sync_single_for_cpu);
EXPORT_SYMBOL(pci_dma_sync_single_for_device);
EXPORT_SYMBOL(pci_dma_sync_sg_for_cpu);
EXPORT_SYMBOL(pci_dma_sync_sg_for_device);
EXPORT_SYMBOL(pci_map_sg);
EXPORT_SYMBOL(pci_unmap_sg);
EXPORT_SYMBOL(pci_map_page);
EXPORT_SYMBOL(pci_unmap_page);
/* Actually, ioremap/iounmap are not PCI specific. But it is ok for drivers. */
EXPORT_SYMBOL(ioremap);
EXPORT_SYMBOL(iounmap);
#endif

/* in arch/sparc/mm/highmem.c */
#ifdef CONFIG_HIGHMEM
EXPORT_SYMBOL(kmap_atomic);
EXPORT_SYMBOL(kunmap_atomic);
#endif

/* prom symbols */
EXPORT_SYMBOL(idprom);
EXPORT_SYMBOL(prom_root_node);
EXPORT_SYMBOL(prom_getchild);
EXPORT_SYMBOL(prom_getsibling);
EXPORT_SYMBOL(prom_searchsiblings);
EXPORT_SYMBOL(prom_firstprop);
EXPORT_SYMBOL(prom_nextprop);
EXPORT_SYMBOL(prom_getproplen);
EXPORT_SYMBOL(prom_getproperty);
EXPORT_SYMBOL(prom_node_has_property);
EXPORT_SYMBOL(prom_setprop);
EXPORT_SYMBOL(saved_command_line);
EXPORT_SYMBOL(prom_apply_obio_ranges);
EXPORT_SYMBOL(prom_feval);
EXPORT_SYMBOL(prom_getbool);
EXPORT_SYMBOL(prom_getstring);
EXPORT_SYMBOL(prom_getint);
EXPORT_SYMBOL(prom_getintdefault);
EXPORT_SYMBOL(prom_finddevice);
EXPORT_SYMBOL(romvec);
EXPORT_SYMBOL(__prom_getchild);
EXPORT_SYMBOL(__prom_getsibling);

/* sparc library symbols */
EXPORT_SYMBOL(page_kernel);

/* Cache flushing.  */
EXPORT_SYMBOL(sparc_flush_page_to_ram);

/* For when serial stuff is built as modules. */
EXPORT_SYMBOL(sun_do_break);

EXPORT_SYMBOL(__ret_efault);

#ifdef CONFIG_DEBUG_BUGVERBOSE
EXPORT_SYMBOL(do_BUG);
#endif

/* Sun Power Management Idle Handler */
EXPORT_SYMBOL(pm_idle);

EXPORT_SYMBOL(empty_zero_page);
