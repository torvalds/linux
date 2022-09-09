/* SPDX-License-Identifier: GPL-2.0 */
/*
 * caam - Freescale FSL CAAM support for Public Key Cryptography descriptors
 *
 * Copyright 2016 Freescale Semiconductor, Inc.
 *
 * There is no Shared Descriptor for PKC so that the Job Descriptor must carry
 * all the desired key parameters, input and output pointers.
 */

#ifndef _PKC_DESC_H_
#define _PKC_DESC_H_
#include "compat.h"
#include "pdb.h"
#include <crypto/engine.h>

/**
 * caam_priv_key_form - CAAM RSA private key representation
 * CAAM RSA private key may have either of three forms.
 *
 * 1. The first representation consists of the pair (n, d), where the
 *    components have the following meanings:
 *        n      the RSA modulus
 *        d      the RSA private exponent
 *
 * 2. The second representation consists of the triplet (p, q, d), where the
 *    components have the following meanings:
 *        p      the first prime factor of the RSA modulus n
 *        q      the second prime factor of the RSA modulus n
 *        d      the RSA private exponent
 *
 * 3. The third representation consists of the quintuple (p, q, dP, dQ, qInv),
 *    where the components have the following meanings:
 *        p      the first prime factor of the RSA modulus n
 *        q      the second prime factor of the RSA modulus n
 *        dP     the first factors's CRT exponent
 *        dQ     the second factors's CRT exponent
 *        qInv   the (first) CRT coefficient
 *
 * The benefit of using the third or the second key form is lower computational
 * cost for the decryption and signature operations.
 */
enum caam_priv_key_form {
	FORM1,
	FORM2,
	FORM3
};

/**
 * caam_rsa_key - CAAM RSA key structure. Keys are allocated in DMA zone.
 * @n           : RSA modulus raw byte stream
 * @e           : RSA public exponent raw byte stream
 * @d           : RSA private exponent raw byte stream
 * @p           : RSA prime factor p of RSA modulus n
 * @q           : RSA prime factor q of RSA modulus n
 * @dp          : RSA CRT exponent of p
 * @dp          : RSA CRT exponent of q
 * @qinv        : RSA CRT coefficient
 * @tmp1        : CAAM uses this temporary buffer as internal state buffer.
 *                It is assumed to be as long as p.
 * @tmp2        : CAAM uses this temporary buffer as internal state buffer.
 *                It is assumed to be as long as q.
 * @n_sz        : length in bytes of RSA modulus n
 * @e_sz        : length in bytes of RSA public exponent
 * @d_sz        : length in bytes of RSA private exponent
 * @p_sz        : length in bytes of RSA prime factor p of RSA modulus n
 * @q_sz        : length in bytes of RSA prime factor q of RSA modulus n
 * @priv_form   : CAAM RSA private key representation
 */
struct caam_rsa_key {
	u8 *n;
	u8 *e;
	u8 *d;
	u8 *p;
	u8 *q;
	u8 *dp;
	u8 *dq;
	u8 *qinv;
	u8 *tmp1;
	u8 *tmp2;
	size_t n_sz;
	size_t e_sz;
	size_t d_sz;
	size_t p_sz;
	size_t q_sz;
	enum caam_priv_key_form priv_form;
};

/**
 * caam_rsa_ctx - per session context.
 * @enginectx   : crypto engine context
 * @key         : RSA key in DMA zone
 * @dev         : device structure
 * @padding_dma : dma address of padding, for adding it to the input
 */
struct caam_rsa_ctx {
	struct crypto_engine_ctx enginectx;
	struct caam_rsa_key key;
	struct device *dev;
	dma_addr_t padding_dma;

};

/**
 * caam_rsa_req_ctx - per request context.
 * @src           : input scatterlist (stripped of leading zeros)
 * @fixup_src     : input scatterlist (that might be stripped of leading zeros)
 * @fixup_src_len : length of the fixup_src input scatterlist
 * @edesc         : s/w-extended rsa descriptor
 * @akcipher_op_done : callback used when operation is done
 */
struct caam_rsa_req_ctx {
	struct scatterlist src[2];
	struct scatterlist *fixup_src;
	unsigned int fixup_src_len;
	struct rsa_edesc *edesc;
	void (*akcipher_op_done)(struct device *jrdev, u32 *desc, u32 err,
				 void *context);
};

/**
 * rsa_edesc - s/w-extended rsa descriptor
 * @src_nents     : number of segments in input s/w scatterlist
 * @dst_nents     : number of segments in output s/w scatterlist
 * @mapped_src_nents: number of segments in input h/w link table
 * @mapped_dst_nents: number of segments in output h/w link table
 * @sec4_sg_bytes : length of h/w link table
 * @bklog         : stored to determine if the request needs backlog
 * @sec4_sg_dma   : dma address of h/w link table
 * @sec4_sg       : pointer to h/w link table
 * @pdb           : specific RSA Protocol Data Block (PDB)
 * @hw_desc       : descriptor followed by link tables if any
 */
struct rsa_edesc {
	int src_nents;
	int dst_nents;
	int mapped_src_nents;
	int mapped_dst_nents;
	int sec4_sg_bytes;
	bool bklog;
	dma_addr_t sec4_sg_dma;
	struct sec4_sg_entry *sec4_sg;
	union {
		struct rsa_pub_pdb pub;
		struct rsa_priv_f1_pdb priv_f1;
		struct rsa_priv_f2_pdb priv_f2;
		struct rsa_priv_f3_pdb priv_f3;
	} pdb;
	u32 hw_desc[];
};

/* Descriptor construction primitives. */
void init_rsa_pub_desc(u32 *desc, struct rsa_pub_pdb *pdb);
void init_rsa_priv_f1_desc(u32 *desc, struct rsa_priv_f1_pdb *pdb);
void init_rsa_priv_f2_desc(u32 *desc, struct rsa_priv_f2_pdb *pdb);
void init_rsa_priv_f3_desc(u32 *desc, struct rsa_priv_f3_pdb *pdb);

#endif
