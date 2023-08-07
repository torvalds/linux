/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Copyright IBM Corp. 2019
 *  Author(s): Harald Freudenberger <freude@linux.ibm.com>
 *
 *  Collection of EP11 misc functions used by zcrypt and pkey
 */

#ifndef _ZCRYPT_EP11MISC_H_
#define _ZCRYPT_EP11MISC_H_

#include <asm/zcrypt.h>
#include <asm/pkey.h>

#define EP11_API_V 4  /* highest known and supported EP11 API version */
#define EP11_STRUCT_MAGIC 0x1234
#define EP11_BLOB_PKEY_EXTRACTABLE 0x00200000

/*
 * Internal used values for the version field of the key header.
 * Should match to the enum pkey_key_type in pkey.h.
 */
#define TOKVER_EP11_AES  0x03  /* EP11 AES key blob (old style) */
#define TOKVER_EP11_AES_WITH_HEADER 0x06 /* EP11 AES key blob with header */
#define TOKVER_EP11_ECC_WITH_HEADER 0x07 /* EP11 ECC key blob with header */

/* inside view of an EP11 secure key blob */
struct ep11keyblob {
	union {
		u8 session[32];
		/* only used for PKEY_TYPE_EP11: */
		struct {
			u8  type;      /* 0x00 (TOKTYPE_NON_CCA) */
			u8  res0;      /* unused */
			u16 len;       /* total length in bytes of this blob */
			u8  version;   /* 0x03 (TOKVER_EP11_AES) */
			u8  res1;      /* unused */
			u16 keybitlen; /* clear key bit len, 0 for unknown */
		} head;
	};
	u8  wkvp[16];  /* wrapping key verification pattern */
	u64 attr;      /* boolean key attributes */
	u64 mode;      /* mode bits */
	u16 version;   /* 0x1234, EP11_STRUCT_MAGIC */
	u8  iv[14];
	u8  encrypted_key_data[144];
	u8  mac[32];
} __packed;

/* check ep11 key magic to find out if this is an ep11 key blob */
static inline bool is_ep11_keyblob(const u8 *key)
{
	struct ep11keyblob *kb = (struct ep11keyblob *)key;

	return (kb->version == EP11_STRUCT_MAGIC);
}

/*
 * Simple check if the key blob is a valid EP11 AES key blob with header.
 * If checkcpacfexport is enabled, the key is also checked for the
 * attributes needed to export this key for CPACF use.
 * Returns 0 on success or errno value on failure.
 */
int ep11_check_aes_key_with_hdr(debug_info_t *dbg, int dbflvl,
				const u8 *key, size_t keylen, int checkcpacfexp);

/*
 * Simple check if the key blob is a valid EP11 ECC key blob with header.
 * If checkcpacfexport is enabled, the key is also checked for the
 * attributes needed to export this key for CPACF use.
 * Returns 0 on success or errno value on failure.
 */
int ep11_check_ecc_key_with_hdr(debug_info_t *dbg, int dbflvl,
				const u8 *key, size_t keylen, int checkcpacfexp);

/*
 * Simple check if the key blob is a valid EP11 AES key blob with
 * the header in the session field (old style EP11 AES key).
 * If checkcpacfexport is enabled, the key is also checked for the
 * attributes needed to export this key for CPACF use.
 * Returns 0 on success or errno value on failure.
 */
int ep11_check_aes_key(debug_info_t *dbg, int dbflvl,
		       const u8 *key, size_t keylen, int checkcpacfexp);

/* EP11 card info struct */
struct ep11_card_info {
	u32  API_ord_nr;    /* API ordinal number */
	u16  FW_version;    /* Firmware major and minor version */
	char serial[16];    /* serial number string (16 ascii, no 0x00 !) */
	u64  op_mode;	    /* card operational mode(s) */
};

/* EP11 domain info struct */
struct ep11_domain_info {
	char cur_wk_state;  /* '0' invalid, '1' valid */
	char new_wk_state;  /* '0' empty, '1' uncommitted, '2' committed */
	u8   cur_wkvp[32];  /* current wrapping key verification pattern */
	u8   new_wkvp[32];  /* new wrapping key verification pattern */
	u64  op_mode;	    /* domain operational mode(s) */
};

/*
 * Provide information about an EP11 card.
 */
int ep11_get_card_info(u16 card, struct ep11_card_info *info, int verify);

/*
 * Provide information about a domain within an EP11 card.
 */
int ep11_get_domain_info(u16 card, u16 domain, struct ep11_domain_info *info);

/*
 * Generate (random) EP11 AES secure key.
 */
int ep11_genaeskey(u16 card, u16 domain, u32 keybitsize, u32 keygenflags,
		   u8 *keybuf, size_t *keybufsize);

/*
 * Generate EP11 AES secure key with given clear key value.
 */
int ep11_clr2keyblob(u16 cardnr, u16 domain, u32 keybitsize, u32 keygenflags,
		     const u8 *clrkey, u8 *keybuf, size_t *keybufsize);

/*
 * Build a list of ep11 apqns meeting the following constrains:
 * - apqn is online and is in fact an EP11 apqn
 * - if cardnr is not FFFF only apqns with this cardnr
 * - if domain is not FFFF only apqns with this domainnr
 * - if minhwtype > 0 only apqns with hwtype >= minhwtype
 * - if minapi > 0 only apqns with API_ord_nr >= minapi
 * - if wkvp != NULL only apqns where the wkvp (EP11_WKVPLEN bytes) matches
 *   to the first EP11_WKVPLEN bytes of the wkvp of the current wrapping
 *   key for this domain. When a wkvp is given there will always be a re-fetch
 *   of the domain info for the potential apqn - so this triggers an request
 *   reply to each apqn eligible.
 * The array of apqn entries is allocated with kmalloc and returned in *apqns;
 * the number of apqns stored into the list is returned in *nr_apqns. One apqn
 * entry is simple a 32 bit value with 16 bit cardnr and 16 bit domain nr and
 * may be casted to struct pkey_apqn. The return value is either 0 for success
 * or a negative errno value. If no apqn meeting the criteria is found,
 * -ENODEV is returned.
 */
int ep11_findcard2(u32 **apqns, u32 *nr_apqns, u16 cardnr, u16 domain,
		   int minhwtype, int minapi, const u8 *wkvp);

/*
 * Derive proteced key from EP11 key blob (AES and ECC keys).
 */
int ep11_kblob2protkey(u16 card, u16 dom, const u8 *key, size_t keylen,
		       u8 *protkey, u32 *protkeylen, u32 *protkeytype);

void zcrypt_ep11misc_exit(void);

#endif /* _ZCRYPT_EP11MISC_H_ */
