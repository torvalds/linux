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

#include <asm/cpu.h>
#include <asm/setup.h>
#include <asm/microcode_amd.h>

static bool ucode_loaded;
static u32 ucode_new_rev;

/*
 * Microcode patch container file is prepended to the initrd in cpio format.
 * See Documentation/x86/early-microcode.txt
 */
static __initdata char ucode_path[] = "kernel/x86/microcode/AuthenticAMD.bin";

static struct cpio_data __init find_ucode_in_initrd(void)
{
	long offset = 0;
	struct cpio_data cd;

#ifdef CONFIG_X86_32
	/*
	 * On 32-bit, early load occurs before paging is turned on so we need
	 * to use physical addresses.
	 */
	if (!(read_cr0() & X86_CR0_PG)) {
		struct boot_params *p;
		p  = (struct boot_params *)__pa_nodebug(&boot_params);
		cd = find_cpio_data((char *)__pa_nodebug(ucode_path),
			(void *)p->hdr.ramdisk_image, p->hdr.ramdisk_size,
			&offset);
	} else
#endif
		cd = find_cpio_data(ucode_path,
			(void *)(boot_params.hdr.ramdisk_image + PAGE_OFFSET),
			boot_params.hdr.ramdisk_size, &offset);

	if (*(u32 *)cd.data != UCODE_MAGIC) {
		cd.data = NULL;
		cd.size = 0;
	}

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
static void __init apply_ucode_in_initrd(void)
{
	struct cpio_data cd;
	struct equiv_cpu_entry *eq;
	u32 *header;
	u8  *data;
	u16 eq_id;
	int offset, left;
	u32 rev, dummy;
	u32 *new_rev;

#ifdef CONFIG_X86_32
	new_rev = (u32 *)__pa_nodebug(&ucode_new_rev);
#else
	new_rev = &ucode_new_rev;
#endif
	cd = find_ucode_in_initrd();
	if (!cd.data)
		return;

	data   = cd.data;
	left   = cd.size;
	header = (u32 *)data;

	/* find equiv cpu table */

	if (header[1] != UCODE_EQUIV_CPU_TABLE_TYPE || /* type */
	    header[2] == 0)                            /* size */
		return;

	eq     = (struct equiv_cpu_entry *)(data + CONTAINER_HDR_SZ);
	offset = header[2] + CONTAINER_HDR_SZ;
	data  += offset;
	left  -= offset;

	eq_id  = find_equiv_id(eq, cpuid_eax(0x00000001));
	if (!eq_id)
		return;

	/* find ucode and update if needed */

	rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);

	while (left > 0) {
		struct microcode_amd *mc;

		header = (u32 *)data;
		if (header[0] != UCODE_UCODE_TYPE || /* type */
		    header[1] == 0)                  /* size */
			break;

		mc = (struct microcode_amd *)(data + SECTION_HDR_SIZE);
		if (eq_id == mc->hdr.processor_rev_id && rev < mc->hdr.patch_id)
			if (__apply_microcode_amd(mc) == 0) {
				if (!(*new_rev))
					*new_rev = mc->hdr.patch_id;
				break;
			}

		offset  = header[1] + SECTION_HDR_SIZE;
		data   += offset;
		left   -= offset;
	}
}

void __init load_ucode_amd_bsp(void)
{
	apply_ucode_in_initrd();
}

#ifdef CONFIG_X86_32
u8 __cpuinitdata amd_bsp_mpb[MPB_MAX_SIZE];

/*
 * On 32-bit, since AP's early load occurs before paging is turned on, we
 * cannot traverse cpu_equiv_table and pcache in kernel heap memory. So during
 * cold boot, AP will apply_ucode_in_initrd() just like the BSP. During
 * save_microcode_in_initrd_amd() BSP's patch is copied to amd_bsp_mpb, which
 * is used upon resume from suspend.
 */
void __cpuinit load_ucode_amd_ap(void)
{
	struct microcode_amd *mc;

	mc = (struct microcode_amd *)__pa_nodebug(amd_bsp_mpb);
	if (mc->hdr.patch_id && mc->hdr.processor_rev_id)
		__apply_microcode_amd(mc);
	else
		apply_ucode_in_initrd();
}

static void __init collect_cpu_sig_on_bsp(void *arg)
{
	unsigned int cpu = smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	uci->cpu_sig.sig = cpuid_eax(0x00000001);
}
#else
static void __cpuinit collect_cpu_info_amd_early(struct cpuinfo_x86 *c,
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

void __cpuinit load_ucode_amd_ap(void)
{
	unsigned int cpu = smp_processor_id();

	collect_cpu_info_amd_early(&cpu_data(cpu), ucode_cpu_info + cpu);

	if (cpu && !ucode_loaded) {
		struct cpio_data cd = find_ucode_in_initrd();
		if (load_microcode_amd(0, cd.data, cd.size) != UCODE_OK)
			return;
		ucode_loaded = true;
	}

	apply_microcode_amd(cpu);
}
#endif

int __init save_microcode_in_initrd_amd(void)
{
	enum ucode_state ret;
	struct cpio_data cd;
#ifdef CONFIG_X86_32
	unsigned int bsp = boot_cpu_data.cpu_index;
	struct ucode_cpu_info *uci = ucode_cpu_info + bsp;

	if (!uci->cpu_sig.sig)
		smp_call_function_single(bsp, collect_cpu_sig_on_bsp, NULL, 1);
#endif
	if (ucode_new_rev)
		pr_info("microcode: updated early to new patch_level=0x%08x\n",
			ucode_new_rev);

	if (ucode_loaded)
		return 0;

	cd = find_ucode_in_initrd();
	if (!cd.data)
		return -EINVAL;

	ret = load_microcode_amd(0, cd.data, cd.size);
	if (ret != UCODE_OK)
		return -EINVAL;

	ucode_loaded = true;
	return 0;
}
