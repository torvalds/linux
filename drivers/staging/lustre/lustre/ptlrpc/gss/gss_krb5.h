/*
 * Modifications for Lustre
 *
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

/*
 *  linux/include/linux/sunrpc/gss_krb5_types.h
 *
 *  Adapted from MIT Kerberos 5-1.2.1 lib/include/krb5.h,
 *  lib/gssapi/krb5/gssapiP_krb5.h, and others
 *
 *  Copyright (c) 2000 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson   <andros@umich.edu>
 *  Bruce Fields   <bfields@umich.edu>
 */

/*
 * Copyright 1995 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 */

#ifndef PTLRPC_GSS_KRB5_H
#define PTLRPC_GSS_KRB5_H

/*
 * RFC 4142
 */

#define KG_USAGE_ACCEPTOR_SEAL	  22
#define KG_USAGE_ACCEPTOR_SIGN	  23
#define KG_USAGE_INITIATOR_SEAL	 24
#define KG_USAGE_INITIATOR_SIGN	 25

#define KG_TOK_MIC_MSG		  0x0404
#define KG_TOK_WRAP_MSG		 0x0504

#define FLAG_SENDER_IS_ACCEPTOR	 0x01
#define FLAG_WRAP_CONFIDENTIAL	  0x02
#define FLAG_ACCEPTOR_SUBKEY	    0x04

struct krb5_header {
	__u16	   kh_tok_id;      /* token id */
	__u8	    kh_flags;       /* acceptor flags */
	__u8	    kh_filler;      /* 0xff */
	__u16	   kh_ec;	  /* extra count */
	__u16	   kh_rrc;	 /* right rotation count */
	__u64	   kh_seq;	 /* sequence number */
	__u8	    kh_cksum[0];    /* checksum */
};

struct krb5_keyblock {
	rawobj_t		 kb_key;
	struct ll_crypto_cipher *kb_tfm;
};

struct krb5_ctx {
	unsigned int	    kc_initiate:1,
				kc_cfx:1,
				kc_seed_init:1,
				kc_have_acceptor_subkey:1;
	__s32		   kc_endtime;
	__u8		    kc_seed[16];
	__u64		   kc_seq_send;
	__u64		   kc_seq_recv;
	__u32		   kc_enctype;
	struct krb5_keyblock    kc_keye;	/* encryption */
	struct krb5_keyblock    kc_keyi;	/* integrity */
	struct krb5_keyblock    kc_keyc;	/* checksum */
	rawobj_t		kc_mech_used;
};

enum sgn_alg {
	SGN_ALG_DES_MAC_MD5	   = 0x0000,
	SGN_ALG_MD2_5		 = 0x0001,
	SGN_ALG_DES_MAC	       = 0x0002,
	SGN_ALG_3		     = 0x0003, /* not published */
	SGN_ALG_HMAC_MD5	      = 0x0011, /* microsoft w2k; no support */
	SGN_ALG_HMAC_SHA1_DES3_KD     = 0x0004
};

enum seal_alg {
	SEAL_ALG_NONE		 = 0xffff,
	SEAL_ALG_DES		  = 0x0000,
	SEAL_ALG_1		    = 0x0001, /* not published */
	SEAL_ALG_MICROSOFT_RC4	= 0x0010, /* microsoft w2k; no support */
	SEAL_ALG_DES3KD	       = 0x0002
};

#define CKSUMTYPE_CRC32		 0x0001
#define CKSUMTYPE_RSA_MD4	       0x0002
#define CKSUMTYPE_RSA_MD4_DES	   0x0003
#define CKSUMTYPE_DESCBC		0x0004
/* des-mac-k */
/* rsa-md4-des-k */
#define CKSUMTYPE_RSA_MD5	       0x0007
#define CKSUMTYPE_RSA_MD5_DES	   0x0008
#define CKSUMTYPE_NIST_SHA	      0x0009
#define CKSUMTYPE_HMAC_SHA1_DES3	0x000c
#define CKSUMTYPE_HMAC_SHA1_96_AES128   0x000f
#define CKSUMTYPE_HMAC_SHA1_96_AES256   0x0010
#define CKSUMTYPE_HMAC_MD5_ARCFOUR      -138

/* from gssapi_err_krb5.h */
#define KG_CCACHE_NOMATCH			(39756032L)
#define KG_KEYTAB_NOMATCH			(39756033L)
#define KG_TGT_MISSING			   (39756034L)
#define KG_NO_SUBKEY			     (39756035L)
#define KG_CONTEXT_ESTABLISHED		   (39756036L)
#define KG_BAD_SIGN_TYPE			 (39756037L)
#define KG_BAD_LENGTH			    (39756038L)
#define KG_CTX_INCOMPLETE			(39756039L)
#define KG_CONTEXT			       (39756040L)
#define KG_CRED				  (39756041L)
#define KG_ENC_DESC			      (39756042L)
#define KG_BAD_SEQ			       (39756043L)
#define KG_EMPTY_CCACHE			  (39756044L)
#define KG_NO_CTYPES			     (39756045L)

/* per Kerberos v5 protocol spec crypto types from the wire.
 * these get mapped to linux kernel crypto routines.
 */
#define ENCTYPE_NULL	    0x0000
#define ENCTYPE_DES_CBC_CRC     0x0001	/* DES cbc mode with CRC-32 */
#define ENCTYPE_DES_CBC_MD4     0x0002	/* DES cbc mode with RSA-MD4 */
#define ENCTYPE_DES_CBC_MD5     0x0003	/* DES cbc mode with RSA-MD5 */
#define ENCTYPE_DES_CBC_RAW     0x0004	/* DES cbc mode raw */
/* XXX deprecated? */
#define ENCTYPE_DES3_CBC_SHA    0x0005	/* DES-3 cbc mode with NIST-SHA */
#define ENCTYPE_DES3_CBC_RAW    0x0006	/* DES-3 cbc mode raw */
#define ENCTYPE_DES_HMAC_SHA1   0x0008
#define ENCTYPE_DES3_CBC_SHA1   0x0010
#define ENCTYPE_AES128_CTS_HMAC_SHA1_96 0x0011
#define ENCTYPE_AES256_CTS_HMAC_SHA1_96 0x0012
#define ENCTYPE_ARCFOUR_HMAC    0x0017
#define ENCTYPE_ARCFOUR_HMAC_EXP 0x0018
#define ENCTYPE_UNKNOWN	 0x01ff

#endif /* PTLRPC_GSS_KRB5_H */
