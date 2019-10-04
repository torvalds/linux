// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Shared descriptors for ahash algorithms
 *
 * Copyright 2017-2019 NXP
 */

#include "compat.h"
#include "desc_constr.h"
#include "caamhash_desc.h"

/**
 * cnstr_shdsc_ahash - ahash shared descriptor
 * @desc: pointer to buffer used for descriptor construction
 * @adata: pointer to authentication transform definitions.
 *         A split key is required for SEC Era < 6; the size of the split key
 *         is specified in this case.
 *         Valid algorithm values - one of OP_ALG_ALGSEL_{MD5, SHA1, SHA224,
 *         SHA256, SHA384, SHA512}.
 * @state: algorithm state OP_ALG_AS_{INIT, FINALIZE, INITFINALIZE, UPDATE}
 * @digestsize: algorithm's digest size
 * @ctx_len: size of Context Register
 * @import_ctx: true if previous Context Register needs to be restored
 *              must be true for ahash update and final
 *              must be false for for ahash first and digest
 * @era: SEC Era
 */
void cnstr_shdsc_ahash(u32 * const desc, struct alginfo *adata, u32 state,
		       int digestsize, int ctx_len, bool import_ctx, int era)
{
	u32 op = adata->algtype;

	init_sh_desc(desc, HDR_SHARE_SERIAL);

	/* Append key if it has been set; ahash update excluded */
	if (state != OP_ALG_AS_UPDATE && adata->keylen) {
		u32 *skip_key_load;

		/* Skip key loading if already shared */
		skip_key_load = append_jump(desc, JUMP_JSL | JUMP_TEST_ALL |
					    JUMP_COND_SHRD);

		if (era < 6)
			append_key_as_imm(desc, adata->key_virt,
					  adata->keylen_pad,
					  adata->keylen, CLASS_2 |
					  KEY_DEST_MDHA_SPLIT | KEY_ENC);
		else
			append_proto_dkp(desc, adata);

		set_jump_tgt_here(desc, skip_key_load);

		op |= OP_ALG_AAI_HMAC_PRECOMP;
	}

	/* If needed, import context from software */
	if (import_ctx)
		append_seq_load(desc, ctx_len, LDST_CLASS_2_CCB |
				LDST_SRCDST_BYTE_CONTEXT);

	/* Class 2 operation */
	append_operation(desc, op | state | OP_ALG_ENCRYPT);

	/*
	 * Load from buf and/or src and write to req->result or state->context
	 * Calculate remaining bytes to read
	 */
	append_math_add(desc, VARSEQINLEN, SEQINLEN, REG0, CAAM_CMD_SZ);
	/* Read remaining bytes */
	append_seq_fifo_load(desc, 0, FIFOLD_CLASS_CLASS2 | FIFOLD_TYPE_LAST2 |
			     FIFOLD_TYPE_MSG | KEY_VLF);
	/* Store class2 context bytes */
	append_seq_store(desc, digestsize, LDST_CLASS_2_CCB |
			 LDST_SRCDST_BYTE_CONTEXT);
}
EXPORT_SYMBOL(cnstr_shdsc_ahash);

/**
 * cnstr_shdsc_sk_hash - shared descriptor for symmetric key cipher-based
 *                       hash algorithms
 * @desc: pointer to buffer used for descriptor construction
 * @adata: pointer to authentication transform definitions.
 * @state: algorithm state OP_ALG_AS_{INIT, FINALIZE, INITFINALIZE, UPDATE}
 * @digestsize: algorithm's digest size
 * @ctx_len: size of Context Register
 */
void cnstr_shdsc_sk_hash(u32 * const desc, struct alginfo *adata, u32 state,
			 int digestsize, int ctx_len)
{
	u32 *skip_key_load;

	init_sh_desc(desc, HDR_SHARE_SERIAL | HDR_SAVECTX);

	/* Skip loading of key, context if already shared */
	skip_key_load = append_jump(desc, JUMP_TEST_ALL | JUMP_COND_SHRD);

	if (state == OP_ALG_AS_INIT || state == OP_ALG_AS_INITFINAL) {
		append_key_as_imm(desc, adata->key_virt, adata->keylen,
				  adata->keylen, CLASS_1 | KEY_DEST_CLASS_REG);
	} else { /* UPDATE, FINALIZE */
		if (is_xcbc_aes(adata->algtype))
			/* Load K1 */
			append_key(desc, adata->key_dma, adata->keylen,
				   CLASS_1 | KEY_DEST_CLASS_REG | KEY_ENC);
		else /* CMAC */
			append_key_as_imm(desc, adata->key_virt, adata->keylen,
					  adata->keylen, CLASS_1 |
					  KEY_DEST_CLASS_REG);
		/* Restore context */
		append_seq_load(desc, ctx_len, LDST_CLASS_1_CCB |
				LDST_SRCDST_BYTE_CONTEXT);
	}

	set_jump_tgt_here(desc, skip_key_load);

	/* Class 1 operation */
	append_operation(desc, adata->algtype | state | OP_ALG_ENCRYPT);

	/*
	 * Load from buf and/or src and write to req->result or state->context
	 * Calculate remaining bytes to read
	 */
	append_math_add(desc, VARSEQINLEN, SEQINLEN, REG0, CAAM_CMD_SZ);

	/* Read remaining bytes */
	append_seq_fifo_load(desc, 0, FIFOLD_CLASS_CLASS1 | FIFOLD_TYPE_LAST1 |
			     FIFOLD_TYPE_MSG | FIFOLDST_VLF);

	/*
	 * Save context:
	 * - xcbc: partial hash, keys K2 and K3
	 * - cmac: partial hash, constant L = E(K,0)
	 */
	append_seq_store(desc, digestsize, LDST_CLASS_1_CCB |
			 LDST_SRCDST_BYTE_CONTEXT);
	if (is_xcbc_aes(adata->algtype) && state == OP_ALG_AS_INIT)
		/* Save K1 */
		append_fifo_store(desc, adata->key_dma, adata->keylen,
				  LDST_CLASS_1_CCB | FIFOST_TYPE_KEY_KEK);
}
EXPORT_SYMBOL(cnstr_shdsc_sk_hash);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("FSL CAAM ahash descriptors support");
MODULE_AUTHOR("NXP Semiconductors");
