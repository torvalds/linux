/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kernelspace interface to the pkey device driver
 *
 * Copyright IBM Corp. 2016,2019
 *
 * Author: Harald Freudenberger <freude@de.ibm.com>
 *
 */

#ifndef _KAPI_PKEY_H
#define _KAPI_PKEY_H

#include <linux/ioctl.h>
#include <linux/types.h>
#include <uapi/asm/pkey.h>

/*
 * In-kernel API: Transform an key blob (of any type) into a protected key.
 * @param key pointer to a buffer containing the key blob
 * @param keylen size of the key blob in bytes
 * @param protkey pointer to buffer receiving the protected key
 * @return 0 on success, negative errno value on failure
 */
int pkey_keyblob2pkey(const u8 *key, u32 keylen,
		      struct pkey_protkey *protkey);

#endif /* _KAPI_PKEY_H */
