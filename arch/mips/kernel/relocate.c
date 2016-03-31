/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Support for Kernel relocation at boot time
 *
 * Copyright (C) 2015, Imagination Technologies Ltd.
 * Authors: Matt Redfearn (matt.redfearn@imgtec.com)
 */
#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/timex.h>
#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/start_kernel.h>
#include <linux/string.h>

#define RELOCATED(x) ((void *)((long)x + offset))

extern u32 _relocation_start[];	/* End kernel image / start relocation table */
extern u32 _relocation_end[];	/* End relocation table */

extern long __start___ex_table;	/* Start exception table */
extern long __stop___ex_table;	/* End exception table */

static inline u32 __init get_synci_step(void)
{
	u32 res;

	__asm__("rdhwr  %0, $1" : "=r" (res));

	return res;
}

static void __init sync_icache(void *kbase, unsigned long kernel_length)
{
	void *kend = kbase + kernel_length;
	u32 step = get_synci_step();

	do {
		__asm__ __volatile__(
			"synci  0(%0)"
			: /* no output */
			: "r" (kbase));

		kbase += step;
	} while (kbase < kend);

	/* Completion barrier */
	__sync();
}

static int __init apply_r_mips_64_rel(u32 *loc_orig, u32 *loc_new, long offset)
{
	*(u64 *)loc_new += offset;

	return 0;
}

static int __init apply_r_mips_32_rel(u32 *loc_orig, u32 *loc_new, long offset)
{
	*loc_new += offset;

	return 0;
}

static int __init apply_r_mips_26_rel(u32 *loc_orig, u32 *loc_new, long offset)
{
	unsigned long target_addr = (*loc_orig) & 0x03ffffff;

	if (offset % 4) {
		pr_err("Dangerous R_MIPS_26 REL relocation\n");
		return -ENOEXEC;
	}

	/* Original target address */
	target_addr <<= 2;
	target_addr += (unsigned long)loc_orig & ~0x03ffffff;

	/* Get the new target address */
	target_addr += offset;

	if ((target_addr & 0xf0000000) != ((unsigned long)loc_new & 0xf0000000)) {
		pr_err("R_MIPS_26 REL relocation overflow\n");
		return -ENOEXEC;
	}

	target_addr -= (unsigned long)loc_new & ~0x03ffffff;
	target_addr >>= 2;

	*loc_new = (*loc_new & ~0x03ffffff) | (target_addr & 0x03ffffff);

	return 0;
}


static int __init apply_r_mips_hi16_rel(u32 *loc_orig, u32 *loc_new, long offset)
{
	unsigned long insn = *loc_orig;
	unsigned long target = (insn & 0xffff) << 16; /* high 16bits of target */

	target += offset;

	*loc_new = (insn & ~0xffff) | ((target >> 16) & 0xffff);
	return 0;
}

static int (*reloc_handlers_rel[]) (u32 *, u32 *, long) __initdata = {
	[R_MIPS_64]		= apply_r_mips_64_rel,
	[R_MIPS_32]		= apply_r_mips_32_rel,
	[R_MIPS_26]		= apply_r_mips_26_rel,
	[R_MIPS_HI16]		= apply_r_mips_hi16_rel,
};

int __init do_relocations(void *kbase_old, void *kbase_new, long offset)
{
	u32 *r;
	u32 *loc_orig;
	u32 *loc_new;
	int type;
	int res;

	for (r = _relocation_start; r < _relocation_end; r++) {
		/* Sentinel for last relocation */
		if (*r == 0)
			break;

		type = (*r >> 24) & 0xff;
		loc_orig = (void *)(kbase_old + ((*r & 0x00ffffff) << 2));
		loc_new = RELOCATED(loc_orig);

		if (reloc_handlers_rel[type] == NULL) {
			/* Unsupported relocation */
			pr_err("Unhandled relocation type %d at 0x%pK\n",
			       type, loc_orig);
			return -ENOEXEC;
		}

		res = reloc_handlers_rel[type](loc_orig, loc_new, offset);
		if (res)
			return res;
	}

	return 0;
}

/*
 * The exception table is filled in by the relocs tool after vmlinux is linked.
 * It must be relocated separately since there will not be any relocation
 * information for it filled in by the linker.
 */
static int __init relocate_exception_table(long offset)
{
	unsigned long *etable_start, *etable_end, *e;

	etable_start = RELOCATED(&__start___ex_table);
	etable_end = RELOCATED(&__stop___ex_table);

	for (e = etable_start; e < etable_end; e++)
		*e += offset;

	return 0;
}

static inline void __init *determine_relocation_address(void)
{
	/*
	 * Choose a new address for the kernel
	 * For now we'll hard code the destination
	 */
	return (void *)0xffffffff81000000;
}

static inline int __init relocation_addr_valid(void *loc_new)
{
	if ((unsigned long)loc_new & 0x0000ffff) {
		/* Inappropriately aligned new location */
		return 0;
	}
	if ((unsigned long)loc_new < (unsigned long)&_end) {
		/* New location overlaps original kernel */
		return 0;
	}
	return 1;
}

void *__init relocate_kernel(void)
{
	void *loc_new;
	unsigned long kernel_length;
	unsigned long bss_length;
	long offset = 0;
	int res = 1;
	/* Default to original kernel entry point */
	void *kernel_entry = start_kernel;

	kernel_length = (long)(&_relocation_start) - (long)(&_text);
	bss_length = (long)&__bss_stop - (long)&__bss_start;

	loc_new = determine_relocation_address();

	/* Sanity check relocation address */
	if (relocation_addr_valid(loc_new))
		offset = (unsigned long)loc_new - (unsigned long)(&_text);

	if (offset) {
		/* Copy the kernel to it's new location */
		memcpy(loc_new, &_text, kernel_length);

		/* Perform relocations on the new kernel */
		res = do_relocations(&_text, loc_new, offset);
		if (res < 0)
			goto out;

		/* Sync the caches ready for execution of new kernel */
		sync_icache(loc_new, kernel_length);

		res = relocate_exception_table(offset);
		if (res < 0)
			goto out;

		/*
		 * The original .bss has already been cleared, and
		 * some variables such as command line parameters
		 * stored to it so make a copy in the new location.
		 */
		memcpy(RELOCATED(&__bss_start), &__bss_start, bss_length);

		/* The current thread is now within the relocated image */
		__current_thread_info = RELOCATED(&init_thread_union);

		/* Return the new kernel's entry point */
		kernel_entry = RELOCATED(start_kernel);
	}
out:
	return kernel_entry;
}
