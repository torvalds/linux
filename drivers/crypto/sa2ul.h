/* SPDX-License-Identifier: GPL-2.0 */
/*
 * K3 SA2UL crypto accelerator driver
 *
 * Copyright (C) 2018-2020 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors:	Keerthy
 *		Vitaly Andrianov
 *		Tero Kristo
 */

#ifndef _K3_SA2UL_
#define _K3_SA2UL_

#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/hw_random.h>
#include <crypto/aes.h>

#define SA_ENGINE_ENABLE_CONTROL	0x1000

struct sa_tfm_ctx;
/*
 * SA_ENGINE_ENABLE_CONTROL register bits
 */
#define SA_EEC_ENCSS_EN			0x00000001
#define SA_EEC_AUTHSS_EN		0x00000002
#define SA_EEC_TRNG_EN			0x00000008
#define SA_EEC_PKA_EN			0x00000010
#define SA_EEC_CTXCACH_EN		0x00000080
#define SA_EEC_CPPI_PORT_IN_EN		0x00000200
#define SA_EEC_CPPI_PORT_OUT_EN		0x00000800

/*
 * Encoding used to identify the typo of crypto operation
 * performed on the packet when the packet is returned
 * by SA
 */
#define SA_REQ_SUBTYPE_ENC	0x0001
#define SA_REQ_SUBTYPE_DEC	0x0002
#define SA_REQ_SUBTYPE_SHIFT	16
#define SA_REQ_SUBTYPE_MASK	0xffff

/* Number of 32 bit words in EPIB  */
#define SA_DMA_NUM_EPIB_WORDS   4

/* Number of 32 bit words in PS data  */
#define SA_DMA_NUM_PS_WORDS     16
#define NKEY_SZ			3
#define MCI_SZ			27

/*
 * Maximum number of simultaeneous security contexts
 * supported by the driver
 */
#define SA_MAX_NUM_CTX	512

/*
 * Assumption: CTX size is multiple of 32
 */
#define SA_CTX_SIZE_TO_DMA_SIZE(ctx_sz) \
		((ctx_sz) ? ((ctx_sz) / 32 - 1) : 0)

#define SA_CTX_ENC_KEY_OFFSET   32
#define SA_CTX_ENC_AUX1_OFFSET  64
#define SA_CTX_ENC_AUX2_OFFSET  96
#define SA_CTX_ENC_AUX3_OFFSET  112
#define SA_CTX_ENC_AUX4_OFFSET  128

/* Next Engine Select code in CP_ACE */
#define SA_ENG_ID_EM1   2       /* Enc/Dec engine with AES/DEC core */
#define SA_ENG_ID_EM2   3       /* Encryption/Decryption enginefor pass 2 */
#define SA_ENG_ID_AM1   4       /* Auth. engine with SHA1/MD5/SHA2 core */
#define SA_ENG_ID_AM2   5       /*  Authentication engine for pass 2 */
#define SA_ENG_ID_OUTPORT2 20   /*  Egress module 2  */

/*
 * Command Label Definitions
 */
#define SA_CMDL_OFFSET_NESC           0      /* Next Engine Select Code */
#define SA_CMDL_OFFSET_LABEL_LEN      1      /* Engine Command Label Length */
/* 16-bit Length of Data to be processed */
#define SA_CMDL_OFFSET_DATA_LEN       2
#define SA_CMDL_OFFSET_DATA_OFFSET    4      /* Stat Data Offset */
#define SA_CMDL_OFFSET_OPTION_CTRL1   5      /* Option Control Byte 1 */
#define SA_CMDL_OFFSET_OPTION_CTRL2   6      /* Option Control Byte 2 */
#define SA_CMDL_OFFSET_OPTION_CTRL3   7      /* Option Control Byte 3 */
#define SA_CMDL_OFFSET_OPTION_BYTE    8

#define SA_CMDL_HEADER_SIZE_BYTES	8

#define SA_CMDL_OPTION_BYTES_MAX_SIZE     72
#define SA_CMDL_MAX_SIZE_BYTES (SA_CMDL_HEADER_SIZE_BYTES + \
				SA_CMDL_OPTION_BYTES_MAX_SIZE)

/* SWINFO word-0 flags */
#define SA_SW_INFO_FLAG_EVICT   0x0001
#define SA_SW_INFO_FLAG_TEAR    0x0002
#define SA_SW_INFO_FLAG_NOPD    0x0004

/*
 * This type represents the various packet types to be processed
 * by the PHP engine in SA.
 * It is used to identify the corresponding PHP processing function.
 */
#define SA_CTX_PE_PKT_TYPE_3GPP_AIR    0    /* 3GPP Air Cipher */
#define SA_CTX_PE_PKT_TYPE_SRTP        1    /* SRTP */
#define SA_CTX_PE_PKT_TYPE_IPSEC_AH    2    /* IPSec Authentication Header */
/* IPSec Encapsulating Security Payload */
#define SA_CTX_PE_PKT_TYPE_IPSEC_ESP   3
/* Indicates that it is in data mode, It may not be used by PHP */
#define SA_CTX_PE_PKT_TYPE_NONE        4
#define SA_CTX_ENC_TYPE1_SZ     64      /* Encryption SC with Key only */
#define SA_CTX_ENC_TYPE2_SZ     96      /* Encryption SC with Key and Aux1 */

#define SA_CTX_AUTH_TYPE1_SZ    64      /* Auth SC with Key only */
#define SA_CTX_AUTH_TYPE2_SZ    96      /* Auth SC with Key and Aux1 */
/* Size of security context for PHP engine */
#define SA_CTX_PHP_PE_CTX_SZ    64

#define SA_CTX_MAX_SZ (64 + SA_CTX_ENC_TYPE2_SZ + SA_CTX_AUTH_TYPE2_SZ)

/*
 * Encoding of F/E control in SCCTL
 *  Bit 0-1: Fetch PHP Bytes
 *  Bit 2-3: Fetch Encryption/Air Ciphering Bytes
 *  Bit 4-5: Fetch Authentication Bytes or Encr pass 2
 *  Bit 6-7: Evict PHP Bytes
 *
 *  where   00 = 0 bytes
 *          01 = 64 bytes
 *          10 = 96 bytes
 *          11 = 128 bytes
 */
#define SA_CTX_DMA_SIZE_0       0
#define SA_CTX_DMA_SIZE_64      1
#define SA_CTX_DMA_SIZE_96      2
#define SA_CTX_DMA_SIZE_128     3

/*
 * Byte offset of the owner word in SCCTL
 * in the security context
 */
#define SA_CTX_SCCTL_OWNER_OFFSET 0

#define SA_CTX_ENC_KEY_OFFSET   32
#define SA_CTX_ENC_AUX1_OFFSET  64
#define SA_CTX_ENC_AUX2_OFFSET  96
#define SA_CTX_ENC_AUX3_OFFSET  112
#define SA_CTX_ENC_AUX4_OFFSET  128

#define SA_SCCTL_FE_AUTH_ENC	0x65
#define SA_SCCTL_FE_ENC		0x8D

#define SA_ALIGN_MASK		(sizeof(u32) - 1)
#define SA_ALIGNED		__aligned(32)

#define SA_AUTH_SW_CTRL_MD5	1
#define SA_AUTH_SW_CTRL_SHA1	2
#define SA_AUTH_SW_CTRL_SHA224	3
#define SA_AUTH_SW_CTRL_SHA256	4
#define SA_AUTH_SW_CTRL_SHA384	5
#define SA_AUTH_SW_CTRL_SHA512	6

/* SA2UL can only handle maximum data size of 64KB */
#define SA_MAX_DATA_SZ		U16_MAX

/*
 * SA2UL can provide unpredictable results with packet sizes that fall
 * the following range, so avoid using it.
 */
#define SA_UNSAFE_DATA_SZ_MIN	240
#define SA_UNSAFE_DATA_SZ_MAX	256

/**
 * struct sa_crypto_data - Crypto driver instance data
 * @base: Base address of the register space
 * @pdev: Platform device pointer
 * @sc_pool: security context pool
 * @dev: Device pointer
 * @scid_lock: secure context ID lock
 * @sc_id_start: starting index for SC ID
 * @sc_id_end: Ending index for SC ID
 * @sc_id: Security Context ID
 * @ctx_bm: Bitmap to keep track of Security context ID's
 * @ctx: SA tfm context pointer
 * @dma_rx1: Pointer to DMA rx channel for sizes < 256 Bytes
 * @dma_rx2: Pointer to DMA rx channel for sizes > 256 Bytes
 * @dma_tx: Pointer to DMA TX channel
 */
struct sa_crypto_data {
	void __iomem *base;
	struct platform_device	*pdev;
	struct dma_pool		*sc_pool;
	struct device *dev;
	spinlock_t	scid_lock; /* lock for SC-ID allocation */
	/* Security context data */
	u16		sc_id_start;
	u16		sc_id_end;
	u16		sc_id;
	unsigned long	ctx_bm[DIV_ROUND_UP(SA_MAX_NUM_CTX,
				BITS_PER_LONG)];
	struct sa_tfm_ctx	*ctx;
	struct dma_chan		*dma_rx1;
	struct dma_chan		*dma_rx2;
	struct dma_chan		*dma_tx;
};

/**
 * struct sa_cmdl_param_info: Command label parameters info
 * @index: Index of the parameter in the command label format
 * @offset: the offset of the parameter
 * @size: Size of the parameter
 */
struct sa_cmdl_param_info {
	u16	index;
	u16	offset;
	u16	size;
};

/* Maximum length of Auxiliary data in 32bit words */
#define SA_MAX_AUX_DATA_WORDS	8

/**
 * struct sa_cmdl_upd_info: Command label updation info
 * @flags: flags in command label
 * @submode: Encryption submodes
 * @enc_size: Size of first pass encryption size
 * @enc_size2: Size of second pass encryption size
 * @enc_offset: Encryption payload offset in the packet
 * @enc_iv: Encryption initialization vector for pass2
 * @enc_iv2: Encryption initialization vector for pass2
 * @aad: Associated data
 * @payload: Payload info
 * @auth_size: Authentication size for pass 1
 * @auth_size2: Authentication size for pass 2
 * @auth_offset: Authentication payload offset
 * @auth_iv: Authentication initialization vector
 * @aux_key_info: Authentication aux key information
 * @aux_key: Aux key for authentication
 */
struct sa_cmdl_upd_info {
	u16	flags;
	u16	submode;
	struct sa_cmdl_param_info	enc_size;
	struct sa_cmdl_param_info	enc_size2;
	struct sa_cmdl_param_info	enc_offset;
	struct sa_cmdl_param_info	enc_iv;
	struct sa_cmdl_param_info	enc_iv2;
	struct sa_cmdl_param_info	aad;
	struct sa_cmdl_param_info	payload;
	struct sa_cmdl_param_info	auth_size;
	struct sa_cmdl_param_info	auth_size2;
	struct sa_cmdl_param_info	auth_offset;
	struct sa_cmdl_param_info	auth_iv;
	struct sa_cmdl_param_info	aux_key_info;
	u32				aux_key[SA_MAX_AUX_DATA_WORDS];
};

/*
 * Number of 32bit words appended after the command label
 * in PSDATA to identify the crypto request context.
 * word-0: Request type
 * word-1: pointer to request
 */
#define SA_PSDATA_CTX_WORDS 4

/* Maximum size of Command label in 32 words */
#define SA_MAX_CMDL_WORDS (SA_DMA_NUM_PS_WORDS - SA_PSDATA_CTX_WORDS)

/**
 * struct sa_ctx_info: SA context information
 * @sc: Pointer to security context
 * @sc_phys: Security context physical address that is passed on to SA2UL
 * @sc_id: Security context ID
 * @cmdl_size: Command label size
 * @cmdl: Command label for a particular iteration
 * @cmdl_upd_info: structure holding command label updation info
 * @epib: Extended protocol information block words
 */
struct sa_ctx_info {
	u8		*sc;
	dma_addr_t	sc_phys;
	u16		sc_id;
	u16		cmdl_size;
	u32		cmdl[SA_MAX_CMDL_WORDS];
	struct sa_cmdl_upd_info cmdl_upd_info;
	/* Store Auxiliary data such as K2/K3 subkeys in AES-XCBC */
	u32		epib[SA_DMA_NUM_EPIB_WORDS];
};

/**
 * struct sa_tfm_ctx: TFM context structure
 * @dev_data: struct sa_crypto_data pointer
 * @enc: struct sa_ctx_info for encryption
 * @dec: struct sa_ctx_info for decryption
 * @keylen: encrption/decryption keylength
 * @iv_idx: Initialization vector index
 * @key: encryption key
 * @fallback: SW fallback algorithm
 */
struct sa_tfm_ctx {
	struct sa_crypto_data *dev_data;
	struct sa_ctx_info enc;
	struct sa_ctx_info dec;
	struct sa_ctx_info auth;
	int keylen;
	int iv_idx;
	u32 key[AES_KEYSIZE_256 / sizeof(u32)];
	u8 authkey[SHA512_BLOCK_SIZE];
	struct crypto_shash	*shash;
	/* for fallback */
	union {
		struct crypto_sync_skcipher	*skcipher;
		struct crypto_ahash		*ahash;
		struct crypto_aead		*aead;
	} fallback;
};

/**
 * struct sa_sha_req_ctx: Structure used for sha request
 * @dev_data: struct sa_crypto_data pointer
 * @cmdl: Complete command label with psdata and epib included
 * @fallback_req: SW fallback request container
 */
struct sa_sha_req_ctx {
	struct sa_crypto_data	*dev_data;
	u32			cmdl[SA_MAX_CMDL_WORDS + SA_PSDATA_CTX_WORDS];
	struct ahash_request	fallback_req;
};

enum sa_submode {
	SA_MODE_GEN = 0,
	SA_MODE_CCM,
	SA_MODE_GCM,
	SA_MODE_GMAC
};

/* Encryption algorithms */
enum sa_ealg_id {
	SA_EALG_ID_NONE = 0,        /* No encryption */
	SA_EALG_ID_NULL,            /* NULL encryption */
	SA_EALG_ID_AES_CTR,         /* AES Counter mode */
	SA_EALG_ID_AES_F8,          /* AES F8 mode */
	SA_EALG_ID_AES_CBC,         /* AES CBC mode */
	SA_EALG_ID_DES_CBC,         /* DES CBC mode */
	SA_EALG_ID_3DES_CBC,        /* 3DES CBC mode */
	SA_EALG_ID_CCM,             /* Counter with CBC-MAC mode */
	SA_EALG_ID_GCM,             /* Galois Counter mode */
	SA_EALG_ID_AES_ECB,
	SA_EALG_ID_LAST
};

/* Authentication algorithms */
enum sa_aalg_id {
	SA_AALG_ID_NONE = 0,      /* No Authentication  */
	SA_AALG_ID_NULL = SA_EALG_ID_LAST, /* NULL Authentication  */
	SA_AALG_ID_MD5,           /* MD5 mode */
	SA_AALG_ID_SHA1,          /* SHA1 mode */
	SA_AALG_ID_SHA2_224,      /* 224-bit SHA2 mode */
	SA_AALG_ID_SHA2_256,      /* 256-bit SHA2 mode */
	SA_AALG_ID_SHA2_512,      /* 512-bit SHA2 mode */
	SA_AALG_ID_HMAC_MD5,      /* HMAC with MD5 mode */
	SA_AALG_ID_HMAC_SHA1,     /* HMAC with SHA1 mode */
	SA_AALG_ID_HMAC_SHA2_224, /* HMAC with 224-bit SHA2 mode */
	SA_AALG_ID_HMAC_SHA2_256, /* HMAC with 256-bit SHA2 mode */
	SA_AALG_ID_GMAC,          /* Galois Message Auth. Code mode */
	SA_AALG_ID_CMAC,          /* Cipher-based Mes. Auth. Code mode */
	SA_AALG_ID_CBC_MAC,       /* Cipher Block Chaining */
	SA_AALG_ID_AES_XCBC       /* AES Extended Cipher Block Chaining */
};

/*
 * Mode control engine algorithms used to index the
 * mode control instruction tables
 */
enum sa_eng_algo_id {
	SA_ENG_ALGO_ECB = 0,
	SA_ENG_ALGO_CBC,
	SA_ENG_ALGO_CFB,
	SA_ENG_ALGO_OFB,
	SA_ENG_ALGO_CTR,
	SA_ENG_ALGO_F8,
	SA_ENG_ALGO_F8F9,
	SA_ENG_ALGO_GCM,
	SA_ENG_ALGO_GMAC,
	SA_ENG_ALGO_CCM,
	SA_ENG_ALGO_CMAC,
	SA_ENG_ALGO_CBCMAC,
	SA_NUM_ENG_ALGOS
};

/**
 * struct sa_eng_info: Security accelerator engine info
 * @eng_id: Engine ID
 * @sc_size: security context size
 */
struct sa_eng_info {
	u8	eng_id;
	u16	sc_size;
};

#endif /* _K3_SA2UL_ */
