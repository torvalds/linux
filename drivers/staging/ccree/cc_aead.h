/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

/* \file cc_aead.h
 * ARM CryptoCell AEAD Crypto API
 */

#ifndef __CC_AEAD_H__
#define __CC_AEAD_H__

#include <linux/kernel.h>
#include <crypto/algapi.h>
#include <crypto/ctr.h>

/* mac_cmp - HW writes 8 B but all bytes hold the same value */
#define ICV_CMP_SIZE 8
#define CCM_CONFIG_BUF_SIZE (AES_BLOCK_SIZE * 3)
#define MAX_MAC_SIZE SHA256_DIGEST_SIZE

/* defines for AES GCM configuration buffer */
#define GCM_BLOCK_LEN_SIZE 8

#define GCM_BLOCK_RFC4_IV_OFFSET	4
#define GCM_BLOCK_RFC4_IV_SIZE		8  /* IV size for rfc's */
#define GCM_BLOCK_RFC4_NONCE_OFFSET	0
#define GCM_BLOCK_RFC4_NONCE_SIZE	4

/* Offsets into AES CCM configuration buffer */
#define CCM_B0_OFFSET 0
#define CCM_A0_OFFSET 16
#define CCM_CTR_COUNT_0_OFFSET 32
/* CCM B0 and CTR_COUNT constants. */
#define CCM_BLOCK_NONCE_OFFSET 1  /* Nonce offset inside B0 and CTR_COUNT */
#define CCM_BLOCK_NONCE_SIZE   3  /* Nonce size inside B0 and CTR_COUNT */
#define CCM_BLOCK_IV_OFFSET    4  /* IV offset inside B0 and CTR_COUNT */
#define CCM_BLOCK_IV_SIZE      8  /* IV size inside B0 and CTR_COUNT */

enum aead_ccm_header_size {
	ccm_header_size_null = -1,
	ccm_header_size_zero = 0,
	ccm_header_size_2 = 2,
	ccm_header_size_6 = 6,
	ccm_header_size_max = S32_MAX
};

struct aead_req_ctx {
	/* Allocate cache line although only 4 bytes are needed to
	 *  assure next field falls @ cache line
	 *  Used for both: digest HW compare and CCM/GCM MAC value
	 */
	u8 mac_buf[MAX_MAC_SIZE] ____cacheline_aligned;
	u8 ctr_iv[AES_BLOCK_SIZE] ____cacheline_aligned;

	//used in gcm
	u8 gcm_iv_inc1[AES_BLOCK_SIZE] ____cacheline_aligned;
	u8 gcm_iv_inc2[AES_BLOCK_SIZE] ____cacheline_aligned;
	u8 hkey[AES_BLOCK_SIZE] ____cacheline_aligned;
	struct {
		u8 len_a[GCM_BLOCK_LEN_SIZE] ____cacheline_aligned;
		u8 len_c[GCM_BLOCK_LEN_SIZE];
	} gcm_len_block;

	u8 ccm_config[CCM_CONFIG_BUF_SIZE] ____cacheline_aligned;
	/* HW actual size input */
	unsigned int hw_iv_size ____cacheline_aligned;
	/* used to prevent cache coherence problem */
	u8 backup_mac[MAX_MAC_SIZE];
	u8 *backup_iv; /*store iv for generated IV flow*/
	u8 *backup_giv; /*store iv for rfc3686(ctr) flow*/
	dma_addr_t mac_buf_dma_addr; /* internal ICV DMA buffer */
	/* buffer for internal ccm configurations */
	dma_addr_t ccm_iv0_dma_addr;
	dma_addr_t icv_dma_addr; /* Phys. address of ICV */

	//used in gcm
	/* buffer for internal gcm configurations */
	dma_addr_t gcm_iv_inc1_dma_addr;
	/* buffer for internal gcm configurations */
	dma_addr_t gcm_iv_inc2_dma_addr;
	dma_addr_t hkey_dma_addr; /* Phys. address of hkey */
	dma_addr_t gcm_block_len_dma_addr; /* Phys. address of gcm block len */
	bool is_gcm4543;

	u8 *icv_virt_addr; /* Virt. address of ICV */
	struct async_gen_req_ctx gen_ctx;
	struct cc_mlli assoc;
	struct cc_mlli src;
	struct cc_mlli dst;
	struct scatterlist *src_sgl;
	struct scatterlist *dst_sgl;
	unsigned int src_offset;
	unsigned int dst_offset;
	enum cc_req_dma_buf_type assoc_buff_type;
	enum cc_req_dma_buf_type data_buff_type;
	struct mlli_params mlli_params;
	unsigned int cryptlen;
	struct scatterlist ccm_adata_sg;
	enum aead_ccm_header_size ccm_hdr_size;
	unsigned int req_authsize;
	enum drv_cipher_mode cipher_mode;
	bool is_icv_fragmented;
	bool is_single_pass;
	bool plaintext_authenticate_only; //for gcm_rfc4543
};

int cc_aead_alloc(struct cc_drvdata *drvdata);
int cc_aead_free(struct cc_drvdata *drvdata);

#endif /*__CC_AEAD_H__*/
