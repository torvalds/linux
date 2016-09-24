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

/**
 * caam_rsa_key - CAAM RSA key structure. Keys are allocated in DMA zone.
 * @n           : RSA modulus raw byte stream
 * @e           : RSA public exponent raw byte stream
 * @d           : RSA private exponent raw byte stream
 * @n_sz        : length in bytes of RSA modulus n
 * @e_sz        : length in bytes of RSA public exponent
 * @d_sz        : length in bytes of RSA private exponent
 */
struct caam_rsa_key {
	u8 *n;
	u8 *e;
	u8 *d;
	size_t n_sz;
	size_t e_sz;
	size_t d_sz;
};

/**
 * caam_rsa_ctx - per session context.
 * @key         : RSA key in DMA zone
 * @dev         : device structure
 */
struct caam_rsa_ctx {
	struct caam_rsa_key key;
	struct device *dev;
};

/**
 * rsa_edesc - s/w-extended rsa descriptor
 * @src_nents     : number of segments in input scatterlist
 * @dst_nents     : number of segments in output scatterlist
 * @sec4_sg_bytes : length of h/w link table
 * @sec4_sg_dma   : dma address of h/w link table
 * @sec4_sg       : pointer to h/w link table
 * @pdb           : specific RSA Protocol Data Block (PDB)
 * @hw_desc       : descriptor followed by link tables if any
 */
struct rsa_edesc {
	int src_nents;
	int dst_nents;
	int sec4_sg_bytes;
	dma_addr_t sec4_sg_dma;
	struct sec4_sg_entry *sec4_sg;
	union {
		struct rsa_pub_pdb pub;
		struct rsa_priv_f1_pdb priv_f1;
	} pdb;
	u32 hw_desc[];
};

/* Descriptor construction primitives. */
void init_rsa_pub_desc(u32 *desc, struct rsa_pub_pdb *pdb);
void init_rsa_priv_f1_desc(u32 *desc, struct rsa_priv_f1_pdb *pdb);

#endif
