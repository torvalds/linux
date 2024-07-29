/* SPDX-License-Identifier: GPL-2.0 */


#ifndef SPACC_CORE_H_
#define SPACC_CORE_H_

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>
#include <crypto/skcipher.h>
#include "spacc_hal.h"

enum {
	SPACC_DMA_UNDEF  = 0,
	SPACC_DMA_DDT	 = 1,
	SPACC_DMA_LINEAR = 2
};

enum {
	SPACC_OP_MODE_IRQ = 0,
	SPACC_OP_MODE_WD  = 1	/* watchdog */
};

#define OP_ENCRYPT		0
#define OP_DECRYPT		1

#define SPACC_CRYPTO_OPERATION	1
#define SPACC_HASH_OPERATION	2

#define SPACC_AADCOPY_FLAG	0x80000000

#define SPACC_AUTO_SIZE		(-1)

#define SPACC_WD_LIMIT		0x80
#define SPACC_WD_TIMER_INIT	0x40000

/********* Register Offsets **********/
#define SPACC_REG_IRQ_EN	0x00000L
#define SPACC_REG_IRQ_STAT	0x00004L
#define SPACC_REG_IRQ_CTRL	0x00008L
#define SPACC_REG_FIFO_STAT	0x0000CL
#define SPACC_REG_SDMA_BRST_SZ	0x00010L

#define SPACC_REG_SRC_PTR	0x00020L
#define SPACC_REG_DST_PTR	0x00024L
#define SPACC_REG_OFFSET	0x00028L
#define SPACC_REG_PRE_AAD_LEN	0x0002CL
#define SPACC_REG_POST_AAD_LEN	0x00030L

#define SPACC_REG_PROC_LEN	0x00034L
#define SPACC_REG_ICV_LEN	0x00038L
#define SPACC_REG_ICV_OFFSET	0x0003CL
#define SPACC_REG_IV_OFFSET	0x00040L

#define SPACC_REG_SW_CTRL	0x00044L
#define SPACC_REG_AUX_INFO	0x00048L
#define SPACC_REG_CTRL		0x0004CL

#define SPACC_REG_STAT_POP	0x00050L
#define SPACC_REG_STATUS	0x00054L

#define SPACC_REG_STAT_WD_CTRL	0x00080L

#define SPACC_REG_KEY_SZ	0x00100L

#define SPACC_REG_VIRTUAL_RQST	0x00140L
#define SPACC_REG_VIRTUAL_ALLOC	0x00144L
#define SPACC_REG_VIRTUAL_PRIO	0x00148L

#define SPACC_REG_ID		0x00180L
#define SPACC_REG_CONFIG	0x00184L
#define SPACC_REG_CONFIG2	0x00190L

#define SPACC_REG_SECURE_CTRL		0x001C0L
#define SPACC_REG_SECURE_RELEASE	0x001C4

#define SPACC_REG_SK_LOAD	0x00200L
#define SPACC_REG_SK_STAT	0x00204L
#define SPACC_REG_SK_KEY	0x00240L

#define SPACC_REG_VERSION_EXT_3	0x00194L

/* out 8MB from base of SPACC */
#define SPACC_REG_SKP		0x800000UL

/********** Context Offsets **********/
#define SPACC_CTX_CIPH_KEY	0x04000L
#define SPACC_CTX_HASH_KEY	0x08000L

/******** Sub-Context Offsets ********/
#define SPACC_CTX_AES_KEY	0x00
#define SPACC_CTX_AES_IV	0x20

#define SPACC_CTX_DES_KEY	0x08
#define SPACC_CTX_DES_IV	0x00

/* use these to loop over CMDX macros */
#define SPACC_CMDX_MAX		1
#define SPACC_CMDX_MAX_QOS	3
/********** IRQ_EN Bit Masks **********/

#define _SPACC_IRQ_CMD0		0
#define _SPACC_IRQ_STAT		4
#define _SPACC_IRQ_STAT_WD	12
#define _SPACC_IRQ_GLBL		31

#define SPACC_IRQ_EN_CMD(x)	(1UL << _SPACC_IRQ_CMD0 << (x))
#define SPACC_IRQ_EN_STAT	BIT(_SPACC_IRQ_STAT)
#define SPACC_IRQ_EN_STAT_WD	BIT(_SPACC_IRQ_STAT_WD)
#define SPACC_IRQ_EN_GLBL	BIT(_SPACC_IRQ_GLBL)

/********* IRQ_STAT Bitmasks *********/

#define SPACC_IRQ_STAT_CMDX(x)	(1UL << _SPACC_IRQ_CMD0 << (x))
#define SPACC_IRQ_STAT_STAT	BIT(_SPACC_IRQ_STAT)
#define SPACC_IRQ_STAT_STAT_WD	BIT(_SPACC_IRQ_STAT_WD)

#define SPACC_IRQ_STAT_CLEAR_STAT(spacc)    writel(SPACC_IRQ_STAT_STAT, \
		(spacc)->regmap + SPACC_REG_IRQ_STAT)

#define SPACC_IRQ_STAT_CLEAR_STAT_WD(spacc) writel(SPACC_IRQ_STAT_STAT_WD, \
		(spacc)->regmap + SPACC_REG_IRQ_STAT)

#define SPACC_IRQ_STAT_CLEAR_CMDX(spacc, x) writel(SPACC_IRQ_STAT_CMDX(x), \
		(spacc)->regmap + SPACC_REG_IRQ_STAT)

/********* IRQ_CTRL Bitmasks *********/
/* CMD0 = 0; for QOS, CMD1 = 8, CMD2 = 16 */
#define _SPACC_IRQ_CTRL_CMDX_CNT(x)       (8 * (x))
#define SPACC_IRQ_CTRL_CMDX_CNT_SET(x, n) \
	(((n) & 0xFF) << _SPACC_IRQ_CTRL_CMDX_CNT(x))
#define SPACC_IRQ_CTRL_CMDX_CNT_MASK(x) \
	(0xFF << _SPACC_IRQ_CTRL_CMDX_CNT(x))

/* STAT_CNT is at 16 and for QOS at 24 */
#define _SPACC_IRQ_CTRL_STAT_CNT          16
#define SPACC_IRQ_CTRL_STAT_CNT_SET(n)    ((n) << _SPACC_IRQ_CTRL_STAT_CNT)
#define SPACC_IRQ_CTRL_STAT_CNT_MASK      (0x1FF << _SPACC_IRQ_CTRL_STAT_CNT)

#define _SPACC_IRQ_CTRL_STAT_CNT_QOS         24
#define SPACC_IRQ_CTRL_STAT_CNT_SET_QOS(n) \
	((n) << _SPACC_IRQ_CTRL_STAT_CNT_QOS)
#define SPACC_IRQ_CTRL_STAT_CNT_MASK_QOS \
	(0x7F << _SPACC_IRQ_CTRL_STAT_CNT_QOS)

/******** FIFO_STAT Bitmasks *********/

/* SPACC with QOS */
#define SPACC_FIFO_STAT_CMDX_CNT_MASK(x) \
	(0x7F << ((x) * 8))
#define SPACC_FIFO_STAT_CMDX_CNT_GET(x, y) \
	(((y) & SPACC_FIFO_STAT_CMDX_CNT_MASK(x)) >> ((x) * 8))
#define SPACC_FIFO_STAT_CMDX_FULL(x)          (1UL << (7 + (x) * 8))

#define _SPACC_FIFO_STAT_STAT_CNT_QOS         24
#define SPACC_FIFO_STAT_STAT_CNT_MASK_QOS \
	(0x7F << _SPACC_FIFO_STAT_STAT_CNT_QOS)
#define SPACC_FIFO_STAT_STAT_CNT_GET_QOS(y)	\
	(((y) &					\
	SPACC_FIFO_STAT_STAT_CNT_MASK_QOS) >> _SPACC_FIFO_STAT_STAT_CNT_QOS)

/* SPACC without QOS */
#define SPACC_FIFO_STAT_CMD0_CNT_MASK	(0x1FF)
#define SPACC_FIFO_STAT_CMD0_CNT_GET(y)	((y) & SPACC_FIFO_STAT_CMD0_CNT_MASK)
#define _SPACC_FIFO_STAT_CMD0_FULL      15
#define SPACC_FIFO_STAT_CMD0_FULL       BIT(_SPACC_FIFO_STAT_CMD0_FULL)

#define _SPACC_FIFO_STAT_STAT_CNT       16
#define SPACC_FIFO_STAT_STAT_CNT_MASK   (0x1FF << _SPACC_FIFO_STAT_STAT_CNT)
#define SPACC_FIFO_STAT_STAT_CNT_GET(y) \
	(((y) & SPACC_FIFO_STAT_STAT_CNT_MASK) >> _SPACC_FIFO_STAT_STAT_CNT)

/* both */
#define _SPACC_FIFO_STAT_STAT_EMPTY	31
#define SPACC_FIFO_STAT_STAT_EMPTY	BIT(_SPACC_FIFO_STAT_STAT_EMPTY)

/********* SRC/DST_PTR Bitmasks **********/

#define SPACC_SRC_PTR_PTR           0xFFFFFFF8
#define SPACC_DST_PTR_PTR           0xFFFFFFF8

/********** OFFSET Bitmasks **********/

#define SPACC_OFFSET_SRC_O          0
#define SPACC_OFFSET_SRC_W          16
#define SPACC_OFFSET_DST_O          16
#define SPACC_OFFSET_DST_W          16

#define SPACC_MIN_CHUNK_SIZE        1024
#define SPACC_MAX_CHUNK_SIZE        16384

/********* PKT_LEN Bitmasks **********/

#ifndef _SPACC_PKT_LEN_PROC_LEN
#define _SPACC_PKT_LEN_PROC_LEN     0
#endif
#ifndef _SPACC_PKT_LEN_AAD_LEN
#define _SPACC_PKT_LEN_AAD_LEN      16
#endif

/********* SW_CTRL Bitmasks ***********/

#define _SPACC_SW_CTRL_ID_0          0
#define SPACC_SW_CTRL_ID_W           8
#define SPACC_SW_CTRL_ID_MASK        (0xFF << _SPACC_SW_CTRL_ID_0)
#define SPACC_SW_CTRL_ID_GET(y) \
	(((y) & SPACC_SW_CTRL_ID_MASK) >> _SPACC_SW_CTRL_ID_0)
#define SPACC_SW_CTRL_ID_SET(id) \
	(((id) & SPACC_SW_CTRL_ID_MASK) >> _SPACC_SW_CTRL_ID_0)

#define _SPACC_SW_CTRL_PRIO          30
#define SPACC_SW_CTRL_PRIO_MASK      0x3
#define SPACC_SW_CTRL_PRIO_SET(prio) \
	(((prio) & SPACC_SW_CTRL_PRIO_MASK) << _SPACC_SW_CTRL_PRIO)

/* Priorities */
#define SPACC_SW_CTRL_PRIO_HI         0
#define SPACC_SW_CTRL_PRIO_MED        1
#define SPACC_SW_CTRL_PRIO_LOW        2

/*********** SECURE_CTRL bitmasks *********/
#define _SPACC_SECURE_CTRL_MS_SRC     0
#define _SPACC_SECURE_CTRL_MS_DST     1
#define _SPACC_SECURE_CTRL_MS_DDT     2
#define _SPACC_SECURE_CTRL_LOCK       31

#define SPACC_SECURE_CTRL_MS_SRC    BIT(_SPACC_SECURE_CTRL_MS_SRC)
#define SPACC_SECURE_CTRL_MS_DST    BIT(_SPACC_SECURE_CTRL_MS_DST)
#define SPACC_SECURE_CTRL_MS_DDT    BIT(_SPACC_SECURE_CTRL_MS_DDT)
#define SPACC_SECURE_CTRL_LOCK      BIT(_SPACC_SECURE_CTRL_LOCK)

/********* SKP bits **************/
#define _SPACC_SK_LOAD_CTX_IDX	0
#define _SPACC_SK_LOAD_ALG	8
#define _SPACC_SK_LOAD_MODE	12
#define _SPACC_SK_LOAD_SIZE	16
#define _SPACC_SK_LOAD_ENC_EN	30
#define _SPACC_SK_LOAD_DEC_EN	31
#define _SPACC_SK_STAT_BUSY	0

#define SPACC_SK_LOAD_ENC_EN         BIT(_SPACC_SK_LOAD_ENC_EN)
#define SPACC_SK_LOAD_DEC_EN         BIT(_SPACC_SK_LOAD_DEC_EN)
#define SPACC_SK_STAT_BUSY           BIT(_SPACC_SK_STAT_BUSY)

/*********** CTRL Bitmasks ***********/
/* These CTRL field locations vary with SPACC version
 * and if they are used, they should be set accordingly
 */
#define _SPACC_CTRL_CIPH_ALG	0
#define _SPACC_CTRL_HASH_ALG	4
#define _SPACC_CTRL_CIPH_MODE	8
#define _SPACC_CTRL_HASH_MODE	12
#define _SPACC_CTRL_MSG_BEGIN	14
#define _SPACC_CTRL_MSG_END	15
#define _SPACC_CTRL_CTX_IDX	16
#define _SPACC_CTRL_ENCRYPT	24
#define _SPACC_CTRL_AAD_COPY	25
#define _SPACC_CTRL_ICV_PT	26
#define _SPACC_CTRL_ICV_ENC	27
#define _SPACC_CTRL_ICV_APPEND	28
#define _SPACC_CTRL_KEY_EXP	29
#define _SPACC_CTRL_SEC_KEY	31

/* CTRL bitmasks for 4.15+ cores */
#define _SPACC_CTRL_CIPH_ALG_415	0
#define _SPACC_CTRL_HASH_ALG_415	3
#define _SPACC_CTRL_CIPH_MODE_415	8
#define _SPACC_CTRL_HASH_MODE_415	12

/********* Virtual Spacc Priority Bitmasks **********/
#define _SPACC_VPRIO_MODE		0
#define _SPACC_VPRIO_WEIGHT		8

/********* AUX INFO Bitmasks *********/
#define _SPACC_AUX_INFO_DIR		0
#define _SPACC_AUX_INFO_BIT_ALIGN	1
#define _SPACC_AUX_INFO_CBC_CS		16

/********* STAT_POP Bitmasks *********/
#define _SPACC_STAT_POP_POP	0
#define SPACC_STAT_POP_POP	BIT(_SPACC_STAT_POP_POP)

/********** STATUS Bitmasks **********/
#define _SPACC_STATUS_SW_ID	0
#define _SPACC_STATUS_RET_CODE	24
#define _SPACC_STATUS_SEC_CMD	31
#define SPACC_GET_STATUS_RET_CODE(s) \
	(((s) >> _SPACC_STATUS_RET_CODE) & 0x7)

#define SPACC_STATUS_SW_ID_MASK		(0xFF << _SPACC_STATUS_SW_ID)
#define SPACC_STATUS_SW_ID_GET(y) \
	(((y) & SPACC_STATUS_SW_ID_MASK) >> _SPACC_STATUS_SW_ID)

/********** KEY_SZ Bitmasks **********/
#define _SPACC_KEY_SZ_SIZE	0
#define _SPACC_KEY_SZ_CTX_IDX	8
#define _SPACC_KEY_SZ_CIPHER	31

#define SPACC_KEY_SZ_CIPHER        BIT(_SPACC_KEY_SZ_CIPHER)

#define SPACC_SET_CIPHER_KEY_SZ(z) \
	(((z) << _SPACC_KEY_SZ_SIZE) | (1UL << _SPACC_KEY_SZ_CIPHER))
#define SPACC_SET_HASH_KEY_SZ(z)   ((z) << _SPACC_KEY_SZ_SIZE)
#define SPACC_SET_KEY_CTX(ctx)     ((ctx) << _SPACC_KEY_SZ_CTX_IDX)

/*****************************************************************************/

#define AUX_DIR(a)       ((a) << _SPACC_AUX_INFO_DIR)
#define AUX_BIT_ALIGN(a) ((a) << _SPACC_AUX_INFO_BIT_ALIGN)
#define AUX_CBC_CS(a)    ((a) << _SPACC_AUX_INFO_CBC_CS)

#define VPRIO_SET(mode, weight) \
	(((mode) << _SPACC_VPRIO_MODE) | ((weight) << _SPACC_VPRIO_WEIGHT))

#ifndef MAX_DDT_ENTRIES
/* add one for null at end of list */
#define MAX_DDT_ENTRIES \
	((SPACC_MAX_MSG_MALLOC_SIZE / SPACC_MAX_PARTICLE_SIZE) + 1)
#endif

#define DDT_ENTRY_SIZE (sizeof(ddt_entry) * MAX_DDT_ENTRIES)

#ifndef SPACC_MAX_JOBS
#define SPACC_MAX_JOBS  BIT(SPACC_SW_CTRL_ID_W)
#endif

#if SPACC_MAX_JOBS > 256
#  error SPACC_MAX_JOBS cannot exceed 256.
#endif

#ifndef SPACC_MAX_JOB_BUFFERS
#define SPACC_MAX_JOB_BUFFERS	192
#endif

#define CRYPTO_USED_JB	256

/* max DDT particle size */
#ifndef SPACC_MAX_PARTICLE_SIZE
#define SPACC_MAX_PARTICLE_SIZE	4096
#endif

/* max message size from HW configuration */
/* usually defined in ICD as (2 exponent 16) -1 */
#ifndef _SPACC_MAX_MSG_MALLOC_SIZE
#define _SPACC_MAX_MSG_MALLOC_SIZE	16
#endif
#define SPACC_MAX_MSG_MALLOC_SIZE	BIT(_SPACC_MAX_MSG_MALLOC_SIZE)

#ifndef SPACC_MAX_MSG_SIZE
#define SPACC_MAX_MSG_SIZE	(SPACC_MAX_MSG_MALLOC_SIZE - 1)
#endif

#define SPACC_LOOP_WAIT		1000000
#define SPACC_CTR_IV_MAX8	((u32)0xFF)
#define SPACC_CTR_IV_MAX16	((u32)0xFFFF)
#define SPACC_CTR_IV_MAX32	((u32)0xFFFFFFFF)
#define SPACC_CTR_IV_MAX64	((u64)0xFFFFFFFFFFFFFFFF)

/* cipher algos */
enum ecipher {
	C_NULL		= 0,
	C_DES		= 1,
	C_AES		= 2,
	C_RC4		= 3,
	C_MULTI2	= 4,
	C_KASUMI	= 5,
	C_SNOW3G_UEA2	= 6,
	C_ZUC_UEA3	= 7,
	C_CHACHA20	= 8,
	C_SM4		= 9,
	C_MAX		= 10
};

/* ctrl reg cipher modes */
enum eciphermode {
	CM_ECB = 0,
	CM_CBC = 1,
	CM_CTR = 2,
	CM_CCM = 3,
	CM_GCM = 5,
	CM_OFB = 7,
	CM_CFB = 8,
	CM_F8  = 9,
	CM_XTS = 10,
	CM_MAX = 11
};

enum echachaciphermode {
	CM_CHACHA_STREAM = 2,
	CM_CHACHA_AEAD	 = 5
};

enum ehash {
	H_NULL		 = 0,
	H_MD5		 = 1,
	H_SHA1		 = 2,
	H_SHA224	 = 3,
	H_SHA256	 = 4,
	H_SHA384	 = 5,
	H_SHA512	 = 6,
	H_XCBC		 = 7,
	H_CMAC		 = 8,
	H_KF9		 = 9,
	H_SNOW3G_UIA2	 = 10,
	H_CRC32_I3E802_3 = 11,
	H_ZUC_UIA3	 = 12,
	H_SHA512_224	 = 13,
	H_SHA512_256	 = 14,
	H_MICHAEL	 = 15,
	H_SHA3_224	 = 16,
	H_SHA3_256	 = 17,
	H_SHA3_384	 = 18,
	H_SHA3_512	 = 19,
	H_SHAKE128	 = 20,
	H_SHAKE256	 = 21,
	H_POLY1305	 = 22,
	H_SM3		 = 23,
	H_SM4_XCBC_MAC	 = 24,
	H_SM4_CMAC	 = 25,
	H_MAX		 = 26
};

enum ehashmode {
	HM_RAW    = 0,
	HM_SSLMAC = 1,
	HM_HMAC   = 2,
	HM_MAX	  = 3
};

enum eshakehashmode {
	HM_SHAKE_SHAKE  = 0,
	HM_SHAKE_CSHAKE = 1,
	HM_SHAKE_KMAC   = 2
};

enum spacc_ret_code {
	SPACC_OK	= 0,
	SPACC_ICVFAIL	= 1,
	SPACC_MEMERR	= 2,
	SPACC_BLOCKERR	= 3,
	SPACC_SECERR	= 4
};

enum eicvpos {
	IP_ICV_OFFSET = 0,
	IP_ICV_APPEND = 1,
	IP_ICV_IGNORE = 2,
	IP_MAX	      = 3
};

enum {
	/* HASH of plaintext */
	ICV_HASH	 = 0,
	/* HASH the plaintext and encrypt the plaintext and ICV */
	ICV_HASH_ENCRYPT = 1,
	/* HASH the ciphertext */
	ICV_ENCRYPT_HASH = 2,
	ICV_IGNORE	 = 3,
	IM_MAX		 = 4
};

enum {
	NO_PARTIAL_PCK	   = 0,
	FIRST_PARTIAL_PCK  = 1,
	MIDDLE_PARTIAL_PCK = 2,
	LAST_PARTIAL_PCK   = 3
};

enum crypto_modes {
	CRYPTO_MODE_NULL,
	CRYPTO_MODE_AES_ECB,
	CRYPTO_MODE_AES_CBC,
	CRYPTO_MODE_AES_CTR,
	CRYPTO_MODE_AES_CCM,
	CRYPTO_MODE_AES_GCM,
	CRYPTO_MODE_AES_F8,
	CRYPTO_MODE_AES_XTS,
	CRYPTO_MODE_AES_CFB,
	CRYPTO_MODE_AES_OFB,
	CRYPTO_MODE_AES_CS1,
	CRYPTO_MODE_AES_CS2,
	CRYPTO_MODE_AES_CS3,
	CRYPTO_MODE_MULTI2_ECB,
	CRYPTO_MODE_MULTI2_CBC,
	CRYPTO_MODE_MULTI2_OFB,
	CRYPTO_MODE_MULTI2_CFB,
	CRYPTO_MODE_3DES_CBC,
	CRYPTO_MODE_3DES_ECB,
	CRYPTO_MODE_DES_CBC,
	CRYPTO_MODE_DES_ECB,
	CRYPTO_MODE_KASUMI_ECB,
	CRYPTO_MODE_KASUMI_F8,
	CRYPTO_MODE_SNOW3G_UEA2,
	CRYPTO_MODE_ZUC_UEA3,
	CRYPTO_MODE_CHACHA20_STREAM,
	CRYPTO_MODE_CHACHA20_POLY1305,
	CRYPTO_MODE_SM4_ECB,
	CRYPTO_MODE_SM4_CBC,
	CRYPTO_MODE_SM4_CFB,
	CRYPTO_MODE_SM4_OFB,
	CRYPTO_MODE_SM4_CTR,
	CRYPTO_MODE_SM4_CCM,
	CRYPTO_MODE_SM4_GCM,
	CRYPTO_MODE_SM4_F8,
	CRYPTO_MODE_SM4_XTS,
	CRYPTO_MODE_SM4_CS1,
	CRYPTO_MODE_SM4_CS2,
	CRYPTO_MODE_SM4_CS3,

	CRYPTO_MODE_HASH_MD5,
	CRYPTO_MODE_HMAC_MD5,
	CRYPTO_MODE_HASH_SHA1,
	CRYPTO_MODE_HMAC_SHA1,
	CRYPTO_MODE_HASH_SHA224,
	CRYPTO_MODE_HMAC_SHA224,
	CRYPTO_MODE_HASH_SHA256,
	CRYPTO_MODE_HMAC_SHA256,
	CRYPTO_MODE_HASH_SHA384,
	CRYPTO_MODE_HMAC_SHA384,
	CRYPTO_MODE_HASH_SHA512,
	CRYPTO_MODE_HMAC_SHA512,
	CRYPTO_MODE_HASH_SHA512_224,
	CRYPTO_MODE_HMAC_SHA512_224,
	CRYPTO_MODE_HASH_SHA512_256,
	CRYPTO_MODE_HMAC_SHA512_256,

	CRYPTO_MODE_MAC_XCBC,
	CRYPTO_MODE_MAC_CMAC,
	CRYPTO_MODE_MAC_KASUMI_F9,
	CRYPTO_MODE_MAC_SNOW3G_UIA2,
	CRYPTO_MODE_MAC_ZUC_UIA3,
	CRYPTO_MODE_MAC_POLY1305,

	CRYPTO_MODE_SSLMAC_MD5,
	CRYPTO_MODE_SSLMAC_SHA1,
	CRYPTO_MODE_HASH_CRC32,
	CRYPTO_MODE_MAC_MICHAEL,

	CRYPTO_MODE_HASH_SHA3_224,
	CRYPTO_MODE_HASH_SHA3_256,
	CRYPTO_MODE_HASH_SHA3_384,
	CRYPTO_MODE_HASH_SHA3_512,

	CRYPTO_MODE_HASH_SHAKE128,
	CRYPTO_MODE_HASH_SHAKE256,
	CRYPTO_MODE_HASH_CSHAKE128,
	CRYPTO_MODE_HASH_CSHAKE256,
	CRYPTO_MODE_MAC_KMAC128,
	CRYPTO_MODE_MAC_KMAC256,
	CRYPTO_MODE_MAC_KMACXOF128,
	CRYPTO_MODE_MAC_KMACXOF256,

	CRYPTO_MODE_HASH_SM3,
	CRYPTO_MODE_HMAC_SM3,
	CRYPTO_MODE_MAC_SM4_XCBC,
	CRYPTO_MODE_MAC_SM4_CMAC,

	CRYPTO_MODE_LAST
};

/* job descriptor */
typedef void (*spacc_callback)(void *spacc_dev, void *data);

struct spacc_job {
	unsigned long
		enc_mode,	/* Encryption Algorithm mode */
		hash_mode,	/* HASH Algorithm mode */
		icv_len,
		icv_offset,
		op,		/* Operation */
		ctrl,		/* CTRL shadow register */

		/* context just initialized or taken,
		 * and this is the first use.
		 */
		first_use,
		pre_aad_sz, post_aad_sz,  /* size of AAD for the latest packet*/
		hkey_sz,
		ckey_sz;

	/* Direction and bit alignment parameters for the AUX_INFO reg */
	unsigned int auxinfo_dir, auxinfo_bit_align;
	unsigned int auxinfo_cs_mode; /* AUX info setting for CBC-CS */

	u32	ctx_idx;
	unsigned int job_used, job_swid, job_done, job_err, job_secure;
	spacc_callback cb;
	void	*cbdata;

};

#define SPACC_CTX_IDX_UNUSED	0xFFFFFFFF
#define SPACC_JOB_IDX_UNUSED	0xFFFFFFFF

struct spacc_ctx {
	/* Memory context to store cipher keys*/
	void __iomem *ciph_key;
	/* Memory context to store hash keys*/
	void __iomem *hash_key;
	/* reference count of jobs using this context */
	int ref_cnt;
	/* number of contexts following related to this one */
	int ncontig;
};

#define SPACC_CTRL_MASK(field) \
	(1UL << spacc->config.ctrl_map[(field)])
#define SPACC_CTRL_SET(field, value) \
	((value) << spacc->config.ctrl_map[(field)])

enum {
	SPACC_CTRL_VER_0,
	SPACC_CTRL_VER_1,
	SPACC_CTRL_VER_2,
	SPACC_CTRL_VER_SIZE
};

enum {
	SPACC_CTRL_CIPH_ALG,
	SPACC_CTRL_CIPH_MODE,
	SPACC_CTRL_HASH_ALG,
	SPACC_CTRL_HASH_MODE,
	SPACC_CTRL_ENCRYPT,
	SPACC_CTRL_CTX_IDX,
	SPACC_CTRL_SEC_KEY,
	SPACC_CTRL_AAD_COPY,
	SPACC_CTRL_ICV_PT,
	SPACC_CTRL_ICV_ENC,
	SPACC_CTRL_ICV_APPEND,
	SPACC_CTRL_KEY_EXP,
	SPACC_CTRL_MSG_BEGIN,
	SPACC_CTRL_MSG_END,
	SPACC_CTRL_MAPSIZE
};

struct spacc_device {
	void __iomem *regmap;
	int	zero_key;

	/* hardware configuration */
	struct {
		unsigned int version,
			     pdu_version,
			     project;
		uint32_t max_msg_size; /* max PROCLEN value */

		unsigned char modes[CRYPTO_MODE_LAST];

		int num_ctx,           /* no. of contexts */
		    num_sec_ctx,       /* no. of SKP contexts*/
		    sec_ctx_page_size, /* page size of SKP context in bytes*/
		    ciph_page_size,    /* cipher context page size in bytes*/
		    hash_page_size,    /* hash context page size in bytes*/
		    string_size,
		    is_qos,            /* QOS spacc? */
		    is_pdu,            /* PDU spacc? */
		    is_secure,
		    is_secure_port,    /* Are we on the secure port? */
		    is_partial,        /* Is partial processing enabled? */
		    is_ivimport,       /* is ivimport enabled? */
		    dma_type,          /* DMA type: linear or scattergather */
		    idx,               /* Which virtual spacc IDX is this? */
		    priority,          /* Weighted priority of virtual spacc */
		    cmd0_fifo_depth,   /* CMD FIFO depths */
		    cmd1_fifo_depth,
		    cmd2_fifo_depth,
		    stat_fifo_depth,   /* depth of STATUS FIFO */
		    fifo_cnt,
		    ideal_stat_level,
		    spacc_endian;

		uint32_t wd_timer;
		u64 oldtimer, timer;

		const u8 *ctrl_map;    /* map of ctrl register field offsets */
	} config;

	struct spacc_job_buffer {
		int active;
		int job_idx;
		struct pdu_ddt *src, *dst;
		u32 proc_sz, aad_offset, pre_aad_sz,
		post_aad_sz, iv_offset, prio;
	} job_buffer[SPACC_MAX_JOB_BUFFERS];

	int jb_head, jb_tail;

	int op_mode,	/* operating mode and watchdog functionality */
	    wdcnt;	/* number of pending WD IRQs*/

	/* SW_ID value which will be used for next job. */
	unsigned int job_next_swid;

	struct spacc_ctx *ctx;	/* This size changes per configured device */
	struct spacc_job *job;	/* allocate memory for [SPACC_MAX_JOBS]; */
	int job_lookup[SPACC_MAX_JOBS];	/* correlate SW_ID back to job index */

	spinlock_t lock;	/* lock for register access */
	spinlock_t ctx_lock;	/* lock for context manager */

	/* callback functions for IRQ processing */
	void (*irq_cb_cmdx)(struct spacc_device *spacc, int x);
	void (*irq_cb_stat)(struct spacc_device *spacc);
	void (*irq_cb_stat_wd)(struct spacc_device *spacc);

	/* this is called after jobs have been popped off the STATUS FIFO
	 * useful so you can be told when there might be space available
	 * in the CMD FIFO
	 */
	void (*spacc_notify_jobs)(struct spacc_device *spacc);

	/* cache*/
	struct {
		u32 src_ptr,
		    dst_ptr,
		    proc_len,
		    icv_len,
		    icv_offset,
		    pre_aad,
		    post_aad,
		    iv_offset,
		    offset,
		    aux;
	} cache;

	struct device *dptr;
};

enum {
	SPACC_IRQ_MODE_WD   = 1,  /* use WD*/
	SPACC_IRQ_MODE_STEP = 2	  /* older use CMD/STAT stepping */
};

enum {
	SPACC_IRQ_CMD_GET = 0,
	SPACC_IRQ_CMD_SET = 1
};

struct spacc_priv {
	struct spacc_device spacc;
	struct semaphore core_running;
	struct tasklet_struct pop_jobs;
	spinlock_t hw_lock;
	unsigned long max_msg_len;
};


int spacc_open(struct spacc_device *spacc, int enc, int hash, int ctx,
	       int secure_mode, spacc_callback cb, void *cbdata);
int spacc_clone_handle(struct spacc_device *spacc, int old_handle,
		       void *cbdata);
int spacc_close(struct spacc_device *spacc, int job_idx);
int spacc_set_operation(struct spacc_device *spacc, int job_idx, int op,
			u32 prot, uint32_t icvcmd, uint32_t icvoff,
			uint32_t icvsz, uint32_t sec_key);
int spacc_set_key_exp(struct spacc_device *spacc, int job_idx);

int spacc_packet_enqueue_ddt_ex(struct spacc_device *spacc, int use_jb,
		int job_idx, struct pdu_ddt *src_ddt, struct pdu_ddt *dst_ddt,
		u32 proc_sz, uint32_t aad_offset, uint32_t pre_aad_sz,
		u32 post_aad_sz, uint32_t iv_offset, uint32_t prio);
int spacc_packet_enqueue_ddt(struct spacc_device *spacc, int job_idx,
		struct pdu_ddt *src_ddt, struct pdu_ddt *dst_ddt,
		uint32_t proc_sz, u32 aad_offset, uint32_t pre_aad_sz,
		uint32_t post_aad_sz, u32 iv_offset, uint32_t prio);

/* IRQ handling functions */
void spacc_irq_cmdx_enable(struct spacc_device *spacc, int cmdx, int cmdx_cnt);
void spacc_irq_cmdx_disable(struct spacc_device *spacc, int cmdx);
void spacc_irq_stat_enable(struct spacc_device *spacc, int stat_cnt);
void spacc_irq_stat_disable(struct spacc_device *spacc);
void spacc_irq_stat_wd_enable(struct spacc_device *spacc);
void spacc_irq_stat_wd_disable(struct spacc_device *spacc);
void spacc_irq_glbl_enable(struct spacc_device *spacc);
void spacc_irq_glbl_disable(struct spacc_device *spacc);
uint32_t spacc_process_irq(struct spacc_device *spacc);
void spacc_set_wd_count(struct spacc_device *spacc, uint32_t val);
irqreturn_t spacc_irq_handler(int irq, void *dev);
int spacc_sgs_to_ddt(struct device *dev,
		     struct scatterlist *sg1, int len1, int *ents1,
		     struct scatterlist *sg2, int len2, int *ents2,
		     struct scatterlist *sg3, int len3, int *ents3,
		     struct pdu_ddt *ddt, int dma_direction);
int spacc_sg_to_ddt(struct device *dev, struct scatterlist *sg,
		    int nbytes, struct pdu_ddt *ddt, int dma_direction);

/* Context Manager */
void spacc_ctx_init_all(struct spacc_device *spacc);

/* SPAcc specific manipulation of context memory */
int spacc_write_context(struct spacc_device *spacc, int job_idx, int op,
			const unsigned char *key, int ksz,
			const unsigned char *iv, int ivsz);

int spacc_read_context(struct spacc_device *spacc, int job_idx, int op,
		       unsigned char *key, int ksz, unsigned char *iv,
		       int ivsz);

/* Job Manager */
void spacc_job_init_all(struct spacc_device *spacc);
int  spacc_job_request(struct spacc_device *dev, int job_idx);
int  spacc_job_release(struct spacc_device *dev, int job_idx);
int  spacc_handle_release(struct spacc_device *spacc, int job_idx);

/* Helper functions */
struct spacc_ctx *context_lookup_by_job(struct spacc_device *spacc,
					int job_idx);
int spacc_isenabled(struct spacc_device *spacc, int mode, int keysize);
int spacc_compute_xcbc_key(struct spacc_device *spacc, int mode_id,
			   int job_idx, const unsigned char *key,
			   int keylen, unsigned char *xcbc_out);

int  spacc_process_jb(struct spacc_device *spacc);
int  spacc_remove(struct platform_device *pdev);
int  spacc_static_config(struct spacc_device *spacc);
int  spacc_autodetect(struct spacc_device *spacc);
void spacc_pop_jobs(unsigned long data);
void spacc_fini(struct spacc_device *spacc);
int  spacc_init(void __iomem *baseaddr, struct spacc_device *spacc,
		struct pdu_info *info);
int  spacc_pop_packets(struct spacc_device *spacc, int *num_popped);
void spacc_stat_process(struct spacc_device *spacc);
void spacc_cmd_process(struct spacc_device *spacc, int x);

#endif
