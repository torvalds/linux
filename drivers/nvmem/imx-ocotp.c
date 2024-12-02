// SPDX-License-Identifier: GPL-2.0-only
/*
 * i.MX6 OCOTP fusebox driver
 *
 * Copyright (c) 2015 Pengutronix, Philipp Zabel <p.zabel@pengutronix.de>
 *
 * Copyright 2019 NXP
 *
 * Based on the barebox ocotp driver,
 * Copyright (c) 2010 Baruch Siach <baruch@tkos.co.il>,
 *	Orex Computed Radiography
 *
 * Write support based on the fsl_otp driver,
 * Copyright (C) 2010-2013 Freescale Semiconductor, Inc
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
#define IMX_OCOTP_ADDR_DATA0		0x0020
#define IMX_OCOTP_ADDR_DATA1		0x0030
#define IMX_OCOTP_ADDR_DATA2		0x0040
#define IMX_OCOTP_ADDR_DATA3		0x0050

#define IMX_OCOTP_BM_CTRL_ADDR		0x000000FF
#define IMX_OCOTP_BM_CTRL_BUSY		0x00000100
#define IMX_OCOTP_BM_CTRL_ERROR		0x00000200
#define IMX_OCOTP_BM_CTRL_REL_SHADOWS	0x00000400

#define IMX_OCOTP_BM_CTRL_ADDR_8MP		0x000001FF
#define IMX_OCOTP_BM_CTRL_BUSY_8MP		0x00000200
#define IMX_OCOTP_BM_CTRL_ERROR_8MP		0x00000400
#define IMX_OCOTP_BM_CTRL_REL_SHADOWS_8MP	0x00000800

#define IMX_OCOTP_BM_CTRL_DEFAULT				\
	{							\
		.bm_addr = IMX_OCOTP_BM_CTRL_ADDR,		\
		.bm_busy = IMX_OCOTP_BM_CTRL_BUSY,		\
		.bm_error = IMX_OCOTP_BM_CTRL_ERROR,		\
		.bm_rel_shadows = IMX_OCOTP_BM_CTRL_REL_SHADOWS,\
	}

#define IMX_OCOTP_BM_CTRL_8MP					\
	{							\
		.bm_addr = IMX_OCOTP_BM_CTRL_ADDR_8MP,		\
		.bm_busy = IMX_OCOTP_BM_CTRL_BUSY_8MP,		\
		.bm_error = IMX_OCOTP_BM_CTRL_ERROR_8MP,	\
		.bm_rel_shadows = IMX_OCOTP_BM_CTRL_REL_SHADOWS_8MP,\
	}

#define TIMING_STROBE_PROG_US		10	/* Min time to blow a fuse */
#define TIMING_STROBE_READ_NS		37	/* Min time before read */
#define TIMING_RELAX_NS			17
#define DEF_FSOURCE			1001	/* > 1000 ns */
#define DEF_STROBE_PROG			10000	/* IPG clocks */
#define IMX_OCOTP_WR_UNLOCK		0x3E770000
#define IMX_OCOTP_READ_LOCKED_VAL	0xBADABADA

static DEFINE_MUTEX(ocotp_mutex);

struct ocotp_priv {
	struct device *dev;
	struct clk *clk;
	void __iomem *base;
	const struct ocotp_params *params;
	struct nvmem_config *config;
};

struct ocotp_ctrl_reg {
	u32 bm_addr;
	u32 bm_busy;
	u32 bm_error;
	u32 bm_rel_shadows;
};

struct ocotp_params {
	unsigned int nregs;
	unsigned int bank_address_words;
	void (*set_timing)(struct ocotp_priv *priv);
	struct ocotp_ctrl_reg ctrl;
	bool reverse_mac_address;
};

static int imx_ocotp_wait_for_busy(struct ocotp_priv *priv, u32 flags)
{
	int count;
	u32 c, mask;
	u32 bm_ctrl_busy, bm_ctrl_error;
	void __iomem *base = priv->base;

	bm_ctrl_busy = priv->params->ctrl.bm_busy;
	bm_ctrl_error = priv->params->ctrl.bm_error;

	mask = bm_ctrl_busy | bm_ctrl_error | flags;

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
		if (c & bm_ctrl_error)
			return -EPERM;
		return -ETIMEDOUT;
	}

	return 0;
}

static void imx_ocotp_clr_err_if_set(struct ocotp_priv *priv)
{
	u32 c, bm_ctrl_error;
	void __iomem *base = priv->base;

	bm_ctrl_error = priv->params->ctrl.bm_error;

	c = readl(base + IMX_OCOTP_ADDR_CTRL);
	if (!(c & bm_ctrl_error))
		return;

	writel(bm_ctrl_error, base + IMX_OCOTP_ADDR_CTRL_CLR);
}

static int imx_ocotp_read(void *context, unsigned int offset,
			  void *val, size_t bytes)
{
	struct ocotp_priv *priv = context;
	unsigned int count;
	u8 *buf, *p;
	int i, ret;
	u32 index, num_bytes;

	index = offset >> 2;
	num_bytes = round_up((offset % 4) + bytes, 4);
	count = num_bytes >> 2;

	if (count > (priv->params->nregs - index))
		count = priv->params->nregs - index;

	p = kzalloc(num_bytes, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	mutex_lock(&ocotp_mutex);

	buf = p;

	ret = clk_prepare_enable(priv->clk);
	if (ret < 0) {
		mutex_unlock(&ocotp_mutex);
		dev_err(priv->dev, "failed to prepare/enable ocotp clk\n");
		kfree(p);
		return ret;
	}

	ret = imx_ocotp_wait_for_busy(priv, 0);
	if (ret < 0) {
		dev_err(priv->dev, "timeout during read setup\n");
		goto read_end;
	}

	for (i = index; i < (index + count); i++) {
		*(u32 *)buf = readl(priv->base + IMX_OCOTP_OFFSET_B0W0 +
			       i * IMX_OCOTP_OFFSET_PER_WORD);

		/* 47.3.1.2
		 * For "read locked" registers 0xBADABADA will be returned and
		 * HW_OCOTP_CTRL[ERROR] will be set. It must be cleared by
		 * software before any new write, read or reload access can be
		 * issued
		 */
		if (*((u32 *)buf) == IMX_OCOTP_READ_LOCKED_VAL)
			imx_ocotp_clr_err_if_set(priv);

		buf += 4;
	}

	index = offset % 4;
	memcpy(val, &p[index], bytes);

read_end:
	clk_disable_unprepare(priv->clk);
	mutex_unlock(&ocotp_mutex);

	kfree(p);

	return ret;
}

static int imx_ocotp_cell_pp(void *context, const char *id, unsigned int offset,
			     void *data, size_t bytes)
{
	struct ocotp_priv *priv = context;

	/* Deal with some post processing of nvmem cell data */
	if (id && !strcmp(id, "mac-address")) {
		if (priv->params->reverse_mac_address) {
			u8 *buf = data;
			int i;

			for (i = 0; i < bytes/2; i++)
				swap(buf[i], buf[bytes - i - 1]);
		}
	}

	return 0;
}

static void imx_ocotp_set_imx6_timing(struct ocotp_priv *priv)
{
	unsigned long clk_rate;
	unsigned long strobe_read, relax, strobe_prog;
	u32 timing;

	/* 47.3.1.3.1
	 * Program HW_OCOTP_TIMING[STROBE_PROG] and HW_OCOTP_TIMING[RELAX]
	 * fields with timing values to match the current frequency of the
	 * ipg_clk. OTP writes will work at maximum bus frequencies as long
	 * as the HW_OCOTP_TIMING parameters are set correctly.
	 *
	 * Note: there are minimum timings required to ensure an OTP fuse burns
	 * correctly that are independent of the ipg_clk. Those values are not
	 * formally documented anywhere however, working from the minimum
	 * timings given in u-boot we can say:
	 *
	 * - Minimum STROBE_PROG time is 10 microseconds. Intuitively 10
	 *   microseconds feels about right as representative of a minimum time
	 *   to physically burn out a fuse.
	 *
	 * - Minimum STROBE_READ i.e. the time to wait post OTP fuse burn before
	 *   performing another read is 37 nanoseconds
	 *
	 * - Minimum RELAX timing is 17 nanoseconds. This final RELAX minimum
	 *   timing is not entirely clear the documentation says "This
	 *   count value specifies the time to add to all default timing
	 *   parameters other than the Tpgm and Trd. It is given in number
	 *   of ipg_clk periods." where Tpgm and Trd refer to STROBE_PROG
	 *   and STROBE_READ respectively. What the other timing parameters
	 *   are though, is not specified. Experience shows a zero RELAX
	 *   value will mess up a re-load of the shadow registers post OTP
	 *   burn.
	 */
	clk_rate = clk_get_rate(priv->clk);

	relax = DIV_ROUND_UP(clk_rate * TIMING_RELAX_NS, 1000000000) - 1;
	strobe_read = DIV_ROUND_UP(clk_rate * TIMING_STROBE_READ_NS,
				   1000000000);
	strobe_read += 2 * (relax + 1) - 1;
	strobe_prog = DIV_ROUND_CLOSEST(clk_rate * TIMING_STROBE_PROG_US,
					1000000);
	strobe_prog += 2 * (relax + 1) - 1;

	timing = readl(priv->base + IMX_OCOTP_ADDR_TIMING) & 0x0FC00000;
	timing |= strobe_prog & 0x00000FFF;
	timing |= (relax       << 12) & 0x0000F000;
	timing |= (strobe_read << 16) & 0x003F0000;

	writel(timing, priv->base + IMX_OCOTP_ADDR_TIMING);
}

static void imx_ocotp_set_imx7_timing(struct ocotp_priv *priv)
{
	unsigned long clk_rate;
	u64 fsource, strobe_prog;
	u32 timing;

	/* i.MX 7Solo Applications Processor Reference Manual, Rev. 0.1
	 * 6.4.3.3
	 */
	clk_rate = clk_get_rate(priv->clk);
	fsource = DIV_ROUND_UP_ULL((u64)clk_rate * DEF_FSOURCE,
				   NSEC_PER_SEC) + 1;
	strobe_prog = DIV_ROUND_CLOSEST_ULL((u64)clk_rate * DEF_STROBE_PROG,
					    NSEC_PER_SEC) + 1;

	timing = strobe_prog & 0x00000FFF;
	timing |= (fsource << 12) & 0x000FF000;

	writel(timing, priv->base + IMX_OCOTP_ADDR_TIMING);
}

static int imx_ocotp_write(void *context, unsigned int offset, void *val,
			   size_t bytes)
{
	struct ocotp_priv *priv = context;
	u32 *buf = val;
	int ret;

	u32 ctrl;
	u8 waddr;
	u8 word = 0;

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

	/* Setup the write timing values */
	priv->params->set_timing(priv);

	/* 47.3.1.3.2
	 * Check that HW_OCOTP_CTRL[BUSY] and HW_OCOTP_CTRL[ERROR] are clear.
	 * Overlapped accesses are not supported by the controller. Any pending
	 * write or reload must be completed before a write access can be
	 * requested.
	 */
	ret = imx_ocotp_wait_for_busy(priv, 0);
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
	if (priv->params->bank_address_words != 0) {
		/*
		 * In banked/i.MX7 mode the OTP register bank goes into waddr
		 * see i.MX 7Solo Applications Processor Reference Manual, Rev.
		 * 0.1 section 6.4.3.1
		 */
		offset = offset / priv->config->word_size;
		waddr = offset / priv->params->bank_address_words;
		word  = offset & (priv->params->bank_address_words - 1);
	} else {
		/*
		 * Non-banked i.MX6 mode.
		 * OTP write/read address specifies one of 128 word address
		 * locations
		 */
		waddr = offset / 4;
	}

	ctrl = readl(priv->base + IMX_OCOTP_ADDR_CTRL);
	ctrl &= ~priv->params->ctrl.bm_addr;
	ctrl |= waddr & priv->params->ctrl.bm_addr;
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
	 * Note: on i.MX7 there are four data fields to write for banked write
	 *       with the fuse blowing operation only taking place after data0
	 *	 has been written. This is why data0 must always be the last
	 *	 register written.
	 */
	if (priv->params->bank_address_words != 0) {
		/* Banked/i.MX7 mode */
		switch (word) {
		case 0:
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA1);
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA2);
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA3);
			writel(*buf, priv->base + IMX_OCOTP_ADDR_DATA0);
			break;
		case 1:
			writel(*buf, priv->base + IMX_OCOTP_ADDR_DATA1);
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA2);
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA3);
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA0);
			break;
		case 2:
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA1);
			writel(*buf, priv->base + IMX_OCOTP_ADDR_DATA2);
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA3);
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA0);
			break;
		case 3:
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA1);
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA2);
			writel(*buf, priv->base + IMX_OCOTP_ADDR_DATA3);
			writel(0, priv->base + IMX_OCOTP_ADDR_DATA0);
			break;
		}
	} else {
		/* Non-banked i.MX6 mode */
		writel(*buf, priv->base + IMX_OCOTP_ADDR_DATA0);
	}

	/* 47.4.1.4.5
	 * Once complete, the controller will clear BUSY. A write request to a
	 * protected or locked region will result in no OTP access and no
	 * setting of HW_OCOTP_CTRL[BUSY]. In addition HW_OCOTP_CTRL[ERROR] will
	 * be set. It must be cleared by software before any new write access
	 * can be issued.
	 */
	ret = imx_ocotp_wait_for_busy(priv, 0);
	if (ret < 0) {
		if (ret == -EPERM) {
			dev_err(priv->dev, "failed write to locked region");
			imx_ocotp_clr_err_if_set(priv);
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
	writel(priv->params->ctrl.bm_rel_shadows,
	       priv->base + IMX_OCOTP_ADDR_CTRL_SET);
	ret = imx_ocotp_wait_for_busy(priv,
				      priv->params->ctrl.bm_rel_shadows);
	if (ret < 0)
		dev_err(priv->dev, "timeout during shadow register reload\n");

write_end:
	clk_disable_unprepare(priv->clk);
	mutex_unlock(&ocotp_mutex);
	return ret < 0 ? ret : bytes;
}

static struct nvmem_config imx_ocotp_nvmem_config = {
	.name = "imx-ocotp",
	.read_only = false,
	.word_size = 4,
	.stride = 1,
	.reg_read = imx_ocotp_read,
	.reg_write = imx_ocotp_write,
	.cell_post_process = imx_ocotp_cell_pp,
};

static const struct ocotp_params imx6q_params = {
	.nregs = 128,
	.bank_address_words = 0,
	.set_timing = imx_ocotp_set_imx6_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
};

static const struct ocotp_params imx6sl_params = {
	.nregs = 64,
	.bank_address_words = 0,
	.set_timing = imx_ocotp_set_imx6_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
};

static const struct ocotp_params imx6sll_params = {
	.nregs = 80,
	.bank_address_words = 0,
	.set_timing = imx_ocotp_set_imx6_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
};

static const struct ocotp_params imx6sx_params = {
	.nregs = 128,
	.bank_address_words = 0,
	.set_timing = imx_ocotp_set_imx6_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
};

static const struct ocotp_params imx6ul_params = {
	.nregs = 144,
	.bank_address_words = 0,
	.set_timing = imx_ocotp_set_imx6_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
};

static const struct ocotp_params imx6ull_params = {
	.nregs = 80,
	.bank_address_words = 0,
	.set_timing = imx_ocotp_set_imx6_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
};

static const struct ocotp_params imx7d_params = {
	.nregs = 64,
	.bank_address_words = 4,
	.set_timing = imx_ocotp_set_imx7_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
};

static const struct ocotp_params imx7ulp_params = {
	.nregs = 256,
	.bank_address_words = 0,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
};

static const struct ocotp_params imx8mq_params = {
	.nregs = 256,
	.bank_address_words = 0,
	.set_timing = imx_ocotp_set_imx6_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
	.reverse_mac_address = true,
};

static const struct ocotp_params imx8mm_params = {
	.nregs = 256,
	.bank_address_words = 0,
	.set_timing = imx_ocotp_set_imx6_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
	.reverse_mac_address = true,
};

static const struct ocotp_params imx8mn_params = {
	.nregs = 256,
	.bank_address_words = 0,
	.set_timing = imx_ocotp_set_imx6_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_DEFAULT,
	.reverse_mac_address = true,
};

static const struct ocotp_params imx8mp_params = {
	.nregs = 384,
	.bank_address_words = 0,
	.set_timing = imx_ocotp_set_imx6_timing,
	.ctrl = IMX_OCOTP_BM_CTRL_8MP,
	.reverse_mac_address = true,
};

static const struct of_device_id imx_ocotp_dt_ids[] = {
	{ .compatible = "fsl,imx6q-ocotp",  .data = &imx6q_params },
	{ .compatible = "fsl,imx6sl-ocotp", .data = &imx6sl_params },
	{ .compatible = "fsl,imx6sx-ocotp", .data = &imx6sx_params },
	{ .compatible = "fsl,imx6ul-ocotp", .data = &imx6ul_params },
	{ .compatible = "fsl,imx6ull-ocotp", .data = &imx6ull_params },
	{ .compatible = "fsl,imx7d-ocotp",  .data = &imx7d_params },
	{ .compatible = "fsl,imx6sll-ocotp", .data = &imx6sll_params },
	{ .compatible = "fsl,imx7ulp-ocotp", .data = &imx7ulp_params },
	{ .compatible = "fsl,imx8mq-ocotp", .data = &imx8mq_params },
	{ .compatible = "fsl,imx8mm-ocotp", .data = &imx8mm_params },
	{ .compatible = "fsl,imx8mn-ocotp", .data = &imx8mn_params },
	{ .compatible = "fsl,imx8mp-ocotp", .data = &imx8mp_params },
	{ },
};
MODULE_DEVICE_TABLE(of, imx_ocotp_dt_ids);

static int imx_ocotp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ocotp_priv *priv;
	struct nvmem_device *nvmem;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->params = of_device_get_match_data(&pdev->dev);
	imx_ocotp_nvmem_config.size = 4 * priv->params->nregs;
	imx_ocotp_nvmem_config.dev = dev;
	imx_ocotp_nvmem_config.priv = priv;
	priv->config = &imx_ocotp_nvmem_config;

	clk_prepare_enable(priv->clk);
	imx_ocotp_clr_err_if_set(priv);
	clk_disable_unprepare(priv->clk);

	nvmem = devm_nvmem_register(dev, &imx_ocotp_nvmem_config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static struct platform_driver imx_ocotp_driver = {
	.probe	= imx_ocotp_probe,
	.driver = {
		.name	= "imx_ocotp",
		.of_match_table = imx_ocotp_dt_ids,
	},
};
module_platform_driver(imx_ocotp_driver);

MODULE_AUTHOR("Philipp Zabel <p.zabel@pengutronix.de>");
MODULE_DESCRIPTION("i.MX6/i.MX7 OCOTP fuse box driver");
MODULE_LICENSE("GPL v2");
