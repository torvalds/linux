/*
 * Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 * Author: Jacob Shin <jacob.shin@amd.com>
 * Fixes: Borislav Petkov <bp@suse.de>
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

/*
 * This points to the current valid container of microcode patches which we will
 * save from the initrd before jettisoning its contents.
 */
static u8 *container;
static size_t container_size;

static u32 ucode_new_rev;
u8 amd_ucode_patch[PATCH_MAX_SIZE];
static u16 this_equiv_id;

struct cpio_data ucode_cpio;

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
#else
	path    = ucode_path;
	start   = (void *)(boot_params.hdr.ramdisk_image + PAGE_OFFSET);
	size    = boot_params.hdr.ramdisk_size;
#endif

	return find_cpio_data(path, start, size, &offset);
}

static size_t compute_container_size(u8 *data, u32 total_size)
{
	size_t size = 0;
	u32 *header = (u32 *)data;

	if (header[0] != UCODE_MAGIC ||
	    header[1] != UCODE_EQUIV_CPU_TABLE_TYPE || /* type */
	    header[2] == 0)                            /* size */
		return size;

	size = header[2] + CONTAINER_HDR_SZ;
	total_size -= size;
	data += size;

	while (total_size) {
		u16 patch_size;

		header = (u32 *)data;

		if (header[0] != UCODE_UCODE_TYPE)
			break;

		/*
		 * Sanity-check patch size.
		 */
		patch_size = header[1];
		if (patch_size > PATCH_MAX_SIZE)
			break;

		size	   += patch_size + SECTION_HDR_SIZE;
		data	   += patch_size + SECTION_HDR_SIZE;
		total_size -= patch_size + SECTION_HDR_SIZE;
	}

	return size;
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
	size_t *cont_sz;
	u32 *header;
	u8  *data, **cont;
	u16 eq_id = 0;
	int offset, left;
	u32 rev, eax, ebx, ecx, edx;
	u32 *new_rev;

#ifdef CONFIG_X86_32
	new_rev = (u32 *)__pa_nodebug(&ucode_new_rev);
	cont_sz = (size_t *)__pa_nodebug(&container_size);
	cont	= (u8 **)__pa_nodebug(&container);
#else
	new_rev = &ucode_new_rev;
	cont_sz = &container_size;
	cont	= &container;
#endif

	data   = ucode;
	left   = size;
	header = (u32 *)data;

	/* find equiv cpu table */
	if (header[0] != UCODE_MAGIC ||
	    header[1] != UCODE_EQUIV_CPU_TABLE_TYPE || /* type */
	    header[2] == 0)                            /* size */
		return;

	eax = 0x00000001;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);

	while (left > 0) {
		eq = (struct equiv_cpu_entry *)(data + CONTAINER_HDR_SZ);

		*cont = data;

		/* Advance past the container header */
		offset = header[2] + CONTAINER_HDR_SZ;
		data  += offset;
		left  -= offset;

		eq_id = find_equiv_id(eq, eax);
		if (eq_id) {
			this_equiv_id = eq_id;
			*cont_sz = compute_container_size(*cont, left + offset);

			/*
			 * truncate how much we need to iterate over in the
			 * ucode update loop below
			 */
			left = *cont_sz - offset;
			break;
		}

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
		ucode     = data;
	}

	if (!eq_id) {
		*cont = NULL;
		*cont_sz = 0;
		return;
	}

	/* find ucode and update if needed */

	native_rdmsr(MSR_AMD64_PATCH_LEVEL, rev, eax);

	while (left > 0) {
		struct microcode_amd *mc;

		header = (u32 *)data;
		if (header[0] != UCODE_UCODE_TYPE || /* type */
		    header[1] == 0)                  /* size */
			break;

		mc = (struct microcode_amd *)(data + SECTION_HDR_SIZE);

		if (eq_id == mc->hdr.processor_rev_id && rev < mc->hdr.patch_id) {

			if (!__apply_microcode_amd(mc)) {
				rev = mc->hdr.patch_id;
				*new_rev = rev;

				/* save ucode patch */
				memcpy(amd_ucode_patch, mc,
				       min_t(u32, header[1], PATCH_MAX_SIZE));
			}
		}

		offset  = header[1] + SECTION_HDR_SIZE;
		data   += offset;
		left   -= offset;
	}
}

void __init load_ucode_amd_bsp(void)
{
	struct cpio_data cp;
	void **data;
	size_t *size;

#ifdef CONFIG_X86_32
	data =  (void **)__pa_nodebug(&ucode_cpio.data);
	size = (size_t *)__pa_nodebug(&ucode_cpio.size);
#else
	data = &ucode_cpio.data;
	size = &ucode_cpio.size;
#endif

	cp = find_ucode_in_initrd();
	if (!cp.data)
		return;

	*data = cp.data;
	*size = cp.size;

	apply_ucode_in_initrd(cp.data, cp.size);
}

#ifdef CONFIG_X86_32
/*
 * On 32-bit, since AP's early load occurs before paging is turned on, we
 * cannot traverse cpu_equiv_table and pcache in kernel heap memory. So during
 * cold boot, AP will apply_ucode_in_initrd() just like the BSP. During
 * save_microcode_in_initrd_amd() BSP's patch is copied to amd_ucode_patch,
 * which is used upon resume from suspend.
 */
void load_ucode_amd_ap(void)
{
	struct microcode_amd *mc;
	size_t *usize;
	void **ucode;

	mc = (struct microcode_amd *)__pa(amd_ucode_patch);
	if (mc->hdr.patch_id && mc->hdr.processor_rev_id) {
		__apply_microcode_amd(mc);
		return;
	}

	ucode = (void *)__pa_nodebug(&container);
	usize = (size_t *)__pa_nodebug(&container_size);

	if (!*ucode || !*usize)
		return;

	apply_ucode_in_initrd(*ucode, *usize);
}

static void __init collect_cpu_sig_on_bsp(void *arg)
{
	unsigned int cpu = smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	uci->cpu_sig.sig = cpuid_eax(0x00000001);
}

static void __init get_bsp_sig(void)
{
	unsigned int bsp = boot_cpu_data.cpu_index;
	struct ucode_cpu_info *uci = ucode_cpu_info + bsp;

	if (!uci->cpu_sig.sig)
		smp_call_function_single(bsp, collect_cpu_sig_on_bsp, NULL, 1);
}
#else
void load_ucode_amd_ap(void)
{
	unsigned int cpu = smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	struct equiv_cpu_entry *eq;
	struct microcode_amd *mc;
	u32 rev, eax;
	u16 eq_id;

	/* Exit if called on the BSP. */
	if (!cpu)
		return;

	if (!container)
		return;

	rdmsr(MSR_AMD64_PATCH_LEVEL, rev, eax);

	uci->cpu_sig.rev = rev;
	uci->cpu_sig.sig = eax;

	eax = cpuid_eax(0x00000001);
	eq  = (struct equiv_cpu_entry *)(container + CONTAINER_HDR_SZ);

	eq_id = find_equiv_id(eq, eax);
	if (!eq_id)
		return;

	if (eq_id == this_equiv_id) {
		mc = (struct microcode_amd *)amd_ucode_patch;

		if (mc && rev < mc->hdr.patch_id) {
			if (!__apply_microcode_amd(mc))
				ucode_new_rev = mc->hdr.patch_id;
		}

	} else {
		if (!ucode_cpio.data)
			return;

		/*
		 * AP has a different equivalence ID than BSP, looks like
		 * mixed-steppings silicon so go through the ucode blob anew.
		 */
		apply_ucode_in_initrd(ucode_cpio.data, ucode_cpio.size);
	}
}
#endif

int __init save_microcode_in_initrd_amd(void)
{
	unsigned long cont;
	enum ucode_state ret;
	u32 eax;

	if (!container)
		return -EINVAL;

#ifdef CONFIG_X86_32
	get_bsp_sig();
	cont = (unsigned long)container;
#else
	/*
	 * We need the physical address of the container for both bitness since
	 * boot_params.hdr.ramdisk_image is a physical address.
	 */
	cont = __pa(container);
#endif

	/*
	 * Take into account the fact that the ramdisk might get relocated and
	 * therefore we need to recompute the container's position in virtual
	 * memory space.
	 */
	if (relocated_ramdisk)
		container = (u8 *)(__va(relocated_ramdisk) +
			     (cont - boot_params.hdr.ramdisk_image));

	if (ucode_new_rev)
		pr_info("microcode: updated early to new patch_level=0x%08x\n",
			ucode_new_rev);

	eax   = cpuid_eax(0x00000001);
	eax   = ((eax >> 8) & 0xf) + ((eax >> 20) & 0xff);

	ret = load_microcode_amd(eax, container, container_size);
	if (ret != UCODE_OK)
		return -EINVAL;

	/*
	 * This will be freed any msec now, stash patches for the current
	 * family and switch to patch cache for cpu hotplug, etc later.
	 */
	container = NULL;
	container_size = 0;

	return 0;
}
