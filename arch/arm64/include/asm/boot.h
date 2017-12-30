/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_BOOT_H
#define __ASM_BOOT_H

#include <asm/sizes.h>

/*
 * arm64 requires the DTB to be 8 byte aligned and
 * not exceed 2MB in size.
 */
#define MIN_FDT_ALIGN		8
#define MAX_FDT_SIZE		SZ_2M

/*
 * arm64 requires the kernel image to placed
 * TEXT_OFFSET bytes beyond a 2 MB aligned base
 */
#define MIN_KIMG_ALIGN		SZ_2M

#endif
