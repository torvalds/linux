/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PAPR_PHYSICAL_ATTESTATION_H_
#define _UAPI_PAPR_PHYSICAL_ATTESTATION_H_

#include <linux/types.h>
#include <asm/ioctl.h>
#include <asm/papr-miscdev.h>

#define PAPR_PHYATTEST_MAX_INPUT 4084 /* Max 4K buffer: 4K-12 */

/*
 * Defined in PAPR 2.13+ 21.6 Attestation Command Structures.
 * User space pass this struct and the max size should be 4K.
 */
struct papr_phy_attest_io_block {
	__u8 version;
	__u8 command;
	__u8 TCG_major_ver;
	__u8 TCG_minor_ver;
	__be32 length;
	__be32 correlator;
	__u8 payload[PAPR_PHYATTEST_MAX_INPUT];
};

/*
 * ioctl for /dev/papr-physical-attestation. Returns a attestation
 * command fd handle
 */
#define PAPR_PHY_ATTEST_IOC_HANDLE _IOW(PAPR_MISCDEV_IOC_ID, 8, struct papr_phy_attest_io_block)

#endif /* _UAPI_PAPR_PHYSICAL_ATTESTATION_H_ */
