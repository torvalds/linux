/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  Copyright IBM Corp. 2022
 *  Author(s): Steffen Eiden <seiden@linux.ibm.com>
 */
#ifndef __S390_ASM_UVDEVICE_H
#define __S390_ASM_UVDEVICE_H

#include <linux/types.h>

struct uvio_ioctl_cb {
	__u32 flags;
	__u16 uv_rc;			/* UV header rc value */
	__u16 uv_rrc;			/* UV header rrc value */
	__u64 argument_addr;		/* Userspace address of uvio argument */
	__u32 argument_len;
	__u8  reserved14[0x40 - 0x14];	/* must be zero */
};

#define UVIO_ATT_USER_DATA_LEN		0x100
#define UVIO_ATT_UID_LEN		0x10
struct uvio_attest {
	__u64 arcb_addr;				/* 0x0000 */
	__u64 meas_addr;				/* 0x0008 */
	__u64 add_data_addr;				/* 0x0010 */
	__u8  user_data[UVIO_ATT_USER_DATA_LEN];	/* 0x0018 */
	__u8  config_uid[UVIO_ATT_UID_LEN];		/* 0x0118 */
	__u32 arcb_len;					/* 0x0128 */
	__u32 meas_len;					/* 0x012c */
	__u32 add_data_len;				/* 0x0130 */
	__u16 user_data_len;				/* 0x0134 */
	__u16 reserved136;				/* 0x0136 */
};

/*
 * The following max values define an upper length for the IOCTL in/out buffers.
 * However, they do not represent the maximum the Ultravisor allows which is
 * often way smaller. By allowing larger buffer sizes we hopefully do not need
 * to update the code with every machine update. It is therefore possible for
 * userspace to request more memory than actually used by kernel/UV.
 */
#define UVIO_ATT_ARCB_MAX_LEN		0x100000
#define UVIO_ATT_MEASUREMENT_MAX_LEN	0x8000
#define UVIO_ATT_ADDITIONAL_MAX_LEN	0x8000

#define UVIO_DEVICE_NAME "uv"
#define UVIO_TYPE_UVC 'u'

#define UVIO_IOCTL_ATT _IOWR(UVIO_TYPE_UVC, 0x01, struct uvio_ioctl_cb)

#endif /* __S390_ASM_UVDEVICE_H */
