/* arch/arm/mach-rk29/include/mach/loader.h
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __ASM_ARCH_RK29_LOADER_H
#define __ASM_ARCH_RK29_LOADER_H

#define SYS_LOADER_ERR_FLAG      0x1888AAFF
#define SYS_LOADER_REBOOT_FLAG   0x5242C300  //high 24 bits is tag, low 8 bits is type
#define SYS_KERNRL_REBOOT_FLAG   0xC3524200  //high 24 bits is tag, low 8 bits is type

enum {
    BOOT_NORMAL = 0,
    BOOT_LOADER,     /* enter loader rockusb mode */
    BOOT_MASKROM,    /* enter maskrom rockusb mode*/
    BOOT_RECOVER,    /* enter recover */
    BOOT_NORECOVER,  /* do not enter recover */
    BOOT_WINCE,      /* FOR OTHER SYSTEM */
    BOOT_WIPEDATA,   /* enter recover and wipe data. */
    BOOT_WIPEALL,    /* enter recover and wipe all data. */
    BOOT_CHECKIMG,   /* check firmware img with backup part(in loader mode)*/
    BOOT_MAX         /* MAX VALID BOOT TYPE.*/
};

#endif
