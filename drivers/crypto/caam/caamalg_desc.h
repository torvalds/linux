/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared descriptors for aead, ablkcipher algorithms
 *
 * Copyright 2016 NXP
 */

#ifndef _CAAMALG_DESC_H_
#define _CAAMALG_DESC_H_

/* length of descriptors text */
#define DESC_AEAD_BASE			(4 * CAAM_CMD_SZ)
#define DESC_AEAD_ENC_LEN		(DESC_AEAD_BASE + 11 * CAAM_CMD_SZ)
#define DESC_AEAD_DEC_LEN		(DESC_AEAD_BASE + 15 * CAAM_CMD_SZ)
#define DESC_AEAD_GIVENC_LEN		(DESC_AEAD_ENC_LEN + 7 * CAAM_CMD_SZ)
#define DESC_QI_AEAD_ENC_LEN		(DESC_AEAD_ENC_LEN + 3 * CAAM_CMD_SZ)
#define DESC_QI_AEAD_DEC_LEN		(DESC_AEAD_DEC_LEN + 3 * CAAM_CMD_SZ)
#define DESC_QI_AEAD_GIVENC_LEN		(DESC_AEAD_GIVENC_LEN + 3 * CAAM_CMD_SZ)

/* Note: Nonce is counted in cdata.keylen */
#define DESC_AEAD_CTR_RFC3686_LEN	(4 * CAAM_CMD_SZ)

#define DESC_AEAD_NULL_BASE		(3 * CAAM_CMD_SZ)
#define DESC_AEAD_NULL_ENC_LEN		(DESC_AEAD_NULL_BASE + 11 * CAAM_CMD_SZ)
#define DESC_AEAD_NULL_DEC_LEN		(DESC_AEAD_NULL_BASE + 13 * CAAM_CMD_SZ)

#define DESC_GCM_BASE			(3 * CAAM_CMD_SZ)
#define DESC_GCM_ENC_LEN		(DESC_GCM_BASE + 16 * CAAM_CMD_SZ)
#define DESC_GCM_DEC_LEN		(DESC_GCM_BASE + 12 * CAAM_CMD_SZ)

#define DESC_RFC4106_BASE		(3 * CAAM_CMD_SZ)
#define DESC_RFC4106_ENC_LEN		(DESC_RFC4106_BASE + 13 * CAAM_CMD_SZ)
#define DESC_RFC4106_DEC_LEN		(DESC_RFC4106_BASE + 13 * CAAM_CMD_SZ)

#define DESC_RFC4543_BASE		(3 * CAAM_CMD_SZ)
#define DESC_RFC4543_ENC_LEN		(DESC_RFC4543_BASE + 11 * CAAM_CMD_SZ)
#define DESC_RFC4543_DEC_LEN		(DESC_RFC4543_BASE + 12 * CAAM_CMD_SZ)

#define DESC_ABLKCIPHER_BASE		(3 * CAAM_CMD_SZ)
#define DESC_ABLKCIPHER_ENC_LEN		(DESC_ABLKCIPHER_BASE + \
					 20 * CAAM_CMD_SZ)
#define DESC_ABLKCIPHER_DEC_LEN		(DESC_ABLKCIPHER_BASE + \
					 15 * CAAM_CMD_SZ)

void cnstr_shdsc_aead_null_encap(u32 * const desc, struct alginfo *adata,
				 unsigned int icvsize, int era);

void cnstr_shdsc_aead_null_decap(u32 * const desc, struct alginfo *adata,
				 unsigned int icvsize, int era);

void cnstr_shdsc_aead_encap(u32 * const desc, struct alginfo *cdata,
			    struct alginfo *adata, unsigned int ivsize,
			    unsigned int icvsize, const bool is_rfc3686,
			    u32 *nonce, const u32 ctx1_iv_off,
			    const bool is_qi, int era);

void cnstr_shdsc_aead_decap(u32 * const desc, struct alginfo *cdata,
			    struct alginfo *adata, unsigned int ivsize,
			    unsigned int icvsize, const bool geniv,
			    const bool is_rfc3686, u32 *nonce,
			    const u32 ctx1_iv_off, const bool is_qi, int era);

void cnstr_shdsc_aead_givencap(u32 * const desc, struct alginfo *cdata,
			       struct alginfo *adata, unsigned int ivsize,
			       unsigned int icvsize, const bool is_rfc3686,
			       u32 *nonce, const u32 ctx1_iv_off,
			       const bool is_qi, int era);

void cnstr_shdsc_gcm_encap(u32 * const desc, struct alginfo *cdata,
			   unsigned int icvsize);

void cnstr_shdsc_gcm_decap(u32 * const desc, struct alginfo *cdata,
			   unsigned int icvsize);

void cnstr_shdsc_rfc4106_encap(u32 * const desc, struct alginfo *cdata,
			       unsigned int icvsize);

void cnstr_shdsc_rfc4106_decap(u32 * const desc, struct alginfo *cdata,
			       unsigned int icvsize);

void cnstr_shdsc_rfc4543_encap(u32 * const desc, struct alginfo *cdata,
			       unsigned int icvsize);

void cnstr_shdsc_rfc4543_decap(u32 * const desc, struct alginfo *cdata,
			       unsigned int icvsize);

void cnstr_shdsc_ablkcipher_encap(u32 * const desc, struct alginfo *cdata,
				  unsigned int ivsize, const bool is_rfc3686,
				  const u32 ctx1_iv_off);

void cnstr_shdsc_ablkcipher_decap(u32 * const desc, struct alginfo *cdata,
				  unsigned int ivsize, const bool is_rfc3686,
				  const u32 ctx1_iv_off);

void cnstr_shdsc_ablkcipher_givencap(u32 * const desc, struct alginfo *cdata,
				     unsigned int ivsize, const bool is_rfc3686,
				     const u32 ctx1_iv_off);

void cnstr_shdsc_xts_ablkcipher_encap(u32 * const desc, struct alginfo *cdata);

void cnstr_shdsc_xts_ablkcipher_decap(u32 * const desc, struct alginfo *cdata);

#endif /* _CAAMALG_DESC_H_ */
