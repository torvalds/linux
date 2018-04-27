/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2005
 *
 * Author(s): Rolf Adelsberger <adelsberger@de.ibm.com>
 *
 */

#ifndef _S390_KEXEC_H
#define _S390_KEXEC_H

#include <asm/processor.h>
#include <asm/page.h>
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
#define KEXEC_CONTROL_MEMORY_GFP GFP_DMA

/* Maximum address we can use for the crash control pages */
#define KEXEC_CRASH_CONTROL_MEMORY_LIMIT (-1UL)

/* Allocate one page for the pdp and the second for the code */
#define KEXEC_CONTROL_PAGE_SIZE 4096

/* Alignment of crashkernel memory */
#define KEXEC_CRASH_MEM_ALIGN HPAGE_SIZE

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_S390

/* Provide a dummy definition to avoid build failures. */
static inline void crash_setup_regs(struct pt_regs *newregs,
					struct pt_regs *oldregs) { }

struct kimage;
struct s390_load_data {
	/* Pointer to the kernel buffer. Used to register cmdline etc.. */
	void *kernel_buf;

	/* Total size of loaded segments in memory. Used as an offset. */
	size_t memsz;

	/* Load address of initrd. Used to register INITRD_START in kernel. */
	unsigned long initrd_load_addr;
};

int kexec_file_add_purgatory(struct kimage *image,
			     struct s390_load_data *data);
int kexec_file_add_initrd(struct kimage *image,
			  struct s390_load_data *data,
			  char *initrd, unsigned long initrd_len);
int *kexec_file_update_kernel(struct kimage *iamge,
			      struct s390_load_data *data);

extern const struct kexec_file_ops s390_kexec_image_ops;
extern const struct kexec_file_ops s390_kexec_elf_ops;

#endif /*_S390_KEXEC_H */
