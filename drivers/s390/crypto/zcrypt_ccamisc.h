/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Copyright IBM Corp. 2019
 *  Author(s): Harald Freudenberger <freude@linux.ibm.com>
 *	       Ingo Franzki <ifranzki@linux.ibm.com>
 *
 *  Collection of CCA misc functions used by zcrypt and pkey
 */

#ifndef _ZCRYPT_CCAMISC_H_
#define _ZCRYPT_CCAMISC_H_

#include <asm/zcrypt.h>
#include <asm/pkey.h>

/* Key token types */
#define TOKTYPE_NON_CCA		 0x00 /* Non-CCA key token */
#define TOKTYPE_CCA_INTERNAL	 0x01 /* CCA internal sym key token */
#define TOKTYPE_CCA_INTERNAL_PKA 0x1f /* CCA internal asym key token */

/* For TOKTYPE_NON_CCA: */
#define TOKVER_PROTECTED_KEY	0x01 /* Protected key token */
#define TOKVER_CLEAR_KEY	0x02 /* Clear key token */

/* For TOKTYPE_CCA_INTERNAL: */
#define TOKVER_CCA_AES		0x04 /* CCA AES key token */
#define TOKVER_CCA_VLSC		0x05 /* var length sym cipher key token */

/* Max size of a cca variable length cipher key token */
#define MAXCCAVLSCTOKENSIZE 725

/* header part of a CCA key token */
struct keytoken_header {
	u8  type;     /* one of the TOKTYPE values */
	u8  res0[1];
	u16 len;      /* vlsc token: total length in bytes */
	u8  version;  /* one of the TOKVER values */
	u8  res1[3];
} __packed;

/* inside view of a CCA secure key token (only type 0x01 version 0x04) */
struct secaeskeytoken {
	u8  type;     /* 0x01 for internal key token */
	u8  res0[3];
	u8  version;  /* should be 0x04 */
	u8  res1[1];
	u8  flag;     /* key flags */
	u8  res2[1];
	u64 mkvp;     /* master key verification pattern */
	u8  key[32];  /* key value (encrypted) */
	u8  cv[8];    /* control vector */
	u16 bitsize;  /* key bit size */
	u16 keysize;  /* key byte size */
	u8  tvv[4];   /* token validation value */
} __packed;

/* inside view of a variable length symmetric cipher AES key token */
struct cipherkeytoken {
	u8  type;     /* 0x01 for internal key token */
	u8  res0[1];
	u16 len;      /* total key token length in bytes */
	u8  version;  /* should be 0x05 */
	u8  res1[3];
	u8  kms;      /* key material state, 0x03 means wrapped with MK */
	u8  kvpt;     /* key verification pattern type, should be 0x01 */
	u64 mkvp0;    /* master key verification pattern, lo part */
	u64 mkvp1;    /* master key verification pattern, hi part (unused) */
	u8  eskwm;    /* encrypted section key wrapping method */
	u8  hashalg;  /* hash algorithmus used for wrapping key */
	u8  plfver;   /* pay load format version */
	u8  res2[1];
	u8  adsver;   /* associated data section version */
	u8  res3[1];
	u16 adslen;   /* associated data section length */
	u8  kllen;    /* optional key label length */
	u8  ieaslen;  /* optional extended associated data length */
	u8  uadlen;   /* optional user definable associated data length */
	u8  res4[1];
	u16 wpllen;   /* wrapped payload length in bits: */
		      /*   plfver  0x00 0x01		 */
		      /*   AES-128  512  640		 */
		      /*   AES-192  576  640		 */
		      /*   AES-256  640  640		 */
	u8  res5[1];
	u8  algtype;  /* 0x02 for AES cipher */
	u16 keytype;  /* 0x0001 for 'cipher' */
	u8  kufc;     /* key usage field count */
	u16 kuf1;     /* key usage field 1 */
	u16 kuf2;     /* key usage field 2 */
	u8  kmfc;     /* key management field count */
	u16 kmf1;     /* key management field 1 */
	u16 kmf2;     /* key management field 2 */
	u16 kmf3;     /* key management field 3 */
	u8  vdata[]; /* variable part data follows */
} __packed;

/* inside view of an CCA secure ECC private key */
struct eccprivkeytoken {
	u8  type;     /* 0x1f for internal asym key token */
	u8  version;  /* should be 0x00 */
	u16 len;      /* total key token length in bytes */
	u8  res1[4];
	u8  secid;    /* 0x20 for ECC priv key section marker */
	u8  secver;   /* section version */
	u16 seclen;   /* section length */
	u8  wtype;    /* wrapping method, 0x00 clear, 0x01 AES */
	u8  htype;    /* hash method, 0x02 for SHA-256 */
	u8  res2[2];
	u8  kutc;     /* key usage and translation control */
	u8  ctype;    /* curve type */
	u8  kfs;      /* key format and security */
	u8  ksrc;     /* key source */
	u16 pbitlen;  /* length of prime p in bits */
	u16 ibmadlen; /* IBM associated data length in bytes */
	u64 mkvp;     /* master key verification pattern */
	u8  opk[48];  /* encrypted object protection key data */
	u16 adatalen; /* associated data length in bytes */
	u16 fseclen;  /* formatted section length in bytes */
	u8  more_data[]; /* more data follows */
} __packed;

/* Some defines for the CCA AES cipherkeytoken kmf1 field */
#define KMF1_XPRT_SYM  0x8000
#define KMF1_XPRT_UASY 0x4000
#define KMF1_XPRT_AASY 0x2000
#define KMF1_XPRT_RAW  0x1000
#define KMF1_XPRT_CPAC 0x0800
#define KMF1_XPRT_DES  0x0080
#define KMF1_XPRT_AES  0x0040
#define KMF1_XPRT_RSA  0x0008

/*
 * Simple check if the token is a valid CCA secure AES data key
 * token. If keybitsize is given, the bitsize of the key is
 * also checked. Returns 0 on success or errno value on failure.
 */
int cca_check_secaeskeytoken(debug_info_t *dbg, int dbflvl,
			     const u8 *token, int keybitsize);

/*
 * Simple check if the token is a valid CCA secure AES cipher key
 * token. If keybitsize is given, the bitsize of the key is
 * also checked. If checkcpacfexport is enabled, the key is also
 * checked for the export flag to allow CPACF export.
 * Returns 0 on success or errno value on failure.
 */
int cca_check_secaescipherkey(debug_info_t *dbg, int dbflvl,
			      const u8 *token, int keybitsize,
			      int checkcpacfexport);

/*
 * Simple check if the token is a valid CCA secure ECC private
 * key token. Returns 0 on success or errno value on failure.
 */
int cca_check_sececckeytoken(debug_info_t *dbg, int dbflvl,
			     const u8 *token, size_t keysize,
			     int checkcpacfexport);

/*
 * Generate (random) CCA AES DATA secure key.
 */
int cca_genseckey(u16 cardnr, u16 domain, u32 keybitsize, u8 *seckey);

/*
 * Generate CCA AES DATA secure key with given clear key value.
 */
int cca_clr2seckey(u16 cardnr, u16 domain, u32 keybitsize,
		   const u8 *clrkey, u8 *seckey);

/*
 * Derive proteced key from an CCA AES DATA secure key.
 */
int cca_sec2protkey(u16 cardnr, u16 domain,
		    const u8 *seckey, u8 *protkey, u32 *protkeylen,
		    u32 *protkeytype);

/*
 * Generate (random) CCA AES CIPHER secure key.
 */
int cca_gencipherkey(u16 cardnr, u16 domain, u32 keybitsize, u32 keygenflags,
		     u8 *keybuf, size_t *keybufsize);

/*
 * Derive proteced key from CCA AES cipher secure key.
 */
int cca_cipher2protkey(u16 cardnr, u16 domain, const u8 *ckey,
		       u8 *protkey, u32 *protkeylen, u32 *protkeytype);

/*
 * Build CCA AES CIPHER secure key with a given clear key value.
 */
int cca_clr2cipherkey(u16 cardnr, u16 domain, u32 keybitsize, u32 keygenflags,
		      const u8 *clrkey, u8 *keybuf, size_t *keybufsize);

/*
 * Derive proteced key from CCA ECC secure private key.
 */
int cca_ecc2protkey(u16 cardnr, u16 domain, const u8 *key,
		    u8 *protkey, u32 *protkeylen, u32 *protkeytype);

/*
 * Query cryptographic facility from CCA adapter
 */
int cca_query_crypto_facility(u16 cardnr, u16 domain,
			      const char *keyword,
			      u8 *rarray, size_t *rarraylen,
			      u8 *varray, size_t *varraylen);

/*
 * Search for a matching crypto card based on the Master Key
 * Verification Pattern provided inside a secure key.
 * Works with CCA AES data and cipher keys.
 * Returns < 0 on failure, 0 if CURRENT MKVP matches and
 * 1 if OLD MKVP matches.
 */
int cca_findcard(const u8 *key, u16 *pcardnr, u16 *pdomain, int verify);

/*
 * Build a list of cca apqns meeting the following constrains:
 * - apqn is online and is in fact a CCA apqn
 * - if cardnr is not FFFF only apqns with this cardnr
 * - if domain is not FFFF only apqns with this domainnr
 * - if minhwtype > 0 only apqns with hwtype >= minhwtype
 * - if cur_mkvp != 0 only apqns where cur_mkvp == mkvp
 * - if old_mkvp != 0 only apqns where old_mkvp == mkvp
 * - if verify is enabled and a cur_mkvp and/or old_mkvp
 *   value is given, then refetch the cca_info and make sure the current
 *   cur_mkvp or old_mkvp values of the apqn are used.
 * The mktype determines which set of master keys to use:
 *   0 = AES_MK_SET - AES MK set, 1 = APKA MK_SET - APKA MK set
 * The array of apqn entries is allocated with kmalloc and returned in *apqns;
 * the number of apqns stored into the list is returned in *nr_apqns. One apqn
 * entry is simple a 32 bit value with 16 bit cardnr and 16 bit domain nr and
 * may be casted to struct pkey_apqn. The return value is either 0 for success
 * or a negative errno value. If no apqn meeting the criteria is found,
 * -ENODEV is returned.
 */
int cca_findcard2(u32 **apqns, u32 *nr_apqns, u16 cardnr, u16 domain,
		  int minhwtype, int mktype, u64 cur_mkvp, u64 old_mkvp,
		  int verify);

#define AES_MK_SET  0
#define APKA_MK_SET 1

/* struct to hold info for each CCA queue */
struct cca_info {
	int  hwtype;		/* one of the defined AP_DEVICE_TYPE_* */
	char new_aes_mk_state;	/* '1' empty, '2' partially full, '3' full */
	char cur_aes_mk_state;	/* '1' invalid, '2' valid */
	char old_aes_mk_state;	/* '1' invalid, '2' valid */
	char new_apka_mk_state; /* '1' empty, '2' partially full, '3' full */
	char cur_apka_mk_state; /* '1' invalid, '2' valid */
	char old_apka_mk_state; /* '1' invalid, '2' valid */
	char new_asym_mk_state;	/* '1' empty, '2' partially full, '3' full */
	char cur_asym_mk_state;	/* '1' invalid, '2' valid */
	char old_asym_mk_state;	/* '1' invalid, '2' valid */
	u64  new_aes_mkvp;	/* truncated sha256 of new aes master key */
	u64  cur_aes_mkvp;	/* truncated sha256 of current aes master key */
	u64  old_aes_mkvp;	/* truncated sha256 of old aes master key */
	u64  new_apka_mkvp;	/* truncated sha256 of new apka master key */
	u64  cur_apka_mkvp;	/* truncated sha256 of current apka mk */
	u64  old_apka_mkvp;	/* truncated sha256 of old apka mk */
	u8   new_asym_mkvp[16];	/* verify pattern of new asym master key */
	u8   cur_asym_mkvp[16];	/* verify pattern of current asym master key */
	u8   old_asym_mkvp[16];	/* verify pattern of old asym master key */
	char serial[9];		/* serial number (8 ascii numbers + 0x00) */
};

/*
 * Fetch cca information about an CCA queue.
 */
int cca_get_info(u16 card, u16 dom, struct cca_info *ci, int verify);

void zcrypt_ccamisc_exit(void);

#endif /* _ZCRYPT_CCAMISC_H_ */
