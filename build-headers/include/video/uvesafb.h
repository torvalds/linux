/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UVESAFB_H
#define _UVESAFB_H

#include <linux/types.h>

struct v86_regs {
	__u32 ebx;
	__u32 ecx;
	__u32 edx;
	__u32 esi;
	__u32 edi;
	__u32 ebp;
	__u32 eax;
	__u32 eip;
	__u32 eflags;
	__u32 esp;
	__u16 cs;
	__u16 ss;
	__u16 es;
	__u16 ds;
	__u16 fs;
	__u16 gs;
};

/* Task flags */
#define TF_VBEIB	0x01
#define TF_BUF_ESDI	0x02
#define TF_BUF_ESBX	0x04
#define TF_BUF_RET	0x08
#define TF_EXIT		0x10

struct uvesafb_task {
	__u8 flags;
	int buf_len;
	struct v86_regs regs;
};

/* Constants for the capabilities field
 * in vbe_ib */
#define VBE_CAP_CAN_SWITCH_DAC	0x01
#define VBE_CAP_VGACOMPAT	0x02

/* The VBE Info Block */
struct vbe_ib {
	char  vbe_signature[4];
	__u16 vbe_version;
	__u32 oem_string_ptr;
	__u32 capabilities;
	__u32 mode_list_ptr;
	__u16 total_memory;
	__u16 oem_software_rev;
	__u32 oem_vendor_name_ptr;
	__u32 oem_product_name_ptr;
	__u32 oem_product_rev_ptr;
	__u8  reserved[222];
	char  oem_data[256];
	char  misc_data[512];
} __attribute__ ((packed));

#endif /* _UVESAFB_H */
