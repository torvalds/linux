// SPDX-License-Identifier: GPL-2.0-only
/*
 * RISC-V crash hotplug support
 *
 * Copyright (C) 2025 Bytedance Ltd.
 */

#include <linux/kexec.h>
#include <linux/crash_core.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_CRASH_HOTPLUG

#undef pr_fmt
#define pr_fmt(fmt) "crash hp: " fmt

int arch_crash_hotplug_support(struct kimage *image, unsigned long kexec_flags)
{
#ifdef CONFIG_KEXEC_FILE
	if (image->file_mode)
		return 1;
#endif
	/*
	 * For the kexec_load() syscall path, the user space kexec tool
	 * needs to indicate that the elfcorehdr segment is excluded from
	 * SHA verification by setting the appropriate flags.
	 */
	return (kexec_flags & KEXEC_UPDATE_ELFCOREHDR ||
		kexec_flags & KEXEC_CRASH_HOTPLUG_SUPPORT);
}

unsigned int arch_crash_get_elfcorehdr_size(void)
{
	unsigned int sz;

	/* kernel_map, VMCOREINFO and maximum CPUs */
	sz = 2 + CONFIG_NR_CPUS;
	if (IS_ENABLED(CONFIG_MEMORY_HOTPLUG))
		sz += CONFIG_CRASH_MAX_MEMORY_RANGES;
	sz *= sizeof(Elf64_Phdr);
	return sz;
}

/**
 * arch_crash_handle_hotplug_event() - Handle hotplug elfcorehdr changes
 * @image: a pointer to kexec_crash_image
 * @arg: struct memory_notify handler for memory hotplug case and
 *       NULL for CPU hotplug case.
 *
 * Prepare the new elfcorehdr and replace the existing elfcorehdr.
 */
void arch_crash_handle_hotplug_event(struct kimage *image, void *arg)
{
	void *elfbuf = NULL, *old_elfcorehdr;
	unsigned long mem, memsz;
	unsigned long elfsz = 0;

	/*
	 * As crash_prepare_elf64_headers() has already described all
	 * possible CPUs, there is no need to update the elfcorehdr
	 * for additional CPU changes.
	 */
	if ((image->file_mode || image->elfcorehdr_updated) &&
		((image->hp_action == KEXEC_CRASH_HP_ADD_CPU) ||
		(image->hp_action == KEXEC_CRASH_HP_REMOVE_CPU)))
		return;

	/*
	 * Create the new elfcorehdr reflecting the changes to CPU and/or
	 * memory resources.
	 */
	if (crash_prepare_headers(true, &elfbuf, &elfsz, NULL)) {
		pr_err("unable to create new elfcorehdr");
		goto out;
	}

	/*
	 * Obtain address and size of the elfcorehdr segment, and
	 * check it against the new elfcorehdr buffer.
	 */
	mem = image->segment[image->elfcorehdr_index].mem;
	memsz = image->segment[image->elfcorehdr_index].memsz;
	if (elfsz > memsz) {
		pr_err("update elfcorehdr elfsz %lu > memsz %lu",
			elfsz, memsz);
		goto out;
	}

	/*
	 * Copy new elfcorehdr over the old elfcorehdr at destination.
	 */
	old_elfcorehdr = kmap_local_page(pfn_to_page(mem >> PAGE_SHIFT));
	if (!old_elfcorehdr) {
		pr_err("mapping elfcorehdr segment failed\n");
		goto out;
	}

	/*
	 * Temporarily invalidate the crash image while the
	 * elfcorehdr is updated.
	 */
	xchg(&kexec_crash_image, NULL);
	memcpy_flushcache(old_elfcorehdr, elfbuf, elfsz);
	xchg(&kexec_crash_image, image);
	kunmap_local(old_elfcorehdr);
	pr_debug("updated elfcorehdr\n");

out:
	vfree(elfbuf);
}

#endif /* CONFIG_CRASH_HOTPLUG */
