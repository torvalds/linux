/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MARVELL_CESA_H__
#define __MARVELL_CESA_H__

#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>

#include <linux/dma-direction.h>
#include <linux/dmapool.h>

#define CESA_ENGINE_OFF(i)			(((i) * 0x2000))

#define CESA_TDMA_BYTE_CNT			0x800
#define CESA_TDMA_SRC_ADDR			0x810
#define CESA_TDMA_DST_ADDR			0x820
#define CESA_TDMA_NEXT_ADDR			0x830

#define CESA_TDMA_CONTROL			0x840
#define CESA_TDMA_DST_BURST			GENMASK(2, 0)
#define CESA_TDMA_DST_BURST_32B			3
#define CESA_TDMA_DST_BURST_128B		4
#define CESA_TDMA_OUT_RD_EN			BIT(4)
#define CESA_TDMA_SRC_BURST			GENMASK(8, 6)
#define CESA_TDMA_SRC_BURST_32B			(3 << 6)
#define CESA_TDMA_SRC_BURST_128B		(4 << 6)
#define CESA_TDMA_CHAIN				BIT(9)
#define CESA_TDMA_BYTE_SWAP			BIT(11)
#define CESA_TDMA_NO_BYTE_SWAP			BIT(11)
#define CESA_TDMA_EN				BIT(12)
#define CESA_TDMA_FETCH_ND			BIT(13)
#define CESA_TDMA_ACT				BIT(14)

#define CESA_TDMA_CUR				0x870
#define CESA_TDMA_ERROR_CAUSE			0x8c8
#define CESA_TDMA_ERROR_MSK			0x8cc

#define CESA_TDMA_WINDOW_BASE(x)		(((x) * 0x8) + 0xa00)
#define CESA_TDMA_WINDOW_CTRL(x)		(((x) * 0x8) + 0xa04)

#define CESA_IVDIG(x)				(0xdd00 + ((x) * 4) +	\
						 (((x) < 5) ? 0 : 0x14))

#define CESA_SA_CMD				0xde00
#define CESA_SA_CMD_EN_CESA_SA_ACCL0		BIT(0)
#define CESA_SA_CMD_EN_CESA_SA_ACCL1		BIT(1)
#define CESA_SA_CMD_DISABLE_SEC			BIT(2)

#define CESA_SA_DESC_P0				0xde04

#define CESA_SA_DESC_P1				0xde14

#define CESA_SA_CFG				0xde08
#define CESA_SA_CFG_STOP_DIG_ERR		GENMASK(1, 0)
#define CESA_SA_CFG_DIG_ERR_CONT		0
#define CESA_SA_CFG_DIG_ERR_SKIP		1
#define CESA_SA_CFG_DIG_ERR_STOP		3
#define CESA_SA_CFG_CH0_W_IDMA			BIT(7)
#define CESA_SA_CFG_CH1_W_IDMA			BIT(8)
#define CESA_SA_CFG_ACT_CH0_IDMA		BIT(9)
#define CESA_SA_CFG_ACT_CH1_IDMA		BIT(10)
#define CESA_SA_CFG_MULTI_PKT			BIT(11)
#define CESA_SA_CFG_PARA_DIS			BIT(13)

#define CESA_SA_ACCEL_STATUS			0xde0c
#define CESA_SA_ST_ACT_0			BIT(0)
#define CESA_SA_ST_ACT_1			BIT(1)

/*
 * CESA_SA_FPGA_INT_STATUS looks like an FPGA leftover and is documented only
 * in Errata 4.12. It looks like that it was part of an IRQ-controller in FPGA
 * and someone forgot to remove  it while switching to the core and moving to
 * CESA_SA_INT_STATUS.
 */
#define CESA_SA_FPGA_INT_STATUS			0xdd68
#define CESA_SA_INT_STATUS			0xde20
#define CESA_SA_INT_AUTH_DONE			BIT(0)
#define CESA_SA_INT_DES_E_DONE			BIT(1)
#define CESA_SA_INT_AES_E_DONE			BIT(2)
#define CESA_SA_INT_AES_D_DONE			BIT(3)
#define CESA_SA_INT_ENC_DONE			BIT(4)
#define CESA_SA_INT_ACCEL0_DONE			BIT(5)
#define CESA_SA_INT_ACCEL1_DONE			BIT(6)
#define CESA_SA_INT_ACC0_IDMA_DONE		BIT(7)
#define CESA_SA_INT_ACC1_IDMA_DONE		BIT(8)
#define CESA_SA_INT_IDMA_DONE			BIT(9)
#define CESA_SA_INT_IDMA_OWN_ERR		BIT(10)

#define CESA_SA_INT_MSK				0xde24

#define CESA_SA_DESC_CFG_OP_MAC_ONLY		0
#define CESA_SA_DESC_CFG_OP_CRYPT_ONLY		1
#define CESA_SA_DESC_CFG_OP_MAC_CRYPT		2
#define CESA_SA_DESC_CFG_OP_CRYPT_MAC		3
#define CESA_SA_DESC_CFG_OP_MSK			GENMASK(1, 0)
#define CESA_SA_DESC_CFG_MACM_SHA256		(1 << 4)
#define CESA_SA_DESC_CFG_MACM_HMAC_SHA256	(3 << 4)
#define CESA_SA_DESC_CFG_MACM_MD5		(4 << 4)
#define CESA_SA_DESC_CFG_MACM_SHA1		(5 << 4)
#define CESA_SA_DESC_CFG_MACM_HMAC_MD5		(6 << 4)
#define CESA_SA_DESC_CFG_MACM_HMAC_SHA1		(7 << 4)
#define CESA_SA_DESC_CFG_MACM_MSK		GENMASK(6, 4)
#define CESA_SA_DESC_CFG_CRYPTM_DES		(1 << 8)
#define CESA_SA_DESC_CFG_CRYPTM_3DES		(2 << 8)
#define CESA_SA_DESC_CFG_CRYPTM_AES		(3 << 8)
#define CESA_SA_DESC_CFG_CRYPTM_MSK		GENMASK(9, 8)
#define CESA_SA_DESC_CFG_DIR_ENC		(0 << 12)
#define CESA_SA_DESC_CFG_DIR_DEC		(1 << 12)
#define CESA_SA_DESC_CFG_CRYPTCM_ECB		(0 << 16)
#define CESA_SA_DESC_CFG_CRYPTCM_CBC		(1 << 16)
#define CESA_SA_DESC_CFG_CRYPTCM_MSK		BIT(16)
#define CESA_SA_DESC_CFG_3DES_EEE		(0 << 20)
#define CESA_SA_DESC_CFG_3DES_EDE		(1 << 20)
#define CESA_SA_DESC_CFG_AES_LEN_128		(0 << 24)
#define CESA_SA_DESC_CFG_AES_LEN_192		(1 << 24)
#define CESA_SA_DESC_CFG_AES_LEN_256		(2 << 24)
#define CESA_SA_DESC_CFG_AES_LEN_MSK		GENMASK(25, 24)
#define CESA_SA_DESC_CFG_NOT_FRAG		(0 << 30)
#define CESA_SA_DESC_CFG_FIRST_FRAG		(1 << 30)
#define CESA_SA_DESC_CFG_LAST_FRAG		(2 << 30)
#define CESA_SA_DESC_CFG_MID_FRAG		(3 << 30)
#define CESA_SA_DESC_CFG_FRAG_MSK		GENMASK(31, 30)

/*
 * /-----------\ 0
 * | ACCEL CFG |	4 * 8
 * |-----------| 0x20
 * | CRYPT KEY |	8 * 4
 * |-----------| 0x40
 * |  IV   IN  |	4 * 4
 * |-----------| 0x40 (inplace)
 * |  IV BUF   |	4 * 4
 * |-----------| 0x80
 * |  DATA IN  |	16 * x (max ->max_req_size)
 * |-----------| 0x80 (inplace operation)
 * |  DATA OUT |	16 * x (max ->max_req_size)
 * \-----------/ SRAM size
 */

/*
 * Hashing memory map:
 * /-----------\ 0
 * | ACCEL CFG |        4 * 8
 * |-----------| 0x20
 * | Inner IV  |        8 * 4
 * |-----------| 0x40
 * | Outer IV  |        8 * 4
 * |-----------| 0x60
 * | Output BUF|        8 * 4
 * |-----------| 0x80
 * |  DATA IN  |        64 * x (max ->max_req_size)
 * \-----------/ SRAM size
 */

#define CESA_SA_CFG_SRAM_OFFSET			0x00
#define CESA_SA_DATA_SRAM_OFFSET		0x80

#define CESA_SA_CRYPT_KEY_SRAM_OFFSET		0x20
#define CESA_SA_CRYPT_IV_SRAM_OFFSET		0x40

#define CESA_SA_MAC_IIV_SRAM_OFFSET		0x20
#define CESA_SA_MAC_OIV_SRAM_OFFSET		0x40
#define CESA_SA_MAC_DIG_SRAM_OFFSET		0x60

#define CESA_SA_DESC_CRYPT_DATA(offset)					\
	cpu_to_le32((CESA_SA_DATA_SRAM_OFFSET + (offset)) |		\
		    ((CESA_SA_DATA_SRAM_OFFSET + (offset)) << 16))

#define CESA_SA_DESC_CRYPT_IV(offset)					\
	cpu_to_le32((CESA_SA_CRYPT_IV_SRAM_OFFSET + (offset)) |	\
		    ((CESA_SA_CRYPT_IV_SRAM_OFFSET + (offset)) << 16))

#define CESA_SA_DESC_CRYPT_KEY(offset)					\
	cpu_to_le32(CESA_SA_CRYPT_KEY_SRAM_OFFSET + (offset))

#define CESA_SA_DESC_MAC_DATA(offset)					\
	cpu_to_le32(CESA_SA_DATA_SRAM_OFFSET + (offset))
#define CESA_SA_DESC_MAC_DATA_MSK		cpu_to_le32(GENMASK(15, 0))

#define CESA_SA_DESC_MAC_TOTAL_LEN(total_len)	cpu_to_le32((total_len) << 16)
#define CESA_SA_DESC_MAC_TOTAL_LEN_MSK		cpu_to_le32(GENMASK(31, 16))

#define CESA_SA_DESC_MAC_SRC_TOTAL_LEN_MAX	0xffff

#define CESA_SA_DESC_MAC_DIGEST(offset)					\
	cpu_to_le32(CESA_SA_MAC_DIG_SRAM_OFFSET + (offset))
#define CESA_SA_DESC_MAC_DIGEST_MSK		cpu_to_le32(GENMASK(15, 0))

#define CESA_SA_DESC_MAC_FRAG_LEN(frag_len)	cpu_to_le32((frag_len) << 16)
#define CESA_SA_DESC_MAC_FRAG_LEN_MSK		cpu_to_le32(GENMASK(31, 16))

#define CESA_SA_DESC_MAC_IV(offset)					\
	cpu_to_le32((CESA_SA_MAC_IIV_SRAM_OFFSET + (offset)) |		\
		    ((CESA_SA_MAC_OIV_SRAM_OFFSET + (offset)) << 16))

#define CESA_SA_SRAM_SIZE			2048
#define CESA_SA_SRAM_PAYLOAD_SIZE		(cesa_dev->sram_size - \
						 CESA_SA_DATA_SRAM_OFFSET)

#define CESA_SA_DEFAULT_SRAM_SIZE		2048
#define CESA_SA_MIN_SRAM_SIZE			1024

#define CESA_SA_SRAM_MSK			(2048 - 1)

#define CESA_MAX_HASH_BLOCK_SIZE		64
#define CESA_HASH_BLOCK_SIZE_MSK		(CESA_MAX_HASH_BLOCK_SIZE - 1)

/**
 * struct mv_cesa_sec_accel_desc - security accelerator descriptor
 * @config:	engine config
 * @enc_p:	input and output data pointers for a cipher operation
 * @enc_len:	cipher operation length
 * @enc_key_p:	cipher key pointer
 * @enc_iv:	cipher IV pointers
 * @mac_src_p:	input pointer and total hash length
 * @mac_digest:	digest pointer and hash operation length
 * @mac_iv:	hmac IV pointers
 *
 * Structure passed to the CESA engine to describe the crypto operation
 * to be executed.
 */
struct mv_cesa_sec_accel_desc {
	__le32 config;
	__le32 enc_p;
	__le32 enc_len;
	__le32 enc_key_p;
	__le32 enc_iv;
	__le32 mac_src_p;
	__le32 mac_digest;
	__le32 mac_iv;
};

/**
 * struct mv_cesa_skcipher_op_ctx - cipher operation context
 * @key:	cipher key
 * @iv:		cipher IV
 *
 * Context associated to a cipher operation.
 */
struct mv_cesa_skcipher_op_ctx {
	__le32 key[8];
	u32 iv[4];
};

/**
 * struct mv_cesa_hash_op_ctx - hash or hmac operation context
 * @key:	cipher key
 * @iv:		cipher IV
 *
 * Context associated to an hash or hmac operation.
 */
struct mv_cesa_hash_op_ctx {
	u32 iv[16];
	__le32 hash[8];
};

/**
 * struct mv_cesa_op_ctx - crypto operation context
 * @desc:	CESA descriptor
 * @ctx:	context associated to the crypto operation
 *
 * Context associated to a crypto operation.
 */
struct mv_cesa_op_ctx {
	struct mv_cesa_sec_accel_desc desc;
	union {
		struct mv_cesa_skcipher_op_ctx skcipher;
		struct mv_cesa_hash_op_ctx hash;
	} ctx;
};

/* TDMA descriptor flags */
#define CESA_TDMA_DST_IN_SRAM			BIT(31)
#define CESA_TDMA_SRC_IN_SRAM			BIT(30)
#define CESA_TDMA_END_OF_REQ			BIT(29)
#define CESA_TDMA_BREAK_CHAIN			BIT(28)
#define CESA_TDMA_SET_STATE			BIT(27)
#define CESA_TDMA_TYPE_MSK			GENMASK(26, 0)
#define CESA_TDMA_DUMMY				0
#define CESA_TDMA_DATA				1
#define CESA_TDMA_OP				2
#define CESA_TDMA_RESULT			3

/**
 * struct mv_cesa_tdma_desc - TDMA descriptor
 * @byte_cnt:	number of bytes to transfer
 * @src:	DMA address of the source
 * @dst:	DMA address of the destination
 * @next_dma:	DMA address of the next TDMA descriptor
 * @cur_dma:	DMA address of this TDMA descriptor
 * @next:	pointer to the next TDMA descriptor
 * @op:		CESA operation attached to this TDMA descriptor
 * @data:	raw data attached to this TDMA descriptor
 * @flags:	flags describing the TDMA transfer. See the
 *		"TDMA descriptor flags" section above
 *
 * TDMA descriptor used to create a transfer chain describing a crypto
 * operation.
 */
struct mv_cesa_tdma_desc {
	__le32 byte_cnt;
	union {
		__le32 src;
		u32 src_dma;
	};
	union {
		__le32 dst;
		u32 dst_dma;
	};
	__le32 next_dma;

	/* Software state */
	dma_addr_t cur_dma;
	struct mv_cesa_tdma_desc *next;
	union {
		struct mv_cesa_op_ctx *op;
		void *data;
	};
	u32 flags;
};

/**
 * struct mv_cesa_sg_dma_iter - scatter-gather iterator
 * @dir:	transfer direction
 * @sg:		scatter list
 * @offset:	current position in the scatter list
 * @op_offset:	current position in the crypto operation
 *
 * Iterator used to iterate over a scatterlist while creating a TDMA chain for
 * a crypto operation.
 */
struct mv_cesa_sg_dma_iter {
	enum dma_data_direction dir;
	struct scatterlist *sg;
	unsigned int offset;
	unsigned int op_offset;
};

/**
 * struct mv_cesa_dma_iter - crypto operation iterator
 * @len:	the crypto operation length
 * @offset:	current position in the crypto operation
 * @op_len:	sub-operation length (the crypto engine can only act on 2kb
 *		chunks)
 *
 * Iterator used to create a TDMA chain for a given crypto operation.
 */
struct mv_cesa_dma_iter {
	unsigned int len;
	unsigned int offset;
	unsigned int op_len;
};

/**
 * struct mv_cesa_tdma_chain - TDMA chain
 * @first:	first entry in the TDMA chain
 * @last:	last entry in the TDMA chain
 *
 * Stores a TDMA chain for a specific crypto operation.
 */
struct mv_cesa_tdma_chain {
	struct mv_cesa_tdma_desc *first;
	struct mv_cesa_tdma_desc *last;
};

struct mv_cesa_engine;

/**
 * struct mv_cesa_caps - CESA device capabilities
 * @engines:		number of engines
 * @has_tdma:		whether this device has a TDMA block
 * @cipher_algs:	supported cipher algorithms
 * @ncipher_algs:	number of supported cipher algorithms
 * @ahash_algs:		supported hash algorithms
 * @nahash_algs:	number of supported hash algorithms
 *
 * Structure used to describe CESA device capabilities.
 */
struct mv_cesa_caps {
	int nengines;
	bool has_tdma;
	struct skcipher_alg **cipher_algs;
	int ncipher_algs;
	struct ahash_alg **ahash_algs;
	int nahash_algs;
};

/**
 * struct mv_cesa_dev_dma - DMA pools
 * @tdma_desc_pool:	TDMA desc pool
 * @op_pool:		crypto operation pool
 * @cache_pool:		data cache pool (used by hash implementation when the
 *			hash request is smaller than the hash block size)
 * @padding_pool:	padding pool (used by hash implementation when hardware
 *			padding cannot be used)
 *
 * Structure containing the different DMA pools used by this driver.
 */
struct mv_cesa_dev_dma {
	struct dma_pool *tdma_desc_pool;
	struct dma_pool *op_pool;
	struct dma_pool *cache_pool;
	struct dma_pool *padding_pool;
};

/**
 * struct mv_cesa_dev - CESA device
 * @caps:	device capabilities
 * @regs:	device registers
 * @sram_size:	usable SRAM size
 * @lock:	device lock
 * @engines:	array of engines
 * @dma:	dma pools
 *
 * Structure storing CESA device information.
 */
struct mv_cesa_dev {
	const struct mv_cesa_caps *caps;
	void __iomem *regs;
	struct device *dev;
	unsigned int sram_size;
	spinlock_t lock;
	struct mv_cesa_engine *engines;
	struct mv_cesa_dev_dma *dma;
};

/**
 * struct mv_cesa_engine - CESA engine
 * @id:			engine id
 * @regs:		engine registers
 * @sram:		SRAM memory region
 * @sram_pool:		SRAM memory region from pool
 * @sram_dma:		DMA address of the SRAM memory region
 * @lock:		engine lock
 * @req:		current crypto request
 * @clk:		engine clk
 * @zclk:		engine zclk
 * @max_req_len:	maximum chunk length (useful to create the TDMA chain)
 * @int_mask:		interrupt mask cache
 * @pool:		memory pool pointing to the memory region reserved in
 *			SRAM
 * @queue:		fifo of the pending crypto requests
 * @load:		engine load counter, useful for load balancing
 * @chain:		list of the current tdma descriptors being processed
 *			by this engine.
 * @complete_queue:	fifo of the processed requests by the engine
 *
 * Structure storing CESA engine information.
 */
struct mv_cesa_engine {
	int id;
	void __iomem *regs;
	union {
		void __iomem *sram;
		void *sram_pool;
	};
	dma_addr_t sram_dma;
	spinlock_t lock;
	struct crypto_async_request *req;
	struct clk *clk;
	struct clk *zclk;
	size_t max_req_len;
	u32 int_mask;
	struct gen_pool *pool;
	struct crypto_queue queue;
	atomic_t load;
	struct mv_cesa_tdma_chain chain;
	struct list_head complete_queue;
	int irq;
};

/**
 * struct mv_cesa_req_ops - CESA request operations
 * @process:	process a request chunk result (should return 0 if the
 *		operation, -EINPROGRESS if it needs more steps or an error
 *		code)
 * @step:	launch the crypto operation on the next chunk
 * @cleanup:	cleanup the crypto request (release associated data)
 * @complete:	complete the request, i.e copy result or context from sram when
 *		needed.
 */
struct mv_cesa_req_ops {
	int (*process)(struct crypto_async_request *req, u32 status);
	void (*step)(struct crypto_async_request *req);
	void (*cleanup)(struct crypto_async_request *req);
	void (*complete)(struct crypto_async_request *req);
};

/**
 * struct mv_cesa_ctx - CESA operation context
 * @ops:	crypto operations
 *
 * Base context structure inherited by operation specific ones.
 */
struct mv_cesa_ctx {
	const struct mv_cesa_req_ops *ops;
};

/**
 * struct mv_cesa_hash_ctx - CESA hash operation context
 * @base:	base context structure
 *
 * Hash context structure.
 */
struct mv_cesa_hash_ctx {
	struct mv_cesa_ctx base;
};

/**
 * struct mv_cesa_hash_ctx - CESA hmac operation context
 * @base:	base context structure
 * @iv:		initialization vectors
 *
 * HMAC context structure.
 */
struct mv_cesa_hmac_ctx {
	struct mv_cesa_ctx base;
	__be32 iv[16];
};

/**
 * enum mv_cesa_req_type - request type definitions
 * @CESA_STD_REQ:	standard request
 * @CESA_DMA_REQ:	DMA request
 */
enum mv_cesa_req_type {
	CESA_STD_REQ,
	CESA_DMA_REQ,
};

/**
 * struct mv_cesa_req - CESA request
 * @engine:	engine associated with this request
 * @chain:	list of tdma descriptors associated  with this request
 */
struct mv_cesa_req {
	struct mv_cesa_engine *engine;
	struct mv_cesa_tdma_chain chain;
};

/**
 * struct mv_cesa_sg_std_iter - CESA scatter-gather iterator for standard
 *				requests
 * @iter:	sg mapping iterator
 * @offset:	current offset in the SG entry mapped in memory
 */
struct mv_cesa_sg_std_iter {
	struct sg_mapping_iter iter;
	unsigned int offset;
};

/**
 * struct mv_cesa_skcipher_std_req - cipher standard request
 * @op:		operation context
 * @offset:	current operation offset
 * @size:	size of the crypto operation
 */
struct mv_cesa_skcipher_std_req {
	struct mv_cesa_op_ctx op;
	unsigned int offset;
	unsigned int size;
	bool skip_ctx;
};

/**
 * struct mv_cesa_skcipher_req - cipher request
 * @req:	type specific request information
 * @src_nents:	number of entries in the src sg list
 * @dst_nents:	number of entries in the dest sg list
 */
struct mv_cesa_skcipher_req {
	struct mv_cesa_req base;
	struct mv_cesa_skcipher_std_req std;
	int src_nents;
	int dst_nents;
};

/**
 * struct mv_cesa_ahash_std_req - standard hash request
 * @offset:	current operation offset
 */
struct mv_cesa_ahash_std_req {
	unsigned int offset;
};

/**
 * struct mv_cesa_ahash_dma_req - DMA hash request
 * @padding:		padding buffer
 * @padding_dma:	DMA address of the padding buffer
 * @cache_dma:		DMA address of the cache buffer
 */
struct mv_cesa_ahash_dma_req {
	u8 *padding;
	dma_addr_t padding_dma;
	u8 *cache;
	dma_addr_t cache_dma;
};

/**
 * struct mv_cesa_ahash_req - hash request
 * @req:		type specific request information
 * @cache:		cache buffer
 * @cache_ptr:		write pointer in the cache buffer
 * @len:		hash total length
 * @src_nents:		number of entries in the scatterlist
 * @last_req:		define whether the current operation is the last one
 *			or not
 * @state:		hash state
 */
struct mv_cesa_ahash_req {
	struct mv_cesa_req base;
	union {
		struct mv_cesa_ahash_dma_req dma;
		struct mv_cesa_ahash_std_req std;
	} req;
	struct mv_cesa_op_ctx op_tmpl;
	u8 cache[CESA_MAX_HASH_BLOCK_SIZE];
	unsigned int cache_ptr;
	u64 len;
	int src_nents;
	bool last_req;
	bool algo_le;
	u32 state[8];
};

/* CESA functions */

extern struct mv_cesa_dev *cesa_dev;


static inline void
mv_cesa_engine_enqueue_complete_request(struct mv_cesa_engine *engine,
					struct crypto_async_request *req)
{
	list_add_tail(&req->list, &engine->complete_queue);
}

static inline struct crypto_async_request *
mv_cesa_engine_dequeue_complete_request(struct mv_cesa_engine *engine)
{
	struct crypto_async_request *req;

	req = list_first_entry_or_null(&engine->complete_queue,
				       struct crypto_async_request,
				       list);
	if (req)
		list_del(&req->list);

	return req;
}


static inline enum mv_cesa_req_type
mv_cesa_req_get_type(struct mv_cesa_req *req)
{
	return req->chain.first ? CESA_DMA_REQ : CESA_STD_REQ;
}

static inline void mv_cesa_update_op_cfg(struct mv_cesa_op_ctx *op,
					 u32 cfg, u32 mask)
{
	op->desc.config &= cpu_to_le32(~mask);
	op->desc.config |= cpu_to_le32(cfg);
}

static inline u32 mv_cesa_get_op_cfg(const struct mv_cesa_op_ctx *op)
{
	return le32_to_cpu(op->desc.config);
}

static inline void mv_cesa_set_op_cfg(struct mv_cesa_op_ctx *op, u32 cfg)
{
	op->desc.config = cpu_to_le32(cfg);
}

static inline void mv_cesa_adjust_op(struct mv_cesa_engine *engine,
				     struct mv_cesa_op_ctx *op)
{
	u32 offset = engine->sram_dma & CESA_SA_SRAM_MSK;

	op->desc.enc_p = CESA_SA_DESC_CRYPT_DATA(offset);
	op->desc.enc_key_p = CESA_SA_DESC_CRYPT_KEY(offset);
	op->desc.enc_iv = CESA_SA_DESC_CRYPT_IV(offset);
	op->desc.mac_src_p &= ~CESA_SA_DESC_MAC_DATA_MSK;
	op->desc.mac_src_p |= CESA_SA_DESC_MAC_DATA(offset);
	op->desc.mac_digest &= ~CESA_SA_DESC_MAC_DIGEST_MSK;
	op->desc.mac_digest |= CESA_SA_DESC_MAC_DIGEST(offset);
	op->desc.mac_iv = CESA_SA_DESC_MAC_IV(offset);
}

static inline void mv_cesa_set_crypt_op_len(struct mv_cesa_op_ctx *op, int len)
{
	op->desc.enc_len = cpu_to_le32(len);
}

static inline void mv_cesa_set_mac_op_total_len(struct mv_cesa_op_ctx *op,
						int len)
{
	op->desc.mac_src_p &= ~CESA_SA_DESC_MAC_TOTAL_LEN_MSK;
	op->desc.mac_src_p |= CESA_SA_DESC_MAC_TOTAL_LEN(len);
}

static inline void mv_cesa_set_mac_op_frag_len(struct mv_cesa_op_ctx *op,
					       int len)
{
	op->desc.mac_digest &= ~CESA_SA_DESC_MAC_FRAG_LEN_MSK;
	op->desc.mac_digest |= CESA_SA_DESC_MAC_FRAG_LEN(len);
}

static inline void mv_cesa_set_int_mask(struct mv_cesa_engine *engine,
					u32 int_mask)
{
	if (int_mask == engine->int_mask)
		return;

	writel_relaxed(int_mask, engine->regs + CESA_SA_INT_MSK);
	engine->int_mask = int_mask;
}

static inline u32 mv_cesa_get_int_mask(struct mv_cesa_engine *engine)
{
	return engine->int_mask;
}

static inline bool mv_cesa_mac_op_is_first_frag(const struct mv_cesa_op_ctx *op)
{
	return (mv_cesa_get_op_cfg(op) & CESA_SA_DESC_CFG_FRAG_MSK) ==
		CESA_SA_DESC_CFG_FIRST_FRAG;
}

int mv_cesa_queue_req(struct crypto_async_request *req,
		      struct mv_cesa_req *creq);

struct crypto_async_request *
mv_cesa_dequeue_req_locked(struct mv_cesa_engine *engine,
			   struct crypto_async_request **backlog);

static inline struct mv_cesa_engine *mv_cesa_select_engine(int weight)
{
	int i;
	u32 min_load = U32_MAX;
	struct mv_cesa_engine *selected = NULL;

	for (i = 0; i < cesa_dev->caps->nengines; i++) {
		struct mv_cesa_engine *engine = cesa_dev->engines + i;
		u32 load = atomic_read(&engine->load);

		if (load < min_load) {
			min_load = load;
			selected = engine;
		}
	}

	atomic_add(weight, &selected->load);

	return selected;
}

/*
 * Helper function that indicates whether a crypto request needs to be
 * cleaned up or not after being enqueued using mv_cesa_queue_req().
 */
static inline int mv_cesa_req_needs_cleanup(struct crypto_async_request *req,
					    int ret)
{
	/*
	 * The queue still had some space, the request was queued
	 * normally, so there's no need to clean it up.
	 */
	if (ret == -EINPROGRESS)
		return false;

	/*
	 * The queue had not space left, but since the request is
	 * flagged with CRYPTO_TFM_REQ_MAY_BACKLOG, it was added to
	 * the backlog and will be processed later. There's no need to
	 * clean it up.
	 */
	if (ret == -EBUSY)
		return false;

	/* Request wasn't queued, we need to clean it up */
	return true;
}

/* TDMA functions */

static inline void mv_cesa_req_dma_iter_init(struct mv_cesa_dma_iter *iter,
					     unsigned int len)
{
	iter->len = len;
	iter->op_len = min(len, CESA_SA_SRAM_PAYLOAD_SIZE);
	iter->offset = 0;
}

static inline void mv_cesa_sg_dma_iter_init(struct mv_cesa_sg_dma_iter *iter,
					    struct scatterlist *sg,
					    enum dma_data_direction dir)
{
	iter->op_offset = 0;
	iter->offset = 0;
	iter->sg = sg;
	iter->dir = dir;
}

static inline unsigned int
mv_cesa_req_dma_iter_transfer_len(struct mv_cesa_dma_iter *iter,
				  struct mv_cesa_sg_dma_iter *sgiter)
{
	return min(iter->op_len - sgiter->op_offset,
		   sg_dma_len(sgiter->sg) - sgiter->offset);
}

bool mv_cesa_req_dma_iter_next_transfer(struct mv_cesa_dma_iter *chain,
					struct mv_cesa_sg_dma_iter *sgiter,
					unsigned int len);

static inline bool mv_cesa_req_dma_iter_next_op(struct mv_cesa_dma_iter *iter)
{
	iter->offset += iter->op_len;
	iter->op_len = min(iter->len - iter->offset,
			   CESA_SA_SRAM_PAYLOAD_SIZE);

	return iter->op_len;
}

void mv_cesa_dma_step(struct mv_cesa_req *dreq);

static inline int mv_cesa_dma_process(struct mv_cesa_req *dreq,
				      u32 status)
{
	if (!(status & CESA_SA_INT_ACC0_IDMA_DONE))
		return -EINPROGRESS;

	if (status & CESA_SA_INT_IDMA_OWN_ERR)
		return -EINVAL;

	return 0;
}

void mv_cesa_dma_prepare(struct mv_cesa_req *dreq,
			 struct mv_cesa_engine *engine);
void mv_cesa_dma_cleanup(struct mv_cesa_req *dreq);
void mv_cesa_tdma_chain(struct mv_cesa_engine *engine,
			struct mv_cesa_req *dreq);
int mv_cesa_tdma_process(struct mv_cesa_engine *engine, u32 status);


static inline void
mv_cesa_tdma_desc_iter_init(struct mv_cesa_tdma_chain *chain)
{
	memset(chain, 0, sizeof(*chain));
}

int mv_cesa_dma_add_result_op(struct mv_cesa_tdma_chain *chain, dma_addr_t src,
			  u32 size, u32 flags, gfp_t gfp_flags);

struct mv_cesa_op_ctx *mv_cesa_dma_add_op(struct mv_cesa_tdma_chain *chain,
					const struct mv_cesa_op_ctx *op_templ,
					bool skip_ctx,
					gfp_t flags);

int mv_cesa_dma_add_data_transfer(struct mv_cesa_tdma_chain *chain,
				  dma_addr_t dst, dma_addr_t src, u32 size,
				  u32 flags, gfp_t gfp_flags);

int mv_cesa_dma_add_dummy_launch(struct mv_cesa_tdma_chain *chain, gfp_t flags);
int mv_cesa_dma_add_dummy_end(struct mv_cesa_tdma_chain *chain, gfp_t flags);

int mv_cesa_dma_add_op_transfers(struct mv_cesa_tdma_chain *chain,
				 struct mv_cesa_dma_iter *dma_iter,
				 struct mv_cesa_sg_dma_iter *sgiter,
				 gfp_t gfp_flags);

size_t mv_cesa_sg_copy(struct mv_cesa_engine *engine,
		       struct scatterlist *sgl, unsigned int nents,
		       unsigned int sram_off, size_t buflen, off_t skip,
		       bool to_sram);

static inline size_t mv_cesa_sg_copy_to_sram(struct mv_cesa_engine *engine,
					     struct scatterlist *sgl,
					     unsigned int nents,
					     unsigned int sram_off,
					     size_t buflen, off_t skip)
{
	return mv_cesa_sg_copy(engine, sgl, nents, sram_off, buflen, skip,
			       true);
}

static inline size_t mv_cesa_sg_copy_from_sram(struct mv_cesa_engine *engine,
					       struct scatterlist *sgl,
					       unsigned int nents,
					       unsigned int sram_off,
					       size_t buflen, off_t skip)
{
	return mv_cesa_sg_copy(engine, sgl, nents, sram_off, buflen, skip,
			       false);
}

/* Algorithm definitions */

extern struct ahash_alg mv_md5_alg;
extern struct ahash_alg mv_sha1_alg;
extern struct ahash_alg mv_sha256_alg;
extern struct ahash_alg mv_ahmac_md5_alg;
extern struct ahash_alg mv_ahmac_sha1_alg;
extern struct ahash_alg mv_ahmac_sha256_alg;

extern struct skcipher_alg mv_cesa_ecb_des_alg;
extern struct skcipher_alg mv_cesa_cbc_des_alg;
extern struct skcipher_alg mv_cesa_ecb_des3_ede_alg;
extern struct skcipher_alg mv_cesa_cbc_des3_ede_alg;
extern struct skcipher_alg mv_cesa_ecb_aes_alg;
extern struct skcipher_alg mv_cesa_cbc_aes_alg;

#endif /* __MARVELL_CESA_H__ */
