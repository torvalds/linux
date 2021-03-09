// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 */

#include <asm/cacheflush.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/initramfs.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/soc/rockchip/rockchip_decompress.h>

#define SHA256_PROBE_TIMEOUT		1000
#define SHA256_COMPARE_TIMEOUT		2000
#define SHA256_HASH_SIZE		32
#define _SBF(s, v)			((v) << (s))
#define CRYPTO_WRITE_MASK_SHIFT		(16)
#define CRYPTO_WRITE_MASK_ALL		((0xffffu << CRYPTO_WRITE_MASK_SHIFT))

/* Crypto DMA control registers*/
#define CRYPTO_DMA_INT_EN		0x0008
#define CRYPTO_ZERO_ERR_INT_EN		BIT(6)
#define CRYPTO_LIST_ERR_INT_EN		BIT(5)
#define CRYPTO_SRC_ERR_INT_EN		BIT(4)
#define CRYPTO_DST_ERR_INT_EN		BIT(3)
#define CRYPTO_SRC_ITEM_INT_EN		BIT(2)
#define CRYPTO_DST_ITEM_DONE_INT_EN	BIT(1)
#define CRYPTO_LIST_DONE_INT_EN		BIT(0)

#define CRYPTO_DMA_INT_ST		0x000C
#define CRYPTO_ZERO_LEN_INT_ST		BIT(6)
#define CRYPTO_LIST_ERR_INT_ST		BIT(5)
#define CRYPTO_SRC_ERR_INT_ST		BIT(4)
#define CRYPTO_DST_ERR_INT_ST		BIT(3)
#define CRYPTO_SRC_ITEM_DONE_INT_ST	BIT(2)
#define CRYPTO_DST_ITEM_DONE_INT_ST	BIT(1)
#define CRYPTO_LIST_DONE_INT_ST		BIT(0)

#define CRYPTO_DMA_CTL			0x0010
#define CRYPTO_DMA_RESTART		BIT(1)
#define CRYPTO_DMA_START		BIT(0)

/* DMA LIST Start Address Register */
#define CRYPTO_DMA_LLI_ADDR		0x0014

#define CRYPTO_FIFO_CTL			0x0040
#define CRYPTO_DOUT_BYTESWAP		BIT(1)
#define CRYPTO_DOIN_BYTESWAP		BIT(0)

/* Hash Control Register */
#define CRYPTO_HASH_CTL			0x0048
#define CRYPTO_SHA1			_SBF(4, 0x00)
#define CRYPTO_MD5			_SBF(4, 0x01)
#define CRYPTO_SHA256			_SBF(4, 0x02)
#define CRYPTO_SHA224			_SBF(4, 0x03)
#define CRYPTO_SM3			_SBF(4, 0x06)
#define CRYPTO_SHA512			_SBF(4, 0x08)
#define CRYPTO_SHA384			_SBF(4, 0x09)
#define CRYPTO_SHA512_224		_SBF(4, 0x0A)
#define CRYPTO_SHA512_256		_SBF(4, 0x0B)
#define CRYPTO_HMAC_ENABLE		BIT(3)
#define CRYPTO_HW_PAD_ENABLE		BIT(2)
#define CRYPTO_HASH_SRC_SEL		BIT(1)
#define CRYPTO_HASH_ENABLE		BIT(0)

#define CRYPTO_HASH_DOUT_0		0x03a0
#define CRYPTO_HASH_DOUT_1		0x03a4
#define CRYPTO_HASH_DOUT_2		0x03a8
#define CRYPTO_HASH_DOUT_3		0x03ac
#define CRYPTO_HASH_DOUT_4		0x03b0
#define CRYPTO_HASH_DOUT_5		0x03b4
#define CRYPTO_HASH_DOUT_6		0x03b8
#define CRYPTO_HASH_DOUT_7		0x03bc
#define CRYPTO_HASH_DOUT_8		0x03c0
#define CRYPTO_HASH_DOUT_9		0x03c4
#define CRYPTO_HASH_DOUT_10		0x03c8
#define CRYPTO_HASH_DOUT_11		0x03cc
#define CRYPTO_HASH_DOUT_12		0x03d0
#define CRYPTO_HASH_DOUT_13		0x03d4
#define CRYPTO_HASH_DOUT_14		0x03d8
#define CRYPTO_HASH_DOUT_15		0x03dc

#define CRYPTO_HASH_VALID		0x03e4
#define CRYPTO_HASH_IS_VALID		BIT(0)

#define	LLI_DMA_CTRL_LAST		BIT(0)
#define	LLI_DMA_CTRL_PAUSE		BIT(1)
#define	LLI_DMA_CTRL_LIST_DONE		BIT(8)
#define	LLI_DMA_CTRL_DST_DONE		BIT(9)
#define	LLI_DMA_CTRL_SRC_DONE		BIT(10)

#define LLI_USER_CPIHER_START		BIT(0)
#define LLI_USER_STRING_START		BIT(1)
#define LLI_USER_STRING_LAST		BIT(2)
#define LLI_USER_STRING_ADA		BIT(3)
#define LLI_USER_PRIVACY_KEY		BIT(7)
#define LLI_USER_ROOT_KEY		BIT(8)

#define CRYPTO_READ(dev, offset)		\
		readl_relaxed(((dev)->reg + (offset)))
#define CRYPTO_WRITE(dev, offset, val)	  \
		writel_relaxed((val), ((dev)->reg + (offset)))

#ifdef DEBUG
#define CRYPTO_TRACE(format, ...) pr_err("[%s, %05d]-trace: " format "\n", \
					 __func__, __LINE__, ##__VA_ARGS__)
#define CRYPTO_MSG(format, ...) pr_err("[%s, %05d]-msg:" format "\n", \
				       __func__, __LINE__, ##__VA_ARGS__)
#define CRYPTO_DUMPHEX(var_name, data, len) \
		print_hex_dump(KERN_CONT, (var_name), \
			       DUMP_PREFIX_OFFSET, \
			       16, 1, (data), (len), false)
#else
#define CRYPTO_TRACE(format, ...)
#define CRYPTO_MSG(format, ...)
#define CRYPTO_DUMPHEX(var_name, data, len)
#endif

struct crypto_lli_desc {
	u32 src_addr;
	u32 src_len;
	u32 dst_addr;
	u32 dst_len;
	u32 user_define;
	u32 reserve;
	u32 dma_ctrl;
	u32 next_addr;
};

struct  crypto_data {
	struct device		*dev;
	void __iomem		*reg;
	int			irq;
	int			clks_num;
	struct clk_bulk_data	*clk_bulks;
	struct crypto_lli_desc	*desc;
	dma_addr_t		desc_dma;
	int			calc_ret;
	void			(*done_cb)(void *user_data,
					   int hash_ret,
					   u8 *hash_val);
	void			*cb_data;
	u8			*hash;
};

enum endian_mode {
	BIG_ENDIAN = 0,
	LITTLE_ENDIAN
};

static struct crypto_data *g_crypto_info;
static DECLARE_COMPLETION(sha256_probe_complete);

static DECLARE_WAIT_QUEUE_HEAD(crypto_sha256_compare_done);
static bool compare_done;

int __init rk_tb_crypto_sha256_wait_compare_done(void)
{
	if (wait_event_timeout(crypto_sha256_compare_done, compare_done,
			       SHA256_COMPARE_TIMEOUT))
		return 0;

	return -ETIMEDOUT;
}

static void word2byte(u32 word, u8 *ch, u32 endian)
{
	/* 0: Big-Endian 1: Little-Endian */
	if (endian == BIG_ENDIAN) {
		ch[0] = (word >> 24) & 0xff;
		ch[1] = (word >> 16) & 0xff;
		ch[2] = (word >> 8) & 0xff;
		ch[3] = (word >> 0) & 0xff;
	} else if (endian == LITTLE_ENDIAN) {
		ch[0] = (word >> 0) & 0xff;
		ch[1] = (word >> 8) & 0xff;
		ch[2] = (word >> 16) & 0xff;
		ch[3] = (word >> 24) & 0xff;
	} else {
		ch[0] = 0;
		ch[1] = 0;
		ch[2] = 0;
		ch[3] = 0;
	}
}

static void sha256_done_cb(void *user_data, int hash_ret, u8 *hash_val)
{
	CRYPTO_TRACE();
	if (!memcmp(user_data, hash_val, 32)) {
		compare_done = true;
		wake_up(&crypto_sha256_compare_done);
	}
}

static inline void clear_hash_out_reg(struct crypto_data *dev)
{
	int i;

	/*clear out register*/
	for (i = 0; i < 16; i++)
		CRYPTO_WRITE(dev, CRYPTO_HASH_DOUT_0 + 4 * i, 0);
}

static int get_hash_value(struct crypto_data *dev, u8 *data, u32 data_len)
{
	int ret = 0;
	u32 i, offset;

	offset = CRYPTO_HASH_DOUT_0;
	for (i = 0; i < data_len / 4; i++, offset += 4)
		word2byte(CRYPTO_READ(dev, offset), data + i * 4, BIG_ENDIAN);

	if (data_len % 4) {
		uint8_t tmp_buf[4];

		word2byte(CRYPTO_READ(dev, offset), tmp_buf, BIG_ENDIAN);
		memcpy(data + i * 4, tmp_buf, data_len % 4);
	}

	CRYPTO_WRITE(dev, CRYPTO_HASH_VALID, CRYPTO_HASH_IS_VALID);

	return ret;
}

static void rk_tb_crypto_disable_clk(struct crypto_data *dev)
{
	dev_dbg(dev->dev, "clk_bulk_disable_unprepare.\n");

	clk_bulk_disable_unprepare(dev->clks_num, dev->clk_bulks);
}

static irqreturn_t rk_tb_crypto_irq_handle(int irq, void *dev_id)
{
	struct crypto_data *crypto_info = platform_get_drvdata(dev_id);

	CRYPTO_TRACE("xxxxxxxxxx irq xxxxxxxxxx");

	if (crypto_info) {
		u32 interrupt_status;

		get_hash_value(crypto_info, crypto_info->hash, SHA256_HASH_SIZE);
		CRYPTO_WRITE(crypto_info, CRYPTO_HASH_CTL, CRYPTO_WRITE_MASK_ALL | 0);
		interrupt_status = CRYPTO_READ(crypto_info, CRYPTO_DMA_INT_ST);
		CRYPTO_WRITE(crypto_info, CRYPTO_DMA_INT_ST, interrupt_status);
		if (interrupt_status == CRYPTO_LIST_DONE_INT_ST)
			crypto_info->calc_ret = 0;

		CRYPTO_TRACE("interrupt_status = %08x", interrupt_status);
		if (crypto_info->done_cb)
			crypto_info->done_cb(crypto_info->cb_data,
					     crypto_info->calc_ret,
					     crypto_info->hash);

		rk_tb_crypto_disable_clk(crypto_info);
	}

	return IRQ_HANDLED;
}

int rk_tb_sha256(dma_addr_t data, size_t data_len, void *user_data)
{
	u32 reg_ctrl = 0;
	struct crypto_data *crypto_info;

	wait_for_completion_interruptible_timeout(&sha256_probe_complete,
						  SHA256_PROBE_TIMEOUT);
	crypto_info = g_crypto_info;
	if (!crypto_info)
		return -ENODEV;

	if (data % 4)
		return -EINVAL;

	clear_hash_out_reg(crypto_info);

	reg_ctrl = CRYPTO_SHA256 | CRYPTO_HW_PAD_ENABLE;
	CRYPTO_WRITE(crypto_info, CRYPTO_HASH_CTL,
		     reg_ctrl | CRYPTO_WRITE_MASK_ALL);

	reg_ctrl = CRYPTO_ZERO_ERR_INT_EN |
		   CRYPTO_LIST_ERR_INT_EN |
		   CRYPTO_SRC_ERR_INT_EN |
		   CRYPTO_DST_ERR_INT_EN |
		   CRYPTO_LIST_DONE_INT_EN;

	CRYPTO_WRITE(crypto_info, CRYPTO_FIFO_CTL, 0x00030003);
	CRYPTO_WRITE(crypto_info, CRYPTO_DMA_INT_EN, reg_ctrl);

	memset(crypto_info->desc, 0x00, sizeof(*crypto_info->desc));

	crypto_info->desc->src_addr    = (u32)data;
	crypto_info->desc->src_len     = data_len;
	crypto_info->desc->next_addr   = 0;
	crypto_info->desc->dma_ctrl    = LLI_DMA_CTRL_LIST_DONE |
					 LLI_DMA_CTRL_LAST;
	crypto_info->desc->user_define = LLI_USER_CPIHER_START |
					 LLI_USER_STRING_START |
					 LLI_USER_STRING_LAST;
#ifdef CONFIG_ARM64
	__flush_dcache_area((void *)crypto_info->desc,
			    sizeof(struct crypto_data));
#else
	__cpuc_flush_dcache_area((void *)crypto_info->desc,
				 sizeof(struct crypto_data));
#endif
	CRYPTO_WRITE(crypto_info, CRYPTO_DMA_LLI_ADDR, crypto_info->desc_dma);
	CRYPTO_WRITE(crypto_info, CRYPTO_HASH_CTL,
		     (CRYPTO_HASH_ENABLE <<
		      CRYPTO_WRITE_MASK_SHIFT) |
		      CRYPTO_HASH_ENABLE);

	CRYPTO_WRITE(crypto_info, CRYPTO_DMA_CTL, 0x00010001); /* start */

	crypto_info->calc_ret = -1;

	crypto_info->done_cb = sha256_done_cb;
	crypto_info->cb_data = user_data;
	crypto_info->hash = devm_kzalloc(crypto_info->dev, 32, GFP_KERNEL);
	if (!crypto_info->hash)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(rk_tb_sha256);

static int __init rk_tb_crypto_probe(struct platform_device *pdev)
{
	struct crypto_data *crypto_info;
	struct resource *res;
	int ret = 0;

	CRYPTO_TRACE();

	crypto_info = devm_kzalloc(&pdev->dev, sizeof(*crypto_info),
				   GFP_KERNEL);
	if (!crypto_info)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	crypto_info->reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(crypto_info->reg)) {
		dev_err(crypto_info->dev,
			"devm_ioremap_resource crypto reg error.\n");
		ret = PTR_ERR(crypto_info->reg);
		goto exit;
	}

	crypto_info->dev = &pdev->dev;
	crypto_info->clks_num =
		devm_clk_bulk_get_all(&pdev->dev, &crypto_info->clk_bulks);
	if (crypto_info->clks_num < 0) {
		dev_err(&pdev->dev, "failed to get clks property\n");
		ret = -ENODEV;
		goto exit;
	}

	ret = clk_bulk_prepare_enable(crypto_info->clks_num, crypto_info->clk_bulks);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clks\n");
		goto exit;
	}

	crypto_info->irq = platform_get_irq(pdev, 0);
	if (crypto_info->irq < 0) {
		dev_err(crypto_info->dev,
			"control Interrupt is not available.\n");
		ret = crypto_info->irq;
		goto exit;
	}

	ret = devm_request_irq(&pdev->dev, crypto_info->irq,
			       rk_tb_crypto_irq_handle, IRQF_SHARED,
			       "rk-tb-crypto", pdev);

	if (ret) {
		dev_err(crypto_info->dev, "irq request failed.\n");
		goto exit;
	}

	crypto_info->desc = devm_kzalloc(&pdev->dev, sizeof(struct crypto_data),
					 GFP_KERNEL | GFP_DMA);
	crypto_info->desc_dma = (dma_addr_t)virt_to_phys(crypto_info->desc);
	if (!crypto_info->desc) {
		dev_err(crypto_info->dev, "desc alloc failed.\n");
		ret = -ENOMEM;
		goto exit;
	}

	g_crypto_info = crypto_info;
	platform_set_drvdata(pdev, crypto_info);
	complete(&sha256_probe_complete);
exit:
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id rk_tb_crypto_dt_match[] = {
	{ .compatible = "rockchip,rv1126-crypto" },
	{},
};
#endif

static struct platform_driver rk_tb_crypto_driver = {
	.driver	= {
		.name	= "rockchip_thunder_boot_crypto",
		.of_match_table = rk_tb_crypto_dt_match,
	},
};

static int __init rk_tb_crypto_init(void)
{
	struct device_node *node;

	CRYPTO_TRACE();

	node = of_find_matching_node(NULL, rk_tb_crypto_dt_match);
	if (node) {
		of_platform_device_create(node, NULL, NULL);
		of_node_put(node);
		return platform_driver_probe(&rk_tb_crypto_driver,
					     rk_tb_crypto_probe);
	}

	CRYPTO_TRACE();

	return 0;
}

pure_initcall(rk_tb_crypto_init);
