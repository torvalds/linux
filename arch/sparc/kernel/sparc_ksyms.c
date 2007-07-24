/* $Id: sparc_ksyms.c,v 1.107 2001/07/17 16:17:33 anton Exp $
 * arch/sparc/kernel/ksyms.c: Sparc specific ksyms support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 */

/* Tell string.h we don't want memcpy etc. as cpp defines */
#define EXPORT_SYMTAB_STROPS
#define PROMLIB_INTERNAL

#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/string.h>
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
#include <asm/svr4.h>
#include <asm/head.h>
#include <asm/smp.h>
#include <asm/mostek.h>
#include <asm/ptrace.h>
#include <asm/user.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#ifdef CONFIG_SBUS
#include <asm/sbus.h>
#include <asm/dma.h>
#endif
#ifdef CONFIG_PCI
#include <asm/ebus.h>
#endif
#include <asm/a.out.h>
#include <asm/io-unit.h>
#include <asm/bug.h>

extern spinlock_t rtc_lock;

struct poll {
	int fd;
	short events;
	short revents;
};

extern int svr4_getcontext (svr4_ucontext_t *, struct pt_regs *);
extern int svr4_setcontext (svr4_ucontext_t *, struct pt_regs *);
extern void (*__copy_1page)(void *, const void *);
extern void __memmove(void *, const void *, __kernel_size_t);
extern void (*bzero_1page)(void *);
extern void *__bzero(void *, size_t);
extern void *__memscan_zero(void *, size_t);
extern void *__memscan_generic(void *, int, size_t);
extern int __memcmp(const void *, const void *, __kernel_size_t);
extern int __strncmp(const char *, const char *, __kernel_size_t);

extern int __ashrdi3(int, int);
extern int __ashldi3(int, int);
extern int __lshrdi3(int, int);
extern int __muldi3(int, int);
extern int __divdi3(int, int);

/* Private functions with odd calling conventions. */
extern void ___atomic24_add(void);
extern void ___atomic24_sub(void);
extern void ___rw_read_enter(void);
extern void ___rw_read_try(void);
extern void ___rw_read_exit(void);
extern void ___rw_write_enter(void);

/* Alias functions whose names begin with "." and export the aliases.
 * The module references will be fixed up by module_frob_arch_sections.
 */
extern int _Div(int, int);
extern int _Mul(int, int);
extern int _Rem(int, int);
extern unsigned _Udiv(unsigned, unsigned);
extern unsigned _Umul(unsigned, unsigned);
extern unsigned _Urem(unsigned, unsigned);

/* used by various drivers */
EXPORT_SYMBOL(sparc_cpu_model);
EXPORT_SYMBOL(kernel_thread);
#ifdef CONFIG_SMP
// XXX find what uses (or used) these.   AV: see asm/spinlock.h
EXPORT_SYMBOL(___rw_read_enter);
EXPORT_SYMBOL(___rw_read_try);
EXPORT_SYMBOL(___rw_read_exit);
EXPORT_SYMBOL(___rw_write_enter);
#endif
/* semaphores */
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__down_trylock);
EXPORT_SYMBOL(__down_interruptible);

EXPORT_SYMBOL(sparc_valid_addr_bitmap);
EXPORT_SYMBOL(phys_base);
EXPORT_SYMBOL(pfn_base);

/* Atomic operations. */
EXPORT_SYMBOL(___atomic24_add);
EXPORT_SYMBOL(___atomic24_sub);

/* Per-CPU information table */
EXPORT_PER_CPU_SYMBOL(__cpu_data);

#ifdef CONFIG_SMP
/* IRQ implementation. */
EXPORT_SYMBOL(synchronize_irq);

/* CPU online map and active count. */
EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(phys_cpu_present_map);
#endif

EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__ndelay);
EXPORT_SYMBOL(rtc_lock);
EXPORT_SYMBOL(mostek_lock);
EXPORT_SYMBOL(mstk48t02_regs);
#ifdef CONFIG_SUN_AUXIO
EXPORT_SYMBOL(set_auxio);
EXPORT_SYMBOL(get_auxio);
#endif
EXPORT_SYMBOL(request_fast_irq);
EXPORT_SYMBOL(io_remap_pfn_range);
  /* P3: iounit_xxx may be needed, sun4d users */
/* EXPORT_SYMBOL(iounit_map_dma_init); */
/* EXPORT_SYMBOL(iounit_map_dma_page); */

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
EXPORT_SYMBOL(sbus_root);
EXPORT_SYMBOL(dma_chain);
EXPORT_SYMBOL(sbus_set_sbus64);
EXPORT_SYMBOL(sbus_alloc_consistent);
EXPORT_SYMBOL(sbus_free_consistent);
EXPORT_SYMBOL(sbus_map_single);
EXPORT_SYMBOL(sbus_unmap_single);
EXPORT_SYMBOL(sbus_map_sg);
EXPORT_SYMBOL(sbus_unmap_sg);
EXPORT_SYMBOL(sbus_dma_sync_single_for_cpu);
EXPORT_SYMBOL(sbus_dma_sync_single_for_device);
EXPORT_SYMBOL(sbus_dma_sync_sg_for_cpu);
EXPORT_SYMBOL(sbus_dma_sync_sg_for_device);
EXPORT_SYMBOL(sbus_iounmap);
EXPORT_SYMBOL(sbus_ioremap);
#endif
#ifdef CONFIG_PCI
EXPORT_SYMBOL(ebus_chain);
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

/* Solaris/SunOS binary compatibility */
EXPORT_SYMBOL(svr4_setcontext);
EXPORT_SYMBOL(svr4_getcontext);

EXPORT_SYMBOL(dump_thread);

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
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(page_kernel);

/* Special internal versions of library functions. */
EXPORT_SYMBOL(__copy_1page);
EXPORT_SYMBOL(__memcpy);
EXPORT_SYMBOL(__memset);
EXPORT_SYMBOL(bzero_1page);
EXPORT_SYMBOL(__bzero);
EXPORT_SYMBOL(__memscan_zero);
EXPORT_SYMBOL(__memscan_generic);
EXPORT_SYMBOL(__memcmp);
EXPORT_SYMBOL(__strncmp);
EXPORT_SYMBOL(__memmove);

/* Moving data to/from userspace. */
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__strnlen_user);

/* Networking helper routines. */
EXPORT_SYMBOL(__csum_partial_copy_sparc_generic);
EXPORT_SYMBOL(csum_partial);

/* Cache flushing.  */
EXPORT_SYMBOL(sparc_flush_page_to_ram);

/* For when serial stuff is built as modules. */
EXPORT_SYMBOL(sun_do_break);

EXPORT_SYMBOL(__ret_efault);

EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__muldi3);
EXPORT_SYMBOL(__divdi3);

EXPORT_SYMBOL(_Rem);
EXPORT_SYMBOL(_Urem);
EXPORT_SYMBOL(_Mul);
EXPORT_SYMBOL(_Umul);
EXPORT_SYMBOL(_Div);
EXPORT_SYMBOL(_Udiv);

#ifdef CONFIG_DEBUG_BUGVERBOSE
EXPORT_SYMBOL(do_BUG);
#endif

/* Sun Power Management Idle Handler */
EXPORT_SYMBOL(pm_idle);
