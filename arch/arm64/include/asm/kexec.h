/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * kexec for arm64
 *
 * Copyright (C) Linaro.
 * Copyright (C) Huawei Futurewei Technologies.
 */

#ifndef _ARM64_KEXEC_H
#define _ARM64_KEXEC_H

/* Maximum physical address we can use pages from */

#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)

/* Maximum address we can reach in physical address mode */

#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)

/* Maximum address we can use for the control code buffer */

#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)

#define KEXEC_CONTROL_PAGE_SIZE 4096

#define KEXEC_ARCH KEXEC_ARCH_AARCH64

#ifndef __ASSEMBLY__

/**
 * crash_setup_regs() - save registers for the panic kernel
 *
 * @newregs: registers are saved here
 * @oldregs: registers to be saved (may be %NULL)
 */

static inline void crash_setup_regs(struct pt_regs *newregs,
				    struct pt_regs *oldregs)
{
	if (oldregs) {
		memcpy(newregs, oldregs, sizeof(*newregs));
	} else {
		u64 tmp1, tmp2;

		__asm__ __volatile__ (
			"stp	 x0,   x1, [%2, #16 *  0]\n"
			"stp	 x2,   x3, [%2, #16 *  1]\n"
			"stp	 x4,   x5, [%2, #16 *  2]\n"
			"stp	 x6,   x7, [%2, #16 *  3]\n"
			"stp	 x8,   x9, [%2, #16 *  4]\n"
			"stp	x10,  x11, [%2, #16 *  5]\n"
			"stp	x12,  x13, [%2, #16 *  6]\n"
			"stp	x14,  x15, [%2, #16 *  7]\n"
			"stp	x16,  x17, [%2, #16 *  8]\n"
			"stp	x18,  x19, [%2, #16 *  9]\n"
			"stp	x20,  x21, [%2, #16 * 10]\n"
			"stp	x22,  x23, [%2, #16 * 11]\n"
			"stp	x24,  x25, [%2, #16 * 12]\n"
			"stp	x26,  x27, [%2, #16 * 13]\n"
			"stp	x28,  x29, [%2, #16 * 14]\n"
			"mov	 %0,  sp\n"
			"stp	x30,  %0,  [%2, #16 * 15]\n"

			"/* faked current PSTATE */\n"
			"mrs	 %0, CurrentEL\n"
			"mrs	 %1, SPSEL\n"
			"orr	 %0, %0, %1\n"
			"mrs	 %1, DAIF\n"
			"orr	 %0, %0, %1\n"
			"mrs	 %1, NZCV\n"
			"orr	 %0, %0, %1\n"
			/* pc */
			"adr	 %1, 1f\n"
		"1:\n"
			"stp	 %1, %0,   [%2, #16 * 16]\n"
			: "=&r" (tmp1), "=&r" (tmp2)
			: "r" (newregs)
			: "memory"
		);
	}
}

#if defined(CONFIG_KEXEC_CORE) && defined(CONFIG_HIBERNATION)
extern bool crash_is_nosave(unsigned long pfn);
extern void crash_prepare_suspend(void);
extern void crash_post_resume(void);
#else
static inline bool crash_is_nosave(unsigned long pfn) {return false; }
static inline void crash_prepare_suspend(void) {}
static inline void crash_post_resume(void) {}
#endif

#if defined(CONFIG_KEXEC_CORE)
void cpu_soft_restart(unsigned long el2_switch, unsigned long entry,
		      unsigned long arg0, unsigned long arg1,
		      unsigned long arg2);
#endif

#define ARCH_HAS_KIMAGE_ARCH

struct kimage_arch {
	void *dtb;
	phys_addr_t dtb_mem;
	phys_addr_t kern_reloc;
	phys_addr_t el2_vectors;
	phys_addr_t ttbr0;
	phys_addr_t ttbr1;
	phys_addr_t zero_page;
	unsigned long phys_offset;
	unsigned long t0sz;
};

#ifdef CONFIG_KEXEC_FILE
extern const struct kexec_file_ops kexec_image_ops;

struct kimage;

extern int arch_kimage_file_post_load_cleanup(struct kimage *image);
extern int load_other_segments(struct kimage *image,
		unsigned long kernel_load_addr, unsigned long kernel_size,
		char *initrd, unsigned long initrd_len,
		char *cmdline);
#endif

#endif /* __ASSEMBLY__ */

#endif
