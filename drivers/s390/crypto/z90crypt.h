/*
 *  linux/drivers/s390/crypto/z90crypt.h
 *
 *  z90crypt 1.3.3
 *
 *  Copyright (C)  2001, 2005 IBM Corporation
 *  Author(s): Robert Burroughs (burrough@us.ibm.com)
 *             Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _Z90CRYPT_H_
#define _Z90CRYPT_H_

#include <linux/ioctl.h>

#define VERSION_Z90CRYPT_H "$Revision: 1.2.2.4 $"

#define z90crypt_VERSION 1
#define z90crypt_RELEASE 3	// 2 = PCIXCC, 3 = rewrite for coding standards
#define z90crypt_VARIANT 3	// 3 = CEX2A support

/**
 * struct ica_rsa_modexpo
 *
 * Requirements:
 * - outputdatalength is at least as large as inputdatalength.
 * - All key parts are right justified in their fields, padded on
 *   the left with zeroes.
 * - length(b_key) = inputdatalength
 * - length(n_modulus) = inputdatalength
 */
struct ica_rsa_modexpo {
	char __user *	inputdata;
	unsigned int	inputdatalength;
	char __user *	outputdata;
	unsigned int	outputdatalength;
	char __user *	b_key;
	char __user *	n_modulus;
};

/**
 * struct ica_rsa_modexpo_crt
 *
 * Requirements:
 * - inputdatalength is even.
 * - outputdatalength is at least as large as inputdatalength.
 * - All key parts are right justified in their fields, padded on
 *   the left with zeroes.
 * - length(bp_key)	= inputdatalength/2 + 8
 * - length(bq_key)	= inputdatalength/2
 * - length(np_key)	= inputdatalength/2 + 8
 * - length(nq_key)	= inputdatalength/2
 * - length(u_mult_inv) = inputdatalength/2 + 8
 */
struct ica_rsa_modexpo_crt {
	char __user *	inputdata;
	unsigned int	inputdatalength;
	char __user *	outputdata;
	unsigned int	outputdatalength;
	char __user *	bp_key;
	char __user *	bq_key;
	char __user *	np_prime;
	char __user *	nq_prime;
	char __user *	u_mult_inv;
};

#define Z90_IOCTL_MAGIC 'z'  // NOTE:  Need to allocate from linux folks

/**
 * Interface notes:
 *
 * The ioctl()s which are implemented (along with relevant details)
 * are:
 *
 *   ICARSAMODEXPO
 *     Perform an RSA operation using a Modulus-Exponent pair
 *     This takes an ica_rsa_modexpo struct as its arg.
 *
 *     NOTE: please refer to the comments preceding this structure
 *           for the implementation details for the contents of the
 *           block
 *
 *   ICARSACRT
 *     Perform an RSA operation using a Chinese-Remainder Theorem key
 *     This takes an ica_rsa_modexpo_crt struct as its arg.
 *
 *     NOTE: please refer to the comments preceding this structure
 *           for the implementation details for the contents of the
 *           block
 *
 *   Z90STAT_TOTALCOUNT
 *     Return an integer count of all device types together.
 *
 *   Z90STAT_PCICACOUNT
 *     Return an integer count of all PCICAs.
 *
 *   Z90STAT_PCICCCOUNT
 *     Return an integer count of all PCICCs.
 *
 *   Z90STAT_PCIXCCMCL2COUNT
 *     Return an integer count of all MCL2 PCIXCCs.
 *
 *   Z90STAT_PCIXCCMCL3COUNT
 *     Return an integer count of all MCL3 PCIXCCs.
 *
 *   Z90STAT_CEX2CCOUNT
 *     Return an integer count of all CEX2Cs.
 *
 *   Z90STAT_CEX2ACOUNT
 *     Return an integer count of all CEX2As.
 *
 *   Z90STAT_REQUESTQ_COUNT
 *     Return an integer count of the number of entries waiting to be
 *     sent to a device.
 *
 *   Z90STAT_PENDINGQ_COUNT
 *     Return an integer count of the number of entries sent to a
 *     device awaiting the reply.
 *
 *   Z90STAT_TOTALOPEN_COUNT
 *     Return an integer count of the number of open file handles.
 *
 *   Z90STAT_DOMAIN_INDEX
 *     Return the integer value of the Cryptographic Domain.
 *
 *   Z90STAT_STATUS_MASK
 *     Return an 64 element array of unsigned chars for the status of
 *     all devices.
 *       0x01: PCICA
 *       0x02: PCICC
 *       0x03: PCIXCC_MCL2
 *       0x04: PCIXCC_MCL3
 *       0x05: CEX2C
 *       0x06: CEX2A
 *       0x0d: device is disabled via the proc filesystem
 *
 *   Z90STAT_QDEPTH_MASK
 *     Return an 64 element array of unsigned chars for the queue
 *     depth of all devices.
 *
 *   Z90STAT_PERDEV_REQCNT
 *     Return an 64 element array of unsigned integers for the number
 *     of successfully completed requests per device since the device
 *     was detected and made available.
 *
 *   ICAZ90STATUS (deprecated)
 *     Return some device driver status in a ica_z90_status struct
 *     This takes an ica_z90_status struct as its arg.
 *
 *     NOTE: this ioctl() is deprecated, and has been replaced with
 *           single ioctl()s for each type of status being requested
 *
 *   Z90STAT_PCIXCCCOUNT (deprecated)
 *     Return an integer count of all PCIXCCs (MCL2 + MCL3).
 *     This is DEPRECATED now that MCL3 PCIXCCs are treated differently from
 *     MCL2 PCIXCCs.
 *
 *   Z90QUIESCE (not recommended)
 *     Quiesce the driver.  This is intended to stop all new
 *     requests from being processed.  Its use is NOT recommended,
 *     except in circumstances where there is no other way to stop
 *     callers from accessing the driver.  Its original use was to
 *     allow the driver to be "drained" of work in preparation for
 *     a system shutdown.
 *
 *     NOTE: once issued, this ban on new work cannot be undone
 *           except by unloading and reloading the driver.
 */

/**
 * Supported ioctl calls
 */
#define ICARSAMODEXPO	_IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x05, 0)
#define ICARSACRT	_IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x06, 0)

/* DEPRECATED status calls (bound for removal at some point) */
#define ICAZ90STATUS	_IOR(Z90_IOCTL_MAGIC, 0x10, struct ica_z90_status)
#define Z90STAT_PCIXCCCOUNT	_IOR(Z90_IOCTL_MAGIC, 0x43, int)

/* unrelated to ICA callers */
#define Z90QUIESCE	_IO(Z90_IOCTL_MAGIC, 0x11)

/* New status calls */
#define Z90STAT_TOTALCOUNT	_IOR(Z90_IOCTL_MAGIC, 0x40, int)
#define Z90STAT_PCICACOUNT	_IOR(Z90_IOCTL_MAGIC, 0x41, int)
#define Z90STAT_PCICCCOUNT	_IOR(Z90_IOCTL_MAGIC, 0x42, int)
#define Z90STAT_PCIXCCMCL2COUNT	_IOR(Z90_IOCTL_MAGIC, 0x4b, int)
#define Z90STAT_PCIXCCMCL3COUNT	_IOR(Z90_IOCTL_MAGIC, 0x4c, int)
#define Z90STAT_CEX2CCOUNT	_IOR(Z90_IOCTL_MAGIC, 0x4d, int)
#define Z90STAT_CEX2ACOUNT	_IOR(Z90_IOCTL_MAGIC, 0x4e, int)
#define Z90STAT_REQUESTQ_COUNT	_IOR(Z90_IOCTL_MAGIC, 0x44, int)
#define Z90STAT_PENDINGQ_COUNT	_IOR(Z90_IOCTL_MAGIC, 0x45, int)
#define Z90STAT_TOTALOPEN_COUNT _IOR(Z90_IOCTL_MAGIC, 0x46, int)
#define Z90STAT_DOMAIN_INDEX	_IOR(Z90_IOCTL_MAGIC, 0x47, int)
#define Z90STAT_STATUS_MASK	_IOR(Z90_IOCTL_MAGIC, 0x48, char[64])
#define Z90STAT_QDEPTH_MASK	_IOR(Z90_IOCTL_MAGIC, 0x49, char[64])
#define Z90STAT_PERDEV_REQCNT	_IOR(Z90_IOCTL_MAGIC, 0x4a, int[64])

/**
 * local errno definitions
 */
#define ENOBUFF	  129	// filp->private_data->...>work_elem_p->buffer is NULL
#define EWORKPEND 130	// user issues ioctl while another pending
#define ERELEASED 131	// user released while ioctl pending
#define EQUIESCE  132	// z90crypt quiescing (no more work allowed)
#define ETIMEOUT  133	// request timed out
#define EUNKNOWN  134	// some unrecognized error occured (retry may succeed)
#define EGETBUFF  135	// Error getting buffer or hardware lacks capability
			// (retry in software)

/**
 * DEPRECATED STRUCTURES
 */

/**
 * This structure is DEPRECATED and the corresponding ioctl() has been
 * replaced with individual ioctl()s for each piece of data!
 * This structure will NOT survive past version 1.3.1, so switch to the
 * new ioctl()s.
 */
#define MASK_LENGTH 64 // mask length
struct ica_z90_status {
	int totalcount;
	int leedslitecount; // PCICA
	int leeds2count;    // PCICC
	// int PCIXCCCount; is not in struct for backward compatibility
	int requestqWaitCount;
	int pendingqWaitCount;
	int totalOpenCount;
	int cryptoDomain;
	// status: 0=not there, 1=PCICA, 2=PCICC, 3=PCIXCC_MCL2, 4=PCIXCC_MCL3,
	//         5=CEX2C
	unsigned char status[MASK_LENGTH];
	// qdepth: # work elements waiting for each device
	unsigned char qdepth[MASK_LENGTH];
};

#endif /* _Z90CRYPT_H_ */
