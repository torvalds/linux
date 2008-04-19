/* arch/sparc64/kernel/sparc64_ksyms.c: Sparc64 specific ksyms support.
 *
 * Copyright (C) 1996, 2007 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 * Copyright (C) 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

/* Tell string.h we don't want memcpy etc. as cpp defines */
#define EXPORT_SYMTAB_STROPS
#define PROMLIB_INTERNAL

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/fs_struct.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/syscalls.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/rwsem.h>
#include <net/compat.h>

#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/auxio.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/idprom.h>
#include <asm/svr4.h>
#include <asm/elf.h>
#include <asm/head.h>
#include <asm/smp.h>
#include <asm/mostek.h>
#include <asm/ptrace.h>
#include <asm/user.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/fpumacro.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#ifdef CONFIG_SBUS
#include <asm/sbus.h>
#include <asm/dma.h>
#endif
#ifdef CONFIG_PCI
#include <asm/ebus.h>
#include <asm/isa.h>
#endif
#include <asm/ns87303.h>
#include <asm/timer.h>
#include <asm/cpudata.h>

struct poll {
	int fd;
	short events;
	short revents;
};

extern void die_if_kernel(char *str, struct pt_regs *regs);
extern pid_t kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);
extern void *__bzero(void *, size_t);
extern void *__memscan_zero(void *, size_t);
extern void *__memscan_generic(void *, int, size_t);
extern int __memcmp(const void *, const void *, __kernel_size_t);
extern __kernel_size_t strlen(const char *);
extern void linux_sparc_syscall(void);
extern void rtrap(void);
extern void show_regs(struct pt_regs *);
extern void solaris_syscall(void);
extern void syscall_trace(struct pt_regs *, int);
extern u32 sunos_sys_table[], sys_call_table32[];
extern void tl0_solaris(void);
extern void sys_sigsuspend(void);
extern int svr4_getcontext(svr4_ucontext_t *uc, struct pt_regs *regs);
extern int svr4_setcontext(svr4_ucontext_t *uc, struct pt_regs *regs);
extern int compat_sys_ioctl(unsigned int fd, unsigned int cmd, u32 arg);
extern int (*handle_mathemu)(struct pt_regs *, struct fpustate *);
extern long sparc32_open(const char __user * filename, int flags, int mode);
extern int io_remap_pfn_range(struct vm_area_struct *vma, unsigned long from,
	unsigned long pfn, unsigned long size, pgprot_t prot);

extern int __ashrdi3(int, int);

extern int dump_fpu (struct pt_regs * regs, elf_fpregset_t * fpregs);

extern unsigned int sys_call_table[];

extern void xor_vis_2(unsigned long, unsigned long *, unsigned long *);
extern void xor_vis_3(unsigned long, unsigned long *, unsigned long *,
		      unsigned long *);
extern void xor_vis_4(unsigned long, unsigned long *, unsigned long *,
		      unsigned long *, unsigned long *);
extern void xor_vis_5(unsigned long, unsigned long *, unsigned long *,
		      unsigned long *, unsigned long *, unsigned long *);

extern void xor_niagara_2(unsigned long, unsigned long *, unsigned long *);
extern void xor_niagara_3(unsigned long, unsigned long *, unsigned long *,
			  unsigned long *);
extern void xor_niagara_4(unsigned long, unsigned long *, unsigned long *,
			  unsigned long *, unsigned long *);
extern void xor_niagara_5(unsigned long, unsigned long *, unsigned long *,
			  unsigned long *, unsigned long *, unsigned long *);

/* Per-CPU information table */
EXPORT_PER_CPU_SYMBOL(__cpu_data);

/* used by various drivers */
#ifdef CONFIG_SMP
/* Out of line rw-locking implementation. */
EXPORT_SYMBOL(__read_lock);
EXPORT_SYMBOL(__read_unlock);
EXPORT_SYMBOL(__write_lock);
EXPORT_SYMBOL(__write_unlock);
EXPORT_SYMBOL(__write_trylock);

EXPORT_SYMBOL(smp_call_function);
#endif /* CONFIG_SMP */

#if defined(CONFIG_MCOUNT)
extern void _mcount(void);
EXPORT_SYMBOL(_mcount);
#endif

EXPORT_SYMBOL(sparc64_get_clock_tick);

/* RW semaphores */
EXPORT_SYMBOL(__down_read);
EXPORT_SYMBOL(__down_read_trylock);
EXPORT_SYMBOL(__down_write);
EXPORT_SYMBOL(__down_write_trylock);
EXPORT_SYMBOL(__up_read);
EXPORT_SYMBOL(__up_write);
EXPORT_SYMBOL(__downgrade_write);

/* Atomic counter implementation. */
EXPORT_SYMBOL(atomic_add);
EXPORT_SYMBOL(atomic_add_ret);
EXPORT_SYMBOL(atomic_sub);
EXPORT_SYMBOL(atomic_sub_ret);
EXPORT_SYMBOL(atomic64_add);
EXPORT_SYMBOL(atomic64_add_ret);
EXPORT_SYMBOL(atomic64_sub);
EXPORT_SYMBOL(atomic64_sub_ret);

/* Atomic bit operations. */
EXPORT_SYMBOL(test_and_set_bit);
EXPORT_SYMBOL(test_and_clear_bit);
EXPORT_SYMBOL(test_and_change_bit);
EXPORT_SYMBOL(set_bit);
EXPORT_SYMBOL(clear_bit);
EXPORT_SYMBOL(change_bit);

EXPORT_SYMBOL(__flushw_user);

EXPORT_SYMBOL(tlb_type);
EXPORT_SYMBOL(sun4v_chip_type);
EXPORT_SYMBOL(get_fb_unmapped_area);
EXPORT_SYMBOL(flush_icache_range);

EXPORT_SYMBOL(flush_dcache_page);
#ifdef DCACHE_ALIASING_POSSIBLE
EXPORT_SYMBOL(__flush_dcache_range);
#endif

EXPORT_SYMBOL(mostek_lock);
EXPORT_SYMBOL(mstk48t02_regs);
#ifdef CONFIG_SUN_AUXIO
EXPORT_SYMBOL(auxio_set_led);
EXPORT_SYMBOL(auxio_set_lte);
#endif
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
#endif
EXPORT_SYMBOL(outsb);
EXPORT_SYMBOL(outsw);
EXPORT_SYMBOL(outsl);
EXPORT_SYMBOL(insb);
EXPORT_SYMBOL(insw);
EXPORT_SYMBOL(insl);
#ifdef CONFIG_PCI
EXPORT_SYMBOL(ebus_chain);
EXPORT_SYMBOL(isa_chain);
EXPORT_SYMBOL(pci_alloc_consistent);
EXPORT_SYMBOL(pci_free_consistent);
EXPORT_SYMBOL(pci_map_single);
EXPORT_SYMBOL(pci_unmap_single);
EXPORT_SYMBOL(pci_map_sg);
EXPORT_SYMBOL(pci_unmap_sg);
EXPORT_SYMBOL(pci_dma_sync_single_for_cpu);
EXPORT_SYMBOL(pci_dma_sync_sg_for_cpu);
EXPORT_SYMBOL(pci_dma_supported);
#endif

/* I/O device mmaping on Sparc64. */
EXPORT_SYMBOL(io_remap_pfn_range);

#if defined(CONFIG_COMPAT) && defined(CONFIG_NET)
/* Solaris/SunOS binary compatibility */
EXPORT_SYMBOL(verify_compat_iovec);
#endif

EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(put_fs_struct);

/* math-emu wants this */
EXPORT_SYMBOL(die_if_kernel);

/* Kernel thread creation. */
EXPORT_SYMBOL(kernel_thread);

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
EXPORT_SYMBOL(prom_finddevice);
EXPORT_SYMBOL(prom_feval);
EXPORT_SYMBOL(prom_getbool);
EXPORT_SYMBOL(prom_getstring);
EXPORT_SYMBOL(prom_getint);
EXPORT_SYMBOL(prom_getintdefault);
EXPORT_SYMBOL(__prom_getchild);
EXPORT_SYMBOL(__prom_getsibling);

/* sparc library symbols */
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(__strlen_user);
EXPORT_SYMBOL(__strnlen_user);

#ifdef CONFIG_SOLARIS_EMUL_MODULE
EXPORT_SYMBOL(linux_sparc_syscall);
EXPORT_SYMBOL(rtrap);
EXPORT_SYMBOL(show_regs);
EXPORT_SYMBOL(solaris_syscall);
EXPORT_SYMBOL(syscall_trace);
EXPORT_SYMBOL(sunos_sys_table);
EXPORT_SYMBOL(sys_call_table32);
EXPORT_SYMBOL(tl0_solaris);
EXPORT_SYMBOL(sys_sigsuspend);
EXPORT_SYMBOL(sys_getppid);
EXPORT_SYMBOL(sys_getpid);
EXPORT_SYMBOL(sys_geteuid);
EXPORT_SYMBOL(sys_getuid);
EXPORT_SYMBOL(sys_getegid);
EXPORT_SYMBOL(sysctl_nr_open);
EXPORT_SYMBOL(sys_getgid);
EXPORT_SYMBOL(svr4_getcontext);
EXPORT_SYMBOL(svr4_setcontext);
EXPORT_SYMBOL(compat_sys_ioctl);
EXPORT_SYMBOL(sys_ioctl);
EXPORT_SYMBOL(sparc32_open);
#endif

/* Special internal versions of library functions. */
EXPORT_SYMBOL(_clear_page);
EXPORT_SYMBOL(clear_user_page);
EXPORT_SYMBOL(copy_user_page);
EXPORT_SYMBOL(__bzero);
EXPORT_SYMBOL(__memscan_zero);
EXPORT_SYMBOL(__memscan_generic);
EXPORT_SYMBOL(__memcmp);
EXPORT_SYMBOL(__memset);

EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy_nocheck);
EXPORT_SYMBOL(__csum_partial_copy_from_user);
EXPORT_SYMBOL(__csum_partial_copy_to_user);
EXPORT_SYMBOL(ip_fast_csum);

/* Moving data to/from/in userspace. */
EXPORT_SYMBOL(___copy_to_user);
EXPORT_SYMBOL(___copy_from_user);
EXPORT_SYMBOL(___copy_in_user);
EXPORT_SYMBOL(copy_to_user_fixup);
EXPORT_SYMBOL(copy_from_user_fixup);
EXPORT_SYMBOL(copy_in_user_fixup);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__clear_user);

/* Various address conversion macros use this. */
EXPORT_SYMBOL(sparc64_valid_addr_bitmap);

/* No version information on this, heavily used in inline asm,
 * and will always be 'void __ret_efault(void)'.
 */
EXPORT_SYMBOL(__ret_efault);

/* No version information on these, as gcc produces such symbols. */
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(strncmp);

void VISenter(void);
/* RAID code needs this */
EXPORT_SYMBOL(VISenter);

/* for input/keybdev */
EXPORT_SYMBOL(sun_do_break);
EXPORT_SYMBOL(stop_a_enabled);

#ifdef CONFIG_DEBUG_BUGVERBOSE
EXPORT_SYMBOL(do_BUG);
#endif

/* for ns8703 */
EXPORT_SYMBOL(ns87303_lock);

/* for solaris compat module */
EXPORT_SYMBOL_GPL(sys_call_table);

EXPORT_SYMBOL(tick_ops);

EXPORT_SYMBOL(xor_vis_2);
EXPORT_SYMBOL(xor_vis_3);
EXPORT_SYMBOL(xor_vis_4);
EXPORT_SYMBOL(xor_vis_5);

EXPORT_SYMBOL(xor_niagara_2);
EXPORT_SYMBOL(xor_niagara_3);
EXPORT_SYMBOL(xor_niagara_4);
EXPORT_SYMBOL(xor_niagara_5);
