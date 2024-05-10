/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KEXEC_H
#define _ASM_POWERPC_KEXEC_H
#ifdef __KERNEL__

#if defined(CONFIG_PPC_85xx) || defined(CONFIG_44x)

/*
 * On FSL-BookE we setup a 1:1 mapping which covers the first 2GiB of memory
 * and therefore we can only deal with memory within this range
 */
#define KEXEC_SOURCE_MEMORY_LIMIT	(2 * 1024 * 1024 * 1024UL - 1)
#define KEXEC_DESTINATION_MEMORY_LIMIT	(2 * 1024 * 1024 * 1024UL - 1)
#define KEXEC_CONTROL_MEMORY_LIMIT	(2 * 1024 * 1024 * 1024UL - 1)

#else

/*
 * Maximum page that is mapped directly into kernel memory.
 * XXX: Since we copy virt we can use any page we allocate
 */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)

/*
 * Maximum address we can reach in physical address mode.
 * XXX: I want to allow initrd in highmem. Otherwise set to rmo on LPAR.
 */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)

/* Maximum address we can use for the control code buffer */
#ifdef __powerpc64__
#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)
#else
/* TASK_SIZE, probably left over from use_mm ?? */
#define KEXEC_CONTROL_MEMORY_LIMIT TASK_SIZE
#endif
#endif

#define KEXEC_CONTROL_PAGE_SIZE 4096

/* The native architecture */
#ifdef __powerpc64__
#define KEXEC_ARCH KEXEC_ARCH_PPC64
#else
#define KEXEC_ARCH KEXEC_ARCH_PPC
#endif

#define KEXEC_STATE_NONE 0
#define KEXEC_STATE_IRQS_OFF 1
#define KEXEC_STATE_REAL_MODE 2

#ifndef __ASSEMBLY__
#include <asm/reg.h>

typedef void (*crash_shutdown_t)(void);

#ifdef CONFIG_KEXEC_CORE
struct kimage;
struct pt_regs;

extern void kexec_smp_wait(void);	/* get and clear naca physid, wait for
					  master to copy new code to 0 */
extern void default_machine_kexec(struct kimage *image);
extern void machine_kexec_mask_interrupts(void);

void relocate_new_kernel(unsigned long indirection_page, unsigned long reboot_code_buffer,
			 unsigned long start_address) __noreturn;
void kexec_copy_flush(struct kimage *image);

#ifdef CONFIG_KEXEC_FILE
extern const struct kexec_file_ops kexec_elf64_ops;

#define ARCH_HAS_KIMAGE_ARCH

struct kimage_arch {
	struct crash_mem *exclude_ranges;

	unsigned long backup_start;
	void *backup_buf;
	void *fdt;
};

char *setup_kdump_cmdline(struct kimage *image, char *cmdline,
			  unsigned long cmdline_len);
int setup_purgatory(struct kimage *image, const void *slave_code,
		    const void *fdt, unsigned long kernel_load_addr,
		    unsigned long fdt_load_addr);

#ifdef CONFIG_PPC64
struct kexec_buf;

int arch_kexec_kernel_image_probe(struct kimage *image, void *buf, unsigned long buf_len);
#define arch_kexec_kernel_image_probe arch_kexec_kernel_image_probe

int arch_kimage_file_post_load_cleanup(struct kimage *image);
#define arch_kimage_file_post_load_cleanup arch_kimage_file_post_load_cleanup

int arch_kexec_locate_mem_hole(struct kexec_buf *kbuf);
#define arch_kexec_locate_mem_hole arch_kexec_locate_mem_hole

int load_crashdump_segments_ppc64(struct kimage *image,
				  struct kexec_buf *kbuf);
int setup_purgatory_ppc64(struct kimage *image, const void *slave_code,
			  const void *fdt, unsigned long kernel_load_addr,
			  unsigned long fdt_load_addr);
unsigned int kexec_extra_fdt_size_ppc64(struct kimage *image, struct crash_mem *rmem);
int setup_new_fdt_ppc64(const struct kimage *image, void *fdt, struct crash_mem *rmem);
#endif /* CONFIG_PPC64 */

#endif /* CONFIG_KEXEC_FILE */

#endif /* CONFIG_KEXEC_CORE */

#ifdef CONFIG_CRASH_RESERVE
int __init overlaps_crashkernel(unsigned long start, unsigned long size);
extern void reserve_crashkernel(void);
#else
static inline void reserve_crashkernel(void) {}
static inline int overlaps_crashkernel(unsigned long start, unsigned long size) { return 0; }
#endif

#if defined(CONFIG_CRASH_DUMP)
/*
 * This function is responsible for capturing register states if coming
 * via panic or invoking dump using sysrq-trigger.
 */
static inline void crash_setup_regs(struct pt_regs *newregs,
					struct pt_regs *oldregs)
{
	if (oldregs)
		memcpy(newregs, oldregs, sizeof(*newregs));
	else
		ppc_save_regs(newregs);
}

#ifdef CONFIG_CRASH_HOTPLUG
void arch_crash_handle_hotplug_event(struct kimage *image, void *arg);
#define arch_crash_handle_hotplug_event arch_crash_handle_hotplug_event

int arch_crash_hotplug_support(struct kimage *image, unsigned long kexec_flags);
#define arch_crash_hotplug_support arch_crash_hotplug_support

unsigned int arch_crash_get_elfcorehdr_size(void);
#define crash_get_elfcorehdr_size arch_crash_get_elfcorehdr_size
#endif /* CONFIG_CRASH_HOTPLUG */

extern int crashing_cpu;
extern void crash_send_ipi(void (*crash_ipi_callback)(struct pt_regs *));
extern void crash_ipi_callback(struct pt_regs *regs);
extern int crash_wake_offline;

extern int crash_shutdown_register(crash_shutdown_t handler);
extern int crash_shutdown_unregister(crash_shutdown_t handler);
extern void default_machine_crash_shutdown(struct pt_regs *regs);

extern void crash_kexec_prepare(void);
extern void crash_kexec_secondary(struct pt_regs *regs);

static inline bool kdump_in_progress(void)
{
	return crashing_cpu >= 0;
}

bool is_kdump_kernel(void);
#define is_kdump_kernel			is_kdump_kernel
#if defined(CONFIG_PPC_RTAS)
void crash_free_reserved_phys_range(unsigned long begin, unsigned long end);
#define crash_free_reserved_phys_range crash_free_reserved_phys_range
#endif /* CONFIG_PPC_RTAS */

#else /* !CONFIG_CRASH_DUMP */
static inline void crash_kexec_secondary(struct pt_regs *regs) { }

static inline int crash_shutdown_register(crash_shutdown_t handler)
{
	return 0;
}

static inline int crash_shutdown_unregister(crash_shutdown_t handler)
{
	return 0;
}

static inline bool kdump_in_progress(void)
{
	return false;
}

static inline void crash_ipi_callback(struct pt_regs *regs) { }

static inline void crash_send_ipi(void (*crash_ipi_callback)(struct pt_regs *))
{
}

#endif /* CONFIG_CRASH_DUMP */

#if defined(CONFIG_KEXEC_FILE) || defined(CONFIG_CRASH_DUMP)
int update_cpus_node(void *fdt);
#endif

#ifdef CONFIG_PPC_BOOK3S_64
#include <asm/book3s/64/kexec.h>
#endif

#ifndef reset_sprs
#define reset_sprs reset_sprs
static inline void reset_sprs(void)
{
}
#endif

#endif /* ! __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_KEXEC_H */
