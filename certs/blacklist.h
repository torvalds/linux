/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <crypto/pkcs7.h>

/* The `blacklist_hashes` array stores hashes of blacklisted certificates.
 * These hashes are used to prevent the usage of certificates that are deemed untrusted or compromised.
 */
extern const char __initconst *const blacklist_hashes[];
