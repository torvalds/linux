/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * asm/bootinfo-virt.h -- Virtual-m68k-specific boot information definitions
 */

#ifndef _UAPI_ASM_M68K_BOOTINFO_VIRT_H
#define _UAPI_ASM_M68K_BOOTINFO_VIRT_H

#define BI_VIRT_QEMU_VERSION	0x8000
#define BI_VIRT_GF_PIC_BASE	0x8001
#define BI_VIRT_GF_RTC_BASE	0x8002
#define BI_VIRT_GF_TTY_BASE	0x8003
#define BI_VIRT_VIRTIO_BASE	0x8004
#define BI_VIRT_CTRL_BASE	0x8005

/* No longer used -- replaced with BI_RNG_SEED -- but don't reuse this index:
 * #define BI_VIRT_RNG_SEED	0x8006 */

#define VIRT_BOOTI_VERSION	MK_BI_VERSION(2, 0)

#endif /* _UAPI_ASM_M68K_BOOTINFO_MAC_H */
