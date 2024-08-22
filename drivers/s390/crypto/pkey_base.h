/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright IBM Corp. 2024
 *
 * Pkey base: debug feature, defines and structs
 * common to all pkey code.
 */

#ifndef _PKEY_BASE_H_
#define _PKEY_BASE_H_

#include <linux/types.h>
#include <asm/debug.h>
#include <asm/pkey.h>

/*
 * pkey debug feature
 */

extern debug_info_t *pkey_dbf_info;

#define PKEY_DBF_INFO(...) debug_sprintf_event(pkey_dbf_info, 5, ##__VA_ARGS__)
#define PKEY_DBF_WARN(...) debug_sprintf_event(pkey_dbf_info, 4, ##__VA_ARGS__)
#define PKEY_DBF_ERR(...)  debug_sprintf_event(pkey_dbf_info, 3, ##__VA_ARGS__)

/*
 * common defines and common structs
 */

#define KEYBLOBBUFSIZE 8192	/* key buffer size used for internal processing */
#define MINKEYBLOBBUFSIZE (sizeof(struct keytoken_header))
#define PROTKEYBLOBBUFSIZE 256	/* protected key buffer size used internal */
#define MAXAPQNSINLIST 64	/* max 64 apqns within a apqn list */
#define AES_WK_VP_SIZE 32	/* Size of WK VP block appended to a prot key */

/* inside view of a protected key token (only type 0x00 version 0x01) */
struct protaeskeytoken {
	u8  type;     /* 0x00 for PAES specific key tokens */
	u8  res0[3];
	u8  version;  /* should be 0x01 for protected AES key token */
	u8  res1[3];
	u32 keytype;  /* key type, one of the PKEY_KEYTYPE values */
	u32 len;      /* bytes actually stored in protkey[] */
	u8  protkey[MAXPROTKEYSIZE]; /* the protected key blob */
} __packed;

/* inside view of a clear key token (type 0x00 version 0x02) */
struct clearkeytoken {
	u8  type;	/* 0x00 for PAES specific key tokens */
	u8  res0[3];
	u8  version;	/* 0x02 for clear key token */
	u8  res1[3];
	u32 keytype;	/* key type, one of the PKEY_KEYTYPE_* values */
	u32 len;	/* bytes actually stored in clearkey[] */
	u8  clearkey[]; /* clear key value */
} __packed;

/* helper function which translates the PKEY_KEYTYPE_AES_* to their keysize */
static inline u32 pkey_keytype_aes_to_size(u32 keytype)
{
	switch (keytype) {
	case PKEY_KEYTYPE_AES_128:
		return 16;
	case PKEY_KEYTYPE_AES_192:
		return 24;
	case PKEY_KEYTYPE_AES_256:
		return 32;
	default:
		return 0;
	}
}

/* helper function which translates AES key bit size into PKEY_KEYTYPE_AES_* */
static inline u32 pkey_aes_bitsize_to_keytype(u32 keybitsize)
{
	switch (keybitsize) {
	case 128:
		return PKEY_KEYTYPE_AES_128;
	case 192:
		return PKEY_KEYTYPE_AES_192;
	case 256:
		return PKEY_KEYTYPE_AES_256;
	default:
		return 0;
	}
}

/*
 * pkey_cca.c:
 */

bool pkey_is_cca_key(const u8 *key, u32 keylen);
bool pkey_is_cca_keytype(enum pkey_key_type);
int pkey_cca_key2protkey(u16 card, u16 dom,
			 const u8 *key, u32 keylen,
			 u8 *protkey, u32 *protkeylen, u32 *protkeytype);
int pkey_cca_gen_key(u16 card, u16 dom,
		     u32 keytype, u32 keysubtype,
		     u32 keybitsize, u32 flags,
		     u8 *keybuf, u32 *keybuflen);
int pkey_cca_clr2key(u16 card, u16 dom,
		     u32 keytype, u32 keysubtype,
		     u32 keybitsize, u32 flags,
		     const u8 *clrkey, u32 clrkeylen,
		     u8 *keybuf, u32 *keybuflen);
int pkey_cca_verifykey(const u8 *key, u32 keylen,
		       u16 *card, u16 *dom,
		       u32 *keytype, u32 *keybitsize, u32 *flags);
int pkey_cca_apqns4key(const u8 *key, u32 keylen, u32 flags,
		       struct pkey_apqn *apqns, size_t *nr_apqns);
int pkey_cca_apqns4type(enum pkey_key_type ktype,
			u8 cur_mkvp[32], u8 alt_mkvp[32], u32 flags,
			struct pkey_apqn *apqns, size_t *nr_apqns);

/*
 * pkey_ep11.c:
 */

bool pkey_is_ep11_key(const u8 *key, u32 keylen);
bool pkey_is_ep11_keytype(enum pkey_key_type);
int pkey_ep11_key2protkey(u16 card, u16 dom,
			  const u8 *key, u32 keylen,
			  u8 *protkey, u32 *protkeylen, u32 *protkeytype);
int pkey_ep11_gen_key(u16 card, u16 dom,
		      u32 keytype, u32 keysubtype,
		      u32 keybitsize, u32 flags,
		      u8 *keybuf, u32 *keybuflen);
int pkey_ep11_clr2key(u16 card, u16 dom,
		      u32 keytype, u32 keysubtype,
		      u32 keybitsize, u32 flags,
		      const u8 *clrkey, u32 clrkeylen,
		      u8 *keybuf, u32 *keybuflen);
int pkey_ep11_verifykey(const u8 *key, u32 keylen,
			u16 *card, u16 *dom,
			u32 *keytype, u32 *keybitsize, u32 *flags);
int pkey_ep11_apqns4key(const u8 *key, u32 keylen, u32 flags,
			struct pkey_apqn *apqns, size_t *nr_apqns);
int pkey_ep11_apqns4type(enum pkey_key_type ktype,
			 u8 cur_mkvp[32], u8 alt_mkvp[32], u32 flags,
			 struct pkey_apqn *apqns, size_t *nr_apqns);

/*
 * pkey_pckmo.c:
 */

bool pkey_is_pckmo_key(const u8 *key, u32 keylen);
int pkey_pckmo_key2protkey(const u8 *key, u32 keylen,
			   u8 *protkey, u32 *protkeylen, u32 *protkeytype);
int pkey_pckmo_gen_protkey(u32 keytype,
			   u8 *protkey, u32 *protkeylen, u32 *protkeytype);
int pkey_pckmo_clr2protkey(u32 keytype, const u8 *clrkey,
			   u8 *protkey, u32 *protkeylen, u32 *protkeytype);
int pkey_pckmo_verify_protkey(const u8 *protkey, u32 protkeylen,
			      u32 protkeytype);

/*
 * pkey_sysfs.c:
 */

extern const struct attribute_group *pkey_attr_groups[];

#endif /* _PKEY_BASE_H_ */
