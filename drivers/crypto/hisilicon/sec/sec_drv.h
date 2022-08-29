/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016-2017 HiSilicon Limited. */

#ifndef _SEC_DRV_H_
#define _SEC_DRV_H_

#include <crypto/algapi.h>
#include <linux/kfifo.h>

#define SEC_MAX_SGE_NUM			64
#define SEC_HW_RING_NUM			3

#define SEC_CMD_RING			0
#define SEC_OUTORDER_RING		1
#define SEC_DBG_RING			2

/* A reasonable length to balance memory use against flexibility */
#define SEC_QUEUE_LEN			512

#define SEC_MAX_SGE_NUM   64

struct sec_bd_info {
#define SEC_BD_W0_T_LEN_M			GENMASK(4, 0)
#define SEC_BD_W0_T_LEN_S			0

#define SEC_BD_W0_C_WIDTH_M			GENMASK(6, 5)
#define SEC_BD_W0_C_WIDTH_S			5
#define   SEC_C_WIDTH_AES_128BIT		0
#define   SEC_C_WIDTH_AES_8BIT		1
#define   SEC_C_WIDTH_AES_1BIT		2
#define   SEC_C_WIDTH_DES_64BIT		0
#define   SEC_C_WIDTH_DES_8BIT		1
#define   SEC_C_WIDTH_DES_1BIT		2

#define SEC_BD_W0_C_MODE_M			GENMASK(9, 7)
#define SEC_BD_W0_C_MODE_S			7
#define   SEC_C_MODE_ECB			0
#define   SEC_C_MODE_CBC			1
#define   SEC_C_MODE_CTR			4
#define   SEC_C_MODE_CCM			5
#define   SEC_C_MODE_GCM			6
#define   SEC_C_MODE_XTS			7

#define SEC_BD_W0_SEQ				BIT(10)
#define SEC_BD_W0_DE				BIT(11)
#define SEC_BD_W0_DAT_SKIP_M			GENMASK(13, 12)
#define SEC_BD_W0_DAT_SKIP_S			12
#define SEC_BD_W0_C_GRAN_SIZE_19_16_M		GENMASK(17, 14)
#define SEC_BD_W0_C_GRAN_SIZE_19_16_S		14

#define SEC_BD_W0_CIPHER_M			GENMASK(19, 18)
#define SEC_BD_W0_CIPHER_S			18
#define   SEC_CIPHER_NULL			0
#define   SEC_CIPHER_ENCRYPT			1
#define   SEC_CIPHER_DECRYPT			2

#define SEC_BD_W0_AUTH_M			GENMASK(21, 20)
#define SEC_BD_W0_AUTH_S			20
#define   SEC_AUTH_NULL				0
#define   SEC_AUTH_MAC				1
#define   SEC_AUTH_VERIF			2

#define SEC_BD_W0_AI_GEN			BIT(22)
#define SEC_BD_W0_CI_GEN			BIT(23)
#define SEC_BD_W0_NO_HPAD			BIT(24)
#define SEC_BD_W0_HM_M				GENMASK(26, 25)
#define SEC_BD_W0_HM_S				25
#define SEC_BD_W0_ICV_OR_SKEY_EN_M		GENMASK(28, 27)
#define SEC_BD_W0_ICV_OR_SKEY_EN_S		27

/* Multi purpose field - gran size bits for send, flag for recv */
#define SEC_BD_W0_FLAG_M			GENMASK(30, 29)
#define SEC_BD_W0_C_GRAN_SIZE_21_20_M		GENMASK(30, 29)
#define SEC_BD_W0_FLAG_S			29
#define SEC_BD_W0_C_GRAN_SIZE_21_20_S		29

#define SEC_BD_W0_DONE				BIT(31)
	u32 w0;

#define SEC_BD_W1_AUTH_GRAN_SIZE_M		GENMASK(21, 0)
#define SEC_BD_W1_AUTH_GRAN_SIZE_S		0
#define SEC_BD_W1_M_KEY_EN			BIT(22)
#define SEC_BD_W1_BD_INVALID			BIT(23)
#define SEC_BD_W1_ADDR_TYPE			BIT(24)

#define SEC_BD_W1_A_ALG_M			GENMASK(28, 25)
#define SEC_BD_W1_A_ALG_S			25
#define   SEC_A_ALG_SHA1			0
#define   SEC_A_ALG_SHA256			1
#define   SEC_A_ALG_MD5				2
#define   SEC_A_ALG_SHA224			3
#define   SEC_A_ALG_HMAC_SHA1			8
#define   SEC_A_ALG_HMAC_SHA224			10
#define   SEC_A_ALG_HMAC_SHA256			11
#define   SEC_A_ALG_HMAC_MD5			12
#define   SEC_A_ALG_AES_XCBC			13
#define   SEC_A_ALG_AES_CMAC			14

#define SEC_BD_W1_C_ALG_M			GENMASK(31, 29)
#define SEC_BD_W1_C_ALG_S			29
#define   SEC_C_ALG_DES				0
#define   SEC_C_ALG_3DES			1
#define   SEC_C_ALG_AES				2

	u32 w1;

#define SEC_BD_W2_C_GRAN_SIZE_15_0_M		GENMASK(15, 0)
#define SEC_BD_W2_C_GRAN_SIZE_15_0_S		0
#define SEC_BD_W2_GRAN_NUM_M			GENMASK(31, 16)
#define SEC_BD_W2_GRAN_NUM_S			16
	u32 w2;

#define SEC_BD_W3_AUTH_LEN_OFFSET_M		GENMASK(9, 0)
#define SEC_BD_W3_AUTH_LEN_OFFSET_S		0
#define SEC_BD_W3_CIPHER_LEN_OFFSET_M		GENMASK(19, 10)
#define SEC_BD_W3_CIPHER_LEN_OFFSET_S		10
#define SEC_BD_W3_MAC_LEN_M			GENMASK(24, 20)
#define SEC_BD_W3_MAC_LEN_S			20
#define SEC_BD_W3_A_KEY_LEN_M			GENMASK(29, 25)
#define SEC_BD_W3_A_KEY_LEN_S			25
#define SEC_BD_W3_C_KEY_LEN_M			GENMASK(31, 30)
#define SEC_BD_W3_C_KEY_LEN_S			30
#define   SEC_KEY_LEN_AES_128			0
#define   SEC_KEY_LEN_AES_192			1
#define   SEC_KEY_LEN_AES_256			2
#define   SEC_KEY_LEN_DES			1
#define   SEC_KEY_LEN_3DES_3_KEY		1
#define   SEC_KEY_LEN_3DES_2_KEY		3
	u32 w3;

	/* W4,5 */
	union {
		u32 authkey_addr_lo;
		u32 authiv_addr_lo;
	};
	union {
		u32 authkey_addr_hi;
		u32 authiv_addr_hi;
	};

	/* W6,7 */
	u32 cipher_key_addr_lo;
	u32 cipher_key_addr_hi;

	/* W8,9 */
	u32 cipher_iv_addr_lo;
	u32 cipher_iv_addr_hi;

	/* W10,11 */
	u32 data_addr_lo;
	u32 data_addr_hi;

	/* W12,13 */
	u32 mac_addr_lo;
	u32 mac_addr_hi;

	/* W14,15 */
	u32 cipher_destin_addr_lo;
	u32 cipher_destin_addr_hi;
};

enum sec_mem_region {
	SEC_COMMON = 0,
	SEC_SAA,
	SEC_NUM_ADDR_REGIONS
};

#define SEC_NAME_SIZE				64
#define SEC_Q_NUM				16


/**
 * struct sec_queue_ring_cmd - store information about a SEC HW cmd ring
 * @used: Local counter used to cheaply establish if the ring is empty.
 * @lock: Protect against simultaneous adjusting of the read and write pointers.
 * @vaddr: Virtual address for the ram pages used for the ring.
 * @paddr: Physical address of the dma mapped region of ram used for the ring.
 * @callback: Callback function called on a ring element completing.
 */
struct sec_queue_ring_cmd {
	atomic_t used;
	struct mutex lock;
	struct sec_bd_info *vaddr;
	dma_addr_t paddr;
	void (*callback)(struct sec_bd_info *resp, void *ctx);
};

struct sec_debug_bd_info;
struct sec_queue_ring_db {
	struct sec_debug_bd_info *vaddr;
	dma_addr_t paddr;
};

struct sec_out_bd_info;
struct sec_queue_ring_cq {
	struct sec_out_bd_info *vaddr;
	dma_addr_t paddr;
};

struct sec_dev_info;

enum sec_cipher_alg {
	SEC_C_DES_ECB_64,
	SEC_C_DES_CBC_64,

	SEC_C_3DES_ECB_192_3KEY,
	SEC_C_3DES_ECB_192_2KEY,

	SEC_C_3DES_CBC_192_3KEY,
	SEC_C_3DES_CBC_192_2KEY,

	SEC_C_AES_ECB_128,
	SEC_C_AES_ECB_192,
	SEC_C_AES_ECB_256,

	SEC_C_AES_CBC_128,
	SEC_C_AES_CBC_192,
	SEC_C_AES_CBC_256,

	SEC_C_AES_CTR_128,
	SEC_C_AES_CTR_192,
	SEC_C_AES_CTR_256,

	SEC_C_AES_XTS_128,
	SEC_C_AES_XTS_256,

	SEC_C_NULL,
};

/**
 * struct sec_alg_tfm_ctx - hardware specific tranformation context
 * @cipher_alg: Cipher algorithm enabled include encryption mode.
 * @key: Key storage if required.
 * @pkey: DMA address for the key storage.
 * @req_template: Request template to save time on setup.
 * @queue: The hardware queue associated with this tfm context.
 * @lock: Protect key and pkey to ensure they are consistent
 * @auth_buf: Current context buffer for auth operations.
 * @backlog: The backlog queue used for cases where our buffers aren't
 * large enough.
 */
struct sec_alg_tfm_ctx {
	enum sec_cipher_alg cipher_alg;
	u8 *key;
	dma_addr_t pkey;
	struct sec_bd_info req_template;
	struct sec_queue *queue;
	struct mutex lock;
	u8 *auth_buf;
	struct list_head backlog;
};

/**
 * struct sec_request - data associate with a single crypto request
 * @elements: List of subparts of this request (hardware size restriction)
 * @num_elements: The number of subparts (used as an optimization)
 * @lock: Protect elements of this structure against concurrent change.
 * @tfm_ctx: hardware specific context.
 * @len_in: length of in sgl from upper layers
 * @len_out: length of out sgl from upper layers
 * @dma_iv: initialization vector - phsyical address
 * @err: store used to track errors across subelements of this request.
 * @req_base: pointer to base element of associate crypto context.
 * This is needed to allow shared handling skcipher, ahash etc.
 * @cb: completion callback.
 * @backlog_head: list head to allow backlog maintenance.
 *
 * The hardware is limited in the maximum size of data that it can
 * process from a single BD.  Typically this is fairly large (32MB)
 * but still requires the complexity of splitting the incoming
 * skreq up into a number of elements complete with appropriate
 * iv chaining.
 */
struct sec_request {
	struct list_head elements;
	int num_elements;
	struct mutex lock;
	struct sec_alg_tfm_ctx *tfm_ctx;
	int len_in;
	int len_out;
	dma_addr_t dma_iv;
	int err;
	struct crypto_async_request *req_base;
	void (*cb)(struct sec_bd_info *resp, struct crypto_async_request *req);
	struct list_head backlog_head;
};

/**
 * struct sec_request_el - A subpart of a request.
 * @head: allow us to attach this to the list in the sec_request
 * @req: hardware block descriptor corresponding to this request subpart
 * @in: hardware sgl for input - virtual address
 * @dma_in: hardware sgl for input - physical address
 * @sgl_in: scatterlist for this request subpart
 * @out: hardware sgl for output - virtual address
 * @dma_out: hardware sgl for output - physical address
 * @sgl_out: scatterlist for this request subpart
 * @sec_req: The request which this subpart forms a part of
 * @el_length: Number of bytes in this subpart. Needed to locate
 * last ivsize chunk for iv chaining.
 */
struct sec_request_el {
	struct list_head head;
	struct sec_bd_info req;
	struct sec_hw_sgl *in;
	dma_addr_t dma_in;
	struct scatterlist *sgl_in;
	struct sec_hw_sgl *out;
	dma_addr_t dma_out;
	struct scatterlist *sgl_out;
	struct sec_request *sec_req;
	size_t el_length;
};

/**
 * struct sec_queue - All the information about a HW queue
 * @dev_info: The parent SEC device to which this queue belongs.
 * @task_irq: Completion interrupt for the queue.
 * @name: Human readable queue description also used as irq name.
 * @ring: The several HW rings associated with one queue.
 * @regs: The iomapped device registers
 * @queue_id: Index of the queue used for naming and resource selection.
 * @in_use: Flag to say if the queue is in use.
 * @expected: The next expected element to finish assuming we were in order.
 * @uprocessed: A bitmap to track which OoO elements are done but not handled.
 * @softqueue: A software queue used when chaining requirements prevent direct
 *   use of the hardware queues.
 * @havesoftqueue: A flag to say we have a queues - as we may need one for the
 *   current mode.
 * @queuelock: Protect the soft queue from concurrent changes to avoid some
 *   potential loss of data races.
 * @shadow: Pointers back to the shadow copy of the hardware ring element
 *   need because we can't store any context reference in the bd element.
 */
struct sec_queue {
	struct sec_dev_info *dev_info;
	int task_irq;
	char name[SEC_NAME_SIZE];
	struct sec_queue_ring_cmd ring_cmd;
	struct sec_queue_ring_cq ring_cq;
	struct sec_queue_ring_db ring_db;
	void __iomem *regs;
	u32 queue_id;
	bool in_use;
	int expected;

	DECLARE_BITMAP(unprocessed, SEC_QUEUE_LEN);
	DECLARE_KFIFO_PTR(softqueue, typeof(struct sec_request_el *));
	bool havesoftqueue;
	spinlock_t queuelock;
	void *shadow[SEC_QUEUE_LEN];
};

/**
 * struct sec_hw_sge: Track each of the 64 element SEC HW SGL entries
 * @buf: The IOV dma address for this entry.
 * @len: Length of this IOV.
 * @pad: Reserved space.
 */
struct sec_hw_sge {
	dma_addr_t buf;
	unsigned int len;
	unsigned int pad;
};

/**
 * struct sec_hw_sgl: One hardware SGL entry.
 * @next_sgl: The next entry if we need to chain dma address. Null if last.
 * @entry_sum_in_chain: The full count of SGEs - only matters for first SGL.
 * @entry_sum_in_sgl: The number of SGEs in this SGL element.
 * @flag: Unused in skciphers.
 * @serial_num: Unsued in skciphers.
 * @cpuid: Currently unused.
 * @data_bytes_in_sgl: Count of bytes from all SGEs in this SGL.
 * @next: Virtual address used to stash the next sgl - useful in completion.
 * @reserved: A reserved field not currently used.
 * @sge_entries: The (up to) 64 Scatter Gather Entries, representing IOVs.
 * @node: Currently unused.
 */
struct sec_hw_sgl {
	dma_addr_t next_sgl;
	u16 entry_sum_in_chain;
	u16 entry_sum_in_sgl;
	u32 flag;
	u64 serial_num;
	u32 cpuid;
	u32 data_bytes_in_sgl;
	struct sec_hw_sgl *next;
	u64 reserved;
	struct sec_hw_sge  sge_entries[SEC_MAX_SGE_NUM];
	u8 node[16];
};

struct dma_pool;

/**
 * struct sec_dev_info: The full SEC unit comprising queues and processors.
 * @sec_id: Index used to track which SEC this is when more than one is present.
 * @num_saas: The number of backed processors enabled.
 * @regs: iomapped register regions shared by whole SEC unit.
 * @dev_lock: Protects concurrent queue allocation / freeing for the SEC.
 * @queues: The 16 queues that this SEC instance provides.
 * @dev: Device pointer.
 * @hw_sgl_pool: DMA pool used to mimise mapping for the scatter gather lists.
 */
struct sec_dev_info {
	int sec_id;
	int num_saas;
	void __iomem *regs[SEC_NUM_ADDR_REGIONS];
	struct mutex dev_lock;
	int queues_in_use;
	struct sec_queue queues[SEC_Q_NUM];
	struct device *dev;
	struct dma_pool *hw_sgl_pool;
};

int sec_queue_send(struct sec_queue *queue, struct sec_bd_info *msg, void *ctx);
bool sec_queue_can_enqueue(struct sec_queue *queue, int num);
int sec_queue_stop_release(struct sec_queue *queue);
struct sec_queue *sec_queue_alloc_start_safe(void);
bool sec_queue_empty(struct sec_queue *queue);

/* Algorithm specific elements from sec_algs.c */
void sec_alg_callback(struct sec_bd_info *resp, void *ctx);
int sec_algs_register(void);
void sec_algs_unregister(void);

#endif /* _SEC_DRV_H_ */
