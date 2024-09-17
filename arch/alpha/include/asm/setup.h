/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ALPHA_SETUP_H
#define __ALPHA_SETUP_H

#include <uapi/asm/setup.h>

/*
 * We leave one page for the initial stack page, and one page for
 * the initial process structure. Also, the console eats 3 MB for
 * the initial bootloader (one of which we can reclaim later).
 */
#define BOOT_PCB	0x20000000
#define BOOT_ADDR	0x20000000
/* Remove when official MILO sources have ELF support: */
#define BOOT_SIZE	(16*1024)

#ifdef CONFIG_ALPHA_LEGACY_START_ADDRESS
#define KERNEL_START_PHYS	0x300000 /* Old bootloaders hardcoded this.  */
#else
#define KERNEL_START_PHYS	0x1000000 /* required: Wildfire/Titan/Marvel */
#endif

#define KERNEL_START	(PAGE_OFFSET+KERNEL_START_PHYS)
#define SWAPPER_PGD	KERNEL_START
#define INIT_STACK	(PAGE_OFFSET+KERNEL_START_PHYS+0x02000)
#define EMPTY_PGT	(PAGE_OFFSET+KERNEL_START_PHYS+0x04000)
#define EMPTY_PGE	(PAGE_OFFSET+KERNEL_START_PHYS+0x08000)
#define ZERO_PGE	(PAGE_OFFSET+KERNEL_START_PHYS+0x0A000)

#define START_ADDR	(PAGE_OFFSET+KERNEL_START_PHYS+0x10000)

/*
 * This is setup by the secondary bootstrap loader.  Because
 * the zero page is zeroed out as soon as the vm system is
 * initialized, we need to copy things out into a more permanent
 * place.
 */
#define PARAM			ZERO_PGE
#define COMMAND_LINE		((char *)(absolute_pointer(PARAM + 0x0000)))
#define INITRD_START		(*(unsigned long *) (PARAM+0x100))
#define INITRD_SIZE		(*(unsigned long *) (PARAM+0x108))

#endif
