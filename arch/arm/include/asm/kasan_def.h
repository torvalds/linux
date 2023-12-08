/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  arch/arm/include/asm/kasan_def.h
 *
 *  Copyright (c) 2018 Huawei Technologies Co., Ltd.
 *
 *  Author: Abbott Liu <liuwenliang@huawei.com>
 */

#ifndef __ASM_KASAN_DEF_H
#define __ASM_KASAN_DEF_H

#ifdef CONFIG_KASAN

/*
 * Define KASAN_SHADOW_OFFSET,KASAN_SHADOW_START and KASAN_SHADOW_END for
 * the Arm kernel address sanitizer. We are "stealing" lowmem (the 4GB
 * addressable by a 32bit architecture) out of the virtual address
 * space to use as shadow memory for KASan as follows:
 *
 * +----+ 0xffffffff
 * |    |							\
 * |    | |-> Static kernel image (vmlinux) BSS and page table
 * |    |/
 * +----+ PAGE_OFFSET
 * |    |							\
 * |    | |->  Loadable kernel modules virtual address space area
 * |    |/
 * +----+ MODULES_VADDR = KASAN_SHADOW_END
 * |    |						\
 * |    | |-> The shadow area of kernel virtual address.
 * |    |/
 * +----+->  TASK_SIZE (start of kernel space) = KASAN_SHADOW_START the
 * |    |\   shadow address of MODULES_VADDR
 * |    | |
 * |    | |
 * |    | |-> The user space area in lowmem. The kernel address
 * |    | |   sanitizer do not use this space, nor does it map it.
 * |    | |
 * |    | |
 * |    | |
 * |    | |
 * |    |/
 * ------ 0
 *
 * 1) KASAN_SHADOW_START
 *   This value begins with the MODULE_VADDR's shadow address. It is the
 *   start of kernel virtual space. Since we have modules to load, we need
 *   to cover also that area with shadow memory so we can find memory
 *   bugs in modules.
 *
 * 2) KASAN_SHADOW_END
 *   This value is the 0x100000000's shadow address: the mapping that would
 *   be after the end of the kernel memory at 0xffffffff. It is the end of
 *   kernel address sanitizer shadow area. It is also the start of the
 *   module area.
 *
 * 3) KASAN_SHADOW_OFFSET:
 *   This value is used to map an address to the corresponding shadow
 *   address by the following formula:
 *
 *	shadow_addr = (address >> 3) + KASAN_SHADOW_OFFSET;
 *
 *  As you would expect, >> 3 is equal to dividing by 8, meaning each
 *  byte in the shadow memory covers 8 bytes of kernel memory, so one
 *  bit shadow memory per byte of kernel memory is used.
 *
 *  The KASAN_SHADOW_OFFSET is provided in a Kconfig option depending
 *  on the VMSPLIT layout of the system: the kernel and userspace can
 *  split up lowmem in different ways according to needs, so we calculate
 *  the shadow offset depending on this.
 */

#define KASAN_SHADOW_SCALE_SHIFT	3
#define KASAN_SHADOW_OFFSET	_AC(CONFIG_KASAN_SHADOW_OFFSET, UL)
#define KASAN_SHADOW_END	((UL(1) << (32 - KASAN_SHADOW_SCALE_SHIFT)) \
				 + KASAN_SHADOW_OFFSET)
#define KASAN_SHADOW_START      ((KASAN_SHADOW_END >> 3) + KASAN_SHADOW_OFFSET)

#endif
#endif
