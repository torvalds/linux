/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace interface to the pkey device driver
 *
 * Copyright IBM Corp. 2017, 2023
 *
 * Author: Harald Freudenberger <freude@de.ibm.com>
 *
 */

#ifndef _UAPI_PKEY_H
#define _UAPI_PKEY_H

#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * Ioctl calls supported by the pkey device driver
 */

#define PKEY_IOCTL_MAGIC 'p'

#define SECKEYBLOBSIZE	64	   /* secure key blob size is always 64 bytes */
#define PROTKEYBLOBSIZE 80	/* protected key blob size is always 80 bytes */
#define MAXPROTKEYSIZE	64	/* a protected key blob may be up to 64 bytes */
#define MAXCLRKEYSIZE	32	   /* a clear key value may be up to 32 bytes */
#define MAXAESCIPHERKEYSIZE 136  /* our aes cipher keys have always 136 bytes */
#define MINEP11AESKEYBLOBSIZE 256  /* min EP11 AES key blob size  */
#define MAXEP11AESKEYBLOBSIZE 320  /* max EP11 AES key blob size */

/* Minimum size of a key blob */
#define MINKEYBLOBSIZE	SECKEYBLOBSIZE

/* defines for the type field within the pkey_protkey struct */
#define PKEY_KEYTYPE_AES_128		1
#define PKEY_KEYTYPE_AES_192		2
#define PKEY_KEYTYPE_AES_256		3
#define PKEY_KEYTYPE_ECC		4
#define PKEY_KEYTYPE_ECC_P256		5
#define PKEY_KEYTYPE_ECC_P384		6
#define PKEY_KEYTYPE_ECC_P521		7
#define PKEY_KEYTYPE_ECC_ED25519	8
#define PKEY_KEYTYPE_ECC_ED448		9

/* the newer ioctls use a pkey_key_type enum for type information */
enum pkey_key_type {
	PKEY_TYPE_CCA_DATA   = (__u32) 1,
	PKEY_TYPE_CCA_CIPHER = (__u32) 2,
	PKEY_TYPE_EP11	     = (__u32) 3,
	PKEY_TYPE_CCA_ECC    = (__u32) 0x1f,
	PKEY_TYPE_EP11_AES   = (__u32) 6,
	PKEY_TYPE_EP11_ECC   = (__u32) 7,
};

/* the newer ioctls use a pkey_key_size enum for key size information */
enum pkey_key_size {
	PKEY_SIZE_AES_128 = (__u32) 128,
	PKEY_SIZE_AES_192 = (__u32) 192,
	PKEY_SIZE_AES_256 = (__u32) 256,
	PKEY_SIZE_UNKNOWN = (__u32) 0xFFFFFFFF,
};

/* some of the newer ioctls use these flags */
#define PKEY_FLAGS_MATCH_CUR_MKVP  0x00000002
#define PKEY_FLAGS_MATCH_ALT_MKVP  0x00000004

/* keygenflags defines for CCA AES cipher keys */
#define PKEY_KEYGEN_XPRT_SYM  0x00008000
#define PKEY_KEYGEN_XPRT_UASY 0x00004000
#define PKEY_KEYGEN_XPRT_AASY 0x00002000
#define PKEY_KEYGEN_XPRT_RAW  0x00001000
#define PKEY_KEYGEN_XPRT_CPAC 0x00000800
#define PKEY_KEYGEN_XPRT_DES  0x00000080
#define PKEY_KEYGEN_XPRT_AES  0x00000040
#define PKEY_KEYGEN_XPRT_RSA  0x00000008

/* Struct to hold apqn target info (card/domain pair) */
struct pkey_apqn {
	__u16 card;
	__u16 domain;
};

/* Struct to hold a CCA AES secure key blob */
struct pkey_seckey {
	__u8  seckey[SECKEYBLOBSIZE];		  /* the secure key blob */
};

/* Struct to hold protected key and length info */
struct pkey_protkey {
	__u32 type;	 /* key type, one of the PKEY_KEYTYPE_AES values */
	__u32 len;		/* bytes actually stored in protkey[]	 */
	__u8  protkey[MAXPROTKEYSIZE];	       /* the protected key blob */
};

/* Struct to hold an AES clear key value */
struct pkey_clrkey {
	__u8  clrkey[MAXCLRKEYSIZE]; /* 16, 24, or 32 byte clear key value */
};

/*
 * EP11 key blobs of type PKEY_TYPE_EP11_AES and PKEY_TYPE_EP11_ECC
 * are ep11 blobs prepended by this header:
 */
struct ep11kblob_header {
	__u8  type;	/* always 0x00 */
	__u8  hver;	/* header version,  currently needs to be 0x00 */
	__u16 len;	/* total length in bytes (including this header) */
	__u8  version;	/* PKEY_TYPE_EP11_AES or PKEY_TYPE_EP11_ECC */
	__u8  res0;	/* unused */
	__u16 bitlen;	/* clear key bit len, 0 for unknown */
	__u8  res1[8];	/* unused */
} __packed;

/*
 * Generate CCA AES secure key.
 */
struct pkey_genseck {
	__u16 cardnr;		    /* in: card to use or FFFF for any	 */
	__u16 domain;		    /* in: domain or FFFF for any	 */
	__u32 keytype;		    /* in: key type to generate		 */
	struct pkey_seckey seckey;  /* out: the secure key blob		 */
};
#define PKEY_GENSECK _IOWR(PKEY_IOCTL_MAGIC, 0x01, struct pkey_genseck)

/*
 * Construct CCA AES secure key from clear key value
 */
struct pkey_clr2seck {
	__u16 cardnr;		    /* in: card to use or FFFF for any	 */
	__u16 domain;		    /* in: domain or FFFF for any	 */
	__u32 keytype;		    /* in: key type to generate		 */
	struct pkey_clrkey clrkey;  /* in: the clear key value		 */
	struct pkey_seckey seckey;  /* out: the secure key blob		 */
};
#define PKEY_CLR2SECK _IOWR(PKEY_IOCTL_MAGIC, 0x02, struct pkey_clr2seck)

/*
 * Fabricate AES protected key from a CCA AES secure key
 */
struct pkey_sec2protk {
	__u16 cardnr;		     /* in: card to use or FFFF for any   */
	__u16 domain;		     /* in: domain or FFFF for any	  */
	struct pkey_seckey seckey;   /* in: the secure key blob		  */
	struct pkey_protkey protkey; /* out: the protected key		  */
};
#define PKEY_SEC2PROTK _IOWR(PKEY_IOCTL_MAGIC, 0x03, struct pkey_sec2protk)

/*
 * Fabricate AES protected key from clear key value
 */
struct pkey_clr2protk {
	__u32 keytype;		     /* in: key type to generate	  */
	struct pkey_clrkey clrkey;   /* in: the clear key value		  */
	struct pkey_protkey protkey; /* out: the protected key		  */
};
#define PKEY_CLR2PROTK _IOWR(PKEY_IOCTL_MAGIC, 0x04, struct pkey_clr2protk)

/*
 * Search for matching crypto card based on the Master Key
 * Verification Pattern provided inside a CCA AES secure key.
 */
struct pkey_findcard {
	struct pkey_seckey seckey;	       /* in: the secure key blob */
	__u16  cardnr;			       /* out: card number	  */
	__u16  domain;			       /* out: domain number	  */
};
#define PKEY_FINDCARD _IOWR(PKEY_IOCTL_MAGIC, 0x05, struct pkey_findcard)

/*
 * Combined together: findcard + sec2prot
 */
struct pkey_skey2pkey {
	struct pkey_seckey seckey;   /* in: the secure key blob		  */
	struct pkey_protkey protkey; /* out: the protected key		  */
};
#define PKEY_SKEY2PKEY _IOWR(PKEY_IOCTL_MAGIC, 0x06, struct pkey_skey2pkey)

/*
 * Verify the given CCA AES secure key for being able to be usable with
 * the pkey module. Check for correct key type and check for having at
 * least one crypto card being able to handle this key (master key
 * or old master key verification pattern matches).
 * Return some info about the key: keysize in bits, keytype (currently
 * only AES), flag if key is wrapped with an old MKVP.
 */
struct pkey_verifykey {
	struct pkey_seckey seckey;	       /* in: the secure key blob */
	__u16  cardnr;			       /* out: card number	  */
	__u16  domain;			       /* out: domain number	  */
	__u16  keysize;			       /* out: key size in bits   */
	__u32  attributes;		       /* out: attribute bits	  */
};
#define PKEY_VERIFYKEY _IOWR(PKEY_IOCTL_MAGIC, 0x07, struct pkey_verifykey)
#define PKEY_VERIFY_ATTR_AES	   0x00000001  /* key is an AES key */
#define PKEY_VERIFY_ATTR_OLD_MKVP  0x00000100  /* key has old MKVP value */

/*
 * Generate AES random protected key.
 */
struct pkey_genprotk {
	__u32 keytype;			       /* in: key type to generate */
	struct pkey_protkey protkey;	       /* out: the protected key   */
};

#define PKEY_GENPROTK _IOWR(PKEY_IOCTL_MAGIC, 0x08, struct pkey_genprotk)

/*
 * Verify an AES protected key.
 */
struct pkey_verifyprotk {
	struct pkey_protkey protkey;	/* in: the protected key to verify */
};

#define PKEY_VERIFYPROTK _IOW(PKEY_IOCTL_MAGIC, 0x09, struct pkey_verifyprotk)

/*
 * Transform an key blob (of any type) into a protected key
 */
struct pkey_kblob2pkey {
	__u8 __user *key;		/* in: the key blob	   */
	__u32 keylen;			/* in: the key blob length */
	struct pkey_protkey protkey;	/* out: the protected key  */
};
#define PKEY_KBLOB2PROTK _IOWR(PKEY_IOCTL_MAGIC, 0x0A, struct pkey_kblob2pkey)

/*
 * Generate secure key, version 2.
 * Generate CCA AES secure key, CCA AES cipher key or EP11 AES secure key.
 * There needs to be a list of apqns given with at least one entry in there.
 * All apqns in the list need to be exact apqns, 0xFFFF as ANY card or domain
 * is not supported. The implementation walks through the list of apqns and
 * tries to send the request to each apqn without any further checking (like
 * card type or online state). If the apqn fails, simple the next one in the
 * list is tried until success (return 0) or the end of the list is reached
 * (return -1 with errno ENODEV). You may use the PKEY_APQNS4KT ioctl to
 * generate a list of apqns based on the key type to generate.
 * The keygenflags argument is passed to the low level generation functions
 * individual for the key type and has a key type specific meaning. When
 * generating CCA cipher keys you can use one or more of the PKEY_KEYGEN_*
 * flags to widen the export possibilities. By default a cipher key is
 * only exportable for CPACF (PKEY_KEYGEN_XPRT_CPAC).
 * The keygenflag argument for generating an EP11 AES key should either be 0
 * to use the defaults which are XCP_BLOB_ENCRYPT, XCP_BLOB_DECRYPT and
 * XCP_BLOB_PROTKEY_EXTRACTABLE or a valid combination of XCP_BLOB_* flags.
 */
struct pkey_genseck2 {
	struct pkey_apqn __user *apqns; /* in: ptr to list of apqn targets*/
	__u32 apqn_entries;	    /* in: # of apqn target list entries  */
	enum pkey_key_type type;    /* in: key type to generate		  */
	enum pkey_key_size size;    /* in: key size to generate		  */
	__u32 keygenflags;	    /* in: key generation flags		  */
	__u8 __user *key;	    /* in: pointer to key blob buffer	  */
	__u32 keylen;		    /* in: available key blob buffer size */
				    /* out: actual key blob size	  */
};
#define PKEY_GENSECK2 _IOWR(PKEY_IOCTL_MAGIC, 0x11, struct pkey_genseck2)

/*
 * Generate secure key from clear key value, version 2.
 * Construct an CCA AES secure key, CCA AES cipher key or EP11 AES secure
 * key from a given clear key value.
 * There needs to be a list of apqns given with at least one entry in there.
 * All apqns in the list need to be exact apqns, 0xFFFF as ANY card or domain
 * is not supported. The implementation walks through the list of apqns and
 * tries to send the request to each apqn without any further checking (like
 * card type or online state). If the apqn fails, simple the next one in the
 * list is tried until success (return 0) or the end of the list is reached
 * (return -1 with errno ENODEV). You may use the PKEY_APQNS4KT ioctl to
 * generate a list of apqns based on the key type to generate.
 * The keygenflags argument is passed to the low level generation functions
 * individual for the key type and has a key type specific meaning. When
 * generating CCA cipher keys you can use one or more of the PKEY_KEYGEN_*
 * flags to widen the export possibilities. By default a cipher key is
 * only exportable for CPACF (PKEY_KEYGEN_XPRT_CPAC).
 * The keygenflag argument for generating an EP11 AES key should either be 0
 * to use the defaults which are XCP_BLOB_ENCRYPT, XCP_BLOB_DECRYPT and
 * XCP_BLOB_PROTKEY_EXTRACTABLE or a valid combination of XCP_BLOB_* flags.
 */
struct pkey_clr2seck2 {
	struct pkey_apqn __user *apqns; /* in: ptr to list of apqn targets */
	__u32 apqn_entries;	    /* in: # of apqn target list entries   */
	enum pkey_key_type type;    /* in: key type to generate		   */
	enum pkey_key_size size;    /* in: key size to generate		   */
	__u32 keygenflags;	    /* in: key generation flags		   */
	struct pkey_clrkey clrkey;  /* in: the clear key value		   */
	__u8 __user *key;	    /* in: pointer to key blob buffer	   */
	__u32 keylen;		    /* in: available key blob buffer size  */
				    /* out: actual key blob size	   */
};
#define PKEY_CLR2SECK2 _IOWR(PKEY_IOCTL_MAGIC, 0x12, struct pkey_clr2seck2)

/*
 * Verify the given secure key, version 2.
 * Check for correct key type. If cardnr and domain are given (are not
 * 0xFFFF) also check if this apqn is able to handle this type of key.
 * If cardnr and/or domain is 0xFFFF, on return these values are filled
 * with one apqn able to handle this key.
 * The function also checks for the master key verification patterns
 * of the key matching to the current or alternate mkvp of the apqn.
 * For CCA AES secure keys and CCA AES cipher keys this means to check
 * the key's mkvp against the current or old mkvp of the apqns. The flags
 * field is updated with some additional info about the apqn mkvp
 * match: If the current mkvp matches to the key's mkvp then the
 * PKEY_FLAGS_MATCH_CUR_MKVP bit is set, if the alternate mkvp matches to
 * the key's mkvp the PKEY_FLAGS_MATCH_ALT_MKVP is set. For CCA keys the
 * alternate mkvp is the old master key verification pattern.
 * CCA AES secure keys are also checked to have the CPACF export allowed
 * bit enabled (XPRTCPAC) in the kmf1 field.
 * EP11 keys are also supported and the wkvp of the key is checked against
 * the current wkvp of the apqns. There is no alternate for this type of
 * key and so on a match the flag PKEY_FLAGS_MATCH_CUR_MKVP always is set.
 * EP11 keys are also checked to have XCP_BLOB_PROTKEY_EXTRACTABLE set.
 * The ioctl returns 0 as long as the given or found apqn matches to
 * matches with the current or alternate mkvp to the key's mkvp. If the given
 * apqn does not match or there is no such apqn found, -1 with errno
 * ENODEV is returned.
 */
struct pkey_verifykey2 {
	__u8 __user *key;	    /* in: pointer to key blob		 */
	__u32 keylen;		    /* in: key blob size		 */
	__u16 cardnr;		    /* in/out: card number		 */
	__u16 domain;		    /* in/out: domain number		 */
	enum pkey_key_type type;    /* out: the key type		 */
	enum pkey_key_size size;    /* out: the key size		 */
	__u32 flags;		    /* out: additional key info flags	 */
};
#define PKEY_VERIFYKEY2 _IOWR(PKEY_IOCTL_MAGIC, 0x17, struct pkey_verifykey2)

/*
 * Transform a key blob into a protected key, version 2.
 * There needs to be a list of apqns given with at least one entry in there.
 * All apqns in the list need to be exact apqns, 0xFFFF as ANY card or domain
 * is not supported. The implementation walks through the list of apqns and
 * tries to send the request to each apqn without any further checking (like
 * card type or online state). If the apqn fails, simple the next one in the
 * list is tried until success (return 0) or the end of the list is reached
 * (return -1 with errno ENODEV). You may use the PKEY_APQNS4K ioctl to
 * generate a list of apqns based on the key.
 * Deriving ECC protected keys from ECC secure keys is not supported with
 * this ioctl, use PKEY_KBLOB2PROTK3 for this purpose.
 */
struct pkey_kblob2pkey2 {
	__u8 __user *key;	     /* in: pointer to key blob		   */
	__u32 keylen;		     /* in: key blob size		   */
	struct pkey_apqn __user *apqns; /* in: ptr to list of apqn targets */
	__u32 apqn_entries;	     /* in: # of apqn target list entries  */
	struct pkey_protkey protkey; /* out: the protected key		   */
};
#define PKEY_KBLOB2PROTK2 _IOWR(PKEY_IOCTL_MAGIC, 0x1A, struct pkey_kblob2pkey2)

/*
 * Build a list of APQNs based on a key blob given.
 * Is able to find out which type of secure key is given (CCA AES secure
 * key, CCA AES cipher key, CCA ECC private key, EP11 AES key, EP11 ECC private
 * key) and tries to find all matching crypto cards based on the MKVP and maybe
 * other criteria (like CCA AES cipher keys need a CEX5C or higher, EP11 keys
 * with BLOB_PKEY_EXTRACTABLE need a CEX7 and EP11 api version 4). The list of
 * APQNs is further filtered by the key's mkvp which needs to match to either
 * the current mkvp (CCA and EP11) or the alternate mkvp (old mkvp, CCA adapters
 * only) of the apqns. The flags argument may be used to limit the matching
 * apqns. If the PKEY_FLAGS_MATCH_CUR_MKVP is given, only the current mkvp of
 * each apqn is compared. Likewise with the PKEY_FLAGS_MATCH_ALT_MKVP. If both
 * are given, it is assumed to return apqns where either the current or the
 * alternate mkvp matches. At least one of the matching flags needs to be given.
 * The flags argument for EP11 keys has no further action and is currently
 * ignored (but needs to be given as PKEY_FLAGS_MATCH_CUR_MKVP) as there is only
 * the wkvp from the key to match against the apqn's wkvp.
 * The list of matching apqns is stored into the space given by the apqns
 * argument and the number of stored entries goes into apqn_entries. If the list
 * is empty (apqn_entries is 0) the apqn_entries field is updated to the number
 * of apqn targets found and the ioctl returns with 0. If apqn_entries is > 0
 * but the number of apqn targets does not fit into the list, the apqn_targets
 * field is updated with the number of required entries but there are no apqn
 * values stored in the list and the ioctl returns with ENOSPC. If no matching
 * APQN is found, the ioctl returns with 0 but the apqn_entries value is 0.
 */
struct pkey_apqns4key {
	__u8 __user *key;	   /* in: pointer to key blob		      */
	__u32 keylen;		   /* in: key blob size			      */
	__u32 flags;		   /* in: match controlling flags	      */
	struct pkey_apqn __user *apqns; /* in/out: ptr to list of apqn targets*/
	__u32 apqn_entries;	   /* in: max # of apqn entries in the list   */
				   /* out: # apqns stored into the list	      */
};
#define PKEY_APQNS4K _IOWR(PKEY_IOCTL_MAGIC, 0x1B, struct pkey_apqns4key)

/*
 * Build a list of APQNs based on a key type given.
 * Build a list of APQNs based on a given key type and maybe further
 * restrict the list by given master key verification patterns.
 * For different key types there may be different ways to match the
 * master key verification patterns. For CCA keys (CCA data key and CCA
 * cipher key) the first 8 bytes of cur_mkvp refer to the current AES mkvp value
 * of the apqn and the first 8 bytes of the alt_mkvp refer to the old AES mkvp.
 * For CCA ECC keys it is similar but the match is against the APKA current/old
 * mkvp. The flags argument controls if the apqns current and/or alternate mkvp
 * should match. If the PKEY_FLAGS_MATCH_CUR_MKVP is given, only the current
 * mkvp of each apqn is compared. Likewise with the PKEY_FLAGS_MATCH_ALT_MKVP.
 * If both are given, it is assumed to return apqns where either the
 * current or the alternate mkvp matches. If no match flag is given
 * (flags is 0) the mkvp values are ignored for the match process.
 * For EP11 keys there is only the current wkvp. So if the apqns should also
 * match to a given wkvp, then the PKEY_FLAGS_MATCH_CUR_MKVP flag should be
 * set. The wkvp value is 32 bytes but only the leftmost 16 bytes are compared
 * against the leftmost 16 byte of the wkvp of the apqn.
 * The list of matching apqns is stored into the space given by the apqns
 * argument and the number of stored entries goes into apqn_entries. If the list
 * is empty (apqn_entries is 0) the apqn_entries field is updated to the number
 * of apqn targets found and the ioctl returns with 0. If apqn_entries is > 0
 * but the number of apqn targets does not fit into the list, the apqn_targets
 * field is updated with the number of required entries but there are no apqn
 * values stored in the list and the ioctl returns with ENOSPC. If no matching
 * APQN is found, the ioctl returns with 0 but the apqn_entries value is 0.
 */
struct pkey_apqns4keytype {
	enum pkey_key_type type;   /* in: key type			      */
	__u8  cur_mkvp[32];	   /* in: current mkvp			      */
	__u8  alt_mkvp[32];	   /* in: alternate mkvp		      */
	__u32 flags;		   /* in: match controlling flags	      */
	struct pkey_apqn __user *apqns; /* in/out: ptr to list of apqn targets*/
	__u32 apqn_entries;	   /* in: max # of apqn entries in the list   */
				   /* out: # apqns stored into the list	      */
};
#define PKEY_APQNS4KT _IOWR(PKEY_IOCTL_MAGIC, 0x1C, struct pkey_apqns4keytype)

/*
 * Transform a key blob into a protected key, version 3.
 * The difference to version 2 of this ioctl is that the protected key
 * buffer is now explicitly and not within a struct pkey_protkey any more.
 * So this ioctl is also able to handle EP11 and CCA ECC secure keys and
 * provide ECC protected keys.
 * There needs to be a list of apqns given with at least one entry in there.
 * All apqns in the list need to be exact apqns, 0xFFFF as ANY card or domain
 * is not supported. The implementation walks through the list of apqns and
 * tries to send the request to each apqn without any further checking (like
 * card type or online state). If the apqn fails, simple the next one in the
 * list is tried until success (return 0) or the end of the list is reached
 * (return -1 with errno ENODEV). You may use the PKEY_APQNS4K ioctl to
 * generate a list of apqns based on the key.
 */
struct pkey_kblob2pkey3 {
	__u8 __user *key;	     /* in: pointer to key blob		   */
	__u32 keylen;		     /* in: key blob size		   */
	struct pkey_apqn __user *apqns; /* in: ptr to list of apqn targets */
	__u32 apqn_entries;	     /* in: # of apqn target list entries  */
	__u32 pkeytype;		/* out: prot key type (enum pkey_key_type) */
	__u32 pkeylen;	 /* in/out: size of pkey buffer/actual len of pkey */
	__u8 __user *pkey;		 /* in: pkey blob buffer space ptr */
};
#define PKEY_KBLOB2PROTK3 _IOWR(PKEY_IOCTL_MAGIC, 0x1D, struct pkey_kblob2pkey3)

#endif /* _UAPI_PKEY_H */
