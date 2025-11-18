/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kexec.h for kexec
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_KEXEC_H
#define _ASM_KEXEC_H

#include <asm/stacktrace.h>
#include <asm/page.h>

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
 /* Maximum address we can use for the control code buffer */
#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)

/* Reserve a page for the control code buffer */
#define KEXEC_CONTROL_PAGE_SIZE PAGE_SIZE

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_LOONGARCH

static inline void crash_setup_regs(struct pt_regs *newregs,
				    struct pt_regs *oldregs)
{
	if (oldregs)
		memcpy(newregs, oldregs, sizeof(*newregs));
	else
		prepare_frametrace(newregs);
}

#define ARCH_HAS_KIMAGE_ARCH

struct kimage_arch {
	unsigned long efi_boot;
	unsigned long cmdline_ptr;
	unsigned long systable_ptr;
};

#ifdef CONFIG_KEXEC_FILE
extern const struct kexec_file_ops kexec_efi_ops;
extern const struct kexec_file_ops kexec_elf_ops;

int arch_kimage_file_post_load_cleanup(struct kimage *image);
#define arch_kimage_file_post_load_cleanup arch_kimage_file_post_load_cleanup

extern int load_other_segments(struct kimage *image,
		unsigned long kernel_load_addr, unsigned long kernel_size,
		char *initrd, unsigned long initrd_len, char *cmdline, unsigned long cmdline_len);
#endif

typedef void (*do_kexec_t)(unsigned long efi_boot,
			   unsigned long cmdline_ptr,
			   unsigned long systable_ptr,
			   unsigned long start_addr,
			   unsigned long first_ind_entry);

struct kimage;
extern const unsigned char relocate_new_kernel[];
extern const size_t relocate_new_kernel_size;
extern void kexec_reboot(void);

#ifdef CONFIG_SMP
extern atomic_t kexec_ready_to_reboot;
extern const unsigned char kexec_smp_wait[];
#endif

#endif /* !_ASM_KEXEC_H */
