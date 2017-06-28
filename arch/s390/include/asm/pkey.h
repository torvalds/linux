/*
 * Kernelspace interface to the pkey device driver
 *
 * Copyright IBM Corp. 2016
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
 * Generate (AES) random secure key.
 * @param cardnr may be -1 (use default card)
 * @param domain may be -1 (use default domain)
 * @param keytype one of the PKEY_KEYTYPE values
 * @param seckey pointer to buffer receiving the secure key
 * @return 0 on success, negative errno value on failure
 */
int pkey_genseckey(__u16 cardnr, __u16 domain,
		   __u32 keytype, struct pkey_seckey *seckey);

/*
 * Generate (AES) secure key with given key value.
 * @param cardnr may be -1 (use default card)
 * @param domain may be -1 (use default domain)
 * @param keytype one of the PKEY_KEYTYPE values
 * @param clrkey pointer to buffer with clear key data
 * @param seckey pointer to buffer receiving the secure key
 * @return 0 on success, negative errno value on failure
 */
int pkey_clr2seckey(__u16 cardnr, __u16 domain, __u32 keytype,
		    const struct pkey_clrkey *clrkey,
		    struct pkey_seckey *seckey);

/*
 * Derive (AES) proteced key from the (AES) secure key blob.
 * @param cardnr may be -1 (use default card)
 * @param domain may be -1 (use default domain)
 * @param seckey pointer to buffer with the input secure key
 * @param protkey pointer to buffer receiving the protected key and
 *	  additional info (type, length)
 * @return 0 on success, negative errno value on failure
 */
int pkey_sec2protkey(__u16 cardnr, __u16 domain,
		     const struct pkey_seckey *seckey,
		     struct pkey_protkey *protkey);

/*
 * Derive (AES) protected key from a given clear key value.
 * @param keytype one of the PKEY_KEYTYPE values
 * @param clrkey pointer to buffer with clear key data
 * @param protkey pointer to buffer receiving the protected key and
 *	  additional info (type, length)
 * @return 0 on success, negative errno value on failure
 */
int pkey_clr2protkey(__u32 keytype,
		     const struct pkey_clrkey *clrkey,
		     struct pkey_protkey *protkey);

/*
 * Search for a matching crypto card based on the Master Key
 * Verification Pattern provided inside a secure key.
 * @param seckey pointer to buffer with the input secure key
 * @param cardnr pointer to cardnr, receives the card number on success
 * @param domain pointer to domain, receives the domain number on success
 * @param verify if set, always verify by fetching verification pattern
 *	  from card
 * @return 0 on success, negative errno value on failure. If no card could be
 *	   found, -ENODEV is returned.
 */
int pkey_findcard(const struct pkey_seckey *seckey,
		  __u16 *cardnr, __u16 *domain, int verify);

/*
 * Find card and transform secure key to protected key.
 * @param seckey pointer to buffer with the input secure key
 * @param protkey pointer to buffer receiving the protected key and
 *	  additional info (type, length)
 * @return 0 on success, negative errno value on failure
 */
int pkey_skey2pkey(const struct pkey_seckey *seckey,
		   struct pkey_protkey *protkey);

#endif /* _KAPI_PKEY_H */
