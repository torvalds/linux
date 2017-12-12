/*
 *   Driver for ARTPEC-6 crypto block using the kernel asynchronous crypto api.
 *
 *    Copyright (C) 2014-2017  Axis Communications AB
 */
#define pr_fmt(fmt)     KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/crypto.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fault-inject.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include <crypto/aes.h>
#include <crypto/gcm.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha.h>
#include <crypto/xts.h>

/* Max length of a line in all cache levels for Artpec SoCs. */
#define ARTPEC_CACHE_LINE_MAX	32

#define PDMA_OUT_CFG		0x0000
#define PDMA_OUT_BUF_CFG	0x0004
#define PDMA_OUT_CMD		0x0008
#define PDMA_OUT_DESCRQ_PUSH	0x0010
#define PDMA_OUT_DESCRQ_STAT	0x0014

#define A6_PDMA_IN_CFG		0x0028
#define A6_PDMA_IN_BUF_CFG	0x002c
#define A6_PDMA_IN_CMD		0x0030
#define A6_PDMA_IN_STATQ_PUSH	0x0038
#define A6_PDMA_IN_DESCRQ_PUSH	0x0044
#define A6_PDMA_IN_DESCRQ_STAT	0x0048
#define A6_PDMA_INTR_MASK	0x0068
#define A6_PDMA_ACK_INTR	0x006c
#define A6_PDMA_MASKED_INTR	0x0074

#define A7_PDMA_IN_CFG		0x002c
#define A7_PDMA_IN_BUF_CFG	0x0030
#define A7_PDMA_IN_CMD		0x0034
#define A7_PDMA_IN_STATQ_PUSH	0x003c
#define A7_PDMA_IN_DESCRQ_PUSH	0x0048
#define A7_PDMA_IN_DESCRQ_STAT	0x004C
#define A7_PDMA_INTR_MASK	0x006c
#define A7_PDMA_ACK_INTR	0x0070
#define A7_PDMA_MASKED_INTR	0x0078

#define PDMA_OUT_CFG_EN				BIT(0)

#define PDMA_OUT_BUF_CFG_DATA_BUF_SIZE		GENMASK(4, 0)
#define PDMA_OUT_BUF_CFG_DESCR_BUF_SIZE		GENMASK(9, 5)

#define PDMA_OUT_CMD_START			BIT(0)
#define A6_PDMA_OUT_CMD_STOP			BIT(3)
#define A7_PDMA_OUT_CMD_STOP			BIT(2)

#define PDMA_OUT_DESCRQ_PUSH_LEN		GENMASK(5, 0)
#define PDMA_OUT_DESCRQ_PUSH_ADDR		GENMASK(31, 6)

#define PDMA_OUT_DESCRQ_STAT_LEVEL		GENMASK(3, 0)
#define PDMA_OUT_DESCRQ_STAT_SIZE		GENMASK(7, 4)

#define PDMA_IN_CFG_EN				BIT(0)

#define PDMA_IN_BUF_CFG_DATA_BUF_SIZE		GENMASK(4, 0)
#define PDMA_IN_BUF_CFG_DESCR_BUF_SIZE		GENMASK(9, 5)
#define PDMA_IN_BUF_CFG_STAT_BUF_SIZE		GENMASK(14, 10)

#define PDMA_IN_CMD_START			BIT(0)
#define A6_PDMA_IN_CMD_FLUSH_STAT		BIT(2)
#define A6_PDMA_IN_CMD_STOP			BIT(3)
#define A7_PDMA_IN_CMD_FLUSH_STAT		BIT(1)
#define A7_PDMA_IN_CMD_STOP			BIT(2)

#define PDMA_IN_STATQ_PUSH_LEN			GENMASK(5, 0)
#define PDMA_IN_STATQ_PUSH_ADDR			GENMASK(31, 6)

#define PDMA_IN_DESCRQ_PUSH_LEN			GENMASK(5, 0)
#define PDMA_IN_DESCRQ_PUSH_ADDR		GENMASK(31, 6)

#define PDMA_IN_DESCRQ_STAT_LEVEL		GENMASK(3, 0)
#define PDMA_IN_DESCRQ_STAT_SIZE		GENMASK(7, 4)

#define A6_PDMA_INTR_MASK_IN_DATA		BIT(2)
#define A6_PDMA_INTR_MASK_IN_EOP		BIT(3)
#define A6_PDMA_INTR_MASK_IN_EOP_FLUSH		BIT(4)

#define A7_PDMA_INTR_MASK_IN_DATA		BIT(3)
#define A7_PDMA_INTR_MASK_IN_EOP		BIT(4)
#define A7_PDMA_INTR_MASK_IN_EOP_FLUSH		BIT(5)

#define A6_CRY_MD_OPER		GENMASK(19, 16)

#define A6_CRY_MD_HASH_SEL_CTX	GENMASK(21, 20)
#define A6_CRY_MD_HASH_HMAC_FIN	BIT(23)

#define A6_CRY_MD_CIPHER_LEN	GENMASK(21, 20)
#define A6_CRY_MD_CIPHER_DECR	BIT(22)
#define A6_CRY_MD_CIPHER_TWEAK	BIT(23)
#define A6_CRY_MD_CIPHER_DSEQ	BIT(24)

#define A7_CRY_MD_OPER		GENMASK(11, 8)

#define A7_CRY_MD_HASH_SEL_CTX	GENMASK(13, 12)
#define A7_CRY_MD_HASH_HMAC_FIN	BIT(15)

#define A7_CRY_MD_CIPHER_LEN	GENMASK(13, 12)
#define A7_CRY_MD_CIPHER_DECR	BIT(14)
#define A7_CRY_MD_CIPHER_TWEAK	BIT(15)
#define A7_CRY_MD_CIPHER_DSEQ	BIT(16)

/* DMA metadata constants */
#define regk_crypto_aes_cbc     0x00000002
#define regk_crypto_aes_ctr     0x00000003
#define regk_crypto_aes_ecb     0x00000001
#define regk_crypto_aes_gcm     0x00000004
#define regk_crypto_aes_xts     0x00000005
#define regk_crypto_cache       0x00000002
#define a6_regk_crypto_dlkey    0x0000000a
#define a7_regk_crypto_dlkey    0x0000000e
#define regk_crypto_ext         0x00000001
#define regk_crypto_hmac_sha1   0x00000007
#define regk_crypto_hmac_sha256 0x00000009
#define regk_crypto_hmac_sha384 0x0000000b
#define regk_crypto_hmac_sha512 0x0000000d
#define regk_crypto_init        0x00000000
#define regk_crypto_key_128     0x00000000
#define regk_crypto_key_192     0x00000001
#define regk_crypto_key_256     0x00000002
#define regk_crypto_null        0x00000000
#define regk_crypto_sha1        0x00000006
#define regk_crypto_sha256      0x00000008
#define regk_crypto_sha384      0x0000000a
#define regk_crypto_sha512      0x0000000c

/* DMA descriptor structures */
struct pdma_descr_ctrl  {
	unsigned char short_descr : 1;
	unsigned char pad1        : 1;
	unsigned char eop         : 1;
	unsigned char intr        : 1;
	unsigned char short_len   : 3;
	unsigned char pad2        : 1;
} __packed;

struct pdma_data_descr {
	unsigned int len : 24;
	unsigned int buf : 32;
} __packed;

struct pdma_short_descr {
	unsigned char data[7];
} __packed;

struct pdma_descr {
	struct pdma_descr_ctrl ctrl;
	union {
		struct pdma_data_descr   data;
		struct pdma_short_descr  shrt;
	};
};

struct pdma_stat_descr {
	unsigned char pad1        : 1;
	unsigned char pad2        : 1;
	unsigned char eop         : 1;
	unsigned char pad3        : 5;
	unsigned int  len         : 24;
};

/* Each descriptor array can hold max 64 entries */
#define PDMA_DESCR_COUNT	64

#define MODULE_NAME   "Artpec-6 CA"

/* Hash modes (including HMAC variants) */
#define ARTPEC6_CRYPTO_HASH_SHA1	1
#define ARTPEC6_CRYPTO_HASH_SHA256	2
#define ARTPEC6_CRYPTO_HASH_SHA384	3
#define ARTPEC6_CRYPTO_HASH_SHA512	4

/* Crypto modes */
#define ARTPEC6_CRYPTO_CIPHER_AES_ECB	1
#define ARTPEC6_CRYPTO_CIPHER_AES_CBC	2
#define ARTPEC6_CRYPTO_CIPHER_AES_CTR	3
#define ARTPEC6_CRYPTO_CIPHER_AES_XTS	5

/* The PDMA is a DMA-engine tightly coupled with a ciphering engine.
 * It operates on a descriptor array with up to 64 descriptor entries.
 * The arrays must be 64 byte aligned in memory.
 *
 * The ciphering unit has no registers and is completely controlled by
 * a 4-byte metadata that is inserted at the beginning of each dma packet.
 *
 * A dma packet is a sequence of descriptors terminated by setting the .eop
 * field in the final descriptor of the packet.
 *
 * Multiple packets are used for providing context data, key data and
 * the plain/ciphertext.
 *
 *   PDMA Descriptors (Array)
 *  +------+------+------+~~+-------+------+----
 *  |  0   |  1   |  2   |~~| 11 EOP|  12  |  ....
 *  +--+---+--+---+----+-+~~+-------+----+-+----
 *     |      |        |       |         |
 *     |      |        |       |         |
 *   __|__  +-------++-------++-------+ +----+
 *  | MD  | |Payload||Payload||Payload| | MD |
 *  +-----+ +-------++-------++-------+ +----+
 */

struct artpec6_crypto_bounce_buffer {
	struct list_head list;
	size_t length;
	struct scatterlist *sg;
	size_t offset;
	/* buf is aligned to ARTPEC_CACHE_LINE_MAX and
	 * holds up to ARTPEC_CACHE_LINE_MAX bytes data.
	 */
	void *buf;
};

struct artpec6_crypto_dma_map {
	dma_addr_t dma_addr;
	size_t size;
	enum dma_data_direction dir;
};

struct artpec6_crypto_dma_descriptors {
	struct pdma_descr out[PDMA_DESCR_COUNT] __aligned(64);
	struct pdma_descr in[PDMA_DESCR_COUNT] __aligned(64);
	u32 stat[PDMA_DESCR_COUNT] __aligned(64);
	struct list_head bounce_buffers;
	/* Enough maps for all out/in buffers, and all three descr. arrays */
	struct artpec6_crypto_dma_map maps[PDMA_DESCR_COUNT * 2 + 2];
	dma_addr_t out_dma_addr;
	dma_addr_t in_dma_addr;
	dma_addr_t stat_dma_addr;
	size_t out_cnt;
	size_t in_cnt;
	size_t map_count;
};

enum artpec6_crypto_variant {
	ARTPEC6_CRYPTO,
	ARTPEC7_CRYPTO,
};

struct artpec6_crypto {
	void __iomem *base;
	spinlock_t queue_lock;
	struct list_head queue; /* waiting for pdma fifo space */
	struct list_head pending; /* submitted to pdma fifo */
	struct tasklet_struct task;
	struct kmem_cache *dma_cache;
	int pending_count;
	struct timer_list timer;
	enum artpec6_crypto_variant variant;
	void *pad_buffer; /* cache-aligned block padding buffer */
	void *zero_buffer;
};

enum artpec6_crypto_hash_flags {
	HASH_FLAG_INIT_CTX = 2,
	HASH_FLAG_UPDATE = 4,
	HASH_FLAG_FINALIZE = 8,
	HASH_FLAG_HMAC = 16,
	HASH_FLAG_UPDATE_KEY = 32,
};

struct artpec6_crypto_req_common {
	struct list_head list;
	struct artpec6_crypto_dma_descriptors *dma;
	struct crypto_async_request *req;
	void (*complete)(struct crypto_async_request *req);
	gfp_t gfp_flags;
};

struct artpec6_hash_request_context {
	char partial_buffer[SHA512_BLOCK_SIZE];
	char partial_buffer_out[SHA512_BLOCK_SIZE];
	char key_buffer[SHA512_BLOCK_SIZE];
	char pad_buffer[SHA512_BLOCK_SIZE + 32];
	unsigned char digeststate[SHA512_DIGEST_SIZE];
	size_t partial_bytes;
	u64 digcnt;
	u32 key_md;
	u32 hash_md;
	enum artpec6_crypto_hash_flags hash_flags;
	struct artpec6_crypto_req_common common;
};

struct artpec6_hash_export_state {
	char partial_buffer[SHA512_BLOCK_SIZE];
	unsigned char digeststate[SHA512_DIGEST_SIZE];
	size_t partial_bytes;
	u64 digcnt;
	int oper;
	unsigned int hash_flags;
};

struct artpec6_hashalg_context {
	char hmac_key[SHA512_BLOCK_SIZE];
	size_t hmac_key_length;
	struct crypto_shash *child_hash;
};

struct artpec6_crypto_request_context {
	u32 cipher_md;
	bool decrypt;
	struct artpec6_crypto_req_common common;
};

struct artpec6_cryptotfm_context {
	unsigned char aes_key[2*AES_MAX_KEY_SIZE];
	size_t key_length;
	u32 key_md;
	int crypto_type;
	struct crypto_skcipher *fallback;
};

struct artpec6_crypto_aead_hw_ctx {
	__be64	aad_length_bits;
	__be64  text_length_bits;
	__u8	J0[AES_BLOCK_SIZE];
};

struct artpec6_crypto_aead_req_ctx {
	struct artpec6_crypto_aead_hw_ctx hw_ctx;
	u32 cipher_md;
	bool decrypt;
	struct artpec6_crypto_req_common common;
	__u8 decryption_tag[AES_BLOCK_SIZE] ____cacheline_aligned;
};

/* The crypto framework makes it hard to avoid this global. */
static struct device *artpec6_crypto_dev;

#ifdef CONFIG_FAULT_INJECTION
static DECLARE_FAULT_ATTR(artpec6_crypto_fail_status_read);
static DECLARE_FAULT_ATTR(artpec6_crypto_fail_dma_array_full);
#endif

enum {
	ARTPEC6_CRYPTO_PREPARE_HASH_NO_START,
	ARTPEC6_CRYPTO_PREPARE_HASH_START,
};

static int artpec6_crypto_prepare_aead(struct aead_request *areq);
static int artpec6_crypto_prepare_crypto(struct skcipher_request *areq);
static int artpec6_crypto_prepare_hash(struct ahash_request *areq);

static void
artpec6_crypto_complete_crypto(struct crypto_async_request *req);
static void
artpec6_crypto_complete_cbc_encrypt(struct crypto_async_request *req);
static void
artpec6_crypto_complete_cbc_decrypt(struct crypto_async_request *req);
static void
artpec6_crypto_complete_aead(struct crypto_async_request *req);
static void
artpec6_crypto_complete_hash(struct crypto_async_request *req);

static int
artpec6_crypto_common_destroy(struct artpec6_crypto_req_common *common);

static void
artpec6_crypto_start_dma(struct artpec6_crypto_req_common *common);

struct artpec6_crypto_walk {
	struct scatterlist *sg;
	size_t offset;
};

static void artpec6_crypto_walk_init(struct artpec6_crypto_walk *awalk,
				     struct scatterlist *sg)
{
	awalk->sg = sg;
	awalk->offset = 0;
}

static size_t artpec6_crypto_walk_advance(struct artpec6_crypto_walk *awalk,
					  size_t nbytes)
{
	while (nbytes && awalk->sg) {
		size_t piece;

		WARN_ON(awalk->offset > awalk->sg->length);

		piece = min(nbytes, (size_t)awalk->sg->length - awalk->offset);
		nbytes -= piece;
		awalk->offset += piece;
		if (awalk->offset == awalk->sg->length) {
			awalk->sg = sg_next(awalk->sg);
			awalk->offset = 0;
		}

	}

	return nbytes;
}

static size_t
artpec6_crypto_walk_chunklen(const struct artpec6_crypto_walk *awalk)
{
	WARN_ON(awalk->sg->length == awalk->offset);

	return awalk->sg->length - awalk->offset;
}

static dma_addr_t
artpec6_crypto_walk_chunk_phys(const struct artpec6_crypto_walk *awalk)
{
	return sg_phys(awalk->sg) + awalk->offset;
}

static void
artpec6_crypto_copy_bounce_buffers(struct artpec6_crypto_req_common *common)
{
	struct artpec6_crypto_dma_descriptors *dma = common->dma;
	struct artpec6_crypto_bounce_buffer *b;
	struct artpec6_crypto_bounce_buffer *next;

	list_for_each_entry_safe(b, next, &dma->bounce_buffers, list) {
		pr_debug("bounce entry %p: %zu bytes @ %zu from %p\n",
			 b, b->length, b->offset, b->buf);
		sg_pcopy_from_buffer(b->sg,
				   1,
				   b->buf,
				   b->length,
				   b->offset);

		list_del(&b->list);
		kfree(b);
	}
}

static inline bool artpec6_crypto_busy(void)
{
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);
	int fifo_count = ac->pending_count;

	return fifo_count > 6;
}

static int artpec6_crypto_submit(struct artpec6_crypto_req_common *req)
{
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);
	int ret = -EBUSY;

	spin_lock_bh(&ac->queue_lock);

	if (!artpec6_crypto_busy()) {
		list_add_tail(&req->list, &ac->pending);
		artpec6_crypto_start_dma(req);
		ret = -EINPROGRESS;
	} else if (req->req->flags & CRYPTO_TFM_REQ_MAY_BACKLOG) {
		list_add_tail(&req->list, &ac->queue);
	} else {
		artpec6_crypto_common_destroy(req);
	}

	spin_unlock_bh(&ac->queue_lock);

	return ret;
}

static void artpec6_crypto_start_dma(struct artpec6_crypto_req_common *common)
{
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);
	enum artpec6_crypto_variant variant = ac->variant;
	void __iomem *base = ac->base;
	struct artpec6_crypto_dma_descriptors *dma = common->dma;
	u32 ind, statd, outd;

	/* Make descriptor content visible to the DMA before starting it. */
	wmb();

	ind = FIELD_PREP(PDMA_IN_DESCRQ_PUSH_LEN, dma->in_cnt - 1) |
	      FIELD_PREP(PDMA_IN_DESCRQ_PUSH_ADDR, dma->in_dma_addr >> 6);

	statd = FIELD_PREP(PDMA_IN_STATQ_PUSH_LEN, dma->in_cnt - 1) |
		FIELD_PREP(PDMA_IN_STATQ_PUSH_ADDR, dma->stat_dma_addr >> 6);

	outd = FIELD_PREP(PDMA_OUT_DESCRQ_PUSH_LEN, dma->out_cnt - 1) |
	       FIELD_PREP(PDMA_OUT_DESCRQ_PUSH_ADDR, dma->out_dma_addr >> 6);

	if (variant == ARTPEC6_CRYPTO) {
		writel_relaxed(ind, base + A6_PDMA_IN_DESCRQ_PUSH);
		writel_relaxed(statd, base + A6_PDMA_IN_STATQ_PUSH);
		writel_relaxed(PDMA_IN_CMD_START, base + A6_PDMA_IN_CMD);
	} else {
		writel_relaxed(ind, base + A7_PDMA_IN_DESCRQ_PUSH);
		writel_relaxed(statd, base + A7_PDMA_IN_STATQ_PUSH);
		writel_relaxed(PDMA_IN_CMD_START, base + A7_PDMA_IN_CMD);
	}

	writel_relaxed(outd, base + PDMA_OUT_DESCRQ_PUSH);
	writel_relaxed(PDMA_OUT_CMD_START, base + PDMA_OUT_CMD);

	ac->pending_count++;
}

static void
artpec6_crypto_init_dma_operation(struct artpec6_crypto_req_common *common)
{
	struct artpec6_crypto_dma_descriptors *dma = common->dma;

	dma->out_cnt = 0;
	dma->in_cnt = 0;
	dma->map_count = 0;
	INIT_LIST_HEAD(&dma->bounce_buffers);
}

static bool fault_inject_dma_descr(void)
{
#ifdef CONFIG_FAULT_INJECTION
	return should_fail(&artpec6_crypto_fail_dma_array_full, 1);
#else
	return false;
#endif
}

/** artpec6_crypto_setup_out_descr_phys - Setup an out channel with a
 *                                        physical address
 *
 * @addr: The physical address of the data buffer
 * @len:  The length of the data buffer
 * @eop:  True if this is the last buffer in the packet
 *
 * @return 0 on success or -ENOSPC if there are no more descriptors available
 */
static int
artpec6_crypto_setup_out_descr_phys(struct artpec6_crypto_req_common *common,
				    dma_addr_t addr, size_t len, bool eop)
{
	struct artpec6_crypto_dma_descriptors *dma = common->dma;
	struct pdma_descr *d;

	if (dma->out_cnt >= PDMA_DESCR_COUNT ||
	    fault_inject_dma_descr()) {
		pr_err("No free OUT DMA descriptors available!\n");
		return -ENOSPC;
	}

	d = &dma->out[dma->out_cnt++];
	memset(d, 0, sizeof(*d));

	d->ctrl.short_descr = 0;
	d->ctrl.eop = eop;
	d->data.len = len;
	d->data.buf = addr;
	return 0;
}

/** artpec6_crypto_setup_out_descr_short - Setup a short out descriptor
 *
 * @dst: The virtual address of the data
 * @len: The length of the data, must be between 1 to 7 bytes
 * @eop: True if this is the last buffer in the packet
 *
 * @return 0 on success
 *	-ENOSPC if no more descriptors are available
 *	-EINVAL if the data length exceeds 7 bytes
 */
static int
artpec6_crypto_setup_out_descr_short(struct artpec6_crypto_req_common *common,
				     void *dst, unsigned int len, bool eop)
{
	struct artpec6_crypto_dma_descriptors *dma = common->dma;
	struct pdma_descr *d;

	if (dma->out_cnt >= PDMA_DESCR_COUNT ||
	    fault_inject_dma_descr()) {
		pr_err("No free OUT DMA descriptors available!\n");
		return -ENOSPC;
	} else if (len > 7 || len < 1) {
		return -EINVAL;
	}
	d = &dma->out[dma->out_cnt++];
	memset(d, 0, sizeof(*d));

	d->ctrl.short_descr = 1;
	d->ctrl.short_len = len;
	d->ctrl.eop = eop;
	memcpy(d->shrt.data, dst, len);
	return 0;
}

static int artpec6_crypto_dma_map_page(struct artpec6_crypto_req_common *common,
				      struct page *page, size_t offset,
				      size_t size,
				      enum dma_data_direction dir,
				      dma_addr_t *dma_addr_out)
{
	struct artpec6_crypto_dma_descriptors *dma = common->dma;
	struct device *dev = artpec6_crypto_dev;
	struct artpec6_crypto_dma_map *map;
	dma_addr_t dma_addr;

	*dma_addr_out = 0;

	if (dma->map_count >= ARRAY_SIZE(dma->maps))
		return -ENOMEM;

	dma_addr = dma_map_page(dev, page, offset, size, dir);
	if (dma_mapping_error(dev, dma_addr))
		return -ENOMEM;

	map = &dma->maps[dma->map_count++];
	map->size = size;
	map->dma_addr = dma_addr;
	map->dir = dir;

	*dma_addr_out = dma_addr;

	return 0;
}

static int
artpec6_crypto_dma_map_single(struct artpec6_crypto_req_common *common,
			      void *ptr, size_t size,
			      enum dma_data_direction dir,
			      dma_addr_t *dma_addr_out)
{
	struct page *page = virt_to_page(ptr);
	size_t offset = (uintptr_t)ptr & ~PAGE_MASK;

	return artpec6_crypto_dma_map_page(common, page, offset, size, dir,
					  dma_addr_out);
}

static int
artpec6_crypto_dma_map_descs(struct artpec6_crypto_req_common *common)
{
	struct artpec6_crypto_dma_descriptors *dma = common->dma;
	int ret;

	ret = artpec6_crypto_dma_map_single(common, dma->in,
				sizeof(dma->in[0]) * dma->in_cnt,
				DMA_TO_DEVICE, &dma->in_dma_addr);
	if (ret)
		return ret;

	ret = artpec6_crypto_dma_map_single(common, dma->out,
				sizeof(dma->out[0]) * dma->out_cnt,
				DMA_TO_DEVICE, &dma->out_dma_addr);
	if (ret)
		return ret;

	/* We only read one stat descriptor */
	dma->stat[dma->in_cnt - 1] = 0;

	/*
	 * DMA_BIDIRECTIONAL since we need our zeroing of the stat descriptor
	 * to be written.
	 */
	return artpec6_crypto_dma_map_single(common,
				dma->stat + dma->in_cnt - 1,
				sizeof(dma->stat[0]),
				DMA_BIDIRECTIONAL,
				&dma->stat_dma_addr);
}

static void
artpec6_crypto_dma_unmap_all(struct artpec6_crypto_req_common *common)
{
	struct artpec6_crypto_dma_descriptors *dma = common->dma;
	struct device *dev = artpec6_crypto_dev;
	int i;

	for (i = 0; i < dma->map_count; i++) {
		struct artpec6_crypto_dma_map *map = &dma->maps[i];

		dma_unmap_page(dev, map->dma_addr, map->size, map->dir);
	}

	dma->map_count = 0;
}

/** artpec6_crypto_setup_out_descr - Setup an out descriptor
 *
 * @dst: The virtual address of the data
 * @len: The length of the data
 * @eop: True if this is the last buffer in the packet
 * @use_short: If this is true and the data length is 7 bytes or less then
 *	a short descriptor will be used
 *
 * @return 0 on success
 *	Any errors from artpec6_crypto_setup_out_descr_short() or
 *	setup_out_descr_phys()
 */
static int
artpec6_crypto_setup_out_descr(struct artpec6_crypto_req_common *common,
			       void *dst, unsigned int len, bool eop,
			       bool use_short)
{
	if (use_short && len < 7) {
		return artpec6_crypto_setup_out_descr_short(common, dst, len,
							    eop);
	} else {
		int ret;
		dma_addr_t dma_addr;

		ret = artpec6_crypto_dma_map_single(common, dst, len,
						   DMA_TO_DEVICE,
						   &dma_addr);
		if (ret)
			return ret;

		return artpec6_crypto_setup_out_descr_phys(common, dma_addr,
							   len, eop);
	}
}

/** artpec6_crypto_setup_in_descr_phys - Setup an in channel with a
 *                                       physical address
 *
 * @addr: The physical address of the data buffer
 * @len:  The length of the data buffer
 * @intr: True if an interrupt should be fired after HW processing of this
 *	  descriptor
 *
 */
static int
artpec6_crypto_setup_in_descr_phys(struct artpec6_crypto_req_common *common,
			       dma_addr_t addr, unsigned int len, bool intr)
{
	struct artpec6_crypto_dma_descriptors *dma = common->dma;
	struct pdma_descr *d;

	if (dma->in_cnt >= PDMA_DESCR_COUNT ||
	    fault_inject_dma_descr()) {
		pr_err("No free IN DMA descriptors available!\n");
		return -ENOSPC;
	}
	d = &dma->in[dma->in_cnt++];
	memset(d, 0, sizeof(*d));

	d->ctrl.intr = intr;
	d->data.len = len;
	d->data.buf = addr;
	return 0;
}

/** artpec6_crypto_setup_in_descr - Setup an in channel descriptor
 *
 * @buffer: The virtual address to of the data buffer
 * @len:    The length of the data buffer
 * @last:   If this is the last data buffer in the request (i.e. an interrupt
 *	    is needed
 *
 * Short descriptors are not used for the in channel
 */
static int
artpec6_crypto_setup_in_descr(struct artpec6_crypto_req_common *common,
			  void *buffer, unsigned int len, bool last)
{
	dma_addr_t dma_addr;
	int ret;

	ret = artpec6_crypto_dma_map_single(common, buffer, len,
					   DMA_FROM_DEVICE, &dma_addr);
	if (ret)
		return ret;

	return artpec6_crypto_setup_in_descr_phys(common, dma_addr, len, last);
}

static struct artpec6_crypto_bounce_buffer *
artpec6_crypto_alloc_bounce(gfp_t flags)
{
	void *base;
	size_t alloc_size = sizeof(struct artpec6_crypto_bounce_buffer) +
			    2 * ARTPEC_CACHE_LINE_MAX;
	struct artpec6_crypto_bounce_buffer *bbuf = kzalloc(alloc_size, flags);

	if (!bbuf)
		return NULL;

	base = bbuf + 1;
	bbuf->buf = PTR_ALIGN(base, ARTPEC_CACHE_LINE_MAX);
	return bbuf;
}

static int setup_bounce_buffer_in(struct artpec6_crypto_req_common *common,
				  struct artpec6_crypto_walk *walk, size_t size)
{
	struct artpec6_crypto_bounce_buffer *bbuf;
	int ret;

	bbuf = artpec6_crypto_alloc_bounce(common->gfp_flags);
	if (!bbuf)
		return -ENOMEM;

	bbuf->length = size;
	bbuf->sg = walk->sg;
	bbuf->offset = walk->offset;

	ret =  artpec6_crypto_setup_in_descr(common, bbuf->buf, size, false);
	if (ret) {
		kfree(bbuf);
		return ret;
	}

	pr_debug("BOUNCE %zu offset %zu\n", size, walk->offset);
	list_add_tail(&bbuf->list, &common->dma->bounce_buffers);
	return 0;
}

static int
artpec6_crypto_setup_sg_descrs_in(struct artpec6_crypto_req_common *common,
				  struct artpec6_crypto_walk *walk,
				  size_t count)
{
	size_t chunk;
	int ret;
	dma_addr_t addr;

	while (walk->sg && count) {
		chunk = min(count, artpec6_crypto_walk_chunklen(walk));
		addr = artpec6_crypto_walk_chunk_phys(walk);

		/* When destination buffers are not aligned to the cache line
		 * size we need bounce buffers. The DMA-API requires that the
		 * entire line is owned by the DMA buffer and this holds also
		 * for the case when coherent DMA is used.
		 */
		if (!IS_ALIGNED(addr, ARTPEC_CACHE_LINE_MAX)) {
			chunk = min_t(dma_addr_t, chunk,
				      ALIGN(addr, ARTPEC_CACHE_LINE_MAX) -
				      addr);

			pr_debug("CHUNK-b %pad:%zu\n", &addr, chunk);
			ret = setup_bounce_buffer_in(common, walk, chunk);
		} else if (chunk < ARTPEC_CACHE_LINE_MAX) {
			pr_debug("CHUNK-b %pad:%zu\n", &addr, chunk);
			ret = setup_bounce_buffer_in(common, walk, chunk);
		} else {
			dma_addr_t dma_addr;

			chunk = chunk & ~(ARTPEC_CACHE_LINE_MAX-1);

			pr_debug("CHUNK %pad:%zu\n", &addr, chunk);

			ret = artpec6_crypto_dma_map_page(common,
							 sg_page(walk->sg),
							 walk->sg->offset +
							 walk->offset,
							 chunk,
							 DMA_FROM_DEVICE,
							 &dma_addr);
			if (ret)
				return ret;

			ret = artpec6_crypto_setup_in_descr_phys(common,
								 dma_addr,
								 chunk, false);
		}

		if (ret)
			return ret;

		count = count - chunk;
		artpec6_crypto_walk_advance(walk, chunk);
	}

	if (count)
		pr_err("EOL unexpected %zu bytes left\n", count);

	return count ? -EINVAL : 0;
}

static int
artpec6_crypto_setup_sg_descrs_out(struct artpec6_crypto_req_common *common,
				   struct artpec6_crypto_walk *walk,
				   size_t count)
{
	size_t chunk;
	int ret;
	dma_addr_t addr;

	while (walk->sg && count) {
		chunk = min(count, artpec6_crypto_walk_chunklen(walk));
		addr = artpec6_crypto_walk_chunk_phys(walk);

		pr_debug("OUT-CHUNK %pad:%zu\n", &addr, chunk);

		if (addr & 3) {
			char buf[3];

			chunk = min_t(size_t, chunk, (4-(addr&3)));

			sg_pcopy_to_buffer(walk->sg, 1, buf, chunk,
					   walk->offset);

			ret = artpec6_crypto_setup_out_descr_short(common, buf,
								   chunk,
								   false);
		} else {
			dma_addr_t dma_addr;

			ret = artpec6_crypto_dma_map_page(common,
							 sg_page(walk->sg),
							 walk->sg->offset +
							 walk->offset,
							 chunk,
							 DMA_TO_DEVICE,
							 &dma_addr);
			if (ret)
				return ret;

			ret = artpec6_crypto_setup_out_descr_phys(common,
								 dma_addr,
								 chunk, false);
		}

		if (ret)
			return ret;

		count = count - chunk;
		artpec6_crypto_walk_advance(walk, chunk);
	}

	if (count)
		pr_err("EOL unexpected %zu bytes left\n", count);

	return count ? -EINVAL : 0;
}


/** artpec6_crypto_terminate_out_descrs - Set the EOP on the last out descriptor
 *
 * If the out descriptor list is non-empty, then the eop flag on the
 * last used out descriptor will be set.
 *
 * @return  0 on success
 *	-EINVAL if the out descriptor is empty or has overflown
 */
static int
artpec6_crypto_terminate_out_descrs(struct artpec6_crypto_req_common *common)
{
	struct artpec6_crypto_dma_descriptors *dma = common->dma;
	struct pdma_descr *d;

	if (!dma->out_cnt || dma->out_cnt > PDMA_DESCR_COUNT) {
		pr_err("%s: OUT descriptor list is %s\n",
			MODULE_NAME, dma->out_cnt ? "empty" : "full");
		return -EINVAL;

	}

	d = &dma->out[dma->out_cnt-1];
	d->ctrl.eop = 1;

	return 0;
}

/** artpec6_crypto_terminate_in_descrs - Set the interrupt flag on the last
 *                                       in descriptor
 *
 * See artpec6_crypto_terminate_out_descrs() for return values
 */
static int
artpec6_crypto_terminate_in_descrs(struct artpec6_crypto_req_common *common)
{
	struct artpec6_crypto_dma_descriptors *dma = common->dma;
	struct pdma_descr *d;

	if (!dma->in_cnt || dma->in_cnt > PDMA_DESCR_COUNT) {
		pr_err("%s: IN descriptor list is %s\n",
			MODULE_NAME, dma->in_cnt ? "empty" : "full");
		return -EINVAL;
	}

	d = &dma->in[dma->in_cnt-1];
	d->ctrl.intr = 1;
	return 0;
}

/** create_hash_pad - Create a Secure Hash conformant pad
 *
 * @dst:      The destination buffer to write the pad. Must be at least 64 bytes
 * @dgstlen:  The total length of the hash digest in bytes
 * @bitcount: The total length of the digest in bits
 *
 * @return The total number of padding bytes written to @dst
 */
static size_t
create_hash_pad(int oper, unsigned char *dst, u64 dgstlen, u64 bitcount)
{
	unsigned int mod, target, diff, pad_bytes, size_bytes;
	__be64 bits = __cpu_to_be64(bitcount);

	switch (oper) {
	case regk_crypto_sha1:
	case regk_crypto_sha256:
	case regk_crypto_hmac_sha1:
	case regk_crypto_hmac_sha256:
		target = 448 / 8;
		mod = 512 / 8;
		size_bytes = 8;
		break;
	default:
		target = 896 / 8;
		mod = 1024 / 8;
		size_bytes = 16;
		break;
	}

	target -= 1;
	diff = dgstlen & (mod - 1);
	pad_bytes = diff > target ? target + mod - diff : target - diff;

	memset(dst + 1, 0, pad_bytes);
	dst[0] = 0x80;

	if (size_bytes == 16) {
		memset(dst + 1 + pad_bytes, 0, 8);
		memcpy(dst + 1 + pad_bytes + 8, &bits, 8);
	} else {
		memcpy(dst + 1 + pad_bytes, &bits, 8);
	}

	return pad_bytes + size_bytes + 1;
}

static int artpec6_crypto_common_init(struct artpec6_crypto_req_common *common,
		struct crypto_async_request *parent,
		void (*complete)(struct crypto_async_request *req),
		struct scatterlist *dstsg, unsigned int nbytes)
{
	gfp_t flags;
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);

	flags = (parent->flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		 GFP_KERNEL : GFP_ATOMIC;

	common->gfp_flags = flags;
	common->dma = kmem_cache_alloc(ac->dma_cache, flags);
	if (!common->dma)
		return -ENOMEM;

	common->req = parent;
	common->complete = complete;
	return 0;
}

static void
artpec6_crypto_bounce_destroy(struct artpec6_crypto_dma_descriptors *dma)
{
	struct artpec6_crypto_bounce_buffer *b;
	struct artpec6_crypto_bounce_buffer *next;

	list_for_each_entry_safe(b, next, &dma->bounce_buffers, list) {
		kfree(b);
	}
}

static int
artpec6_crypto_common_destroy(struct artpec6_crypto_req_common *common)
{
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);

	artpec6_crypto_dma_unmap_all(common);
	artpec6_crypto_bounce_destroy(common->dma);
	kmem_cache_free(ac->dma_cache, common->dma);
	common->dma = NULL;
	return 0;
}

/*
 * Ciphering functions.
 */
static int artpec6_crypto_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct artpec6_cryptotfm_context *ctx = crypto_skcipher_ctx(cipher);
	struct artpec6_crypto_request_context *req_ctx = NULL;
	void (*complete)(struct crypto_async_request *req);
	int ret;

	req_ctx = skcipher_request_ctx(req);

	switch (ctx->crypto_type) {
	case ARTPEC6_CRYPTO_CIPHER_AES_CBC:
	case ARTPEC6_CRYPTO_CIPHER_AES_ECB:
	case ARTPEC6_CRYPTO_CIPHER_AES_XTS:
		req_ctx->decrypt = 0;
		break;
	default:
		break;
	}

	switch (ctx->crypto_type) {
	case ARTPEC6_CRYPTO_CIPHER_AES_CBC:
		complete = artpec6_crypto_complete_cbc_encrypt;
		break;
	default:
		complete = artpec6_crypto_complete_crypto;
		break;
	}

	ret = artpec6_crypto_common_init(&req_ctx->common,
				  &req->base,
				  complete,
				  req->dst, req->cryptlen);
	if (ret)
		return ret;

	ret = artpec6_crypto_prepare_crypto(req);
	if (ret) {
		artpec6_crypto_common_destroy(&req_ctx->common);
		return ret;
	}

	return artpec6_crypto_submit(&req_ctx->common);
}

static int artpec6_crypto_decrypt(struct skcipher_request *req)
{
	int ret;
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct artpec6_cryptotfm_context *ctx = crypto_skcipher_ctx(cipher);
	struct artpec6_crypto_request_context *req_ctx = NULL;
	void (*complete)(struct crypto_async_request *req);

	req_ctx = skcipher_request_ctx(req);

	switch (ctx->crypto_type) {
	case ARTPEC6_CRYPTO_CIPHER_AES_CBC:
	case ARTPEC6_CRYPTO_CIPHER_AES_ECB:
	case ARTPEC6_CRYPTO_CIPHER_AES_XTS:
		req_ctx->decrypt = 1;
		break;
	default:
		break;
	}


	switch (ctx->crypto_type) {
	case ARTPEC6_CRYPTO_CIPHER_AES_CBC:
		complete = artpec6_crypto_complete_cbc_decrypt;
		break;
	default:
		complete = artpec6_crypto_complete_crypto;
		break;
	}

	ret = artpec6_crypto_common_init(&req_ctx->common, &req->base,
				  complete,
				  req->dst, req->cryptlen);
	if (ret)
		return ret;

	ret = artpec6_crypto_prepare_crypto(req);
	if (ret) {
		artpec6_crypto_common_destroy(&req_ctx->common);
		return ret;
	}

	return artpec6_crypto_submit(&req_ctx->common);
}

static int
artpec6_crypto_ctr_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct artpec6_cryptotfm_context *ctx = crypto_skcipher_ctx(cipher);
	size_t iv_len = crypto_skcipher_ivsize(cipher);
	unsigned int counter = be32_to_cpup((__be32 *)
					    (req->iv + iv_len - 4));
	unsigned int nblks = ALIGN(req->cryptlen, AES_BLOCK_SIZE) /
			     AES_BLOCK_SIZE;

	/*
	 * The hardware uses only the last 32-bits as the counter while the
	 * kernel tests (aes_ctr_enc_tv_template[4] for example) expect that
	 * the whole IV is a counter.  So fallback if the counter is going to
	 * overlow.
	 */
	if (counter + nblks < counter) {
		int ret;

		pr_debug("counter %x will overflow (nblks %u), falling back\n",
			 counter, counter + nblks);

		ret = crypto_skcipher_setkey(ctx->fallback, ctx->aes_key,
					     ctx->key_length);
		if (ret)
			return ret;

		{
			SKCIPHER_REQUEST_ON_STACK(subreq, ctx->fallback);

			skcipher_request_set_tfm(subreq, ctx->fallback);
			skcipher_request_set_callback(subreq, req->base.flags,
						      NULL, NULL);
			skcipher_request_set_crypt(subreq, req->src, req->dst,
						   req->cryptlen, req->iv);
			ret = encrypt ? crypto_skcipher_encrypt(subreq)
				      : crypto_skcipher_decrypt(subreq);
			skcipher_request_zero(subreq);
		}
		return ret;
	}

	return encrypt ? artpec6_crypto_encrypt(req)
		       : artpec6_crypto_decrypt(req);
}

static int artpec6_crypto_ctr_encrypt(struct skcipher_request *req)
{
	return artpec6_crypto_ctr_crypt(req, true);
}

static int artpec6_crypto_ctr_decrypt(struct skcipher_request *req)
{
	return artpec6_crypto_ctr_crypt(req, false);
}

/*
 * AEAD functions
 */
static int artpec6_crypto_aead_init(struct crypto_aead *tfm)
{
	struct artpec6_cryptotfm_context *tfm_ctx = crypto_aead_ctx(tfm);

	memset(tfm_ctx, 0, sizeof(*tfm_ctx));

	crypto_aead_set_reqsize(tfm,
				sizeof(struct artpec6_crypto_aead_req_ctx));

	return 0;
}

static int artpec6_crypto_aead_set_key(struct crypto_aead *tfm, const u8 *key,
			       unsigned int len)
{
	struct artpec6_cryptotfm_context *ctx = crypto_tfm_ctx(&tfm->base);

	if (len != 16 && len != 24 && len != 32) {
		crypto_aead_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -1;
	}

	ctx->key_length = len;

	memcpy(ctx->aes_key, key, len);
	return 0;
}

static int artpec6_crypto_aead_encrypt(struct aead_request *req)
{
	int ret;
	struct artpec6_crypto_aead_req_ctx *req_ctx = aead_request_ctx(req);

	req_ctx->decrypt = false;
	ret = artpec6_crypto_common_init(&req_ctx->common, &req->base,
				  artpec6_crypto_complete_aead,
				  NULL, 0);
	if (ret)
		return ret;

	ret = artpec6_crypto_prepare_aead(req);
	if (ret) {
		artpec6_crypto_common_destroy(&req_ctx->common);
		return ret;
	}

	return artpec6_crypto_submit(&req_ctx->common);
}

static int artpec6_crypto_aead_decrypt(struct aead_request *req)
{
	int ret;
	struct artpec6_crypto_aead_req_ctx *req_ctx = aead_request_ctx(req);

	req_ctx->decrypt = true;
	if (req->cryptlen < AES_BLOCK_SIZE)
		return -EINVAL;

	ret = artpec6_crypto_common_init(&req_ctx->common,
				  &req->base,
				  artpec6_crypto_complete_aead,
				  NULL, 0);
	if (ret)
		return ret;

	ret = artpec6_crypto_prepare_aead(req);
	if (ret) {
		artpec6_crypto_common_destroy(&req_ctx->common);
		return ret;
	}

	return artpec6_crypto_submit(&req_ctx->common);
}

static int artpec6_crypto_prepare_hash(struct ahash_request *areq)
{
	struct artpec6_hashalg_context *ctx = crypto_tfm_ctx(areq->base.tfm);
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(areq);
	size_t digestsize = crypto_ahash_digestsize(crypto_ahash_reqtfm(areq));
	size_t contextsize = digestsize == SHA384_DIGEST_SIZE ?
		SHA512_DIGEST_SIZE : digestsize;
	size_t blocksize = crypto_tfm_alg_blocksize(
		crypto_ahash_tfm(crypto_ahash_reqtfm(areq)));
	struct artpec6_crypto_req_common *common = &req_ctx->common;
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);
	enum artpec6_crypto_variant variant = ac->variant;
	u32 sel_ctx;
	bool ext_ctx = false;
	bool run_hw = false;
	int error = 0;

	artpec6_crypto_init_dma_operation(common);

	/* Upload HMAC key, must be first the first packet */
	if (req_ctx->hash_flags & HASH_FLAG_HMAC) {
		if (variant == ARTPEC6_CRYPTO) {
			req_ctx->key_md = FIELD_PREP(A6_CRY_MD_OPER,
						     a6_regk_crypto_dlkey);
		} else {
			req_ctx->key_md = FIELD_PREP(A7_CRY_MD_OPER,
						     a7_regk_crypto_dlkey);
		}

		/* Copy and pad up the key */
		memcpy(req_ctx->key_buffer, ctx->hmac_key,
		       ctx->hmac_key_length);
		memset(req_ctx->key_buffer + ctx->hmac_key_length, 0,
		       blocksize - ctx->hmac_key_length);

		error = artpec6_crypto_setup_out_descr(common,
					(void *)&req_ctx->key_md,
					sizeof(req_ctx->key_md), false, false);
		if (error)
			return error;

		error = artpec6_crypto_setup_out_descr(common,
					req_ctx->key_buffer, blocksize,
					true, false);
		if (error)
			return error;
	}

	if (!(req_ctx->hash_flags & HASH_FLAG_INIT_CTX)) {
		/* Restore context */
		sel_ctx = regk_crypto_ext;
		ext_ctx = true;
	} else {
		sel_ctx = regk_crypto_init;
	}

	if (variant == ARTPEC6_CRYPTO) {
		req_ctx->hash_md &= ~A6_CRY_MD_HASH_SEL_CTX;
		req_ctx->hash_md |= FIELD_PREP(A6_CRY_MD_HASH_SEL_CTX, sel_ctx);

		/* If this is the final round, set the final flag */
		if (req_ctx->hash_flags & HASH_FLAG_FINALIZE)
			req_ctx->hash_md |= A6_CRY_MD_HASH_HMAC_FIN;
	} else {
		req_ctx->hash_md &= ~A7_CRY_MD_HASH_SEL_CTX;
		req_ctx->hash_md |= FIELD_PREP(A7_CRY_MD_HASH_SEL_CTX, sel_ctx);

		/* If this is the final round, set the final flag */
		if (req_ctx->hash_flags & HASH_FLAG_FINALIZE)
			req_ctx->hash_md |= A7_CRY_MD_HASH_HMAC_FIN;
	}

	/* Setup up metadata descriptors */
	error = artpec6_crypto_setup_out_descr(common,
				(void *)&req_ctx->hash_md,
				sizeof(req_ctx->hash_md), false, false);
	if (error)
		return error;

	error = artpec6_crypto_setup_in_descr(common, ac->pad_buffer, 4, false);
	if (error)
		return error;

	if (ext_ctx) {
		error = artpec6_crypto_setup_out_descr(common,
					req_ctx->digeststate,
					contextsize, false, false);

		if (error)
			return error;
	}

	if (req_ctx->hash_flags & HASH_FLAG_UPDATE) {
		size_t done_bytes = 0;
		size_t total_bytes = areq->nbytes + req_ctx->partial_bytes;
		size_t ready_bytes = round_down(total_bytes, blocksize);
		struct artpec6_crypto_walk walk;

		run_hw = ready_bytes > 0;
		if (req_ctx->partial_bytes && ready_bytes) {
			/* We have a partial buffer and will at least some bytes
			 * to the HW. Empty this partial buffer before tackling
			 * the SG lists
			 */
			memcpy(req_ctx->partial_buffer_out,
				req_ctx->partial_buffer,
				req_ctx->partial_bytes);

			error = artpec6_crypto_setup_out_descr(common,
						req_ctx->partial_buffer_out,
						req_ctx->partial_bytes,
						false, true);
			if (error)
				return error;

			/* Reset partial buffer */
			done_bytes += req_ctx->partial_bytes;
			req_ctx->partial_bytes = 0;
		}

		artpec6_crypto_walk_init(&walk, areq->src);

		error = artpec6_crypto_setup_sg_descrs_out(common, &walk,
							   ready_bytes -
							   done_bytes);
		if (error)
			return error;

		if (walk.sg) {
			size_t sg_skip = ready_bytes - done_bytes;
			size_t sg_rem = areq->nbytes - sg_skip;

			sg_pcopy_to_buffer(areq->src, sg_nents(areq->src),
					   req_ctx->partial_buffer +
					   req_ctx->partial_bytes,
					   sg_rem, sg_skip);

			req_ctx->partial_bytes += sg_rem;
		}

		req_ctx->digcnt += ready_bytes;
		req_ctx->hash_flags &= ~(HASH_FLAG_UPDATE);
	}

	/* Finalize */
	if (req_ctx->hash_flags & HASH_FLAG_FINALIZE) {
		bool needtrim = contextsize != digestsize;
		size_t hash_pad_len;
		u64 digest_bits;
		u32 oper;

		if (variant == ARTPEC6_CRYPTO)
			oper = FIELD_GET(A6_CRY_MD_OPER, req_ctx->hash_md);
		else
			oper = FIELD_GET(A7_CRY_MD_OPER, req_ctx->hash_md);

		/* Write out the partial buffer if present */
		if (req_ctx->partial_bytes) {
			memcpy(req_ctx->partial_buffer_out,
			       req_ctx->partial_buffer,
			       req_ctx->partial_bytes);
			error = artpec6_crypto_setup_out_descr(common,
						req_ctx->partial_buffer_out,
						req_ctx->partial_bytes,
						false, true);
			if (error)
				return error;

			req_ctx->digcnt += req_ctx->partial_bytes;
			req_ctx->partial_bytes = 0;
		}

		if (req_ctx->hash_flags & HASH_FLAG_HMAC)
			digest_bits = 8 * (req_ctx->digcnt + blocksize);
		else
			digest_bits = 8 * req_ctx->digcnt;

		/* Add the hash pad */
		hash_pad_len = create_hash_pad(oper, req_ctx->pad_buffer,
					       req_ctx->digcnt, digest_bits);
		error = artpec6_crypto_setup_out_descr(common,
						      req_ctx->pad_buffer,
						      hash_pad_len, false,
						      true);
		req_ctx->digcnt = 0;

		if (error)
			return error;

		/* Descriptor for the final result */
		error = artpec6_crypto_setup_in_descr(common, areq->result,
						      digestsize,
						      !needtrim);
		if (error)
			return error;

		if (needtrim) {
			/* Discard the extra context bytes for SHA-384 */
			error = artpec6_crypto_setup_in_descr(common,
					req_ctx->partial_buffer,
					digestsize - contextsize, true);
			if (error)
				return error;
		}

	} else { /* This is not the final operation for this request */
		if (!run_hw)
			return ARTPEC6_CRYPTO_PREPARE_HASH_NO_START;

		/* Save the result to the context */
		error = artpec6_crypto_setup_in_descr(common,
						      req_ctx->digeststate,
						      contextsize, false);
		if (error)
			return error;
		/* fall through */
	}

	req_ctx->hash_flags &= ~(HASH_FLAG_INIT_CTX | HASH_FLAG_UPDATE |
				 HASH_FLAG_FINALIZE);

	error = artpec6_crypto_terminate_in_descrs(common);
	if (error)
		return error;

	error = artpec6_crypto_terminate_out_descrs(common);
	if (error)
		return error;

	error = artpec6_crypto_dma_map_descs(common);
	if (error)
		return error;

	return ARTPEC6_CRYPTO_PREPARE_HASH_START;
}


static int artpec6_crypto_aes_ecb_init(struct crypto_skcipher *tfm)
{
	struct artpec6_cryptotfm_context *ctx = crypto_skcipher_ctx(tfm);

	tfm->reqsize = sizeof(struct artpec6_crypto_request_context);
	ctx->crypto_type = ARTPEC6_CRYPTO_CIPHER_AES_ECB;

	return 0;
}

static int artpec6_crypto_aes_ctr_init(struct crypto_skcipher *tfm)
{
	struct artpec6_cryptotfm_context *ctx = crypto_skcipher_ctx(tfm);

	ctx->fallback = crypto_alloc_skcipher(crypto_tfm_alg_name(&tfm->base),
					      0,
					      CRYPTO_ALG_ASYNC |
					      CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback))
		return PTR_ERR(ctx->fallback);

	tfm->reqsize = sizeof(struct artpec6_crypto_request_context);
	ctx->crypto_type = ARTPEC6_CRYPTO_CIPHER_AES_CTR;

	return 0;
}

static int artpec6_crypto_aes_cbc_init(struct crypto_skcipher *tfm)
{
	struct artpec6_cryptotfm_context *ctx = crypto_skcipher_ctx(tfm);

	tfm->reqsize = sizeof(struct artpec6_crypto_request_context);
	ctx->crypto_type = ARTPEC6_CRYPTO_CIPHER_AES_CBC;

	return 0;
}

static int artpec6_crypto_aes_xts_init(struct crypto_skcipher *tfm)
{
	struct artpec6_cryptotfm_context *ctx = crypto_skcipher_ctx(tfm);

	tfm->reqsize = sizeof(struct artpec6_crypto_request_context);
	ctx->crypto_type = ARTPEC6_CRYPTO_CIPHER_AES_XTS;

	return 0;
}

static void artpec6_crypto_aes_exit(struct crypto_skcipher *tfm)
{
	struct artpec6_cryptotfm_context *ctx = crypto_skcipher_ctx(tfm);

	memset(ctx, 0, sizeof(*ctx));
}

static void artpec6_crypto_aes_ctr_exit(struct crypto_skcipher *tfm)
{
	struct artpec6_cryptotfm_context *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->fallback);
	artpec6_crypto_aes_exit(tfm);
}

static int
artpec6_crypto_cipher_set_key(struct crypto_skcipher *cipher, const u8 *key,
			      unsigned int keylen)
{
	struct artpec6_cryptotfm_context *ctx =
		crypto_skcipher_ctx(cipher);

	switch (keylen) {
	case 16:
	case 24:
	case 32:
		break;
	default:
		crypto_skcipher_set_flags(cipher,
					  CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->aes_key, key, keylen);
	ctx->key_length = keylen;
	return 0;
}

static int
artpec6_crypto_xts_set_key(struct crypto_skcipher *cipher, const u8 *key,
			      unsigned int keylen)
{
	struct artpec6_cryptotfm_context *ctx =
		crypto_skcipher_ctx(cipher);
	int ret;

	ret = xts_check_key(&cipher->base, key, keylen);
	if (ret)
		return ret;

	switch (keylen) {
	case 32:
	case 48:
	case 64:
		break;
	default:
		crypto_skcipher_set_flags(cipher,
					  CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->aes_key, key, keylen);
	ctx->key_length = keylen;
	return 0;
}

/** artpec6_crypto_process_crypto - Prepare an async block cipher crypto request
 *
 * @req: The asynch request to process
 *
 * @return 0 if the dma job was successfully prepared
 *	  <0 on error
 *
 * This function sets up the PDMA descriptors for a block cipher request.
 *
 * The required padding is added for AES-CTR using a statically defined
 * buffer.
 *
 * The PDMA descriptor list will be as follows:
 *
 * OUT: [KEY_MD][KEY][EOP]<CIPHER_MD>[IV]<data_0>...[data_n][AES-CTR_pad]<eop>
 * IN:  <CIPHER_MD><data_0>...[data_n]<intr>
 *
 */
static int artpec6_crypto_prepare_crypto(struct skcipher_request *areq)
{
	int ret;
	struct artpec6_crypto_walk walk;
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(areq);
	struct artpec6_cryptotfm_context *ctx = crypto_skcipher_ctx(cipher);
	struct artpec6_crypto_request_context *req_ctx = NULL;
	size_t iv_len = crypto_skcipher_ivsize(cipher);
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);
	enum artpec6_crypto_variant variant = ac->variant;
	struct artpec6_crypto_req_common *common;
	bool cipher_decr = false;
	size_t cipher_klen;
	u32 cipher_len = 0; /* Same as regk_crypto_key_128 for NULL crypto */
	u32 oper;

	req_ctx = skcipher_request_ctx(areq);
	common = &req_ctx->common;

	artpec6_crypto_init_dma_operation(common);

	if (variant == ARTPEC6_CRYPTO)
		ctx->key_md = FIELD_PREP(A6_CRY_MD_OPER, a6_regk_crypto_dlkey);
	else
		ctx->key_md = FIELD_PREP(A7_CRY_MD_OPER, a7_regk_crypto_dlkey);

	ret = artpec6_crypto_setup_out_descr(common, (void *)&ctx->key_md,
					     sizeof(ctx->key_md), false, false);
	if (ret)
		return ret;

	ret = artpec6_crypto_setup_out_descr(common, ctx->aes_key,
					      ctx->key_length, true, false);
	if (ret)
		return ret;

	req_ctx->cipher_md = 0;

	if (ctx->crypto_type == ARTPEC6_CRYPTO_CIPHER_AES_XTS)
		cipher_klen = ctx->key_length/2;
	else
		cipher_klen =  ctx->key_length;

	/* Metadata */
	switch (cipher_klen) {
	case 16:
		cipher_len = regk_crypto_key_128;
		break;
	case 24:
		cipher_len = regk_crypto_key_192;
		break;
	case 32:
		cipher_len = regk_crypto_key_256;
		break;
	default:
		pr_err("%s: Invalid key length %d!\n",
			MODULE_NAME, ctx->key_length);
		return -EINVAL;
	}

	switch (ctx->crypto_type) {
	case ARTPEC6_CRYPTO_CIPHER_AES_ECB:
		oper = regk_crypto_aes_ecb;
		cipher_decr = req_ctx->decrypt;
		break;

	case ARTPEC6_CRYPTO_CIPHER_AES_CBC:
		oper = regk_crypto_aes_cbc;
		cipher_decr = req_ctx->decrypt;
		break;

	case ARTPEC6_CRYPTO_CIPHER_AES_CTR:
		oper = regk_crypto_aes_ctr;
		cipher_decr = false;
		break;

	case ARTPEC6_CRYPTO_CIPHER_AES_XTS:
		oper = regk_crypto_aes_xts;
		cipher_decr = req_ctx->decrypt;

		if (variant == ARTPEC6_CRYPTO)
			req_ctx->cipher_md |= A6_CRY_MD_CIPHER_DSEQ;
		else
			req_ctx->cipher_md |= A7_CRY_MD_CIPHER_DSEQ;
		break;

	default:
		pr_err("%s: Invalid cipher mode %d!\n",
			MODULE_NAME, ctx->crypto_type);
		return -EINVAL;
	}

	if (variant == ARTPEC6_CRYPTO) {
		req_ctx->cipher_md |= FIELD_PREP(A6_CRY_MD_OPER, oper);
		req_ctx->cipher_md |= FIELD_PREP(A6_CRY_MD_CIPHER_LEN,
						 cipher_len);
		if (cipher_decr)
			req_ctx->cipher_md |= A6_CRY_MD_CIPHER_DECR;
	} else {
		req_ctx->cipher_md |= FIELD_PREP(A7_CRY_MD_OPER, oper);
		req_ctx->cipher_md |= FIELD_PREP(A7_CRY_MD_CIPHER_LEN,
						 cipher_len);
		if (cipher_decr)
			req_ctx->cipher_md |= A7_CRY_MD_CIPHER_DECR;
	}

	ret = artpec6_crypto_setup_out_descr(common,
					    &req_ctx->cipher_md,
					    sizeof(req_ctx->cipher_md),
					    false, false);
	if (ret)
		return ret;

	ret = artpec6_crypto_setup_in_descr(common, ac->pad_buffer, 4, false);
	if (ret)
		return ret;

	if (iv_len) {
		ret = artpec6_crypto_setup_out_descr(common, areq->iv, iv_len,
						     false, false);
		if (ret)
			return ret;
	}
	/* Data out */
	artpec6_crypto_walk_init(&walk, areq->src);
	ret = artpec6_crypto_setup_sg_descrs_out(common, &walk, areq->cryptlen);
	if (ret)
		return ret;

	/* Data in */
	artpec6_crypto_walk_init(&walk, areq->dst);
	ret = artpec6_crypto_setup_sg_descrs_in(common, &walk, areq->cryptlen);
	if (ret)
		return ret;

	/* CTR-mode padding required by the HW. */
	if (ctx->crypto_type == ARTPEC6_CRYPTO_CIPHER_AES_CTR ||
	    ctx->crypto_type == ARTPEC6_CRYPTO_CIPHER_AES_XTS) {
		size_t pad = ALIGN(areq->cryptlen, AES_BLOCK_SIZE) -
			     areq->cryptlen;

		if (pad) {
			ret = artpec6_crypto_setup_out_descr(common,
							     ac->pad_buffer,
							     pad, false, false);
			if (ret)
				return ret;

			ret = artpec6_crypto_setup_in_descr(common,
							    ac->pad_buffer, pad,
							    false);
			if (ret)
				return ret;
		}
	}

	ret = artpec6_crypto_terminate_out_descrs(common);
	if (ret)
		return ret;

	ret = artpec6_crypto_terminate_in_descrs(common);
	if (ret)
		return ret;

	return artpec6_crypto_dma_map_descs(common);
}

static int artpec6_crypto_prepare_aead(struct aead_request *areq)
{
	size_t count;
	int ret;
	size_t input_length;
	struct artpec6_cryptotfm_context *ctx = crypto_tfm_ctx(areq->base.tfm);
	struct artpec6_crypto_aead_req_ctx *req_ctx = aead_request_ctx(areq);
	struct crypto_aead *cipher = crypto_aead_reqtfm(areq);
	struct artpec6_crypto_req_common *common = &req_ctx->common;
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);
	enum artpec6_crypto_variant variant = ac->variant;
	u32 md_cipher_len;

	artpec6_crypto_init_dma_operation(common);

	/* Key */
	if (variant == ARTPEC6_CRYPTO) {
		ctx->key_md = FIELD_PREP(A6_CRY_MD_OPER,
					 a6_regk_crypto_dlkey);
	} else {
		ctx->key_md = FIELD_PREP(A7_CRY_MD_OPER,
					 a7_regk_crypto_dlkey);
	}
	ret = artpec6_crypto_setup_out_descr(common, (void *)&ctx->key_md,
					     sizeof(ctx->key_md), false, false);
	if (ret)
		return ret;

	ret = artpec6_crypto_setup_out_descr(common, ctx->aes_key,
					     ctx->key_length, true, false);
	if (ret)
		return ret;

	req_ctx->cipher_md = 0;

	switch (ctx->key_length) {
	case 16:
		md_cipher_len = regk_crypto_key_128;
		break;
	case 24:
		md_cipher_len = regk_crypto_key_192;
		break;
	case 32:
		md_cipher_len = regk_crypto_key_256;
		break;
	default:
		return -EINVAL;
	}

	if (variant == ARTPEC6_CRYPTO) {
		req_ctx->cipher_md |= FIELD_PREP(A6_CRY_MD_OPER,
						 regk_crypto_aes_gcm);
		req_ctx->cipher_md |= FIELD_PREP(A6_CRY_MD_CIPHER_LEN,
						 md_cipher_len);
		if (req_ctx->decrypt)
			req_ctx->cipher_md |= A6_CRY_MD_CIPHER_DECR;
	} else {
		req_ctx->cipher_md |= FIELD_PREP(A7_CRY_MD_OPER,
						 regk_crypto_aes_gcm);
		req_ctx->cipher_md |= FIELD_PREP(A7_CRY_MD_CIPHER_LEN,
						 md_cipher_len);
		if (req_ctx->decrypt)
			req_ctx->cipher_md |= A7_CRY_MD_CIPHER_DECR;
	}

	ret = artpec6_crypto_setup_out_descr(common,
					    (void *) &req_ctx->cipher_md,
					    sizeof(req_ctx->cipher_md), false,
					    false);
	if (ret)
		return ret;

	ret = artpec6_crypto_setup_in_descr(common, ac->pad_buffer, 4, false);
	if (ret)
		return ret;

	/* For the decryption, cryptlen includes the tag. */
	input_length = areq->cryptlen;
	if (req_ctx->decrypt)
		input_length -= AES_BLOCK_SIZE;

	/* Prepare the context buffer */
	req_ctx->hw_ctx.aad_length_bits =
		__cpu_to_be64(8*areq->assoclen);

	req_ctx->hw_ctx.text_length_bits =
		__cpu_to_be64(8*input_length);

	memcpy(req_ctx->hw_ctx.J0, areq->iv, crypto_aead_ivsize(cipher));
	// The HW omits the initial increment of the counter field.
	memcpy(req_ctx->hw_ctx.J0 + GCM_AES_IV_SIZE, "\x00\x00\x00\x01", 4);

	ret = artpec6_crypto_setup_out_descr(common, &req_ctx->hw_ctx,
		sizeof(struct artpec6_crypto_aead_hw_ctx), false, false);
	if (ret)
		return ret;

	{
		struct artpec6_crypto_walk walk;

		artpec6_crypto_walk_init(&walk, areq->src);

		/* Associated data */
		count = areq->assoclen;
		ret = artpec6_crypto_setup_sg_descrs_out(common, &walk, count);
		if (ret)
			return ret;

		if (!IS_ALIGNED(areq->assoclen, 16)) {
			size_t assoc_pad = 16 - (areq->assoclen % 16);
			/* The HW mandates zero padding here */
			ret = artpec6_crypto_setup_out_descr(common,
							     ac->zero_buffer,
							     assoc_pad, false,
							     false);
			if (ret)
				return ret;
		}

		/* Data to crypto */
		count = input_length;
		ret = artpec6_crypto_setup_sg_descrs_out(common, &walk, count);
		if (ret)
			return ret;

		if (!IS_ALIGNED(input_length, 16)) {
			size_t crypto_pad = 16 - (input_length % 16);
			/* The HW mandates zero padding here */
			ret = artpec6_crypto_setup_out_descr(common,
							     ac->zero_buffer,
							     crypto_pad,
							     false,
							     false);
			if (ret)
				return ret;
		}
	}

	/* Data from crypto */
	{
		struct artpec6_crypto_walk walk;
		size_t output_len = areq->cryptlen;

		if (req_ctx->decrypt)
			output_len -= AES_BLOCK_SIZE;

		artpec6_crypto_walk_init(&walk, areq->dst);

		/* skip associated data in the output */
		count = artpec6_crypto_walk_advance(&walk, areq->assoclen);
		if (count)
			return -EINVAL;

		count = output_len;
		ret = artpec6_crypto_setup_sg_descrs_in(common, &walk, count);
		if (ret)
			return ret;

		/* Put padding between the cryptotext and the auth tag */
		if (!IS_ALIGNED(output_len, 16)) {
			size_t crypto_pad = 16 - (output_len % 16);

			ret = artpec6_crypto_setup_in_descr(common,
							    ac->pad_buffer,
							    crypto_pad, false);
			if (ret)
				return ret;
		}

		/* The authentication tag shall follow immediately after
		 * the output ciphertext. For decryption it is put in a context
		 * buffer for later compare against the input tag.
		 */
		count = AES_BLOCK_SIZE;

		if (req_ctx->decrypt) {
			ret = artpec6_crypto_setup_in_descr(common,
				req_ctx->decryption_tag, count, false);
			if (ret)
				return ret;

		} else {
			ret = artpec6_crypto_setup_sg_descrs_in(common, &walk,
								count);
			if (ret)
				return ret;
		}

	}

	ret = artpec6_crypto_terminate_in_descrs(common);
	if (ret)
		return ret;

	ret = artpec6_crypto_terminate_out_descrs(common);
	if (ret)
		return ret;

	return artpec6_crypto_dma_map_descs(common);
}

static void artpec6_crypto_process_queue(struct artpec6_crypto *ac)
{
	struct artpec6_crypto_req_common *req;

	while (!list_empty(&ac->queue) && !artpec6_crypto_busy()) {
		req = list_first_entry(&ac->queue,
				       struct artpec6_crypto_req_common,
				       list);
		list_move_tail(&req->list, &ac->pending);
		artpec6_crypto_start_dma(req);

		req->req->complete(req->req, -EINPROGRESS);
	}

	/*
	 * In some cases, the hardware can raise an in_eop_flush interrupt
	 * before actually updating the status, so we have an timer which will
	 * recheck the status on timeout.  Since the cases are expected to be
	 * very rare, we use a relatively large timeout value.  There should be
	 * no noticeable negative effect if we timeout spuriously.
	 */
	if (ac->pending_count)
		mod_timer(&ac->timer, jiffies + msecs_to_jiffies(100));
	else
		del_timer(&ac->timer);
}

static void artpec6_crypto_timeout(struct timer_list *t)
{
	struct artpec6_crypto *ac = from_timer(ac, t, timer);

	dev_info_ratelimited(artpec6_crypto_dev, "timeout\n");

	tasklet_schedule(&ac->task);
}

static void artpec6_crypto_task(unsigned long data)
{
	struct artpec6_crypto *ac = (struct artpec6_crypto *)data;
	struct artpec6_crypto_req_common *req;
	struct artpec6_crypto_req_common *n;

	if (list_empty(&ac->pending)) {
		pr_debug("Spurious IRQ\n");
		return;
	}

	spin_lock_bh(&ac->queue_lock);

	list_for_each_entry_safe(req, n, &ac->pending, list) {
		struct artpec6_crypto_dma_descriptors *dma = req->dma;
		u32 stat;

		dma_sync_single_for_cpu(artpec6_crypto_dev, dma->stat_dma_addr,
					sizeof(dma->stat[0]),
					DMA_BIDIRECTIONAL);

		stat = req->dma->stat[req->dma->in_cnt-1];

		/* A non-zero final status descriptor indicates
		 * this job has finished.
		 */
		pr_debug("Request %p status is %X\n", req, stat);
		if (!stat)
			break;

		/* Allow testing of timeout handling with fault injection */
#ifdef CONFIG_FAULT_INJECTION
		if (should_fail(&artpec6_crypto_fail_status_read, 1))
			continue;
#endif

		pr_debug("Completing request %p\n", req);

		list_del(&req->list);

		artpec6_crypto_dma_unmap_all(req);
		artpec6_crypto_copy_bounce_buffers(req);

		ac->pending_count--;
		artpec6_crypto_common_destroy(req);
		req->complete(req->req);
	}

	artpec6_crypto_process_queue(ac);

	spin_unlock_bh(&ac->queue_lock);
}

static void artpec6_crypto_complete_crypto(struct crypto_async_request *req)
{
	req->complete(req, 0);
}

static void
artpec6_crypto_complete_cbc_decrypt(struct crypto_async_request *req)
{
	struct skcipher_request *cipher_req = container_of(req,
		struct skcipher_request, base);

	scatterwalk_map_and_copy(cipher_req->iv, cipher_req->src,
				 cipher_req->cryptlen - AES_BLOCK_SIZE,
				 AES_BLOCK_SIZE, 0);
	req->complete(req, 0);
}

static void
artpec6_crypto_complete_cbc_encrypt(struct crypto_async_request *req)
{
	struct skcipher_request *cipher_req = container_of(req,
		struct skcipher_request, base);

	scatterwalk_map_and_copy(cipher_req->iv, cipher_req->dst,
				 cipher_req->cryptlen - AES_BLOCK_SIZE,
				 AES_BLOCK_SIZE, 0);
	req->complete(req, 0);
}

static void artpec6_crypto_complete_aead(struct crypto_async_request *req)
{
	int result = 0;

	/* Verify GCM hashtag. */
	struct aead_request *areq = container_of(req,
		struct aead_request, base);
	struct artpec6_crypto_aead_req_ctx *req_ctx = aead_request_ctx(areq);

	if (req_ctx->decrypt) {
		u8 input_tag[AES_BLOCK_SIZE];

		sg_pcopy_to_buffer(areq->src,
				   sg_nents(areq->src),
				   input_tag,
				   AES_BLOCK_SIZE,
				   areq->assoclen + areq->cryptlen -
				   AES_BLOCK_SIZE);

		if (memcmp(req_ctx->decryption_tag,
			   input_tag,
			   AES_BLOCK_SIZE)) {
			pr_debug("***EBADMSG:\n");
			print_hex_dump_debug("ref:", DUMP_PREFIX_ADDRESS, 32, 1,
					     input_tag, AES_BLOCK_SIZE, true);
			print_hex_dump_debug("out:", DUMP_PREFIX_ADDRESS, 32, 1,
					     req_ctx->decryption_tag,
					     AES_BLOCK_SIZE, true);

			result = -EBADMSG;
		}
	}

	req->complete(req, result);
}

static void artpec6_crypto_complete_hash(struct crypto_async_request *req)
{
	req->complete(req, 0);
}


/*------------------- Hash functions -----------------------------------------*/
static int
artpec6_crypto_hash_set_key(struct crypto_ahash *tfm,
		    const u8 *key, unsigned int keylen)
{
	struct artpec6_hashalg_context *tfm_ctx = crypto_tfm_ctx(&tfm->base);
	size_t blocksize;
	int ret;

	if (!keylen) {
		pr_err("Invalid length (%d) of HMAC key\n",
			keylen);
		return -EINVAL;
	}

	memset(tfm_ctx->hmac_key, 0, sizeof(tfm_ctx->hmac_key));

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));

	if (keylen > blocksize) {
		SHASH_DESC_ON_STACK(hdesc, tfm_ctx->child_hash);

		hdesc->tfm = tfm_ctx->child_hash;
		hdesc->flags = crypto_ahash_get_flags(tfm) &
			       CRYPTO_TFM_REQ_MAY_SLEEP;

		tfm_ctx->hmac_key_length = blocksize;
		ret = crypto_shash_digest(hdesc, key, keylen,
					  tfm_ctx->hmac_key);
		if (ret)
			return ret;

	} else {
		memcpy(tfm_ctx->hmac_key, key, keylen);
		tfm_ctx->hmac_key_length = keylen;
	}

	return 0;
}

static int
artpec6_crypto_init_hash(struct ahash_request *req, u8 type, int hmac)
{
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);
	enum artpec6_crypto_variant variant = ac->variant;
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);
	u32 oper;

	memset(req_ctx, 0, sizeof(*req_ctx));

	req_ctx->hash_flags = HASH_FLAG_INIT_CTX;
	if (hmac)
		req_ctx->hash_flags |= (HASH_FLAG_HMAC | HASH_FLAG_UPDATE_KEY);

	switch (type) {
	case ARTPEC6_CRYPTO_HASH_SHA1:
		oper = hmac ? regk_crypto_hmac_sha1 : regk_crypto_sha1;
		break;
	case ARTPEC6_CRYPTO_HASH_SHA256:
		oper = hmac ? regk_crypto_hmac_sha256 : regk_crypto_sha256;
		break;
	case ARTPEC6_CRYPTO_HASH_SHA384:
		oper = hmac ? regk_crypto_hmac_sha384 : regk_crypto_sha384;
		break;
	case ARTPEC6_CRYPTO_HASH_SHA512:
		oper = hmac ? regk_crypto_hmac_sha512 : regk_crypto_sha512;
		break;

	default:
		pr_err("%s: Unsupported hash type 0x%x\n", MODULE_NAME, type);
		return -EINVAL;
	}

	if (variant == ARTPEC6_CRYPTO)
		req_ctx->hash_md = FIELD_PREP(A6_CRY_MD_OPER, oper);
	else
		req_ctx->hash_md = FIELD_PREP(A7_CRY_MD_OPER, oper);

	return 0;
}

static int artpec6_crypto_prepare_submit_hash(struct ahash_request *req)
{
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);
	int ret;

	if (!req_ctx->common.dma) {
		ret = artpec6_crypto_common_init(&req_ctx->common,
					  &req->base,
					  artpec6_crypto_complete_hash,
					  NULL, 0);

		if (ret)
			return ret;
	}

	ret = artpec6_crypto_prepare_hash(req);
	switch (ret) {
	case ARTPEC6_CRYPTO_PREPARE_HASH_START:
		ret = artpec6_crypto_submit(&req_ctx->common);
		break;

	case ARTPEC6_CRYPTO_PREPARE_HASH_NO_START:
		ret = 0;
		/* Fallthrough */

	default:
		artpec6_crypto_common_destroy(&req_ctx->common);
		break;
	}

	return ret;
}

static int artpec6_crypto_hash_final(struct ahash_request *req)
{
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);

	req_ctx->hash_flags |= HASH_FLAG_FINALIZE;

	return artpec6_crypto_prepare_submit_hash(req);
}

static int artpec6_crypto_hash_update(struct ahash_request *req)
{
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);

	req_ctx->hash_flags |= HASH_FLAG_UPDATE;

	return artpec6_crypto_prepare_submit_hash(req);
}

static int artpec6_crypto_sha1_init(struct ahash_request *req)
{
	return artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA1, 0);
}

static int artpec6_crypto_sha1_digest(struct ahash_request *req)
{
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);

	artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA1, 0);

	req_ctx->hash_flags |= HASH_FLAG_UPDATE | HASH_FLAG_FINALIZE;

	return artpec6_crypto_prepare_submit_hash(req);
}

static int artpec6_crypto_sha256_init(struct ahash_request *req)
{
	return artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA256, 0);
}

static int artpec6_crypto_sha256_digest(struct ahash_request *req)
{
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);

	artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA256, 0);
	req_ctx->hash_flags |= HASH_FLAG_UPDATE | HASH_FLAG_FINALIZE;

	return artpec6_crypto_prepare_submit_hash(req);
}

static int __maybe_unused artpec6_crypto_sha384_init(struct ahash_request *req)
{
	return artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA384, 0);
}

static int __maybe_unused
artpec6_crypto_sha384_digest(struct ahash_request *req)
{
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);

	artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA384, 0);
	req_ctx->hash_flags |= HASH_FLAG_UPDATE | HASH_FLAG_FINALIZE;

	return artpec6_crypto_prepare_submit_hash(req);
}

static int artpec6_crypto_sha512_init(struct ahash_request *req)
{
	return artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA512, 0);
}

static int artpec6_crypto_sha512_digest(struct ahash_request *req)
{
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);

	artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA512, 0);
	req_ctx->hash_flags |= HASH_FLAG_UPDATE | HASH_FLAG_FINALIZE;

	return artpec6_crypto_prepare_submit_hash(req);
}

static int artpec6_crypto_hmac_sha256_init(struct ahash_request *req)
{
	return artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA256, 1);
}

static int __maybe_unused
artpec6_crypto_hmac_sha384_init(struct ahash_request *req)
{
	return artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA384, 1);
}

static int artpec6_crypto_hmac_sha512_init(struct ahash_request *req)
{
	return artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA512, 1);
}

static int artpec6_crypto_hmac_sha256_digest(struct ahash_request *req)
{
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);

	artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA256, 1);
	req_ctx->hash_flags |= HASH_FLAG_UPDATE | HASH_FLAG_FINALIZE;

	return artpec6_crypto_prepare_submit_hash(req);
}

static int __maybe_unused
artpec6_crypto_hmac_sha384_digest(struct ahash_request *req)
{
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);

	artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA384, 1);
	req_ctx->hash_flags |= HASH_FLAG_UPDATE | HASH_FLAG_FINALIZE;

	return artpec6_crypto_prepare_submit_hash(req);
}

static int artpec6_crypto_hmac_sha512_digest(struct ahash_request *req)
{
	struct artpec6_hash_request_context *req_ctx = ahash_request_ctx(req);

	artpec6_crypto_init_hash(req, ARTPEC6_CRYPTO_HASH_SHA512, 1);
	req_ctx->hash_flags |= HASH_FLAG_UPDATE | HASH_FLAG_FINALIZE;

	return artpec6_crypto_prepare_submit_hash(req);
}

static int artpec6_crypto_ahash_init_common(struct crypto_tfm *tfm,
				    const char *base_hash_name)
{
	struct artpec6_hashalg_context *tfm_ctx = crypto_tfm_ctx(tfm);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct artpec6_hash_request_context));
	memset(tfm_ctx, 0, sizeof(*tfm_ctx));

	if (base_hash_name) {
		struct crypto_shash *child;

		child = crypto_alloc_shash(base_hash_name, 0,
					   CRYPTO_ALG_NEED_FALLBACK);

		if (IS_ERR(child))
			return PTR_ERR(child);

		tfm_ctx->child_hash = child;
	}

	return 0;
}

static int artpec6_crypto_ahash_init(struct crypto_tfm *tfm)
{
	return artpec6_crypto_ahash_init_common(tfm, NULL);
}

static int artpec6_crypto_ahash_init_hmac_sha256(struct crypto_tfm *tfm)
{
	return artpec6_crypto_ahash_init_common(tfm, "sha256");
}

static int __maybe_unused
artpec6_crypto_ahash_init_hmac_sha384(struct crypto_tfm *tfm)
{
	return artpec6_crypto_ahash_init_common(tfm, "sha384");
}

static int artpec6_crypto_ahash_init_hmac_sha512(struct crypto_tfm *tfm)
{
	return artpec6_crypto_ahash_init_common(tfm, "sha512");
}

static void artpec6_crypto_ahash_exit(struct crypto_tfm *tfm)
{
	struct artpec6_hashalg_context *tfm_ctx = crypto_tfm_ctx(tfm);

	if (tfm_ctx->child_hash)
		crypto_free_shash(tfm_ctx->child_hash);

	memset(tfm_ctx->hmac_key, 0, sizeof(tfm_ctx->hmac_key));
	tfm_ctx->hmac_key_length = 0;
}

static int artpec6_crypto_hash_export(struct ahash_request *req, void *out)
{
	const struct artpec6_hash_request_context *ctx = ahash_request_ctx(req);
	struct artpec6_hash_export_state *state = out;
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);
	enum artpec6_crypto_variant variant = ac->variant;

	BUILD_BUG_ON(sizeof(state->partial_buffer) !=
		     sizeof(ctx->partial_buffer));
	BUILD_BUG_ON(sizeof(state->digeststate) != sizeof(ctx->digeststate));

	state->digcnt = ctx->digcnt;
	state->partial_bytes = ctx->partial_bytes;
	state->hash_flags = ctx->hash_flags;

	if (variant == ARTPEC6_CRYPTO)
		state->oper = FIELD_GET(A6_CRY_MD_OPER, ctx->hash_md);
	else
		state->oper = FIELD_GET(A7_CRY_MD_OPER, ctx->hash_md);

	memcpy(state->partial_buffer, ctx->partial_buffer,
	       sizeof(state->partial_buffer));
	memcpy(state->digeststate, ctx->digeststate,
	       sizeof(state->digeststate));

	return 0;
}

static int artpec6_crypto_hash_import(struct ahash_request *req, const void *in)
{
	struct artpec6_hash_request_context *ctx = ahash_request_ctx(req);
	const struct artpec6_hash_export_state *state = in;
	struct artpec6_crypto *ac = dev_get_drvdata(artpec6_crypto_dev);
	enum artpec6_crypto_variant variant = ac->variant;

	memset(ctx, 0, sizeof(*ctx));

	ctx->digcnt = state->digcnt;
	ctx->partial_bytes = state->partial_bytes;
	ctx->hash_flags = state->hash_flags;

	if (variant == ARTPEC6_CRYPTO)
		ctx->hash_md = FIELD_PREP(A6_CRY_MD_OPER, state->oper);
	else
		ctx->hash_md = FIELD_PREP(A7_CRY_MD_OPER, state->oper);

	memcpy(ctx->partial_buffer, state->partial_buffer,
	       sizeof(state->partial_buffer));
	memcpy(ctx->digeststate, state->digeststate,
	       sizeof(state->digeststate));

	return 0;
}

static int init_crypto_hw(struct artpec6_crypto *ac)
{
	enum artpec6_crypto_variant variant = ac->variant;
	void __iomem *base = ac->base;
	u32 out_descr_buf_size;
	u32 out_data_buf_size;
	u32 in_data_buf_size;
	u32 in_descr_buf_size;
	u32 in_stat_buf_size;
	u32 in, out;

	/*
	 * The PDMA unit contains 1984 bytes of internal memory for the OUT
	 * channels and 1024 bytes for the IN channel. This is an elastic
	 * memory used to internally store the descriptors and data. The values
	 * ares specified in 64 byte incremements.  Trustzone buffers are not
	 * used at this stage.
	 */
	out_data_buf_size = 16;  /* 1024 bytes for data */
	out_descr_buf_size = 15; /* 960 bytes for descriptors */
	in_data_buf_size = 8;    /* 512 bytes for data */
	in_descr_buf_size = 4;   /* 256 bytes for descriptors */
	in_stat_buf_size = 4;   /* 256 bytes for stat descrs */

	BUILD_BUG_ON_MSG((out_data_buf_size
				+ out_descr_buf_size) * 64 > 1984,
			  "Invalid OUT configuration");

	BUILD_BUG_ON_MSG((in_data_buf_size
				+ in_descr_buf_size
				+ in_stat_buf_size) * 64 > 1024,
			  "Invalid IN configuration");

	in = FIELD_PREP(PDMA_IN_BUF_CFG_DATA_BUF_SIZE, in_data_buf_size) |
	     FIELD_PREP(PDMA_IN_BUF_CFG_DESCR_BUF_SIZE, in_descr_buf_size) |
	     FIELD_PREP(PDMA_IN_BUF_CFG_STAT_BUF_SIZE, in_stat_buf_size);

	out = FIELD_PREP(PDMA_OUT_BUF_CFG_DATA_BUF_SIZE, out_data_buf_size) |
	      FIELD_PREP(PDMA_OUT_BUF_CFG_DESCR_BUF_SIZE, out_descr_buf_size);

	writel_relaxed(out, base + PDMA_OUT_BUF_CFG);
	writel_relaxed(PDMA_OUT_CFG_EN, base + PDMA_OUT_CFG);

	if (variant == ARTPEC6_CRYPTO) {
		writel_relaxed(in, base + A6_PDMA_IN_BUF_CFG);
		writel_relaxed(PDMA_IN_CFG_EN, base + A6_PDMA_IN_CFG);
		writel_relaxed(A6_PDMA_INTR_MASK_IN_DATA |
			       A6_PDMA_INTR_MASK_IN_EOP_FLUSH,
			       base + A6_PDMA_INTR_MASK);
	} else {
		writel_relaxed(in, base + A7_PDMA_IN_BUF_CFG);
		writel_relaxed(PDMA_IN_CFG_EN, base + A7_PDMA_IN_CFG);
		writel_relaxed(A7_PDMA_INTR_MASK_IN_DATA |
			       A7_PDMA_INTR_MASK_IN_EOP_FLUSH,
			       base + A7_PDMA_INTR_MASK);
	}

	return 0;
}

static void artpec6_crypto_disable_hw(struct artpec6_crypto *ac)
{
	enum artpec6_crypto_variant variant = ac->variant;
	void __iomem *base = ac->base;

	if (variant == ARTPEC6_CRYPTO) {
		writel_relaxed(A6_PDMA_IN_CMD_STOP, base + A6_PDMA_IN_CMD);
		writel_relaxed(0, base + A6_PDMA_IN_CFG);
		writel_relaxed(A6_PDMA_OUT_CMD_STOP, base + PDMA_OUT_CMD);
	} else {
		writel_relaxed(A7_PDMA_IN_CMD_STOP, base + A7_PDMA_IN_CMD);
		writel_relaxed(0, base + A7_PDMA_IN_CFG);
		writel_relaxed(A7_PDMA_OUT_CMD_STOP, base + PDMA_OUT_CMD);
	}

	writel_relaxed(0, base + PDMA_OUT_CFG);

}

static irqreturn_t artpec6_crypto_irq(int irq, void *dev_id)
{
	struct artpec6_crypto *ac = dev_id;
	enum artpec6_crypto_variant variant = ac->variant;
	void __iomem *base = ac->base;
	u32 mask_in_data, mask_in_eop_flush;
	u32 in_cmd_flush_stat, in_cmd_reg;
	u32 ack_intr_reg;
	u32 ack = 0;
	u32 intr;

	if (variant == ARTPEC6_CRYPTO) {
		intr = readl_relaxed(base + A6_PDMA_MASKED_INTR);
		mask_in_data = A6_PDMA_INTR_MASK_IN_DATA;
		mask_in_eop_flush = A6_PDMA_INTR_MASK_IN_EOP_FLUSH;
		in_cmd_flush_stat = A6_PDMA_IN_CMD_FLUSH_STAT;
		in_cmd_reg = A6_PDMA_IN_CMD;
		ack_intr_reg = A6_PDMA_ACK_INTR;
	} else {
		intr = readl_relaxed(base + A7_PDMA_MASKED_INTR);
		mask_in_data = A7_PDMA_INTR_MASK_IN_DATA;
		mask_in_eop_flush = A7_PDMA_INTR_MASK_IN_EOP_FLUSH;
		in_cmd_flush_stat = A7_PDMA_IN_CMD_FLUSH_STAT;
		in_cmd_reg = A7_PDMA_IN_CMD;
		ack_intr_reg = A7_PDMA_ACK_INTR;
	}

	/* We get two interrupt notifications from each job.
	 * The in_data means all data was sent to memory and then
	 * we request a status flush command to write the per-job
	 * status to its status vector. This ensures that the
	 * tasklet can detect exactly how many submitted jobs
	 * that have finished.
	 */
	if (intr & mask_in_data)
		ack |= mask_in_data;

	if (intr & mask_in_eop_flush)
		ack |= mask_in_eop_flush;
	else
		writel_relaxed(in_cmd_flush_stat, base + in_cmd_reg);

	writel_relaxed(ack, base + ack_intr_reg);

	if (intr & mask_in_eop_flush)
		tasklet_schedule(&ac->task);

	return IRQ_HANDLED;
}

/*------------------- Algorithm definitions ----------------------------------*/

/* Hashes */
static struct ahash_alg hash_algos[] = {
	/* SHA-1 */
	{
		.init = artpec6_crypto_sha1_init,
		.update = artpec6_crypto_hash_update,
		.final = artpec6_crypto_hash_final,
		.digest = artpec6_crypto_sha1_digest,
		.import = artpec6_crypto_hash_import,
		.export = artpec6_crypto_hash_export,
		.halg.digestsize = SHA1_DIGEST_SIZE,
		.halg.statesize = sizeof(struct artpec6_hash_export_state),
		.halg.base = {
			.cra_name = "sha1",
			.cra_driver_name = "artpec-sha1",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC,
			.cra_blocksize = SHA1_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct artpec6_hashalg_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
			.cra_init = artpec6_crypto_ahash_init,
			.cra_exit = artpec6_crypto_ahash_exit,
		}
	},
	/* SHA-256 */
	{
		.init = artpec6_crypto_sha256_init,
		.update = artpec6_crypto_hash_update,
		.final = artpec6_crypto_hash_final,
		.digest = artpec6_crypto_sha256_digest,
		.import = artpec6_crypto_hash_import,
		.export = artpec6_crypto_hash_export,
		.halg.digestsize = SHA256_DIGEST_SIZE,
		.halg.statesize = sizeof(struct artpec6_hash_export_state),
		.halg.base = {
			.cra_name = "sha256",
			.cra_driver_name = "artpec-sha256",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC,
			.cra_blocksize = SHA256_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct artpec6_hashalg_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
			.cra_init = artpec6_crypto_ahash_init,
			.cra_exit = artpec6_crypto_ahash_exit,
		}
	},
	/* HMAC SHA-256 */
	{
		.init = artpec6_crypto_hmac_sha256_init,
		.update = artpec6_crypto_hash_update,
		.final = artpec6_crypto_hash_final,
		.digest = artpec6_crypto_hmac_sha256_digest,
		.import = artpec6_crypto_hash_import,
		.export = artpec6_crypto_hash_export,
		.setkey = artpec6_crypto_hash_set_key,
		.halg.digestsize = SHA256_DIGEST_SIZE,
		.halg.statesize = sizeof(struct artpec6_hash_export_state),
		.halg.base = {
			.cra_name = "hmac(sha256)",
			.cra_driver_name = "artpec-hmac-sha256",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC,
			.cra_blocksize = SHA256_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct artpec6_hashalg_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
			.cra_init = artpec6_crypto_ahash_init_hmac_sha256,
			.cra_exit = artpec6_crypto_ahash_exit,
		}
	},
};

static struct ahash_alg artpec7_hash_algos[] = {
	/* SHA-384 */
	{
		.init = artpec6_crypto_sha384_init,
		.update = artpec6_crypto_hash_update,
		.final = artpec6_crypto_hash_final,
		.digest = artpec6_crypto_sha384_digest,
		.import = artpec6_crypto_hash_import,
		.export = artpec6_crypto_hash_export,
		.halg.digestsize = SHA384_DIGEST_SIZE,
		.halg.statesize = sizeof(struct artpec6_hash_export_state),
		.halg.base = {
			.cra_name = "sha384",
			.cra_driver_name = "artpec-sha384",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC,
			.cra_blocksize = SHA384_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct artpec6_hashalg_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
			.cra_init = artpec6_crypto_ahash_init,
			.cra_exit = artpec6_crypto_ahash_exit,
		}
	},
	/* HMAC SHA-384 */
	{
		.init = artpec6_crypto_hmac_sha384_init,
		.update = artpec6_crypto_hash_update,
		.final = artpec6_crypto_hash_final,
		.digest = artpec6_crypto_hmac_sha384_digest,
		.import = artpec6_crypto_hash_import,
		.export = artpec6_crypto_hash_export,
		.setkey = artpec6_crypto_hash_set_key,
		.halg.digestsize = SHA384_DIGEST_SIZE,
		.halg.statesize = sizeof(struct artpec6_hash_export_state),
		.halg.base = {
			.cra_name = "hmac(sha384)",
			.cra_driver_name = "artpec-hmac-sha384",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC,
			.cra_blocksize = SHA384_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct artpec6_hashalg_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
			.cra_init = artpec6_crypto_ahash_init_hmac_sha384,
			.cra_exit = artpec6_crypto_ahash_exit,
		}
	},
	/* SHA-512 */
	{
		.init = artpec6_crypto_sha512_init,
		.update = artpec6_crypto_hash_update,
		.final = artpec6_crypto_hash_final,
		.digest = artpec6_crypto_sha512_digest,
		.import = artpec6_crypto_hash_import,
		.export = artpec6_crypto_hash_export,
		.halg.digestsize = SHA512_DIGEST_SIZE,
		.halg.statesize = sizeof(struct artpec6_hash_export_state),
		.halg.base = {
			.cra_name = "sha512",
			.cra_driver_name = "artpec-sha512",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC,
			.cra_blocksize = SHA512_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct artpec6_hashalg_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
			.cra_init = artpec6_crypto_ahash_init,
			.cra_exit = artpec6_crypto_ahash_exit,
		}
	},
	/* HMAC SHA-512 */
	{
		.init = artpec6_crypto_hmac_sha512_init,
		.update = artpec6_crypto_hash_update,
		.final = artpec6_crypto_hash_final,
		.digest = artpec6_crypto_hmac_sha512_digest,
		.import = artpec6_crypto_hash_import,
		.export = artpec6_crypto_hash_export,
		.setkey = artpec6_crypto_hash_set_key,
		.halg.digestsize = SHA512_DIGEST_SIZE,
		.halg.statesize = sizeof(struct artpec6_hash_export_state),
		.halg.base = {
			.cra_name = "hmac(sha512)",
			.cra_driver_name = "artpec-hmac-sha512",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC,
			.cra_blocksize = SHA512_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct artpec6_hashalg_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
			.cra_init = artpec6_crypto_ahash_init_hmac_sha512,
			.cra_exit = artpec6_crypto_ahash_exit,
		}
	},
};

/* Crypto */
static struct skcipher_alg crypto_algos[] = {
	/* AES - ECB */
	{
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "artpec6-ecb-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				     CRYPTO_ALG_ASYNC,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct artpec6_cryptotfm_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
		},
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.setkey = artpec6_crypto_cipher_set_key,
		.encrypt = artpec6_crypto_encrypt,
		.decrypt = artpec6_crypto_decrypt,
		.init = artpec6_crypto_aes_ecb_init,
		.exit = artpec6_crypto_aes_exit,
	},
	/* AES - CTR */
	{
		.base = {
			.cra_name = "ctr(aes)",
			.cra_driver_name = "artpec6-ctr-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_NEED_FALLBACK,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct artpec6_cryptotfm_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
		},
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = artpec6_crypto_cipher_set_key,
		.encrypt = artpec6_crypto_ctr_encrypt,
		.decrypt = artpec6_crypto_ctr_decrypt,
		.init = artpec6_crypto_aes_ctr_init,
		.exit = artpec6_crypto_aes_ctr_exit,
	},
	/* AES - CBC */
	{
		.base = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "artpec6-cbc-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				     CRYPTO_ALG_ASYNC,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct artpec6_cryptotfm_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
		},
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = artpec6_crypto_cipher_set_key,
		.encrypt = artpec6_crypto_encrypt,
		.decrypt = artpec6_crypto_decrypt,
		.init = artpec6_crypto_aes_cbc_init,
		.exit = artpec6_crypto_aes_exit
	},
	/* AES - XTS */
	{
		.base = {
			.cra_name = "xts(aes)",
			.cra_driver_name = "artpec6-xts-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				     CRYPTO_ALG_ASYNC,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct artpec6_cryptotfm_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
		},
		.min_keysize = 2*AES_MIN_KEY_SIZE,
		.max_keysize = 2*AES_MAX_KEY_SIZE,
		.ivsize = 16,
		.setkey = artpec6_crypto_xts_set_key,
		.encrypt = artpec6_crypto_encrypt,
		.decrypt = artpec6_crypto_decrypt,
		.init = artpec6_crypto_aes_xts_init,
		.exit = artpec6_crypto_aes_exit,
	},
};

static struct aead_alg aead_algos[] = {
	{
		.init   = artpec6_crypto_aead_init,
		.setkey = artpec6_crypto_aead_set_key,
		.encrypt = artpec6_crypto_aead_encrypt,
		.decrypt = artpec6_crypto_aead_decrypt,
		.ivsize = GCM_AES_IV_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,

		.base = {
			.cra_name = "gcm(aes)",
			.cra_driver_name = "artpec-gcm-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct artpec6_cryptotfm_context),
			.cra_alignmask = 3,
			.cra_module = THIS_MODULE,
		},
	}
};

#ifdef CONFIG_DEBUG_FS

struct dbgfs_u32 {
	char *name;
	mode_t mode;
	u32 *flag;
	char *desc;
};

static struct dentry *dbgfs_root;

static void artpec6_crypto_init_debugfs(void)
{
	dbgfs_root = debugfs_create_dir("artpec6_crypto", NULL);

	if (!dbgfs_root || IS_ERR(dbgfs_root)) {
		dbgfs_root = NULL;
		pr_err("%s: Could not initialise debugfs!\n", MODULE_NAME);
		return;
	}

#ifdef CONFIG_FAULT_INJECTION
	fault_create_debugfs_attr("fail_status_read", dbgfs_root,
				  &artpec6_crypto_fail_status_read);

	fault_create_debugfs_attr("fail_dma_array_full", dbgfs_root,
				  &artpec6_crypto_fail_dma_array_full);
#endif
}

static void artpec6_crypto_free_debugfs(void)
{
	if (!dbgfs_root)
		return;

	debugfs_remove_recursive(dbgfs_root);
	dbgfs_root = NULL;
}
#endif

static const struct of_device_id artpec6_crypto_of_match[] = {
	{ .compatible = "axis,artpec6-crypto", .data = (void *)ARTPEC6_CRYPTO },
	{ .compatible = "axis,artpec7-crypto", .data = (void *)ARTPEC7_CRYPTO },
	{}
};
MODULE_DEVICE_TABLE(of, artpec6_crypto_of_match);

static int artpec6_crypto_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	enum artpec6_crypto_variant variant;
	struct artpec6_crypto *ac;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	struct resource *res;
	int irq;
	int err;

	if (artpec6_crypto_dev)
		return -ENODEV;

	match = of_match_node(artpec6_crypto_of_match, dev->of_node);
	if (!match)
		return -EINVAL;

	variant = (enum artpec6_crypto_variant)match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	ac = devm_kzalloc(&pdev->dev, sizeof(struct artpec6_crypto),
			  GFP_KERNEL);
	if (!ac)
		return -ENOMEM;

	platform_set_drvdata(pdev, ac);
	ac->variant = variant;

	spin_lock_init(&ac->queue_lock);
	INIT_LIST_HEAD(&ac->queue);
	INIT_LIST_HEAD(&ac->pending);
	timer_setup(&ac->timer, artpec6_crypto_timeout, 0);

	ac->base = base;

	ac->dma_cache = kmem_cache_create("artpec6_crypto_dma",
		sizeof(struct artpec6_crypto_dma_descriptors),
		64,
		0,
		NULL);
	if (!ac->dma_cache)
		return -ENOMEM;

#ifdef CONFIG_DEBUG_FS
	artpec6_crypto_init_debugfs();
#endif

	tasklet_init(&ac->task, artpec6_crypto_task,
		     (unsigned long)ac);

	ac->pad_buffer = devm_kzalloc(&pdev->dev, 2 * ARTPEC_CACHE_LINE_MAX,
				      GFP_KERNEL);
	if (!ac->pad_buffer)
		return -ENOMEM;
	ac->pad_buffer = PTR_ALIGN(ac->pad_buffer, ARTPEC_CACHE_LINE_MAX);

	ac->zero_buffer = devm_kzalloc(&pdev->dev, 2 * ARTPEC_CACHE_LINE_MAX,
				      GFP_KERNEL);
	if (!ac->zero_buffer)
		return -ENOMEM;
	ac->zero_buffer = PTR_ALIGN(ac->zero_buffer, ARTPEC_CACHE_LINE_MAX);

	err = init_crypto_hw(ac);
	if (err)
		goto free_cache;

	err = devm_request_irq(&pdev->dev, irq, artpec6_crypto_irq, 0,
			       "artpec6-crypto", ac);
	if (err)
		goto disable_hw;

	artpec6_crypto_dev = &pdev->dev;

	err = crypto_register_ahashes(hash_algos, ARRAY_SIZE(hash_algos));
	if (err) {
		dev_err(dev, "Failed to register ahashes\n");
		goto disable_hw;
	}

	if (variant != ARTPEC6_CRYPTO) {
		err = crypto_register_ahashes(artpec7_hash_algos,
					      ARRAY_SIZE(artpec7_hash_algos));
		if (err) {
			dev_err(dev, "Failed to register ahashes\n");
			goto unregister_ahashes;
		}
	}

	err = crypto_register_skciphers(crypto_algos, ARRAY_SIZE(crypto_algos));
	if (err) {
		dev_err(dev, "Failed to register ciphers\n");
		goto unregister_a7_ahashes;
	}

	err = crypto_register_aeads(aead_algos, ARRAY_SIZE(aead_algos));
	if (err) {
		dev_err(dev, "Failed to register aeads\n");
		goto unregister_algs;
	}

	return 0;

unregister_algs:
	crypto_unregister_skciphers(crypto_algos, ARRAY_SIZE(crypto_algos));
unregister_a7_ahashes:
	if (variant != ARTPEC6_CRYPTO)
		crypto_unregister_ahashes(artpec7_hash_algos,
					  ARRAY_SIZE(artpec7_hash_algos));
unregister_ahashes:
	crypto_unregister_ahashes(hash_algos, ARRAY_SIZE(hash_algos));
disable_hw:
	artpec6_crypto_disable_hw(ac);
free_cache:
	kmem_cache_destroy(ac->dma_cache);
	return err;
}

static int artpec6_crypto_remove(struct platform_device *pdev)
{
	struct artpec6_crypto *ac = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	crypto_unregister_ahashes(hash_algos, ARRAY_SIZE(hash_algos));
	if (ac->variant != ARTPEC6_CRYPTO)
		crypto_unregister_ahashes(artpec7_hash_algos,
					  ARRAY_SIZE(artpec7_hash_algos));
	crypto_unregister_skciphers(crypto_algos, ARRAY_SIZE(crypto_algos));
	crypto_unregister_aeads(aead_algos, ARRAY_SIZE(aead_algos));

	tasklet_disable(&ac->task);
	devm_free_irq(&pdev->dev, irq, ac);
	tasklet_kill(&ac->task);
	del_timer_sync(&ac->timer);

	artpec6_crypto_disable_hw(ac);

	kmem_cache_destroy(ac->dma_cache);
#ifdef CONFIG_DEBUG_FS
	artpec6_crypto_free_debugfs();
#endif
	return 0;
}

static struct platform_driver artpec6_crypto_driver = {
	.probe   = artpec6_crypto_probe,
	.remove  = artpec6_crypto_remove,
	.driver  = {
		.name  = "artpec6-crypto",
		.owner = THIS_MODULE,
		.of_match_table = artpec6_crypto_of_match,
	},
};

module_platform_driver(artpec6_crypto_driver);

MODULE_AUTHOR("Axis Communications AB");
MODULE_DESCRIPTION("ARTPEC-6 Crypto driver");
MODULE_LICENSE("GPL");
