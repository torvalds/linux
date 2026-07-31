/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kernelspace interface to the pkey device driver
 *
 * Copyright IBM Corp. 2016, 2023
 *
 * Author: Harald Freudenberger <freude@de.ibm.com>
 *
 */

#ifndef _KAPI_PKEY_H
#define _KAPI_PKEY_H

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <uapi/asm/pkey.h>

/*
 * In-kernel API: Transform an key blob (of any type) into a protected key.
 * @param key pointer to a buffer containing the key blob
 * @param keylen size of the key blob in bytes
 * @param protkey pointer to buffer receiving the protected key
 * @param xflags additional execution flags (see PKEY_XFLAG_* definitions below)
 *	  As of now the only supported flags are PKEY_XFLAG_NOMEMALLOC
 *	  and PKEY_XFLAG_NOCLEARKEY.
 * @return 0 on success, negative errno value on failure
 */
int pkey_key2protkey(const u8 *key, u32 keylen,
		     u8 *protkey, u32 *protkeylen, u32 *protkeytype,
		     u32 xflags);

/*
 * If this flag is given in the xflags parameter, the pkey implementation
 * is not allowed to allocate memory but instead should fall back to use
 * preallocated memory or simple fail with -ENOMEM.
 * This flag is for protected key derive within a cipher or similar
 * which must not allocate memory which would cause io operations - see
 * also the CRYPTO_ALG_ALLOCATES_MEMORY flag in crypto.h.
 */
#define PKEY_XFLAG_NOMEMALLOC 0x0001

/*
 * Do not accept a clear key token as source for a protected key.
 */
#define PKEY_XFLAG_NOCLEARKEY 0x0002

static inline int pkey_handle_expired(void)
{
	/*
	 * Protected key expired due to relocation to another host. The long
	 * running re-wrap has no asynchronous completion notification, so
	 * polling is required. Trigger a re-schedule of this request by
	 * returning -ENOSPC ("hardware queue full") to the crypto engine.
	 * To avoid immediately re-invocation of this callback,
	 * tell the scheduler to voluntarily give up the CPU here.
	 */
	msleep(1);
	pr_debug("rescheduling request\n");
	return -ENOSPC;
}

#endif /* _KAPI_PKEY_H */
