// SPDX-License-Identifier: GPL-2.0
//
// Cryptographic API.
//
// Support for Samsung S5PV210 and Exynos HW acceleration.
//
// Copyright (C) 2011 NetUP Inc. All rights reserved.
// Copyright (c) 2017 Samsung Electronics Co., Ltd. All rights reserved.
//
// Hash part based on omap-sham.c driver.

#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>

#include <crypto/ctr.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/scatterwalk.h>

#include <crypto/hash.h>
#include <crypto/md5.h>
#include <crypto/sha.h>
#include <crypto/internal/hash.h>

#define _SBF(s, v)			((v) << (s))

/* Feed control registers */
#define SSS_REG_FCINTSTAT		0x0000
#define SSS_FCINTSTAT_HPARTINT		BIT(7)
#define SSS_FCINTSTAT_HDONEINT		BIT(5)
#define SSS_FCINTSTAT_BRDMAINT		BIT(3)
#define SSS_FCINTSTAT_BTDMAINT		BIT(2)
#define SSS_FCINTSTAT_HRDMAINT		BIT(1)
#define SSS_FCINTSTAT_PKDMAINT		BIT(0)

#define SSS_REG_FCINTENSET		0x0004
#define SSS_FCINTENSET_HPARTINTENSET	BIT(7)
#define SSS_FCINTENSET_HDONEINTENSET	BIT(5)
#define SSS_FCINTENSET_BRDMAINTENSET	BIT(3)
#define SSS_FCINTENSET_BTDMAINTENSET	BIT(2)
#define SSS_FCINTENSET_HRDMAINTENSET	BIT(1)
#define SSS_FCINTENSET_PKDMAINTENSET	BIT(0)

#define SSS_REG_FCINTENCLR		0x0008
#define SSS_FCINTENCLR_HPARTINTENCLR	BIT(7)
#define SSS_FCINTENCLR_HDONEINTENCLR	BIT(5)
#define SSS_FCINTENCLR_BRDMAINTENCLR	BIT(3)
#define SSS_FCINTENCLR_BTDMAINTENCLR	BIT(2)
#define SSS_FCINTENCLR_HRDMAINTENCLR	BIT(1)
#define SSS_FCINTENCLR_PKDMAINTENCLR	BIT(0)

#define SSS_REG_FCINTPEND		0x000C
#define SSS_FCINTPEND_HPARTINTP		BIT(7)
#define SSS_FCINTPEND_HDONEINTP		BIT(5)
#define SSS_FCINTPEND_BRDMAINTP		BIT(3)
#define SSS_FCINTPEND_BTDMAINTP		BIT(2)
#define SSS_FCINTPEND_HRDMAINTP		BIT(1)
#define SSS_FCINTPEND_PKDMAINTP		BIT(0)

#define SSS_REG_FCFIFOSTAT		0x0010
#define SSS_FCFIFOSTAT_BRFIFOFUL	BIT(7)
#define SSS_FCFIFOSTAT_BRFIFOEMP	BIT(6)
#define SSS_FCFIFOSTAT_BTFIFOFUL	BIT(5)
#define SSS_FCFIFOSTAT_BTFIFOEMP	BIT(4)
#define SSS_FCFIFOSTAT_HRFIFOFUL	BIT(3)
#define SSS_FCFIFOSTAT_HRFIFOEMP	BIT(2)
#define SSS_FCFIFOSTAT_PKFIFOFUL	BIT(1)
#define SSS_FCFIFOSTAT_PKFIFOEMP	BIT(0)

#define SSS_REG_FCFIFOCTRL		0x0014
#define SSS_FCFIFOCTRL_DESSEL		BIT(2)
#define SSS_HASHIN_INDEPENDENT		_SBF(0, 0x00)
#define SSS_HASHIN_CIPHER_INPUT		_SBF(0, 0x01)
#define SSS_HASHIN_CIPHER_OUTPUT	_SBF(0, 0x02)
#define SSS_HASHIN_MASK			_SBF(0, 0x03)

#define SSS_REG_FCBRDMAS		0x0020
#define SSS_REG_FCBRDMAL		0x0024
#define SSS_REG_FCBRDMAC		0x0028
#define SSS_FCBRDMAC_BYTESWAP		BIT(1)
#define SSS_FCBRDMAC_FLUSH		BIT(0)

#define SSS_REG_FCBTDMAS		0x0030
#define SSS_REG_FCBTDMAL		0x0034
#define SSS_REG_FCBTDMAC		0x0038
#define SSS_FCBTDMAC_BYTESWAP		BIT(1)
#define SSS_FCBTDMAC_FLUSH		BIT(0)

#define SSS_REG_FCHRDMAS		0x0040
#define SSS_REG_FCHRDMAL		0x0044
#define SSS_REG_FCHRDMAC		0x0048
#define SSS_FCHRDMAC_BYTESWAP		BIT(1)
#define SSS_FCHRDMAC_FLUSH		BIT(0)

#define SSS_REG_FCPKDMAS		0x0050
#define SSS_REG_FCPKDMAL		0x0054
#define SSS_REG_FCPKDMAC		0x0058
#define SSS_FCPKDMAC_BYTESWAP		BIT(3)
#define SSS_FCPKDMAC_DESCEND		BIT(2)
#define SSS_FCPKDMAC_TRANSMIT		BIT(1)
#define SSS_FCPKDMAC_FLUSH		BIT(0)

#define SSS_REG_FCPKDMAO		0x005C

/* AES registers */
#define SSS_REG_AES_CONTROL		0x00
#define SSS_AES_BYTESWAP_DI		BIT(11)
#define SSS_AES_BYTESWAP_DO		BIT(10)
#define SSS_AES_BYTESWAP_IV		BIT(9)
#define SSS_AES_BYTESWAP_CNT		BIT(8)
#define SSS_AES_BYTESWAP_KEY		BIT(7)
#define SSS_AES_KEY_CHANGE_MODE		BIT(6)
#define SSS_AES_KEY_SIZE_128		_SBF(4, 0x00)
#define SSS_AES_KEY_SIZE_192		_SBF(4, 0x01)
#define SSS_AES_KEY_SIZE_256		_SBF(4, 0x02)
#define SSS_AES_FIFO_MODE		BIT(3)
#define SSS_AES_CHAIN_MODE_ECB		_SBF(1, 0x00)
#define SSS_AES_CHAIN_MODE_CBC		_SBF(1, 0x01)
#define SSS_AES_CHAIN_MODE_CTR		_SBF(1, 0x02)
#define SSS_AES_MODE_DECRYPT		BIT(0)

#define SSS_REG_AES_STATUS		0x04
#define SSS_AES_BUSY			BIT(2)
#define SSS_AES_INPUT_READY		BIT(1)
#define SSS_AES_OUTPUT_READY		BIT(0)

#define SSS_REG_AES_IN_DATA(s)		(0x10 + (s << 2))
#define SSS_REG_AES_OUT_DATA(s)		(0x20 + (s << 2))
#define SSS_REG_AES_IV_DATA(s)		(0x30 + (s << 2))
#define SSS_REG_AES_CNT_DATA(s)		(0x40 + (s << 2))
#define SSS_REG_AES_KEY_DATA(s)		(0x80 + (s << 2))

#define SSS_REG(dev, reg)		((dev)->ioaddr + (SSS_REG_##reg))
#define SSS_READ(dev, reg)		__raw_readl(SSS_REG(dev, reg))
#define SSS_WRITE(dev, reg, val)	__raw_writel((val), SSS_REG(dev, reg))

#define SSS_AES_REG(dev, reg)		((dev)->aes_ioaddr + SSS_REG_##reg)
#define SSS_AES_WRITE(dev, reg, val)    __raw_writel((val), \
						SSS_AES_REG(dev, reg))

/* HW engine modes */
#define FLAGS_AES_DECRYPT		BIT(0)
#define FLAGS_AES_MODE_MASK		_SBF(1, 0x03)
#define FLAGS_AES_CBC			_SBF(1, 0x01)
#define FLAGS_AES_CTR			_SBF(1, 0x02)

#define AES_KEY_LEN			16
#define CRYPTO_QUEUE_LEN		1

/* HASH registers */
#define SSS_REG_HASH_CTRL		0x00

#define SSS_HASH_USER_IV_EN		BIT(5)
#define SSS_HASH_INIT_BIT		BIT(4)
#define SSS_HASH_ENGINE_SHA1		_SBF(1, 0x00)
#define SSS_HASH_ENGINE_MD5		_SBF(1, 0x01)
#define SSS_HASH_ENGINE_SHA256		_SBF(1, 0x02)

#define SSS_HASH_ENGINE_MASK		_SBF(1, 0x03)

#define SSS_REG_HASH_CTRL_PAUSE		0x04

#define SSS_HASH_PAUSE			BIT(0)

#define SSS_REG_HASH_CTRL_FIFO		0x08

#define SSS_HASH_FIFO_MODE_DMA		BIT(0)
#define SSS_HASH_FIFO_MODE_CPU          0

#define SSS_REG_HASH_CTRL_SWAP		0x0C

#define SSS_HASH_BYTESWAP_DI		BIT(3)
#define SSS_HASH_BYTESWAP_DO		BIT(2)
#define SSS_HASH_BYTESWAP_IV		BIT(1)
#define SSS_HASH_BYTESWAP_KEY		BIT(0)

#define SSS_REG_HASH_STATUS		0x10

#define SSS_HASH_STATUS_MSG_DONE	BIT(6)
#define SSS_HASH_STATUS_PARTIAL_DONE	BIT(4)
#define SSS_HASH_STATUS_BUFFER_READY	BIT(0)

#define SSS_REG_HASH_MSG_SIZE_LOW	0x20
#define SSS_REG_HASH_MSG_SIZE_HIGH	0x24

#define SSS_REG_HASH_PRE_MSG_SIZE_LOW	0x28
#define SSS_REG_HASH_PRE_MSG_SIZE_HIGH	0x2C

#define SSS_REG_HASH_IV(s)		(0xB0 + ((s) << 2))
#define SSS_REG_HASH_OUT(s)		(0x100 + ((s) << 2))

#define HASH_BLOCK_SIZE			64
#define HASH_REG_SIZEOF			4
#define HASH_MD5_MAX_REG		(MD5_DIGEST_SIZE / HASH_REG_SIZEOF)
#define HASH_SHA1_MAX_REG		(SHA1_DIGEST_SIZE / HASH_REG_SIZEOF)
#define HASH_SHA256_MAX_REG		(SHA256_DIGEST_SIZE / HASH_REG_SIZEOF)

/*
 * HASH bit numbers, used by device, setting in dev->hash_flags with
 * functions set_bit(), clear_bit() or tested with test_bit() or BIT(),
 * to keep HASH state BUSY or FREE, or to signal state from irq_handler
 * to hash_tasklet. SGS keep track of allocated memory for scatterlist
 */
#define HASH_FLAGS_BUSY		0
#define HASH_FLAGS_FINAL	1
#define HASH_FLAGS_DMA_ACTIVE	2
#define HASH_FLAGS_OUTPUT_READY	3
#define HASH_FLAGS_DMA_READY	4
#define HASH_FLAGS_SGS_COPIED	5
#define HASH_FLAGS_SGS_ALLOCED	6

/* HASH HW constants */
#define BUFLEN			HASH_BLOCK_SIZE

#define SSS_HASH_DMA_LEN_ALIGN	8
#define SSS_HASH_DMA_ALIGN_MASK	(SSS_HASH_DMA_LEN_ALIGN - 1)

#define SSS_HASH_QUEUE_LENGTH	10

/**
 * struct samsung_aes_variant - platform specific SSS driver data
 * @aes_offset: AES register offset from SSS module's base.
 * @hash_offset: HASH register offset from SSS module's base.
 * @clk_names: names of clocks needed to run SSS IP
 *
 * Specifies platform specific configuration of SSS module.
 * Note: A structure for driver specific platform data is used for future
 * expansion of its usage.
 */
struct samsung_aes_variant {
	unsigned int			aes_offset;
	unsigned int			hash_offset;
	const char			*clk_names[2];
};

struct s5p_aes_reqctx {
	unsigned long			mode;
};

struct s5p_aes_ctx {
	struct s5p_aes_dev		*dev;

	u8				aes_key[AES_MAX_KEY_SIZE];
	u8				nonce[CTR_RFC3686_NONCE_SIZE];
	int				keylen;
};

/**
 * struct s5p_aes_dev - Crypto device state container
 * @dev:	Associated device
 * @clk:	Clock for accessing hardware
 * @ioaddr:	Mapped IO memory region
 * @aes_ioaddr:	Per-varian offset for AES block IO memory
 * @irq_fc:	Feed control interrupt line
 * @req:	Crypto request currently handled by the device
 * @ctx:	Configuration for currently handled crypto request
 * @sg_src:	Scatter list with source data for currently handled block
 *		in device.  This is DMA-mapped into device.
 * @sg_dst:	Scatter list with destination data for currently handled block
 *		in device. This is DMA-mapped into device.
 * @sg_src_cpy:	In case of unaligned access, copied scatter list
 *		with source data.
 * @sg_dst_cpy:	In case of unaligned access, copied scatter list
 *		with destination data.
 * @tasklet:	New request scheduling jib
 * @queue:	Crypto queue
 * @busy:	Indicates whether the device is currently handling some request
 *		thus it uses some of the fields from this state, like:
 *		req, ctx, sg_src/dst (and copies).  This essentially
 *		protects against concurrent access to these fields.
 * @lock:	Lock for protecting both access to device hardware registers
 *		and fields related to current request (including the busy field).
 * @res:	Resources for hash.
 * @io_hash_base: Per-variant offset for HASH block IO memory.
 * @hash_lock:	Lock for protecting hash_req, hash_queue and hash_flags
 *		variable.
 * @hash_flags:	Flags for current HASH op.
 * @hash_queue:	Async hash queue.
 * @hash_tasklet: New HASH request scheduling job.
 * @xmit_buf:	Buffer for current HASH request transfer into SSS block.
 * @hash_req:	Current request sending to SSS HASH block.
 * @hash_sg_iter: Scatterlist transferred through DMA into SSS HASH block.
 * @hash_sg_cnt: Counter for hash_sg_iter.
 *
 * @use_hash:	true if HASH algs enabled
 */
struct s5p_aes_dev {
	struct device			*dev;
	struct clk			*clk;
	struct clk			*pclk;
	void __iomem			*ioaddr;
	void __iomem			*aes_ioaddr;
	int				irq_fc;

	struct skcipher_request		*req;
	struct s5p_aes_ctx		*ctx;
	struct scatterlist		*sg_src;
	struct scatterlist		*sg_dst;

	struct scatterlist		*sg_src_cpy;
	struct scatterlist		*sg_dst_cpy;

	struct tasklet_struct		tasklet;
	struct crypto_queue		queue;
	bool				busy;
	spinlock_t			lock;

	struct resource			*res;
	void __iomem			*io_hash_base;

	spinlock_t			hash_lock; /* protect hash_ vars */
	unsigned long			hash_flags;
	struct crypto_queue		hash_queue;
	struct tasklet_struct		hash_tasklet;

	u8				xmit_buf[BUFLEN];
	struct ahash_request		*hash_req;
	struct scatterlist		*hash_sg_iter;
	unsigned int			hash_sg_cnt;

	bool				use_hash;
};

/**
 * struct s5p_hash_reqctx - HASH request context
 * @dd:		Associated device
 * @op_update:	Current request operation (OP_UPDATE or OP_FINAL)
 * @digcnt:	Number of bytes processed by HW (without buffer[] ones)
 * @digest:	Digest message or IV for partial result
 * @nregs:	Number of HW registers for digest or IV read/write
 * @engine:	Bits for selecting type of HASH in SSS block
 * @sg:		sg for DMA transfer
 * @sg_len:	Length of sg for DMA transfer
 * @sgl[]:	sg for joining buffer and req->src scatterlist
 * @skip:	Skip offset in req->src for current op
 * @total:	Total number of bytes for current request
 * @finup:	Keep state for finup or final.
 * @error:	Keep track of error.
 * @bufcnt:	Number of bytes holded in buffer[]
 * @buffer[]:	For byte(s) from end of req->src in UPDATE op
 */
struct s5p_hash_reqctx {
	struct s5p_aes_dev	*dd;
	bool			op_update;

	u64			digcnt;
	u8			digest[SHA256_DIGEST_SIZE];

	unsigned int		nregs; /* digest_size / sizeof(reg) */
	u32			engine;

	struct scatterlist	*sg;
	unsigned int		sg_len;
	struct scatterlist	sgl[2];
	unsigned int		skip;
	unsigned int		total;
	bool			finup;
	bool			error;

	u32			bufcnt;
	u8			buffer[];
};

/**
 * struct s5p_hash_ctx - HASH transformation context
 * @dd:		Associated device
 * @flags:	Bits for algorithm HASH.
 * @fallback:	Software transformation for zero message or size < BUFLEN.
 */
struct s5p_hash_ctx {
	struct s5p_aes_dev	*dd;
	unsigned long		flags;
	struct crypto_shash	*fallback;
};

static const struct samsung_aes_variant s5p_aes_data = {
	.aes_offset	= 0x4000,
	.hash_offset	= 0x6000,
	.clk_names	= { "secss", },
};

static const struct samsung_aes_variant exynos_aes_data = {
	.aes_offset	= 0x200,
	.hash_offset	= 0x400,
	.clk_names	= { "secss", },
};

static const struct samsung_aes_variant exynos5433_slim_aes_data = {
	.aes_offset	= 0x400,
	.hash_offset	= 0x800,
	.clk_names	= { "pclk", "aclk", },
};

static const struct of_device_id s5p_sss_dt_match[] = {
	{
		.compatible = "samsung,s5pv210-secss",
		.data = &s5p_aes_data,
	},
	{
		.compatible = "samsung,exynos4210-secss",
		.data = &exynos_aes_data,
	},
	{
		.compatible = "samsung,exynos5433-slim-sss",
		.data = &exynos5433_slim_aes_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, s5p_sss_dt_match);

static inline const struct samsung_aes_variant *find_s5p_sss_version
				   (const struct platform_device *pdev)
{
	if (IS_ENABLED(CONFIG_OF) && (pdev->dev.of_node)) {
		const struct of_device_id *match;

		match = of_match_node(s5p_sss_dt_match,
					pdev->dev.of_node);
		return (const struct samsung_aes_variant *)match->data;
	}
	return (const struct samsung_aes_variant *)
			platform_get_device_id(pdev)->driver_data;
}

static struct s5p_aes_dev *s5p_dev;

static void s5p_set_dma_indata(struct s5p_aes_dev *dev,
			       const struct scatterlist *sg)
{
	SSS_WRITE(dev, FCBRDMAS, sg_dma_address(sg));
	SSS_WRITE(dev, FCBRDMAL, sg_dma_len(sg));
}

static void s5p_set_dma_outdata(struct s5p_aes_dev *dev,
				const struct scatterlist *sg)
{
	SSS_WRITE(dev, FCBTDMAS, sg_dma_address(sg));
	SSS_WRITE(dev, FCBTDMAL, sg_dma_len(sg));
}

static void s5p_free_sg_cpy(struct s5p_aes_dev *dev, struct scatterlist **sg)
{
	int len;

	if (!*sg)
		return;

	len = ALIGN(dev->req->cryptlen, AES_BLOCK_SIZE);
	free_pages((unsigned long)sg_virt(*sg), get_order(len));

	kfree(*sg);
	*sg = NULL;
}

static void s5p_sg_copy_buf(void *buf, struct scatterlist *sg,
			    unsigned int nbytes, int out)
{
	struct scatter_walk walk;

	if (!nbytes)
		return;

	scatterwalk_start(&walk, sg);
	scatterwalk_copychunks(buf, &walk, nbytes, out);
	scatterwalk_done(&walk, out, 0);
}

static void s5p_sg_done(struct s5p_aes_dev *dev)
{
	struct skcipher_request *req = dev->req;
	struct s5p_aes_reqctx *reqctx = skcipher_request_ctx(req);

	if (dev->sg_dst_cpy) {
		dev_dbg(dev->dev,
			"Copying %d bytes of output data back to original place\n",
			dev->req->cryptlen);
		s5p_sg_copy_buf(sg_virt(dev->sg_dst_cpy), dev->req->dst,
				dev->req->cryptlen, 1);
	}
	s5p_free_sg_cpy(dev, &dev->sg_src_cpy);
	s5p_free_sg_cpy(dev, &dev->sg_dst_cpy);
	if (reqctx->mode & FLAGS_AES_CBC)
		memcpy_fromio(req->iv, dev->aes_ioaddr + SSS_REG_AES_IV_DATA(0), AES_BLOCK_SIZE);

	else if (reqctx->mode & FLAGS_AES_CTR)
		memcpy_fromio(req->iv, dev->aes_ioaddr + SSS_REG_AES_CNT_DATA(0), AES_BLOCK_SIZE);
}

/* Calls the completion. Cannot be called with dev->lock hold. */
static void s5p_aes_complete(struct skcipher_request *req, int err)
{
	req->base.complete(&req->base, err);
}

static void s5p_unset_outdata(struct s5p_aes_dev *dev)
{
	dma_unmap_sg(dev->dev, dev->sg_dst, 1, DMA_FROM_DEVICE);
}

static void s5p_unset_indata(struct s5p_aes_dev *dev)
{
	dma_unmap_sg(dev->dev, dev->sg_src, 1, DMA_TO_DEVICE);
}

static int s5p_make_sg_cpy(struct s5p_aes_dev *dev, struct scatterlist *src,
			   struct scatterlist **dst)
{
	void *pages;
	int len;

	*dst = kmalloc(sizeof(**dst), GFP_ATOMIC);
	if (!*dst)
		return -ENOMEM;

	len = ALIGN(dev->req->cryptlen, AES_BLOCK_SIZE);
	pages = (void *)__get_free_pages(GFP_ATOMIC, get_order(len));
	if (!pages) {
		kfree(*dst);
		*dst = NULL;
		return -ENOMEM;
	}

	s5p_sg_copy_buf(pages, src, dev->req->cryptlen, 0);

	sg_init_table(*dst, 1);
	sg_set_buf(*dst, pages, len);

	return 0;
}

static int s5p_set_outdata(struct s5p_aes_dev *dev, struct scatterlist *sg)
{
	if (!sg->length)
		return -EINVAL;

	if (!dma_map_sg(dev->dev, sg, 1, DMA_FROM_DEVICE))
		return -ENOMEM;

	dev->sg_dst = sg;

	return 0;
}

static int s5p_set_indata(struct s5p_aes_dev *dev, struct scatterlist *sg)
{
	if (!sg->length)
		return -EINVAL;

	if (!dma_map_sg(dev->dev, sg, 1, DMA_TO_DEVICE))
		return -ENOMEM;

	dev->sg_src = sg;

	return 0;
}

/*
 * Returns -ERRNO on error (mapping of new data failed).
 * On success returns:
 *  - 0 if there is no more data,
 *  - 1 if new transmitting (output) data is ready and its address+length
 *     have to be written to device (by calling s5p_set_dma_outdata()).
 */
static int s5p_aes_tx(struct s5p_aes_dev *dev)
{
	int ret = 0;

	s5p_unset_outdata(dev);

	if (!sg_is_last(dev->sg_dst)) {
		ret = s5p_set_outdata(dev, sg_next(dev->sg_dst));
		if (!ret)
			ret = 1;
	}

	return ret;
}

/*
 * Returns -ERRNO on error (mapping of new data failed).
 * On success returns:
 *  - 0 if there is no more data,
 *  - 1 if new receiving (input) data is ready and its address+length
 *     have to be written to device (by calling s5p_set_dma_indata()).
 */
static int s5p_aes_rx(struct s5p_aes_dev *dev/*, bool *set_dma*/)
{
	int ret = 0;

	s5p_unset_indata(dev);

	if (!sg_is_last(dev->sg_src)) {
		ret = s5p_set_indata(dev, sg_next(dev->sg_src));
		if (!ret)
			ret = 1;
	}

	return ret;
}

static inline u32 s5p_hash_read(struct s5p_aes_dev *dd, u32 offset)
{
	return __raw_readl(dd->io_hash_base + offset);
}

static inline void s5p_hash_write(struct s5p_aes_dev *dd,
				  u32 offset, u32 value)
{
	__raw_writel(value, dd->io_hash_base + offset);
}

/**
 * s5p_set_dma_hashdata() - start DMA with sg
 * @dev:	device
 * @sg:		scatterlist ready to DMA transmit
 */
static void s5p_set_dma_hashdata(struct s5p_aes_dev *dev,
				 const struct scatterlist *sg)
{
	dev->hash_sg_cnt--;
	SSS_WRITE(dev, FCHRDMAS, sg_dma_address(sg));
	SSS_WRITE(dev, FCHRDMAL, sg_dma_len(sg)); /* DMA starts */
}

/**
 * s5p_hash_rx() - get next hash_sg_iter
 * @dev:	device
 *
 * Return:
 * 2	if there is no more data and it is UPDATE op
 * 1	if new receiving (input) data is ready and can be written to device
 * 0	if there is no more data and it is FINAL op
 */
static int s5p_hash_rx(struct s5p_aes_dev *dev)
{
	if (dev->hash_sg_cnt > 0) {
		dev->hash_sg_iter = sg_next(dev->hash_sg_iter);
		return 1;
	}

	set_bit(HASH_FLAGS_DMA_READY, &dev->hash_flags);
	if (test_bit(HASH_FLAGS_FINAL, &dev->hash_flags))
		return 0;

	return 2;
}

static irqreturn_t s5p_aes_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct s5p_aes_dev *dev = platform_get_drvdata(pdev);
	struct skcipher_request *req;
	int err_dma_tx = 0;
	int err_dma_rx = 0;
	int err_dma_hx = 0;
	bool tx_end = false;
	bool hx_end = false;
	unsigned long flags;
	u32 status, st_bits;
	int err;

	spin_lock_irqsave(&dev->lock, flags);

	/*
	 * Handle rx or tx interrupt. If there is still data (scatterlist did not
	 * reach end), then map next scatterlist entry.
	 * In case of such mapping error, s5p_aes_complete() should be called.
	 *
	 * If there is no more data in tx scatter list, call s5p_aes_complete()
	 * and schedule new tasklet.
	 *
	 * Handle hx interrupt. If there is still data map next entry.
	 */
	status = SSS_READ(dev, FCINTSTAT);
	if (status & SSS_FCINTSTAT_BRDMAINT)
		err_dma_rx = s5p_aes_rx(dev);

	if (status & SSS_FCINTSTAT_BTDMAINT) {
		if (sg_is_last(dev->sg_dst))
			tx_end = true;
		err_dma_tx = s5p_aes_tx(dev);
	}

	if (status & SSS_FCINTSTAT_HRDMAINT)
		err_dma_hx = s5p_hash_rx(dev);

	st_bits = status & (SSS_FCINTSTAT_BRDMAINT | SSS_FCINTSTAT_BTDMAINT |
				SSS_FCINTSTAT_HRDMAINT);
	/* clear DMA bits */
	SSS_WRITE(dev, FCINTPEND, st_bits);

	/* clear HASH irq bits */
	if (status & (SSS_FCINTSTAT_HDONEINT | SSS_FCINTSTAT_HPARTINT)) {
		/* cannot have both HPART and HDONE */
		if (status & SSS_FCINTSTAT_HPARTINT)
			st_bits = SSS_HASH_STATUS_PARTIAL_DONE;

		if (status & SSS_FCINTSTAT_HDONEINT)
			st_bits = SSS_HASH_STATUS_MSG_DONE;

		set_bit(HASH_FLAGS_OUTPUT_READY, &dev->hash_flags);
		s5p_hash_write(dev, SSS_REG_HASH_STATUS, st_bits);
		hx_end = true;
		/* when DONE or PART, do not handle HASH DMA */
		err_dma_hx = 0;
	}

	if (err_dma_rx < 0) {
		err = err_dma_rx;
		goto error;
	}
	if (err_dma_tx < 0) {
		err = err_dma_tx;
		goto error;
	}

	if (tx_end) {
		s5p_sg_done(dev);
		if (err_dma_hx == 1)
			s5p_set_dma_hashdata(dev, dev->hash_sg_iter);

		spin_unlock_irqrestore(&dev->lock, flags);

		s5p_aes_complete(dev->req, 0);
		/* Device is still busy */
		tasklet_schedule(&dev->tasklet);
	} else {
		/*
		 * Writing length of DMA block (either receiving or
		 * transmitting) will start the operation immediately, so this
		 * should be done at the end (even after clearing pending
		 * interrupts to not miss the interrupt).
		 */
		if (err_dma_tx == 1)
			s5p_set_dma_outdata(dev, dev->sg_dst);
		if (err_dma_rx == 1)
			s5p_set_dma_indata(dev, dev->sg_src);
		if (err_dma_hx == 1)
			s5p_set_dma_hashdata(dev, dev->hash_sg_iter);

		spin_unlock_irqrestore(&dev->lock, flags);
	}

	goto hash_irq_end;

error:
	s5p_sg_done(dev);
	dev->busy = false;
	req = dev->req;
	if (err_dma_hx == 1)
		s5p_set_dma_hashdata(dev, dev->hash_sg_iter);

	spin_unlock_irqrestore(&dev->lock, flags);
	s5p_aes_complete(req, err);

hash_irq_end:
	/*
	 * Note about else if:
	 *   when hash_sg_iter reaches end and its UPDATE op,
	 *   issue SSS_HASH_PAUSE and wait for HPART irq
	 */
	if (hx_end)
		tasklet_schedule(&dev->hash_tasklet);
	else if (err_dma_hx == 2)
		s5p_hash_write(dev, SSS_REG_HASH_CTRL_PAUSE,
			       SSS_HASH_PAUSE);

	return IRQ_HANDLED;
}

/**
 * s5p_hash_read_msg() - read message or IV from HW
 * @req:	AHASH request
 */
static void s5p_hash_read_msg(struct ahash_request *req)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);
	struct s5p_aes_dev *dd = ctx->dd;
	u32 *hash = (u32 *)ctx->digest;
	unsigned int i;

	for (i = 0; i < ctx->nregs; i++)
		hash[i] = s5p_hash_read(dd, SSS_REG_HASH_OUT(i));
}

/**
 * s5p_hash_write_ctx_iv() - write IV for next partial/finup op.
 * @dd:		device
 * @ctx:	request context
 */
static void s5p_hash_write_ctx_iv(struct s5p_aes_dev *dd,
				  const struct s5p_hash_reqctx *ctx)
{
	const u32 *hash = (const u32 *)ctx->digest;
	unsigned int i;

	for (i = 0; i < ctx->nregs; i++)
		s5p_hash_write(dd, SSS_REG_HASH_IV(i), hash[i]);
}

/**
 * s5p_hash_write_iv() - write IV for next partial/finup op.
 * @req:	AHASH request
 */
static void s5p_hash_write_iv(struct ahash_request *req)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);

	s5p_hash_write_ctx_iv(ctx->dd, ctx);
}

/**
 * s5p_hash_copy_result() - copy digest into req->result
 * @req:	AHASH request
 */
static void s5p_hash_copy_result(struct ahash_request *req)
{
	const struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);

	if (!req->result)
		return;

	memcpy(req->result, ctx->digest, ctx->nregs * HASH_REG_SIZEOF);
}

/**
 * s5p_hash_dma_flush() - flush HASH DMA
 * @dev:	secss device
 */
static void s5p_hash_dma_flush(struct s5p_aes_dev *dev)
{
	SSS_WRITE(dev, FCHRDMAC, SSS_FCHRDMAC_FLUSH);
}

/**
 * s5p_hash_dma_enable() - enable DMA mode for HASH
 * @dev:	secss device
 *
 * enable DMA mode for HASH
 */
static void s5p_hash_dma_enable(struct s5p_aes_dev *dev)
{
	s5p_hash_write(dev, SSS_REG_HASH_CTRL_FIFO, SSS_HASH_FIFO_MODE_DMA);
}

/**
 * s5p_hash_irq_disable() - disable irq HASH signals
 * @dev:	secss device
 * @flags:	bitfield with irq's to be disabled
 */
static void s5p_hash_irq_disable(struct s5p_aes_dev *dev, u32 flags)
{
	SSS_WRITE(dev, FCINTENCLR, flags);
}

/**
 * s5p_hash_irq_enable() - enable irq signals
 * @dev:	secss device
 * @flags:	bitfield with irq's to be enabled
 */
static void s5p_hash_irq_enable(struct s5p_aes_dev *dev, int flags)
{
	SSS_WRITE(dev, FCINTENSET, flags);
}

/**
 * s5p_hash_set_flow() - set flow inside SecSS AES/DES with/without HASH
 * @dev:	secss device
 * @hashflow:	HASH stream flow with/without crypto AES/DES
 */
static void s5p_hash_set_flow(struct s5p_aes_dev *dev, u32 hashflow)
{
	unsigned long flags;
	u32 flow;

	spin_lock_irqsave(&dev->lock, flags);

	flow = SSS_READ(dev, FCFIFOCTRL);
	flow &= ~SSS_HASHIN_MASK;
	flow |= hashflow;
	SSS_WRITE(dev, FCFIFOCTRL, flow);

	spin_unlock_irqrestore(&dev->lock, flags);
}

/**
 * s5p_ahash_dma_init() - enable DMA and set HASH flow inside SecSS
 * @dev:	secss device
 * @hashflow:	HASH stream flow with/without AES/DES
 *
 * flush HASH DMA and enable DMA, set HASH stream flow inside SecSS HW,
 * enable HASH irq's HRDMA, HDONE, HPART
 */
static void s5p_ahash_dma_init(struct s5p_aes_dev *dev, u32 hashflow)
{
	s5p_hash_irq_disable(dev, SSS_FCINTENCLR_HRDMAINTENCLR |
			     SSS_FCINTENCLR_HDONEINTENCLR |
			     SSS_FCINTENCLR_HPARTINTENCLR);
	s5p_hash_dma_flush(dev);

	s5p_hash_dma_enable(dev);
	s5p_hash_set_flow(dev, hashflow & SSS_HASHIN_MASK);
	s5p_hash_irq_enable(dev, SSS_FCINTENSET_HRDMAINTENSET |
			    SSS_FCINTENSET_HDONEINTENSET |
			    SSS_FCINTENSET_HPARTINTENSET);
}

/**
 * s5p_hash_write_ctrl() - prepare HASH block in SecSS for processing
 * @dd:		secss device
 * @length:	length for request
 * @final:	true if final op
 *
 * Prepare SSS HASH block for processing bytes in DMA mode. If it is called
 * after previous updates, fill up IV words. For final, calculate and set
 * lengths for HASH so SecSS can finalize hash. For partial, set SSS HASH
 * length as 2^63 so it will be never reached and set to zero prelow and
 * prehigh.
 *
 * This function does not start DMA transfer.
 */
static void s5p_hash_write_ctrl(struct s5p_aes_dev *dd, size_t length,
				bool final)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(dd->hash_req);
	u32 prelow, prehigh, low, high;
	u32 configflags, swapflags;
	u64 tmplen;

	configflags = ctx->engine | SSS_HASH_INIT_BIT;

	if (likely(ctx->digcnt)) {
		s5p_hash_write_ctx_iv(dd, ctx);
		configflags |= SSS_HASH_USER_IV_EN;
	}

	if (final) {
		/* number of bytes for last part */
		low = length;
		high = 0;
		/* total number of bits prev hashed */
		tmplen = ctx->digcnt * 8;
		prelow = (u32)tmplen;
		prehigh = (u32)(tmplen >> 32);
	} else {
		prelow = 0;
		prehigh = 0;
		low = 0;
		high = BIT(31);
	}

	swapflags = SSS_HASH_BYTESWAP_DI | SSS_HASH_BYTESWAP_DO |
		    SSS_HASH_BYTESWAP_IV | SSS_HASH_BYTESWAP_KEY;

	s5p_hash_write(dd, SSS_REG_HASH_MSG_SIZE_LOW, low);
	s5p_hash_write(dd, SSS_REG_HASH_MSG_SIZE_HIGH, high);
	s5p_hash_write(dd, SSS_REG_HASH_PRE_MSG_SIZE_LOW, prelow);
	s5p_hash_write(dd, SSS_REG_HASH_PRE_MSG_SIZE_HIGH, prehigh);

	s5p_hash_write(dd, SSS_REG_HASH_CTRL_SWAP, swapflags);
	s5p_hash_write(dd, SSS_REG_HASH_CTRL, configflags);
}

/**
 * s5p_hash_xmit_dma() - start DMA hash processing
 * @dd:		secss device
 * @length:	length for request
 * @final:	true if final op
 *
 * Update digcnt here, as it is needed for finup/final op.
 */
static int s5p_hash_xmit_dma(struct s5p_aes_dev *dd, size_t length,
			     bool final)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(dd->hash_req);
	unsigned int cnt;

	cnt = dma_map_sg(dd->dev, ctx->sg, ctx->sg_len, DMA_TO_DEVICE);
	if (!cnt) {
		dev_err(dd->dev, "dma_map_sg error\n");
		ctx->error = true;
		return -EINVAL;
	}

	set_bit(HASH_FLAGS_DMA_ACTIVE, &dd->hash_flags);
	dd->hash_sg_iter = ctx->sg;
	dd->hash_sg_cnt = cnt;
	s5p_hash_write_ctrl(dd, length, final);
	ctx->digcnt += length;
	ctx->total -= length;

	/* catch last interrupt */
	if (final)
		set_bit(HASH_FLAGS_FINAL, &dd->hash_flags);

	s5p_set_dma_hashdata(dd, dd->hash_sg_iter); /* DMA starts */

	return -EINPROGRESS;
}

/**
 * s5p_hash_copy_sgs() - copy request's bytes into new buffer
 * @ctx:	request context
 * @sg:		source scatterlist request
 * @new_len:	number of bytes to process from sg
 *
 * Allocate new buffer, copy data for HASH into it. If there was xmit_buf
 * filled, copy it first, then copy data from sg into it. Prepare one sgl[0]
 * with allocated buffer.
 *
 * Set bit in dd->hash_flag so we can free it after irq ends processing.
 */
static int s5p_hash_copy_sgs(struct s5p_hash_reqctx *ctx,
			     struct scatterlist *sg, unsigned int new_len)
{
	unsigned int pages, len;
	void *buf;

	len = new_len + ctx->bufcnt;
	pages = get_order(len);

	buf = (void *)__get_free_pages(GFP_ATOMIC, pages);
	if (!buf) {
		dev_err(ctx->dd->dev, "alloc pages for unaligned case.\n");
		ctx->error = true;
		return -ENOMEM;
	}

	if (ctx->bufcnt)
		memcpy(buf, ctx->dd->xmit_buf, ctx->bufcnt);

	scatterwalk_map_and_copy(buf + ctx->bufcnt, sg, ctx->skip,
				 new_len, 0);
	sg_init_table(ctx->sgl, 1);
	sg_set_buf(ctx->sgl, buf, len);
	ctx->sg = ctx->sgl;
	ctx->sg_len = 1;
	ctx->bufcnt = 0;
	ctx->skip = 0;
	set_bit(HASH_FLAGS_SGS_COPIED, &ctx->dd->hash_flags);

	return 0;
}

/**
 * s5p_hash_copy_sg_lists() - copy sg list and make fixes in copy
 * @ctx:	request context
 * @sg:		source scatterlist request
 * @new_len:	number of bytes to process from sg
 *
 * Allocate new scatterlist table, copy data for HASH into it. If there was
 * xmit_buf filled, prepare it first, then copy page, length and offset from
 * source sg into it, adjusting begin and/or end for skip offset and
 * hash_later value.
 *
 * Resulting sg table will be assigned to ctx->sg. Set flag so we can free
 * it after irq ends processing.
 */
static int s5p_hash_copy_sg_lists(struct s5p_hash_reqctx *ctx,
				  struct scatterlist *sg, unsigned int new_len)
{
	unsigned int skip = ctx->skip, n = sg_nents(sg);
	struct scatterlist *tmp;
	unsigned int len;

	if (ctx->bufcnt)
		n++;

	ctx->sg = kmalloc_array(n, sizeof(*sg), GFP_KERNEL);
	if (!ctx->sg) {
		ctx->error = true;
		return -ENOMEM;
	}

	sg_init_table(ctx->sg, n);

	tmp = ctx->sg;

	ctx->sg_len = 0;

	if (ctx->bufcnt) {
		sg_set_buf(tmp, ctx->dd->xmit_buf, ctx->bufcnt);
		tmp = sg_next(tmp);
		ctx->sg_len++;
	}

	while (sg && skip >= sg->length) {
		skip -= sg->length;
		sg = sg_next(sg);
	}

	while (sg && new_len) {
		len = sg->length - skip;
		if (new_len < len)
			len = new_len;

		new_len -= len;
		sg_set_page(tmp, sg_page(sg), len, sg->offset + skip);
		skip = 0;
		if (new_len <= 0)
			sg_mark_end(tmp);

		tmp = sg_next(tmp);
		ctx->sg_len++;
		sg = sg_next(sg);
	}

	set_bit(HASH_FLAGS_SGS_ALLOCED, &ctx->dd->hash_flags);

	return 0;
}

/**
 * s5p_hash_prepare_sgs() - prepare sg for processing
 * @ctx:	request context
 * @sg:		source scatterlist request
 * @nbytes:	number of bytes to process from sg
 * @final:	final flag
 *
 * Check two conditions: (1) if buffers in sg have len aligned data, and (2)
 * sg table have good aligned elements (list_ok). If one of this checks fails,
 * then either (1) allocates new buffer for data with s5p_hash_copy_sgs, copy
 * data into this buffer and prepare request in sgl, or (2) allocates new sg
 * table and prepare sg elements.
 *
 * For digest or finup all conditions can be good, and we may not need any
 * fixes.
 */
static int s5p_hash_prepare_sgs(struct s5p_hash_reqctx *ctx,
				struct scatterlist *sg,
				unsigned int new_len, bool final)
{
	unsigned int skip = ctx->skip, nbytes = new_len, n = 0;
	bool aligned = true, list_ok = true;
	struct scatterlist *sg_tmp = sg;

	if (!sg || !sg->length || !new_len)
		return 0;

	if (skip || !final)
		list_ok = false;

	while (nbytes > 0 && sg_tmp) {
		n++;
		if (skip >= sg_tmp->length) {
			skip -= sg_tmp->length;
			if (!sg_tmp->length) {
				aligned = false;
				break;
			}
		} else {
			if (!IS_ALIGNED(sg_tmp->length - skip, BUFLEN)) {
				aligned = false;
				break;
			}

			if (nbytes < sg_tmp->length - skip) {
				list_ok = false;
				break;
			}

			nbytes -= sg_tmp->length - skip;
			skip = 0;
		}

		sg_tmp = sg_next(sg_tmp);
	}

	if (!aligned)
		return s5p_hash_copy_sgs(ctx, sg, new_len);
	else if (!list_ok)
		return s5p_hash_copy_sg_lists(ctx, sg, new_len);

	/*
	 * Have aligned data from previous operation and/or current
	 * Note: will enter here only if (digest or finup) and aligned
	 */
	if (ctx->bufcnt) {
		ctx->sg_len = n;
		sg_init_table(ctx->sgl, 2);
		sg_set_buf(ctx->sgl, ctx->dd->xmit_buf, ctx->bufcnt);
		sg_chain(ctx->sgl, 2, sg);
		ctx->sg = ctx->sgl;
		ctx->sg_len++;
	} else {
		ctx->sg = sg;
		ctx->sg_len = n;
	}

	return 0;
}

/**
 * s5p_hash_prepare_request() - prepare request for processing
 * @req:	AHASH request
 * @update:	true if UPDATE op
 *
 * Note 1: we can have update flag _and_ final flag at the same time.
 * Note 2: we enter here when digcnt > BUFLEN (=HASH_BLOCK_SIZE) or
 *	   either req->nbytes or ctx->bufcnt + req->nbytes is > BUFLEN or
 *	   we have final op
 */
static int s5p_hash_prepare_request(struct ahash_request *req, bool update)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);
	bool final = ctx->finup;
	int xmit_len, hash_later, nbytes;
	int ret;

	if (update)
		nbytes = req->nbytes;
	else
		nbytes = 0;

	ctx->total = nbytes + ctx->bufcnt;
	if (!ctx->total)
		return 0;

	if (nbytes && (!IS_ALIGNED(ctx->bufcnt, BUFLEN))) {
		/* bytes left from previous request, so fill up to BUFLEN */
		int len = BUFLEN - ctx->bufcnt % BUFLEN;

		if (len > nbytes)
			len = nbytes;

		scatterwalk_map_and_copy(ctx->buffer + ctx->bufcnt, req->src,
					 0, len, 0);
		ctx->bufcnt += len;
		nbytes -= len;
		ctx->skip = len;
	} else {
		ctx->skip = 0;
	}

	if (ctx->bufcnt)
		memcpy(ctx->dd->xmit_buf, ctx->buffer, ctx->bufcnt);

	xmit_len = ctx->total;
	if (final) {
		hash_later = 0;
	} else {
		if (IS_ALIGNED(xmit_len, BUFLEN))
			xmit_len -= BUFLEN;
		else
			xmit_len -= xmit_len & (BUFLEN - 1);

		hash_later = ctx->total - xmit_len;
		/* copy hash_later bytes from end of req->src */
		/* previous bytes are in xmit_buf, so no overwrite */
		scatterwalk_map_and_copy(ctx->buffer, req->src,
					 req->nbytes - hash_later,
					 hash_later, 0);
	}

	if (xmit_len > BUFLEN) {
		ret = s5p_hash_prepare_sgs(ctx, req->src, nbytes - hash_later,
					   final);
		if (ret)
			return ret;
	} else {
		/* have buffered data only */
		if (unlikely(!ctx->bufcnt)) {
			/* first update didn't fill up buffer */
			scatterwalk_map_and_copy(ctx->dd->xmit_buf, req->src,
						 0, xmit_len, 0);
		}

		sg_init_table(ctx->sgl, 1);
		sg_set_buf(ctx->sgl, ctx->dd->xmit_buf, xmit_len);

		ctx->sg = ctx->sgl;
		ctx->sg_len = 1;
	}

	ctx->bufcnt = hash_later;
	if (!final)
		ctx->total = xmit_len;

	return 0;
}

/**
 * s5p_hash_update_dma_stop() - unmap DMA
 * @dd:		secss device
 *
 * Unmap scatterlist ctx->sg.
 */
static void s5p_hash_update_dma_stop(struct s5p_aes_dev *dd)
{
	const struct s5p_hash_reqctx *ctx = ahash_request_ctx(dd->hash_req);

	dma_unmap_sg(dd->dev, ctx->sg, ctx->sg_len, DMA_TO_DEVICE);
	clear_bit(HASH_FLAGS_DMA_ACTIVE, &dd->hash_flags);
}

/**
 * s5p_hash_finish() - copy calculated digest to crypto layer
 * @req:	AHASH request
 */
static void s5p_hash_finish(struct ahash_request *req)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);
	struct s5p_aes_dev *dd = ctx->dd;

	if (ctx->digcnt)
		s5p_hash_copy_result(req);

	dev_dbg(dd->dev, "hash_finish digcnt: %lld\n", ctx->digcnt);
}

/**
 * s5p_hash_finish_req() - finish request
 * @req:	AHASH request
 * @err:	error
 */
static void s5p_hash_finish_req(struct ahash_request *req, int err)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);
	struct s5p_aes_dev *dd = ctx->dd;
	unsigned long flags;

	if (test_bit(HASH_FLAGS_SGS_COPIED, &dd->hash_flags))
		free_pages((unsigned long)sg_virt(ctx->sg),
			   get_order(ctx->sg->length));

	if (test_bit(HASH_FLAGS_SGS_ALLOCED, &dd->hash_flags))
		kfree(ctx->sg);

	ctx->sg = NULL;
	dd->hash_flags &= ~(BIT(HASH_FLAGS_SGS_ALLOCED) |
			    BIT(HASH_FLAGS_SGS_COPIED));

	if (!err && !ctx->error) {
		s5p_hash_read_msg(req);
		if (test_bit(HASH_FLAGS_FINAL, &dd->hash_flags))
			s5p_hash_finish(req);
	} else {
		ctx->error = true;
	}

	spin_lock_irqsave(&dd->hash_lock, flags);
	dd->hash_flags &= ~(BIT(HASH_FLAGS_BUSY) | BIT(HASH_FLAGS_FINAL) |
			    BIT(HASH_FLAGS_DMA_READY) |
			    BIT(HASH_FLAGS_OUTPUT_READY));
	spin_unlock_irqrestore(&dd->hash_lock, flags);

	if (req->base.complete)
		req->base.complete(&req->base, err);
}

/**
 * s5p_hash_handle_queue() - handle hash queue
 * @dd:		device s5p_aes_dev
 * @req:	AHASH request
 *
 * If req!=NULL enqueue it on dd->queue, if FLAGS_BUSY is not set on the
 * device then processes the first request from the dd->queue
 *
 * Returns: see s5p_hash_final below.
 */
static int s5p_hash_handle_queue(struct s5p_aes_dev *dd,
				 struct ahash_request *req)
{
	struct crypto_async_request *async_req, *backlog;
	struct s5p_hash_reqctx *ctx;
	unsigned long flags;
	int err = 0, ret = 0;

retry:
	spin_lock_irqsave(&dd->hash_lock, flags);
	if (req)
		ret = ahash_enqueue_request(&dd->hash_queue, req);

	if (test_bit(HASH_FLAGS_BUSY, &dd->hash_flags)) {
		spin_unlock_irqrestore(&dd->hash_lock, flags);
		return ret;
	}

	backlog = crypto_get_backlog(&dd->hash_queue);
	async_req = crypto_dequeue_request(&dd->hash_queue);
	if (async_req)
		set_bit(HASH_FLAGS_BUSY, &dd->hash_flags);

	spin_unlock_irqrestore(&dd->hash_lock, flags);

	if (!async_req)
		return ret;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	req = ahash_request_cast(async_req);
	dd->hash_req = req;
	ctx = ahash_request_ctx(req);

	err = s5p_hash_prepare_request(req, ctx->op_update);
	if (err || !ctx->total)
		goto out;

	dev_dbg(dd->dev, "handling new req, op_update: %u, nbytes: %d\n",
		ctx->op_update, req->nbytes);

	s5p_ahash_dma_init(dd, SSS_HASHIN_INDEPENDENT);
	if (ctx->digcnt)
		s5p_hash_write_iv(req); /* restore hash IV */

	if (ctx->op_update) { /* HASH_OP_UPDATE */
		err = s5p_hash_xmit_dma(dd, ctx->total, ctx->finup);
		if (err != -EINPROGRESS && ctx->finup && !ctx->error)
			/* no final() after finup() */
			err = s5p_hash_xmit_dma(dd, ctx->total, true);
	} else { /* HASH_OP_FINAL */
		err = s5p_hash_xmit_dma(dd, ctx->total, true);
	}
out:
	if (err != -EINPROGRESS) {
		/* hash_tasklet_cb will not finish it, so do it here */
		s5p_hash_finish_req(req, err);
		req = NULL;

		/*
		 * Execute next request immediately if there is anything
		 * in queue.
		 */
		goto retry;
	}

	return ret;
}

/**
 * s5p_hash_tasklet_cb() - hash tasklet
 * @data:	ptr to s5p_aes_dev
 */
static void s5p_hash_tasklet_cb(unsigned long data)
{
	struct s5p_aes_dev *dd = (struct s5p_aes_dev *)data;

	if (!test_bit(HASH_FLAGS_BUSY, &dd->hash_flags)) {
		s5p_hash_handle_queue(dd, NULL);
		return;
	}

	if (test_bit(HASH_FLAGS_DMA_READY, &dd->hash_flags)) {
		if (test_and_clear_bit(HASH_FLAGS_DMA_ACTIVE,
				       &dd->hash_flags)) {
			s5p_hash_update_dma_stop(dd);
		}

		if (test_and_clear_bit(HASH_FLAGS_OUTPUT_READY,
				       &dd->hash_flags)) {
			/* hash or semi-hash ready */
			clear_bit(HASH_FLAGS_DMA_READY, &dd->hash_flags);
			goto finish;
		}
	}

	return;

finish:
	/* finish curent request */
	s5p_hash_finish_req(dd->hash_req, 0);

	/* If we are not busy, process next req */
	if (!test_bit(HASH_FLAGS_BUSY, &dd->hash_flags))
		s5p_hash_handle_queue(dd, NULL);
}

/**
 * s5p_hash_enqueue() - enqueue request
 * @req:	AHASH request
 * @op:		operation UPDATE (true) or FINAL (false)
 *
 * Returns: see s5p_hash_final below.
 */
static int s5p_hash_enqueue(struct ahash_request *req, bool op)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);
	struct s5p_hash_ctx *tctx = crypto_tfm_ctx(req->base.tfm);

	ctx->op_update = op;

	return s5p_hash_handle_queue(tctx->dd, req);
}

/**
 * s5p_hash_update() - process the hash input data
 * @req:	AHASH request
 *
 * If request will fit in buffer, copy it and return immediately
 * else enqueue it with OP_UPDATE.
 *
 * Returns: see s5p_hash_final below.
 */
static int s5p_hash_update(struct ahash_request *req)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);

	if (!req->nbytes)
		return 0;

	if (ctx->bufcnt + req->nbytes <= BUFLEN) {
		scatterwalk_map_and_copy(ctx->buffer + ctx->bufcnt, req->src,
					 0, req->nbytes, 0);
		ctx->bufcnt += req->nbytes;
		return 0;
	}

	return s5p_hash_enqueue(req, true); /* HASH_OP_UPDATE */
}

/**
 * s5p_hash_final() - close up hash and calculate digest
 * @req:	AHASH request
 *
 * Note: in final req->src do not have any data, and req->nbytes can be
 * non-zero.
 *
 * If there were no input data processed yet and the buffered hash data is
 * less than BUFLEN (64) then calculate the final hash immediately by using
 * SW algorithm fallback.
 *
 * Otherwise enqueues the current AHASH request with OP_FINAL operation op
 * and finalize hash message in HW. Note that if digcnt!=0 then there were
 * previous update op, so there are always some buffered bytes in ctx->buffer,
 * which means that ctx->bufcnt!=0
 *
 * Returns:
 * 0 if the request has been processed immediately,
 * -EINPROGRESS if the operation has been queued for later execution or is set
 *		to processing by HW,
 * -EBUSY if queue is full and request should be resubmitted later,
 * other negative values denotes an error.
 */
static int s5p_hash_final(struct ahash_request *req)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);

	ctx->finup = true;
	if (ctx->error)
		return -EINVAL; /* uncompleted hash is not needed */

	if (!ctx->digcnt && ctx->bufcnt < BUFLEN) {
		struct s5p_hash_ctx *tctx = crypto_tfm_ctx(req->base.tfm);

		return crypto_shash_tfm_digest(tctx->fallback, ctx->buffer,
					       ctx->bufcnt, req->result);
	}

	return s5p_hash_enqueue(req, false); /* HASH_OP_FINAL */
}

/**
 * s5p_hash_finup() - process last req->src and calculate digest
 * @req:	AHASH request containing the last update data
 *
 * Return values: see s5p_hash_final above.
 */
static int s5p_hash_finup(struct ahash_request *req)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);
	int err1, err2;

	ctx->finup = true;

	err1 = s5p_hash_update(req);
	if (err1 == -EINPROGRESS || err1 == -EBUSY)
		return err1;

	/*
	 * final() has to be always called to cleanup resources even if
	 * update() failed, except EINPROGRESS or calculate digest for small
	 * size
	 */
	err2 = s5p_hash_final(req);

	return err1 ?: err2;
}

/**
 * s5p_hash_init() - initialize AHASH request contex
 * @req:	AHASH request
 *
 * Init async hash request context.
 */
static int s5p_hash_init(struct ahash_request *req)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct s5p_hash_ctx *tctx = crypto_ahash_ctx(tfm);

	ctx->dd = tctx->dd;
	ctx->error = false;
	ctx->finup = false;
	ctx->bufcnt = 0;
	ctx->digcnt = 0;
	ctx->total = 0;
	ctx->skip = 0;

	dev_dbg(tctx->dd->dev, "init: digest size: %d\n",
		crypto_ahash_digestsize(tfm));

	switch (crypto_ahash_digestsize(tfm)) {
	case MD5_DIGEST_SIZE:
		ctx->engine = SSS_HASH_ENGINE_MD5;
		ctx->nregs = HASH_MD5_MAX_REG;
		break;
	case SHA1_DIGEST_SIZE:
		ctx->engine = SSS_HASH_ENGINE_SHA1;
		ctx->nregs = HASH_SHA1_MAX_REG;
		break;
	case SHA256_DIGEST_SIZE:
		ctx->engine = SSS_HASH_ENGINE_SHA256;
		ctx->nregs = HASH_SHA256_MAX_REG;
		break;
	default:
		ctx->error = true;
		return -EINVAL;
	}

	return 0;
}

/**
 * s5p_hash_digest - calculate digest from req->src
 * @req:	AHASH request
 *
 * Return values: see s5p_hash_final above.
 */
static int s5p_hash_digest(struct ahash_request *req)
{
	return s5p_hash_init(req) ?: s5p_hash_finup(req);
}

/**
 * s5p_hash_cra_init_alg - init crypto alg transformation
 * @tfm:	crypto transformation
 */
static int s5p_hash_cra_init_alg(struct crypto_tfm *tfm)
{
	struct s5p_hash_ctx *tctx = crypto_tfm_ctx(tfm);
	const char *alg_name = crypto_tfm_alg_name(tfm);

	tctx->dd = s5p_dev;
	/* Allocate a fallback and abort if it failed. */
	tctx->fallback = crypto_alloc_shash(alg_name, 0,
					    CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(tctx->fallback)) {
		pr_err("fallback alloc fails for '%s'\n", alg_name);
		return PTR_ERR(tctx->fallback);
	}

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct s5p_hash_reqctx) + BUFLEN);

	return 0;
}

/**
 * s5p_hash_cra_init - init crypto tfm
 * @tfm:	crypto transformation
 */
static int s5p_hash_cra_init(struct crypto_tfm *tfm)
{
	return s5p_hash_cra_init_alg(tfm);
}

/**
 * s5p_hash_cra_exit - exit crypto tfm
 * @tfm:	crypto transformation
 *
 * free allocated fallback
 */
static void s5p_hash_cra_exit(struct crypto_tfm *tfm)
{
	struct s5p_hash_ctx *tctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(tctx->fallback);
	tctx->fallback = NULL;
}

/**
 * s5p_hash_export - export hash state
 * @req:	AHASH request
 * @out:	buffer for exported state
 */
static int s5p_hash_export(struct ahash_request *req, void *out)
{
	const struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);

	memcpy(out, ctx, sizeof(*ctx) + ctx->bufcnt);

	return 0;
}

/**
 * s5p_hash_import - import hash state
 * @req:	AHASH request
 * @in:		buffer with state to be imported from
 */
static int s5p_hash_import(struct ahash_request *req, const void *in)
{
	struct s5p_hash_reqctx *ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct s5p_hash_ctx *tctx = crypto_ahash_ctx(tfm);
	const struct s5p_hash_reqctx *ctx_in = in;

	memcpy(ctx, in, sizeof(*ctx) + BUFLEN);
	if (ctx_in->bufcnt > BUFLEN) {
		ctx->error = true;
		return -EINVAL;
	}

	ctx->dd = tctx->dd;
	ctx->error = false;

	return 0;
}

static struct ahash_alg algs_sha1_md5_sha256[] = {
{
	.init		= s5p_hash_init,
	.update		= s5p_hash_update,
	.final		= s5p_hash_final,
	.finup		= s5p_hash_finup,
	.digest		= s5p_hash_digest,
	.export		= s5p_hash_export,
	.import		= s5p_hash_import,
	.halg.statesize = sizeof(struct s5p_hash_reqctx) + BUFLEN,
	.halg.digestsize	= SHA1_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "sha1",
		.cra_driver_name	= "exynos-sha1",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
					  CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= HASH_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct s5p_hash_ctx),
		.cra_alignmask		= SSS_HASH_DMA_ALIGN_MASK,
		.cra_module		= THIS_MODULE,
		.cra_init		= s5p_hash_cra_init,
		.cra_exit		= s5p_hash_cra_exit,
	}
},
{
	.init		= s5p_hash_init,
	.update		= s5p_hash_update,
	.final		= s5p_hash_final,
	.finup		= s5p_hash_finup,
	.digest		= s5p_hash_digest,
	.export		= s5p_hash_export,
	.import		= s5p_hash_import,
	.halg.statesize = sizeof(struct s5p_hash_reqctx) + BUFLEN,
	.halg.digestsize	= MD5_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "md5",
		.cra_driver_name	= "exynos-md5",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
					  CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= HASH_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct s5p_hash_ctx),
		.cra_alignmask		= SSS_HASH_DMA_ALIGN_MASK,
		.cra_module		= THIS_MODULE,
		.cra_init		= s5p_hash_cra_init,
		.cra_exit		= s5p_hash_cra_exit,
	}
},
{
	.init		= s5p_hash_init,
	.update		= s5p_hash_update,
	.final		= s5p_hash_final,
	.finup		= s5p_hash_finup,
	.digest		= s5p_hash_digest,
	.export		= s5p_hash_export,
	.import		= s5p_hash_import,
	.halg.statesize = sizeof(struct s5p_hash_reqctx) + BUFLEN,
	.halg.digestsize	= SHA256_DIGEST_SIZE,
	.halg.base	= {
		.cra_name		= "sha256",
		.cra_driver_name	= "exynos-sha256",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
					  CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= HASH_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct s5p_hash_ctx),
		.cra_alignmask		= SSS_HASH_DMA_ALIGN_MASK,
		.cra_module		= THIS_MODULE,
		.cra_init		= s5p_hash_cra_init,
		.cra_exit		= s5p_hash_cra_exit,
	}
}

};

static void s5p_set_aes(struct s5p_aes_dev *dev,
			const u8 *key, const u8 *iv, const u8 *ctr,
			unsigned int keylen)
{
	void __iomem *keystart;

	if (iv)
		memcpy_toio(dev->aes_ioaddr + SSS_REG_AES_IV_DATA(0), iv,
			    AES_BLOCK_SIZE);

	if (ctr)
		memcpy_toio(dev->aes_ioaddr + SSS_REG_AES_CNT_DATA(0), ctr,
			    AES_BLOCK_SIZE);

	if (keylen == AES_KEYSIZE_256)
		keystart = dev->aes_ioaddr + SSS_REG_AES_KEY_DATA(0);
	else if (keylen == AES_KEYSIZE_192)
		keystart = dev->aes_ioaddr + SSS_REG_AES_KEY_DATA(2);
	else
		keystart = dev->aes_ioaddr + SSS_REG_AES_KEY_DATA(4);

	memcpy_toio(keystart, key, keylen);
}

static bool s5p_is_sg_aligned(struct scatterlist *sg)
{
	while (sg) {
		if (!IS_ALIGNED(sg->length, AES_BLOCK_SIZE))
			return false;
		sg = sg_next(sg);
	}

	return true;
}

static int s5p_set_indata_start(struct s5p_aes_dev *dev,
				struct skcipher_request *req)
{
	struct scatterlist *sg;
	int err;

	dev->sg_src_cpy = NULL;
	sg = req->src;
	if (!s5p_is_sg_aligned(sg)) {
		dev_dbg(dev->dev,
			"At least one unaligned source scatter list, making a copy\n");
		err = s5p_make_sg_cpy(dev, sg, &dev->sg_src_cpy);
		if (err)
			return err;

		sg = dev->sg_src_cpy;
	}

	err = s5p_set_indata(dev, sg);
	if (err) {
		s5p_free_sg_cpy(dev, &dev->sg_src_cpy);
		return err;
	}

	return 0;
}

static int s5p_set_outdata_start(struct s5p_aes_dev *dev,
				 struct skcipher_request *req)
{
	struct scatterlist *sg;
	int err;

	dev->sg_dst_cpy = NULL;
	sg = req->dst;
	if (!s5p_is_sg_aligned(sg)) {
		dev_dbg(dev->dev,
			"At least one unaligned dest scatter list, making a copy\n");
		err = s5p_make_sg_cpy(dev, sg, &dev->sg_dst_cpy);
		if (err)
			return err;

		sg = dev->sg_dst_cpy;
	}

	err = s5p_set_outdata(dev, sg);
	if (err) {
		s5p_free_sg_cpy(dev, &dev->sg_dst_cpy);
		return err;
	}

	return 0;
}

static void s5p_aes_crypt_start(struct s5p_aes_dev *dev, unsigned long mode)
{
	struct skcipher_request *req = dev->req;
	u32 aes_control;
	unsigned long flags;
	int err;
	u8 *iv, *ctr;

	/* This sets bit [13:12] to 00, which selects 128-bit counter */
	aes_control = SSS_AES_KEY_CHANGE_MODE;
	if (mode & FLAGS_AES_DECRYPT)
		aes_control |= SSS_AES_MODE_DECRYPT;

	if ((mode & FLAGS_AES_MODE_MASK) == FLAGS_AES_CBC) {
		aes_control |= SSS_AES_CHAIN_MODE_CBC;
		iv = req->iv;
		ctr = NULL;
	} else if ((mode & FLAGS_AES_MODE_MASK) == FLAGS_AES_CTR) {
		aes_control |= SSS_AES_CHAIN_MODE_CTR;
		iv = NULL;
		ctr = req->iv;
	} else {
		iv = NULL; /* AES_ECB */
		ctr = NULL;
	}

	if (dev->ctx->keylen == AES_KEYSIZE_192)
		aes_control |= SSS_AES_KEY_SIZE_192;
	else if (dev->ctx->keylen == AES_KEYSIZE_256)
		aes_control |= SSS_AES_KEY_SIZE_256;

	aes_control |= SSS_AES_FIFO_MODE;

	/* as a variant it is possible to use byte swapping on DMA side */
	aes_control |= SSS_AES_BYTESWAP_DI
		    |  SSS_AES_BYTESWAP_DO
		    |  SSS_AES_BYTESWAP_IV
		    |  SSS_AES_BYTESWAP_KEY
		    |  SSS_AES_BYTESWAP_CNT;

	spin_lock_irqsave(&dev->lock, flags);

	SSS_WRITE(dev, FCINTENCLR,
		  SSS_FCINTENCLR_BTDMAINTENCLR | SSS_FCINTENCLR_BRDMAINTENCLR);
	SSS_WRITE(dev, FCFIFOCTRL, 0x00);

	err = s5p_set_indata_start(dev, req);
	if (err)
		goto indata_error;

	err = s5p_set_outdata_start(dev, req);
	if (err)
		goto outdata_error;

	SSS_AES_WRITE(dev, AES_CONTROL, aes_control);
	s5p_set_aes(dev, dev->ctx->aes_key, iv, ctr, dev->ctx->keylen);

	s5p_set_dma_indata(dev,  dev->sg_src);
	s5p_set_dma_outdata(dev, dev->sg_dst);

	SSS_WRITE(dev, FCINTENSET,
		  SSS_FCINTENSET_BTDMAINTENSET | SSS_FCINTENSET_BRDMAINTENSET);

	spin_unlock_irqrestore(&dev->lock, flags);

	return;

outdata_error:
	s5p_unset_indata(dev);

indata_error:
	s5p_sg_done(dev);
	dev->busy = false;
	spin_unlock_irqrestore(&dev->lock, flags);
	s5p_aes_complete(req, err);
}

static void s5p_tasklet_cb(unsigned long data)
{
	struct s5p_aes_dev *dev = (struct s5p_aes_dev *)data;
	struct crypto_async_request *async_req, *backlog;
	struct s5p_aes_reqctx *reqctx;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	backlog   = crypto_get_backlog(&dev->queue);
	async_req = crypto_dequeue_request(&dev->queue);

	if (!async_req) {
		dev->busy = false;
		spin_unlock_irqrestore(&dev->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	dev->req = skcipher_request_cast(async_req);
	dev->ctx = crypto_tfm_ctx(dev->req->base.tfm);
	reqctx   = skcipher_request_ctx(dev->req);

	s5p_aes_crypt_start(dev, reqctx->mode);
}

static int s5p_aes_handle_req(struct s5p_aes_dev *dev,
			      struct skcipher_request *req)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&dev->lock, flags);
	err = crypto_enqueue_request(&dev->queue, &req->base);
	if (dev->busy) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return err;
	}
	dev->busy = true;

	spin_unlock_irqrestore(&dev->lock, flags);

	tasklet_schedule(&dev->tasklet);

	return err;
}

static int s5p_aes_crypt(struct skcipher_request *req, unsigned long mode)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s5p_aes_reqctx *reqctx = skcipher_request_ctx(req);
	struct s5p_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct s5p_aes_dev *dev = ctx->dev;

	if (!req->cryptlen)
		return 0;

	if (!IS_ALIGNED(req->cryptlen, AES_BLOCK_SIZE) &&
			((mode & FLAGS_AES_MODE_MASK) != FLAGS_AES_CTR)) {
		dev_dbg(dev->dev, "request size is not exact amount of AES blocks\n");
		return -EINVAL;
	}

	reqctx->mode = mode;

	return s5p_aes_handle_req(dev, req);
}

static int s5p_aes_setkey(struct crypto_skcipher *cipher,
			  const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct s5p_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 &&
	    keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;

	memcpy(ctx->aes_key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int s5p_aes_ecb_encrypt(struct skcipher_request *req)
{
	return s5p_aes_crypt(req, 0);
}

static int s5p_aes_ecb_decrypt(struct skcipher_request *req)
{
	return s5p_aes_crypt(req, FLAGS_AES_DECRYPT);
}

static int s5p_aes_cbc_encrypt(struct skcipher_request *req)
{
	return s5p_aes_crypt(req, FLAGS_AES_CBC);
}

static int s5p_aes_cbc_decrypt(struct skcipher_request *req)
{
	return s5p_aes_crypt(req, FLAGS_AES_DECRYPT | FLAGS_AES_CBC);
}

static int s5p_aes_ctr_crypt(struct skcipher_request *req)
{
	return s5p_aes_crypt(req, FLAGS_AES_CTR);
}

static int s5p_aes_init_tfm(struct crypto_skcipher *tfm)
{
	struct s5p_aes_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->dev = s5p_dev;
	crypto_skcipher_set_reqsize(tfm, sizeof(struct s5p_aes_reqctx));

	return 0;
}

static struct skcipher_alg algs[] = {
	{
		.base.cra_name		= "ecb(aes)",
		.base.cra_driver_name	= "ecb-aes-s5p",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_KERN_DRIVER_ONLY,
		.base.cra_blocksize	= AES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct s5p_aes_ctx),
		.base.cra_alignmask	= 0x0f,
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
		.setkey			= s5p_aes_setkey,
		.encrypt		= s5p_aes_ecb_encrypt,
		.decrypt		= s5p_aes_ecb_decrypt,
		.init			= s5p_aes_init_tfm,
	},
	{
		.base.cra_name		= "cbc(aes)",
		.base.cra_driver_name	= "cbc-aes-s5p",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_KERN_DRIVER_ONLY,
		.base.cra_blocksize	= AES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct s5p_aes_ctx),
		.base.cra_alignmask	= 0x0f,
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
		.ivsize			= AES_BLOCK_SIZE,
		.setkey			= s5p_aes_setkey,
		.encrypt		= s5p_aes_cbc_encrypt,
		.decrypt		= s5p_aes_cbc_decrypt,
		.init			= s5p_aes_init_tfm,
	},
	{
		.base.cra_name		= "ctr(aes)",
		.base.cra_driver_name	= "ctr-aes-s5p",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_KERN_DRIVER_ONLY,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct s5p_aes_ctx),
		.base.cra_alignmask	= 0x0f,
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
		.ivsize			= AES_BLOCK_SIZE,
		.setkey			= s5p_aes_setkey,
		.encrypt		= s5p_aes_ctr_crypt,
		.decrypt		= s5p_aes_ctr_crypt,
		.init			= s5p_aes_init_tfm,
	},
};

static int s5p_aes_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int i, j, err = -ENODEV;
	const struct samsung_aes_variant *variant;
	struct s5p_aes_dev *pdata;
	struct resource *res;
	unsigned int hash_i;

	if (s5p_dev)
		return -EEXIST;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	variant = find_s5p_sss_version(pdev);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	/*
	 * Note: HASH and PRNG uses the same registers in secss, avoid
	 * overwrite each other. This will drop HASH when CONFIG_EXYNOS_RNG
	 * is enabled in config. We need larger size for HASH registers in
	 * secss, current describe only AES/DES
	 */
	if (IS_ENABLED(CONFIG_CRYPTO_DEV_EXYNOS_HASH)) {
		if (variant == &exynos_aes_data) {
			res->end += 0x300;
			pdata->use_hash = true;
		}
	}

	pdata->res = res;
	pdata->ioaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pdata->ioaddr)) {
		if (!pdata->use_hash)
			return PTR_ERR(pdata->ioaddr);
		/* try AES without HASH */
		res->end -= 0x300;
		pdata->use_hash = false;
		pdata->ioaddr = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pdata->ioaddr))
			return PTR_ERR(pdata->ioaddr);
	}

	pdata->clk = devm_clk_get(dev, variant->clk_names[0]);
	if (IS_ERR(pdata->clk)) {
		dev_err(dev, "failed to find secss clock %s\n",
			variant->clk_names[0]);
		return -ENOENT;
	}

	err = clk_prepare_enable(pdata->clk);
	if (err < 0) {
		dev_err(dev, "Enabling clock %s failed, err %d\n",
			variant->clk_names[0], err);
		return err;
	}

	if (variant->clk_names[1]) {
		pdata->pclk = devm_clk_get(dev, variant->clk_names[1]);
		if (IS_ERR(pdata->pclk)) {
			dev_err(dev, "failed to find clock %s\n",
				variant->clk_names[1]);
			err = -ENOENT;
			goto err_clk;
		}

		err = clk_prepare_enable(pdata->pclk);
		if (err < 0) {
			dev_err(dev, "Enabling clock %s failed, err %d\n",
				variant->clk_names[0], err);
			goto err_clk;
		}
	} else {
		pdata->pclk = NULL;
	}

	spin_lock_init(&pdata->lock);
	spin_lock_init(&pdata->hash_lock);

	pdata->aes_ioaddr = pdata->ioaddr + variant->aes_offset;
	pdata->io_hash_base = pdata->ioaddr + variant->hash_offset;

	pdata->irq_fc = platform_get_irq(pdev, 0);
	if (pdata->irq_fc < 0) {
		err = pdata->irq_fc;
		dev_warn(dev, "feed control interrupt is not available.\n");
		goto err_irq;
	}
	err = devm_request_threaded_irq(dev, pdata->irq_fc, NULL,
					s5p_aes_interrupt, IRQF_ONESHOT,
					pdev->name, pdev);
	if (err < 0) {
		dev_warn(dev, "feed control interrupt is not available.\n");
		goto err_irq;
	}

	pdata->busy = false;
	pdata->dev = dev;
	platform_set_drvdata(pdev, pdata);
	s5p_dev = pdata;

	tasklet_init(&pdata->tasklet, s5p_tasklet_cb, (unsigned long)pdata);
	crypto_init_queue(&pdata->queue, CRYPTO_QUEUE_LEN);

	for (i = 0; i < ARRAY_SIZE(algs); i++) {
		err = crypto_register_skcipher(&algs[i]);
		if (err)
			goto err_algs;
	}

	if (pdata->use_hash) {
		tasklet_init(&pdata->hash_tasklet, s5p_hash_tasklet_cb,
			     (unsigned long)pdata);
		crypto_init_queue(&pdata->hash_queue, SSS_HASH_QUEUE_LENGTH);

		for (hash_i = 0; hash_i < ARRAY_SIZE(algs_sha1_md5_sha256);
		     hash_i++) {
			struct ahash_alg *alg;

			alg = &algs_sha1_md5_sha256[hash_i];
			err = crypto_register_ahash(alg);
			if (err) {
				dev_err(dev, "can't register '%s': %d\n",
					alg->halg.base.cra_driver_name, err);
				goto err_hash;
			}
		}
	}

	dev_info(dev, "s5p-sss driver registered\n");

	return 0;

err_hash:
	for (j = hash_i - 1; j >= 0; j--)
		crypto_unregister_ahash(&algs_sha1_md5_sha256[j]);

	tasklet_kill(&pdata->hash_tasklet);
	res->end -= 0x300;

err_algs:
	if (i < ARRAY_SIZE(algs))
		dev_err(dev, "can't register '%s': %d\n", algs[i].base.cra_name,
			err);

	for (j = 0; j < i; j++)
		crypto_unregister_skcipher(&algs[j]);

	tasklet_kill(&pdata->tasklet);

err_irq:
	if (pdata->pclk)
		clk_disable_unprepare(pdata->pclk);

err_clk:
	clk_disable_unprepare(pdata->clk);
	s5p_dev = NULL;

	return err;
}

static int s5p_aes_remove(struct platform_device *pdev)
{
	struct s5p_aes_dev *pdata = platform_get_drvdata(pdev);
	int i;

	if (!pdata)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(algs); i++)
		crypto_unregister_skcipher(&algs[i]);

	tasklet_kill(&pdata->tasklet);
	if (pdata->use_hash) {
		for (i = ARRAY_SIZE(algs_sha1_md5_sha256) - 1; i >= 0; i--)
			crypto_unregister_ahash(&algs_sha1_md5_sha256[i]);

		pdata->res->end -= 0x300;
		tasklet_kill(&pdata->hash_tasklet);
		pdata->use_hash = false;
	}

	if (pdata->pclk)
		clk_disable_unprepare(pdata->pclk);

	clk_disable_unprepare(pdata->clk);
	s5p_dev = NULL;

	return 0;
}

static struct platform_driver s5p_aes_crypto = {
	.probe	= s5p_aes_probe,
	.remove	= s5p_aes_remove,
	.driver	= {
		.name	= "s5p-secss",
		.of_match_table = s5p_sss_dt_match,
	},
};

module_platform_driver(s5p_aes_crypto);

MODULE_DESCRIPTION("S5PV210 AES hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Vladimir Zapolskiy <vzapolskiy@gmail.com>");
MODULE_AUTHOR("Kamil Konieczny <k.konieczny@partner.samsung.com>");
