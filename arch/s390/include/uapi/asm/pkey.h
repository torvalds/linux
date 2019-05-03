/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace interface to the pkey device driver
 *
 * Copyright IBM Corp. 2017
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

#define SECKEYBLOBSIZE	64     /* secure key blob size is always 64 bytes */
#define PROTKEYBLOBSIZE 80  /* protected key blob size is always 80 bytes */
#define MAXPROTKEYSIZE	64  /* a protected key blob may be up to 64 bytes */
#define MAXCLRKEYSIZE	32     /* a clear key value may be up to 32 bytes */

#define MINKEYBLOBSIZE	SECKEYBLOBSIZE	    /* Minimum size of a key blob */
#define MAXKEYBLOBSIZE	PROTKEYBLOBSIZE     /* Maximum size of a key blob */

/* defines for the type field within the pkey_protkey struct */
#define PKEY_KEYTYPE_AES_128  1
#define PKEY_KEYTYPE_AES_192  2
#define PKEY_KEYTYPE_AES_256  3

/* Struct to hold a secure key blob */
struct pkey_seckey {
	__u8  seckey[SECKEYBLOBSIZE];		  /* the secure key blob */
};

/* Struct to hold protected key and length info */
struct pkey_protkey {
	__u32 type;	     /* key type, one of the PKEY_KEYTYPE values */
	__u32 len;		/* bytes actually stored in protkey[]	 */
	__u8  protkey[MAXPROTKEYSIZE];	       /* the protected key blob */
};

/* Struct to hold a clear key value */
struct pkey_clrkey {
	__u8  clrkey[MAXCLRKEYSIZE]; /* 16, 24, or 32 byte clear key value */
};

/*
 * Generate secure key
 */
struct pkey_genseck {
	__u16 cardnr;		    /* in: card to use or FFFF for any	 */
	__u16 domain;		    /* in: domain or FFFF for any	 */
	__u32 keytype;		    /* in: key type to generate		 */
	struct pkey_seckey seckey;  /* out: the secure key blob		 */
};
#define PKEY_GENSECK _IOWR(PKEY_IOCTL_MAGIC, 0x01, struct pkey_genseck)

/*
 * Construct secure key from clear key value
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
 * Fabricate protected key from a secure key
 */
struct pkey_sec2protk {
	__u16 cardnr;		     /* in: card to use or FFFF for any   */
	__u16 domain;		     /* in: domain or FFFF for any	  */
	struct pkey_seckey seckey;   /* in: the secure key blob		  */
	struct pkey_protkey protkey; /* out: the protected key		  */
};
#define PKEY_SEC2PROTK _IOWR(PKEY_IOCTL_MAGIC, 0x03, struct pkey_sec2protk)

/*
 * Fabricate protected key from an clear key value
 */
struct pkey_clr2protk {
	__u32 keytype;		     /* in: key type to generate	  */
	struct pkey_clrkey clrkey;   /* in: the clear key value		  */
	struct pkey_protkey protkey; /* out: the protected key		  */
};
#define PKEY_CLR2PROTK _IOWR(PKEY_IOCTL_MAGIC, 0x04, struct pkey_clr2protk)

/*
 * Search for matching crypto card based on the Master Key
 * Verification Pattern provided inside a secure key.
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
 * Verify the given secure key for being able to be useable with
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
 * Generate (AES) random protected key.
 */
struct pkey_genprotk {
	__u32 keytype;			       /* in: key type to generate */
	struct pkey_protkey protkey;	       /* out: the protected key   */
};

#define PKEY_GENPROTK _IOWR(PKEY_IOCTL_MAGIC, 0x08, struct pkey_genprotk)

/*
 * Verify an (AES) protected key.
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

#endif /* _UAPI_PKEY_H */
