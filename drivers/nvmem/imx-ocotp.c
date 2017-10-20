/*
 * i.MX6 OCOTP fusebox driver
 *
 * Copyright (c) 2015 Pengutronix, Philipp Zabel <p.zabel@pengutronix.de>
 *
 * Based on the barebox ocotp driver,
 * Copyright (c) 2010 Baruch Siach <baruch@tkos.co.il>,
 *	Orex Computed Radiography
 *
 * Write support based on the fsl_otp driver,
 * Copyright (C) 2010-2013 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>

#define IMX_OCOTP_OFFSET_B0W0		0x400 /* Offset from base address of the
					       * OTP Bank0 Word0
					       */
#define IMX_OCOTP_OFFSET_PER_WORD	0x10  /* Offset between the start addr
					       * of two consecutive OTP words.
					       */

#define IMX_OCOTP_ADDR_CTRL		0x0000
#define IMX_OCOTP_ADDR_CTRL_SET		0x0004
#define IMX_OCOTP_ADDR_CTRL_CLR		0x0008
#define IMX_OCOTP_ADDR_TIMING		0x0010
#define IMX_OCOTP_ADDR_DATA		0x0020

#define IMX_OCOTP_BM_CTRL_ADDR		0x0000007F
#define IMX_OCOTP_BM_CTRL_BUSY		0x00000100
#define IMX_OCOTP_BM_CTRL_ERROR		0x00000200
#define IMX_OCOTP_BM_CTRL_REL_SHADOWS	0x00000400

#define DEF_RELAX			20 /* > 16.5ns */
#define IMX_OCOTP_WR_UNLOCK		0x3E770000
#define IMX_OCOTP_READ_LOCKED_VAL	0xBADABADA

static DEFINE_MUTEX(ocotp_mutex);

struct ocotp_priv {
	struct device *dev;
	struct clk *clk;
	void __iomem *base;
	unsigned int nregs;
	struct nvmem_config *config;
};

static int imx_ocotp_wait_for_busy(void __iomem *base, u32 flags)
{
	int count;
	u32 c, mask;

	mask = IMX_OCOTP_BM_CTRL_BUSY | IMX_OCOTP_BM_CTRL_ERROR | flags;

	for (count = 10000; count >= 0; count--) {
		c = readl(base + IMX_OCOTP_ADDR_CTRL);
		if (!(c & mask))
			break;
		cpu_relax();
	}

	if (count < 0) {
		/* HW_OCOTP_CTRL[ERROR] will be set under the following
		 * conditions:
		 * - A write is performed to a shadow register during a shadow
		 *   reload (essentially, while HW_OCOTP_CTRL[RELOAD_SHADOWS] is
		 *   set. In addition, the contents of the shadow register shall
		 *   not be updated.
		 * - A write is performed to a shadow register which has been
		 *   locked.
		 * - A read is performed to from a shadow register which has
		 *   been read locked.
		 * - A program is performed to a fuse word which has been locked
		 * - A read is performed to from a fuse word which has been read
		 *   locked.
		 */
		if (c & IMX_OCOTP_BM_CTRL_ERROR)
			return -EPERM;
		return -ETIMEDOUT;
	}

	return 0;
}

static void imx_ocotp_clr_err_if_set(void __iomem *base)
{
	u32 c;

	c = readl(base + IMX_OCOTP_ADDR_CTRL);
	if (!(c & IMX_OCOTP_BM_CTRL_ERROR))
		return;

	writel(IMX_OCOTP_BM_CTRL_ERROR, base + IMX_OCOTP_ADDR_CTRL_CLR);
}

static int imx_ocotp_read(void *context, unsigned int offset,
			  void *val, size_t bytes)
{
	struct ocotp_priv *priv = context;
	unsigned int count;
	u32 *buf = val;
	int i, ret;
	u32 index;

	index = offset >> 2;
	count = bytes >> 2;

	if (count > (priv->nregs - index))
		count = priv->nregs - index;

	mutex_lock(&ocotp_mutex);

	ret = clk_prepare_enable(priv->clk);
	if (ret < 0) {
		mutex_unlock(&ocotp_mutex);
		dev_err(priv->dev, "failed to prepare/enable ocotp clk\n");
		return ret;
	}

	ret = imx_ocotp_wait_for_busy(priv->base, 0);
	if (ret < 0) {
		dev_err(priv->dev, "timeout during read setup\n");
		goto read_end;
	}

	for (i = index; i < (index + count); i++) {
		*buf++ = readl(priv->base + IMX_OCOTP_OFFSET_B0W0 +
			       i * IMX_OCOTP_OFFSET_PER_WORD);

		/* 47.3.1.2
		 * For "read locked" registers 0xBADABADA will be returned and
		 * HW_OCOTP_CTRL[ERROR] will be set. It must be cleared by
		 * software before any new write, read or reload access can be
		 * issued
		 */
		if (*(buf - 1) == IMX_OCOTP_READ_LOCKED_VAL)
			imx_ocotp_clr_err_if_set(priv->base);
	}
	ret = 0;

read_end:
	clk_disable_unprepare(priv->clk);
	mutex_unlock(&ocotp_mutex);
	return ret;
}

static int imx_ocotp_write(void *context, unsigned int offset, void *val,
			   size_t bytes)
{
	struct ocotp_priv *priv = context;
	u32 *buf = val;
	int ret;

	unsigned long clk_rate = 0;
	unsigned long strobe_read, relax, strobe_prog;
	u32 timing = 0;
	u32 ctrl;
	u8 waddr;

	/* allow only writing one complete OTP word at a time */
	if ((bytes != priv->config->word_size) ||
	    (offset % priv->config->word_size))
		return -EINVAL;

	mutex_lock(&ocotp_mutex);

	ret = clk_prepare_enable(priv->clk);
	if (ret < 0) {
		mutex_unlock(&ocotp_mutex);
		dev_err(priv->dev, "failed to prepare/enable ocotp clk\n");
		return ret;
	}

	/* 47.3.1.3.1
	 * Program HW_OCOTP_TIMING[STROBE_PROG] and HW_OCOTP_TIMING[RELAX]
	 * fields with timing values to match the current frequency of the
	 * ipg_clk. OTP writes will work at maximum bus frequencies as long
	 * as the HW_OCOTP_TIMING parameters are set correctly.
	 */
	clk_rate = clk_get_rate(priv->clk);

	relax = clk_rate / (1000000000 / DEF_RELAX) - 1;
	strobe_prog = clk_rate / (1000000000 / 10000) + 2 * (DEF_RELAX + 1) - 1;
	strobe_read = clk_rate / (1000000000 / 40) + 2 * (DEF_RELAX + 1) - 1;

	timing = strobe_prog & 0x00000FFF;
	timing |= (relax       << 12) & 0x0000F000;
	timing |= (strobe_read << 16) & 0x003F0000;

	writel(timing, priv->base + IMX_OCOTP_ADDR_TIMING);

	/* 47.3.1.3.2
	 * Check that HW_OCOTP_CTRL[BUSY] and HW_OCOTP_CTRL[ERROR] are clear.
	 * Overlapped accesses are not supported by the controller. Any pending
	 * write or reload must be completed before a write access can be
	 * requested.
	 */
	ret = imx_ocotp_wait_for_busy(priv->base, 0);
	if (ret < 0) {
		dev_err(priv->dev, "timeout during timing setup\n");
		goto write_end;
	}

	/* 47.3.1.3.3
	 * Write the requested address to HW_OCOTP_CTRL[ADDR] and program the
	 * unlock code into HW_OCOTP_CTRL[WR_UNLOCK]. This must be programmed
	 * for each write access. The lock code is documented in the register
	 * description. Both the unlock code and address can be written in the
	 * same operation.
	 */
	/* OTP write/read address specifies one of 128 word address locations */
	waddr = offset / 4;

	ctrl = readl(priv->base + IMX_OCOTP_ADDR_CTRL);
	ctrl &= ~IMX_OCOTP_BM_CTRL_ADDR;
	ctrl |= waddr & IMX_OCOTP_BM_CTRL_ADDR;
	ctrl |= IMX_OCOTP_WR_UNLOCK;

	writel(ctrl, priv->base + IMX_OCOTP_ADDR_CTRL);

	/* 47.3.1.3.4
	 * Write the data to the HW_OCOTP_DATA register. This will automatically
	 * set HW_OCOTP_CTRL[BUSY] and clear HW_OCOTP_CTRL[WR_UNLOCK]. To
	 * protect programming same OTP bit twice, before program OCOTP will
	 * automatically read fuse value in OTP and use read value to mask
	 * program data. The controller will use masked program data to program
	 * a 32-bit word in the OTP per the address in HW_OCOTP_CTRL[ADDR]. Bit
	 * fields with 1's will result in that OTP bit being programmed. Bit
	 * fields with 0's will be ignored. At the same time that the write is
	 * accepted, the controller makes an internal copy of
	 * HW_OCOTP_CTRL[ADDR] which cannot be updated until the next write
	 * sequence is initiated. This copy guarantees that erroneous writes to
	 * HW_OCOTP_CTRL[ADDR] will not affect an active write operation. It
	 * should also be noted that during the programming HW_OCOTP_DATA will
	 * shift right (with zero fill). This shifting is required to program
	 * the OTP serially. During the write operation, HW_OCOTP_DATA cannot be
	 * modified.
	 */
	writel(*buf, priv->base + IMX_OCOTP_ADDR_DATA);

	/* 47.4.1.4.5
	 * Once complete, the controller will clear BUSY. A write request to a
	 * protected or locked region will result in no OTP access and no
	 * setting of HW_OCOTP_CTRL[BUSY]. In addition HW_OCOTP_CTRL[ERROR] will
	 * be set. It must be cleared by software before any new write access
	 * can be issued.
	 */
	ret = imx_ocotp_wait_for_busy(priv->base, 0);
	if (ret < 0) {
		if (ret == -EPERM) {
			dev_err(priv->dev, "failed write to locked region");
			imx_ocotp_clr_err_if_set(priv->base);
		} else {
			dev_err(priv->dev, "timeout during data write\n");
		}
		goto write_end;
	}

	/* 47.3.1.4
	 * Write Postamble: Due to internal electrical characteristics of the
	 * OTP during writes, all OTP operations following a write must be
	 * separated by 2 us after the clearing of HW_OCOTP_CTRL_BUSY following
	 * the write.
	 */
	udelay(2);

	/* reload all shadow registers */
	writel(IMX_OCOTP_BM_CTRL_REL_SHADOWS,
	       priv->base + IMX_OCOTP_ADDR_CTRL_SET);
	ret = imx_ocotp_wait_for_busy(priv->base,
				      IMX_OCOTP_BM_CTRL_REL_SHADOWS);
	if (ret < 0) {
		dev_err(priv->dev, "timeout during shadow register reload\n");
		goto write_end;
	}

write_end:
	clk_disable_unprepare(priv->clk);
	mutex_unlock(&ocotp_mutex);
	if (ret < 0)
		return ret;
	return bytes;
}

static struct nvmem_config imx_ocotp_nvmem_config = {
	.name = "imx-ocotp",
	.read_only = false,
	.word_size = 4,
	.stride = 4,
	.reg_read = imx_ocotp_read,
	.reg_write = imx_ocotp_write,
};

static const struct of_device_id imx_ocotp_dt_ids[] = {
	{ .compatible = "fsl,imx6q-ocotp",  (void *)128 },
	{ .compatible = "fsl,imx6sl-ocotp", (void *)64 },
	{ .compatible = "fsl,imx6sx-ocotp", (void *)128 },
	{ .compatible = "fsl,imx6ul-ocotp", (void *)128 },
	{ .compatible = "fsl,imx7d-ocotp", (void *)64 },
	{ },
};
MODULE_DEVICE_TABLE(of, imx_ocotp_dt_ids);

static int imx_ocotp_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct ocotp_priv *priv;
	struct nvmem_device *nvmem;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	of_id = of_match_device(imx_ocotp_dt_ids, dev);
	priv->nregs = (unsigned long)of_id->data;
	imx_ocotp_nvmem_config.size = 4 * priv->nregs;
	imx_ocotp_nvmem_config.dev = dev;
	imx_ocotp_nvmem_config.priv = priv;
	priv->config = &imx_ocotp_nvmem_config;
	nvmem = nvmem_register(&imx_ocotp_nvmem_config);

	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static int imx_ocotp_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static struct platform_driver imx_ocotp_driver = {
	.probe	= imx_ocotp_probe,
	.remove	= imx_ocotp_remove,
	.driver = {
		.name	= "imx_ocotp",
		.of_match_table = imx_ocotp_dt_ids,
	},
};
module_platform_driver(imx_ocotp_driver);

MODULE_AUTHOR("Philipp Zabel <p.zabel@pengutronix.de>");
MODULE_DESCRIPTION("i.MX6 OCOTP fuse box driver");
MODULE_LICENSE("GPL v2");
