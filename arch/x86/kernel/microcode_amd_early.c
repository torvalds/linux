/*
 * Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 * Author: Jacob Shin <jacob.shin@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/earlycpio.h>
#include <linux/initrd.h>

#include <asm/cpu.h>
#include <asm/setup.h>
#include <asm/microcode_amd.h>

static bool ucode_loaded;
static u32 ucode_new_rev;
static unsigned long ucode_offset;
static size_t ucode_size;

/*
 * Microcode patch container file is prepended to the initrd in cpio format.
 * See Documentation/x86/early-microcode.txt
 */
static __initdata char ucode_path[] = "kernel/x86/microcode/AuthenticAMD.bin";

static struct cpio_data __init find_ucode_in_initrd(void)
{
	long offset = 0;
	char *path;
	void *start;
	size_t size;
	unsigned long *uoffset;
	size_t *usize;
	struct cpio_data cd;

#ifdef CONFIG_X86_32
	struct boot_params *p;

	/*
	 * On 32-bit, early load occurs before paging is turned on so we need
	 * to use physical addresses.
	 */
	p       = (struct boot_params *)__pa_nodebug(&boot_params);
	path    = (char *)__pa_nodebug(ucode_path);
	start   = (void *)p->hdr.ramdisk_image;
	size    = p->hdr.ramdisk_size;
	uoffset = (unsigned long *)__pa_nodebug(&ucode_offset);
	usize   = (size_t *)__pa_nodebug(&ucode_size);
#else
	path    = ucode_path;
	start   = (void *)(boot_params.hdr.ramdisk_image + PAGE_OFFSET);
	size    = boot_params.hdr.ramdisk_size;
	uoffset = &ucode_offset;
	usize   = &ucode_size;
#endif

	cd = find_cpio_data(path, start, size, &offset);
	if (!cd.data)
		return cd;

	if (*(u32 *)cd.data != UCODE_MAGIC) {
		cd.data = NULL;
		cd.size = 0;
		return cd;
	}

	*uoffset = (u8 *)cd.data - (u8 *)start;
	*usize   = cd.size;

	return cd;
}

/*
 * Early load occurs before we can vmalloc(). So we look for the microcode
 * patch container file in initrd, traverse equivalent cpu table, look for a
 * matching microcode patch, and update, all in initrd memory in place.
 * When vmalloc() is available for use later -- on 64-bit during first AP load,
 * and on 32-bit during save_microcode_in_initrd_amd() -- we can call
 * load_microcode_amd() to save equivalent cpu table and microcode patches in
 * kernel heap memory.
 */
static void apply_ucode_in_initrd(void *ucode, size_t size)
{
	struct equiv_cpu_entry *eq;
	u32 *header;
	u8  *data;
	u16 eq_id = 0;
	int offset, left;
	u32 rev, eax;
	u32 *new_rev;
	unsigned long *uoffset;
	size_t *usize;

#ifdef CONFIG_X86_32
	new_rev = (u32 *)__pa_nodebug(&ucode_new_rev);
	uoffset = (unsigned long *)__pa_nodebug(&ucode_offset);
	usize   = (size_t *)__pa_nodebug(&ucode_size);
#else
	new_rev = &ucode_new_rev;
	uoffset = &ucode_offset;
	usize   = &ucode_size;
#endif

	data   = ucode;
	left   = size;
	header = (u32 *)data;

	/* find equiv cpu table */

	if (header[1] != UCODE_EQUIV_CPU_TABLE_TYPE || /* type */
	    header[2] == 0)                            /* size */
		return;

	eax = cpuid_eax(0x00000001);

	while (left > 0) {
		eq = (struct equiv_cpu_entry *)(data + CONTAINER_HDR_SZ);

		offset = header[2] + CONTAINER_HDR_SZ;
		data  += offset;
		left  -= offset;

		eq_id = find_equiv_id(eq, eax);
		if (eq_id)
			break;

		/*
		 * support multiple container files appended together. if this
		 * one does not have a matching equivalent cpu entry, we fast
		 * forward to the next container file.
		 */
		while (left > 0) {
			header = (u32 *)data;
			if (header[0] == UCODE_MAGIC &&
			    header[1] == UCODE_EQUIV_CPU_TABLE_TYPE)
				break;

			offset = header[1] + SECTION_HDR_SIZE;
			data  += offset;
			left  -= offset;
		}

		/* mark where the next microcode container file starts */
		offset    = data - (u8 *)ucode;
		*uoffset += offset;
		*usize   -= offset;
		ucode     = data;
	}

	if (!eq_id) {
		*usize = 0;
		return;
	}

	/* find ucode and update if needed */

	rdmsr(MSR_AMD64_PATCH_LEVEL, rev, eax);

	while (left > 0) {
		struct microcode_amd *mc;

		header = (u32 *)data;
		if (header[0] != UCODE_UCODE_TYPE || /* type */
		    header[1] == 0)                  /* size */
			break;

		mc = (struct microcode_amd *)(data + SECTION_HDR_SIZE);
		if (eq_id == mc->hdr.processor_rev_id && rev < mc->hdr.patch_id)
			if (__apply_microcode_amd(mc) == 0) {
				rev = mc->hdr.patch_id;
				*new_rev = rev;
			}

		offset  = header[1] + SECTION_HDR_SIZE;
		data   += offset;
		left   -= offset;
	}

	/* mark where this microcode container file ends */
	offset  = *usize - (data - (u8 *)ucode);
	*usize -= offset;

	if (!(*new_rev))
		*usize = 0;
}

void __init load_ucode_amd_bsp(void)
{
	struct cpio_data cd = find_ucode_in_initrd();
	if (!cd.data)
		return;

	apply_ucode_in_initrd(cd.data, cd.size);
}

#ifdef CONFIG_X86_32
u8 amd_bsp_mpb[MPB_MAX_SIZE];

/*
 * On 32-bit, since AP's early load occurs before paging is turned on, we
 * cannot traverse cpu_equiv_table and pcache in kernel heap memory. So during
 * cold boot, AP will apply_ucode_in_initrd() just like the BSP. During
 * save_microcode_in_initrd_amd() BSP's patch is copied to amd_bsp_mpb, which
 * is used upon resume from suspend.
 */
void load_ucode_amd_ap(void)
{
	struct microcode_amd *mc;
	unsigned long *initrd;
	unsigned long *uoffset;
	size_t *usize;
	void *ucode;

	mc = (struct microcode_amd *)__pa(amd_bsp_mpb);
	if (mc->hdr.patch_id && mc->hdr.processor_rev_id) {
		__apply_microcode_amd(mc);
		return;
	}

	initrd  = (unsigned long *)__pa(&initrd_start);
	uoffset = (unsigned long *)__pa(&ucode_offset);
	usize   = (size_t *)__pa(&ucode_size);

	if (!*usize || !*initrd)
		return;

	ucode = (void *)((unsigned long)__pa(*initrd) + *uoffset);
	apply_ucode_in_initrd(ucode, *usize);
}

static void __init collect_cpu_sig_on_bsp(void *arg)
{
	unsigned int cpu = smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	uci->cpu_sig.sig = cpuid_eax(0x00000001);
}
#else
static void collect_cpu_info_amd_early(struct cpuinfo_x86 *c,
						 struct ucode_cpu_info *uci)
{
	u32 rev, eax;

	rdmsr(MSR_AMD64_PATCH_LEVEL, rev, eax);
	eax = cpuid_eax(0x00000001);

	uci->cpu_sig.sig = eax;
	uci->cpu_sig.rev = rev;
	c->microcode = rev;
	c->x86 = ((eax >> 8) & 0xf) + ((eax >> 20) & 0xff);
}

void load_ucode_amd_ap(void)
{
	unsigned int cpu = smp_processor_id();

	collect_cpu_info_amd_early(&cpu_data(cpu), ucode_cpu_info + cpu);

	if (cpu && !ucode_loaded) {
		void *ucode;

		if (!ucode_size || !initrd_start)
			return;

		ucode = (void *)(initrd_start + ucode_offset);
		if (load_microcode_amd(0, ucode, ucode_size) != UCODE_OK)
			return;
		ucode_loaded = true;
	}

	apply_microcode_amd(cpu);
}
#endif

int __init save_microcode_in_initrd_amd(void)
{
	enum ucode_state ret;
	void *ucode;
#ifdef CONFIG_X86_32
	unsigned int bsp = boot_cpu_data.cpu_index;
	struct ucode_cpu_info *uci = ucode_cpu_info + bsp;

	if (!uci->cpu_sig.sig)
		smp_call_function_single(bsp, collect_cpu_sig_on_bsp, NULL, 1);
#endif
	if (ucode_new_rev)
		pr_info("microcode: updated early to new patch_level=0x%08x\n",
			ucode_new_rev);

	if (ucode_loaded || !ucode_size || !initrd_start)
		return 0;

	ucode = (void *)(initrd_start + ucode_offset);
	ret = load_microcode_amd(0, ucode, ucode_size);
	if (ret != UCODE_OK)
		return -EINVAL;

	ucode_loaded = true;
	return 0;
}
