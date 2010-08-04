/*
 *  linux/arch/i386/kernel/head32.c -- prepare to run common code
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright (C) 2007 Eric Biederman <ebiederm@xmission.com>
 */

#include <linux/init.h>
#include <linux/start_kernel.h>
#include <linux/mm.h>

#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/e820.h>
#include <asm/page.h>
#include <asm/trampoline.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/bios_ebda.h>

static void __init i386_default_early_setup(void)
{
	/* Initilize 32bit specific setup functions */
	x86_init.resources.probe_roms = probe_roms;
	x86_init.resources.reserve_resources = i386_reserve_resources;
	x86_init.mpparse.setup_ioapic_ids = setup_ioapic_ids_from_mpc;

	reserve_ebda_region();
}

void __init i386_start_kernel(void)
{
#ifdef CONFIG_X86_TRAMPOLINE
	/*
	 * But first pinch a few for the stack/trampoline stuff
	 * FIXME: Don't need the extra page at 4K, but need to fix
	 * trampoline before removing it. (see the GDT stuff)
	 */
	reserve_early_overlap_ok(PAGE_SIZE, PAGE_SIZE + PAGE_SIZE,
					 "EX TRAMPOLINE");
#endif

	reserve_early(__pa_symbol(&_text), __pa_symbol(&__bss_stop), "TEXT DATA BSS");

#ifdef CONFIG_BLK_DEV_INITRD
	/* Reserve INITRD */
	if (boot_params.hdr.type_of_loader && boot_params.hdr.ramdisk_image) {
		/* Assume only end is not page aligned */
		u64 ramdisk_image = boot_params.hdr.ramdisk_image;
		u64 ramdisk_size  = boot_params.hdr.ramdisk_size;
		u64 ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);
		reserve_early(ramdisk_image, ramdisk_end, "RAMDISK");
	}
#endif

	/* Call the subarch specific early setup function */
	switch (boot_params.hdr.hardware_subarch) {
	case X86_SUBARCH_MRST:
		x86_mrst_early_setup();
		break;
	default:
		i386_default_early_setup();
		break;
	}

	/*
	 * At this point everything still needed from the boot loader
	 * or BIOS or kernel text should be early reserved or marked not
	 * RAM in e820. All other memory is free game.
	 */

	start_kernel();
}
