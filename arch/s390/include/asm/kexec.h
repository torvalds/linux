/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2005
 *
 * Author(s): Rolf Adelsberger <adelsberger@de.ibm.com>
 *
 */

#ifndef _S390_KEXEC_H
#define _S390_KEXEC_H

#include <linux/module.h>

#include <asm/processor.h>
#include <asm/page.h>
#include <asm/setup.h>
/*
 * KEXEC_SOURCE_MEMORY_LIMIT maximum page get_free_page can return.
 * I.e. Maximum page that is mapped directly into kernel memory,
 * and kmap is not required.
 */

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)

/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)

/* Maximum address we can use for the control pages */
/* Not more than 2GB */
#define KEXEC_CONTROL_MEMORY_LIMIT (1UL<<31)

/* Allocate control page with GFP_DMA */
#define KEXEC_CONTROL_MEMORY_GFP (GFP_DMA | __GFP_NORETRY)

/* Maximum address we can use for the crash control pages */
#define KEXEC_CRASH_CONTROL_MEMORY_LIMIT (-1UL)

/* Allocate one page for the pdp and the second for the code */
#define KEXEC_CONTROL_PAGE_SIZE 4096

/* Alignment of crashkernel memory */
#define KEXEC_CRASH_MEM_ALIGN HPAGE_SIZE

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_S390

/* Allow kexec_file to load a segment to 0 */
#define KEXEC_BUF_MEM_UNKNOWN -1

/* Provide a dummy definition to avoid build failures. */
static inline void crash_setup_regs(struct pt_regs *newregs,
					struct pt_regs *oldregs) { }

struct kimage;
struct s390_load_data {
	/* Pointer to the kernel buffer. Used to register cmdline etc.. */
	void *kernel_buf;

	/* Load address of the kernel_buf. */
	unsigned long kernel_mem;

	/* Parmarea in the kernel buffer. */
	struct parmarea *parm;

	/* Total size of loaded segments in memory. Used as an offset. */
	size_t memsz;

	struct ipl_report *report;
};

int s390_verify_sig(const char *kernel, unsigned long kernel_len);
void *kexec_file_add_components(struct kimage *image,
				int (*add_kernel)(struct kimage *image,
						  struct s390_load_data *data));
int arch_kexec_do_relocs(int r_type, void *loc, unsigned long val,
			 unsigned long addr);

#define ARCH_HAS_KIMAGE_ARCH

struct kimage_arch {
	void *ipl_buf;
};

extern const struct kexec_file_ops s390_kexec_image_ops;
extern const struct kexec_file_ops s390_kexec_elf_ops;

#ifdef CONFIG_KEXEC_FILE
struct purgatory_info;
int arch_kexec_apply_relocations_add(struct purgatory_info *pi,
				     Elf_Shdr *section,
				     const Elf_Shdr *relsec,
				     const Elf_Shdr *symtab);
#define arch_kexec_apply_relocations_add arch_kexec_apply_relocations_add
#endif
#endif /*_S390_KEXEC_H */
