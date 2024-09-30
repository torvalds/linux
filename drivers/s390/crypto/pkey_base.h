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

/* inside view of a generic protected key token */
struct protkeytoken {
	u8  type;     /* 0x00 for PAES specific key tokens */
	u8  res0[3];
	u8  version;  /* should be 0x01 for protected key token */
	u8  res1[3];
	u32 keytype;  /* key type, one of the PKEY_KEYTYPE values */
	u32 len;      /* bytes actually stored in protkey[] */
	u8  protkey[]; /* the protected key blob */
} __packed;

/* inside view of a protected AES key token */
struct protaeskeytoken {
	u8  type;     /* 0x00 for PAES specific key tokens */
	u8  res0[3];
	u8  version;  /* should be 0x01 for protected key token */
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
 * pkey_api.c:
 */
int __init pkey_api_init(void);
void __exit pkey_api_exit(void);

/*
 * pkey_sysfs.c:
 */

extern const struct attribute_group *pkey_attr_groups[];

/*
 * pkey handler registry
 */

struct pkey_handler {
	struct module *module;
	const char *name;
	/*
	 * is_supported_key() and is_supported_keytype() are called
	 * within an rcu_read_lock() scope and thus must not sleep!
	 */
	bool (*is_supported_key)(const u8 *key, u32 keylen);
	bool (*is_supported_keytype)(enum pkey_key_type);
	int (*key_to_protkey)(const struct pkey_apqn *apqns, size_t nr_apqns,
			      const u8 *key, u32 keylen,
			      u8 *protkey, u32 *protkeylen, u32 *protkeytype);
	int (*slowpath_key_to_protkey)(const struct pkey_apqn *apqns,
				       size_t nr_apqns,
				       const u8 *key, u32 keylen,
				       u8 *protkey, u32 *protkeylen,
				       u32 *protkeytype);
	int (*gen_key)(const struct pkey_apqn *apqns, size_t nr_apqns,
		       u32 keytype, u32 keysubtype,
		       u32 keybitsize, u32 flags,
		       u8 *keybuf, u32 *keybuflen, u32 *keyinfo);
	int (*clr_to_key)(const struct pkey_apqn *apqns, size_t nr_apqns,
			  u32 keytype, u32 keysubtype,
			  u32 keybitsize, u32 flags,
			  const u8 *clrkey, u32 clrkeylen,
			  u8 *keybuf, u32 *keybuflen, u32 *keyinfo);
	int (*verify_key)(const u8 *key, u32 keylen,
			  u16 *card, u16 *dom,
			  u32 *keytype, u32 *keybitsize, u32 *flags);
	int (*apqns_for_key)(const u8 *key, u32 keylen, u32 flags,
			     struct pkey_apqn *apqns, size_t *nr_apqns);
	int (*apqns_for_keytype)(enum pkey_key_type ktype,
				 u8 cur_mkvp[32], u8 alt_mkvp[32], u32 flags,
				 struct pkey_apqn *apqns, size_t *nr_apqns);
	/* used internal by pkey base */
	struct list_head list;
};

int pkey_handler_register(struct pkey_handler *handler);
int pkey_handler_unregister(struct pkey_handler *handler);

/*
 * invocation function for the registered pkey handlers
 */

const struct pkey_handler *pkey_handler_get_keybased(const u8 *key, u32 keylen);
const struct pkey_handler *pkey_handler_get_keytypebased(enum pkey_key_type kt);
void pkey_handler_put(const struct pkey_handler *handler);

int pkey_handler_key_to_protkey(const struct pkey_apqn *apqns, size_t nr_apqns,
				const u8 *key, u32 keylen,
				u8 *protkey, u32 *protkeylen, u32 *protkeytype);
int pkey_handler_slowpath_key_to_protkey(const struct pkey_apqn *apqns,
					 size_t nr_apqns,
					 const u8 *key, u32 keylen,
					 u8 *protkey, u32 *protkeylen,
					 u32 *protkeytype);
int pkey_handler_gen_key(const struct pkey_apqn *apqns, size_t nr_apqns,
			 u32 keytype, u32 keysubtype,
			 u32 keybitsize, u32 flags,
			 u8 *keybuf, u32 *keybuflen, u32 *keyinfo);
int pkey_handler_clr_to_key(const struct pkey_apqn *apqns, size_t nr_apqns,
			    u32 keytype, u32 keysubtype,
			    u32 keybitsize, u32 flags,
			    const u8 *clrkey, u32 clrkeylen,
			    u8 *keybuf, u32 *keybuflen, u32 *keyinfo);
int pkey_handler_verify_key(const u8 *key, u32 keylen,
			    u16 *card, u16 *dom,
			    u32 *keytype, u32 *keybitsize, u32 *flags);
int pkey_handler_apqns_for_key(const u8 *key, u32 keylen, u32 flags,
			       struct pkey_apqn *apqns, size_t *nr_apqns);
int pkey_handler_apqns_for_keytype(enum pkey_key_type ktype,
				   u8 cur_mkvp[32], u8 alt_mkvp[32], u32 flags,
				   struct pkey_apqn *apqns, size_t *nr_apqns);

/*
 * Unconditional try to load all handler modules
 */
void pkey_handler_request_modules(void);

#endif /* _PKEY_BASE_H_ */
