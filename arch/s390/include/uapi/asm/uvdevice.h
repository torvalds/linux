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

/**
 * uvio_uvdev_info - Information of supported functions
 * @supp_uvio_cmds - supported IOCTLs by this device
 * @supp_uv_cmds - supported UVCs corresponding to the IOCTL
 *
 * UVIO request to get information about supported request types by this
 * uvdevice and the Ultravisor.  Everything is output. Bits are in LSB0
 * ordering.  If the bit is set in both, @supp_uvio_cmds and @supp_uv_cmds, the
 * uvdevice and the Ultravisor support that call.
 *
 * Note that bit 0 (UVIO_IOCTL_UVDEV_INFO_NR) is always zero for `supp_uv_cmds`
 * as there is no corresponding UV-call.
 */
struct uvio_uvdev_info {
	/*
	 * If bit `n` is set, this device supports the IOCTL with nr `n`.
	 */
	__u64 supp_uvio_cmds;
	/*
	 * If bit `n` is set, the Ultravisor(UV) supports the UV-call
	 * corresponding to the IOCTL with nr `n` in the calling contextx (host
	 * or guest).  The value is only valid if the corresponding bit in
	 * @supp_uvio_cmds is set as well.
	 */
	__u64 supp_uv_cmds;
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
#define UVIO_ADD_SECRET_MAX_LEN		0x100000
#define UVIO_LIST_SECRETS_LEN		0x1000

#define UVIO_DEVICE_NAME "uv"
#define UVIO_TYPE_UVC 'u'

enum UVIO_IOCTL_NR {
	UVIO_IOCTL_UVDEV_INFO_NR = 0x00,
	UVIO_IOCTL_ATT_NR,
	UVIO_IOCTL_ADD_SECRET_NR,
	UVIO_IOCTL_LIST_SECRETS_NR,
	UVIO_IOCTL_LOCK_SECRETS_NR,
	/* must be the last entry */
	UVIO_IOCTL_NUM_IOCTLS
};

#define UVIO_IOCTL(nr)		_IOWR(UVIO_TYPE_UVC, nr, struct uvio_ioctl_cb)
#define UVIO_IOCTL_UVDEV_INFO	UVIO_IOCTL(UVIO_IOCTL_UVDEV_INFO_NR)
#define UVIO_IOCTL_ATT		UVIO_IOCTL(UVIO_IOCTL_ATT_NR)
#define UVIO_IOCTL_ADD_SECRET	UVIO_IOCTL(UVIO_IOCTL_ADD_SECRET_NR)
#define UVIO_IOCTL_LIST_SECRETS	UVIO_IOCTL(UVIO_IOCTL_LIST_SECRETS_NR)
#define UVIO_IOCTL_LOCK_SECRETS	UVIO_IOCTL(UVIO_IOCTL_LOCK_SECRETS_NR)

#define UVIO_SUPP_CALL(nr)	(1ULL << (nr))
#define UVIO_SUPP_UDEV_INFO	UVIO_SUPP_CALL(UVIO_IOCTL_UDEV_INFO_NR)
#define UVIO_SUPP_ATT		UVIO_SUPP_CALL(UVIO_IOCTL_ATT_NR)
#define UVIO_SUPP_ADD_SECRET	UVIO_SUPP_CALL(UVIO_IOCTL_ADD_SECRET_NR)
#define UVIO_SUPP_LIST_SECRETS	UVIO_SUPP_CALL(UVIO_IOCTL_LIST_SECRETS_NR)
#define UVIO_SUPP_LOCK_SECRETS	UVIO_SUPP_CALL(UVIO_IOCTL_LOCK_SECRETS_NR)

#endif /* __S390_ASM_UVDEVICE_H */
