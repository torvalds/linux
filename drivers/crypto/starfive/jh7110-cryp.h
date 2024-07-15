/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __STARFIVE_STR_H__
#define __STARFIVE_STR_H__

#include <crypto/aes.h>
#include <crypto/hash.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha2.h>
#include <crypto/sm3.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>

#define STARFIVE_ALG_CR_OFFSET			0x0
#define STARFIVE_ALG_FIFO_OFFSET		0x4
#define STARFIVE_IE_MASK_OFFSET			0x8
#define STARFIVE_IE_FLAG_OFFSET			0xc
#define STARFIVE_DMA_IN_LEN_OFFSET		0x10
#define STARFIVE_DMA_OUT_LEN_OFFSET		0x14

#define STARFIVE_IE_MASK_AES_DONE		0x1
#define STARFIVE_IE_MASK_HASH_DONE		0x4
#define STARFIVE_IE_MASK_PKA_DONE		0x8
#define STARFIVE_IE_FLAG_AES_DONE		0x1
#define STARFIVE_IE_FLAG_HASH_DONE		0x4
#define STARFIVE_IE_FLAG_PKA_DONE		0x8

#define STARFIVE_MSG_BUFFER_SIZE		SZ_16K
#define MAX_KEY_SIZE				SHA512_BLOCK_SIZE
#define STARFIVE_AES_IV_LEN			AES_BLOCK_SIZE
#define STARFIVE_AES_CTR_LEN			AES_BLOCK_SIZE

union starfive_aes_csr {
	u32 v;
	struct {
		u32 cmode			:1;
#define STARFIVE_AES_KEYMODE_128		0x0
#define STARFIVE_AES_KEYMODE_192		0x1
#define STARFIVE_AES_KEYMODE_256		0x2
		u32 keymode			:2;
#define STARFIVE_AES_BUSY			BIT(3)
		u32 busy			:1;
		u32 done			:1;
#define STARFIVE_AES_KEY_DONE			BIT(5)
		u32 krdy			:1;
		u32 aesrst			:1;
		u32 ie				:1;
#define STARFIVE_AES_CCM_START			BIT(8)
		u32 ccm_start			:1;
#define STARFIVE_AES_MODE_ECB			0x0
#define STARFIVE_AES_MODE_CBC			0x1
#define STARFIVE_AES_MODE_CTR			0x4
#define STARFIVE_AES_MODE_CCM			0x5
#define STARFIVE_AES_MODE_GCM			0x6
		u32 mode			:3;
#define STARFIVE_AES_GCM_START			BIT(12)
		u32 gcm_start			:1;
#define STARFIVE_AES_GCM_DONE			BIT(13)
		u32 gcm_done			:1;
		u32 delay_aes			:1;
		u32 vaes_start			:1;
		u32 rsvd_0			:8;
#define STARFIVE_AES_MODE_XFB_1			0x0
#define STARFIVE_AES_MODE_XFB_128		0x5
		u32 stmode			:3;
		u32 rsvd_1			:5;
	};
};

union starfive_hash_csr {
	u32 v;
	struct {
		u32 start			:1;
		u32 reset			:1;
		u32 ie				:1;
		u32 firstb			:1;
#define STARFIVE_HASH_SM3			0x0
#define STARFIVE_HASH_SHA224			0x3
#define STARFIVE_HASH_SHA256			0x4
#define STARFIVE_HASH_SHA384			0x5
#define STARFIVE_HASH_SHA512			0x6
#define STARFIVE_HASH_MODE_MASK			0x7
		u32 mode			:3;
		u32 rsvd_1			:1;
		u32 final			:1;
		u32 rsvd_2			:2;
#define STARFIVE_HASH_HMAC_FLAGS		0x800
		u32 hmac			:1;
		u32 rsvd_3			:1;
#define STARFIVE_HASH_KEY_DONE			BIT(13)
		u32 key_done			:1;
		u32 key_flag			:1;
#define STARFIVE_HASH_HMAC_DONE			BIT(15)
		u32 hmac_done			:1;
#define STARFIVE_HASH_BUSY			BIT(16)
		u32 busy			:1;
		u32 hashdone			:1;
		u32 rsvd_4			:14;
	};
};

union starfive_pka_cacr {
	u32 v;
	struct {
		u32 start			:1;
		u32 reset			:1;
		u32 ie				:1;
		u32 rsvd_0			:1;
		u32 fifo_mode			:1;
		u32 not_r2			:1;
		u32 ecc_sub			:1;
		u32 pre_expf			:1;
		u32 cmd				:4;
		u32 rsvd_1			:1;
		u32 ctrl_dummy			:1;
		u32 ctrl_false			:1;
		u32 cln_done			:1;
		u32 opsize			:6;
		u32 rsvd_2			:2;
		u32 exposize			:6;
		u32 rsvd_3			:1;
		u32 bigendian			:1;
	};
};

union starfive_pka_casr {
	u32 v;
	struct {
#define STARFIVE_PKA_DONE			BIT(0)
		u32 done			:1;
		u32 rsvd_0			:31;
	};
};

struct starfive_rsa_key {
	u8	*n;
	u8	*e;
	u8	*d;
	int	e_bitlen;
	int	d_bitlen;
	int	bitlen;
	size_t	key_sz;
};

union starfive_alg_cr {
	u32 v;
	struct {
		u32 start			:1;
		u32 aes_dma_en			:1;
		u32 rsvd_0			:1;
		u32 hash_dma_en			:1;
		u32 alg_done			:1;
		u32 rsvd_1			:3;
		u32 clear			:1;
		u32 rsvd_2			:23;
	};
};

struct starfive_cryp_ctx {
	struct starfive_cryp_dev		*cryp;
	struct starfive_cryp_request_ctx	*rctx;

	unsigned int				hash_mode;
	u8					key[MAX_KEY_SIZE];
	int					keylen;
	bool					is_hmac;
	struct starfive_rsa_key			rsa_key;
	struct crypto_akcipher			*akcipher_fbk;
	struct crypto_ahash			*ahash_fbk;
	struct crypto_aead			*aead_fbk;
	struct crypto_skcipher			*skcipher_fbk;
};

struct starfive_cryp_dev {
	struct list_head			list;
	struct device				*dev;
	struct clk				*hclk;
	struct clk				*ahb;
	struct reset_control			*rst;

	void __iomem				*base;
	phys_addr_t				phys_base;

	u32					dma_maxburst;
	struct dma_chan				*tx;
	struct dma_chan				*rx;
	struct dma_slave_config			cfg_in;
	struct dma_slave_config			cfg_out;
	struct crypto_engine			*engine;
	struct completion			dma_done;
	size_t					assoclen;
	size_t					total_in;
	size_t					total_out;
	u32					tag_in[4];
	u32					tag_out[4];
	unsigned int				authsize;
	unsigned long				flags;
	int					err;
	bool					side_chan;
	union starfive_alg_cr			alg_cr;
	union {
		struct ahash_request		*hreq;
		struct aead_request		*areq;
		struct skcipher_request		*sreq;
	} req;
};

struct starfive_cryp_request_ctx {
	union {
		union starfive_hash_csr		hash;
		union starfive_pka_cacr		pka;
		union starfive_aes_csr		aes;
	} csr;

	struct scatterlist			*in_sg;
	struct scatterlist			*out_sg;
	struct ahash_request			ahash_fbk_req;
	size_t					total;
	size_t					nents;
	unsigned int				blksize;
	unsigned int				digsize;
	unsigned long				in_sg_len;
	unsigned char				*adata;
	u8 rsa_data[] __aligned(sizeof(u32));
};

struct starfive_cryp_dev *starfive_cryp_find_dev(struct starfive_cryp_ctx *ctx);

int starfive_hash_register_algs(void);
void starfive_hash_unregister_algs(void);

int starfive_rsa_register_algs(void);
void starfive_rsa_unregister_algs(void);

int starfive_aes_register_algs(void);
void starfive_aes_unregister_algs(void);
#endif
