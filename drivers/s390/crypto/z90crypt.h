/*
 *  linux/drivers/s390/crypto/z90crypt.h
 *
 *  z90crypt 1.3.3 (kernel-private header)
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

#include <asm/z90crypt.h>

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
