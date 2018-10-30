// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Manager Driver for Altera Arria10 SoCFPGA
 *
 * Copyright (C) 2015-2016 Altera Corporation
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/regmap.h>

#define A10_FPGAMGR_DCLKCNT_OFST				0x08
#define A10_FPGAMGR_DCLKSTAT_OFST				0x0c
#define A10_FPGAMGR_IMGCFG_CTL_00_OFST				0x70
#define A10_FPGAMGR_IMGCFG_CTL_01_OFST				0x74
#define A10_FPGAMGR_IMGCFG_CTL_02_OFST				0x78
#define A10_FPGAMGR_IMGCFG_STAT_OFST				0x80

#define A10_FPGAMGR_DCLKSTAT_DCLKDONE				BIT(0)

#define A10_FPGAMGR_IMGCFG_CTL_00_S2F_NENABLE_NCONFIG		BIT(0)
#define A10_FPGAMGR_IMGCFG_CTL_00_S2F_NENABLE_NSTATUS		BIT(1)
#define A10_FPGAMGR_IMGCFG_CTL_00_S2F_NENABLE_CONDONE		BIT(2)
#define A10_FPGAMGR_IMGCFG_CTL_00_S2F_NCONFIG			BIT(8)
#define A10_FPGAMGR_IMGCFG_CTL_00_S2F_NSTATUS_OE		BIT(16)
#define A10_FPGAMGR_IMGCFG_CTL_00_S2F_CONDONE_OE		BIT(24)

#define A10_FPGAMGR_IMGCFG_CTL_01_S2F_NENABLE_CONFIG		BIT(0)
#define A10_FPGAMGR_IMGCFG_CTL_01_S2F_PR_REQUEST		BIT(16)
#define A10_FPGAMGR_IMGCFG_CTL_01_S2F_NCE			BIT(24)

#define A10_FPGAMGR_IMGCFG_CTL_02_EN_CFG_CTRL			BIT(0)
#define A10_FPGAMGR_IMGCFG_CTL_02_CDRATIO_MASK		(BIT(16) | BIT(17))
#define A10_FPGAMGR_IMGCFG_CTL_02_CDRATIO_SHIFT			16
#define A10_FPGAMGR_IMGCFG_CTL_02_CFGWIDTH			BIT(24)
#define A10_FPGAMGR_IMGCFG_CTL_02_CFGWIDTH_SHIFT		24

#define A10_FPGAMGR_IMGCFG_STAT_F2S_CRC_ERROR			BIT(0)
#define A10_FPGAMGR_IMGCFG_STAT_F2S_EARLY_USERMODE		BIT(1)
#define A10_FPGAMGR_IMGCFG_STAT_F2S_USERMODE			BIT(2)
#define A10_FPGAMGR_IMGCFG_STAT_F2S_NSTATUS_PIN			BIT(4)
#define A10_FPGAMGR_IMGCFG_STAT_F2S_CONDONE_PIN			BIT(6)
#define A10_FPGAMGR_IMGCFG_STAT_F2S_PR_READY			BIT(9)
#define A10_FPGAMGR_IMGCFG_STAT_F2S_PR_DONE			BIT(10)
#define A10_FPGAMGR_IMGCFG_STAT_F2S_PR_ERROR			BIT(11)
#define A10_FPGAMGR_IMGCFG_STAT_F2S_NCONFIG_PIN			BIT(12)
#define A10_FPGAMGR_IMGCFG_STAT_F2S_MSEL_MASK	(BIT(16) | BIT(17) | BIT(18))
#define A10_FPGAMGR_IMGCFG_STAT_F2S_MSEL_SHIFT		        16

/* FPGA CD Ratio Value */
#define CDRATIO_x1						0x0
#define CDRATIO_x2						0x1
#define CDRATIO_x4						0x2
#define CDRATIO_x8						0x3

/* Configuration width 16/32 bit */
#define CFGWDTH_32						1
#define CFGWDTH_16						0

/*
 * struct a10_fpga_priv - private data for fpga manager
 * @regmap: regmap for register access
 * @fpga_data_addr: iomap for single address data register to FPGA
 * @clk: clock
 */
struct a10_fpga_priv {
	struct regmap *regmap;
	void __iomem *fpga_data_addr;
	struct clk *clk;
};

static bool socfpga_a10_fpga_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case A10_FPGAMGR_DCLKCNT_OFST:
	case A10_FPGAMGR_DCLKSTAT_OFST:
	case A10_FPGAMGR_IMGCFG_CTL_00_OFST:
	case A10_FPGAMGR_IMGCFG_CTL_01_OFST:
	case A10_FPGAMGR_IMGCFG_CTL_02_OFST:
		return true;
	}
	return false;
}

static bool socfpga_a10_fpga_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case A10_FPGAMGR_DCLKCNT_OFST:
	case A10_FPGAMGR_DCLKSTAT_OFST:
	case A10_FPGAMGR_IMGCFG_CTL_00_OFST:
	case A10_FPGAMGR_IMGCFG_CTL_01_OFST:
	case A10_FPGAMGR_IMGCFG_CTL_02_OFST:
	case A10_FPGAMGR_IMGCFG_STAT_OFST:
		return true;
	}
	return false;
}

static const struct regmap_config socfpga_a10_fpga_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.writeable_reg = socfpga_a10_fpga_writeable_reg,
	.readable_reg = socfpga_a10_fpga_readable_reg,
	.max_register = A10_FPGAMGR_IMGCFG_STAT_OFST,
	.cache_type = REGCACHE_NONE,
};

/*
 * from the register map description of cdratio in imgcfg_ctrl_02:
 *  Normal Configuration    : 32bit Passive Parallel
 *  Partial Reconfiguration : 16bit Passive Parallel
 */
static void socfpga_a10_fpga_set_cfg_width(struct a10_fpga_priv *priv,
					   int width)
{
	width <<= A10_FPGAMGR_IMGCFG_CTL_02_CFGWIDTH_SHIFT;

	regmap_update_bits(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_02_OFST,
			   A10_FPGAMGR_IMGCFG_CTL_02_CFGWIDTH, width);
}

static void socfpga_a10_fpga_generate_dclks(struct a10_fpga_priv *priv,
					    u32 count)
{
	u32 val;

	/* Clear any existing DONE status. */
	regmap_write(priv->regmap, A10_FPGAMGR_DCLKSTAT_OFST,
		     A10_FPGAMGR_DCLKSTAT_DCLKDONE);

	/* Issue the DCLK regmap. */
	regmap_write(priv->regmap, A10_FPGAMGR_DCLKCNT_OFST, count);

	/* wait till the dclkcnt done */
	regmap_read_poll_timeout(priv->regmap, A10_FPGAMGR_DCLKSTAT_OFST, val,
				 val, 1, 100);

	/* Clear DONE status. */
	regmap_write(priv->regmap, A10_FPGAMGR_DCLKSTAT_OFST,
		     A10_FPGAMGR_DCLKSTAT_DCLKDONE);
}

#define RBF_ENCRYPTION_MODE_OFFSET		69
#define RBF_DECOMPRESS_OFFSET			229

static int socfpga_a10_fpga_encrypted(u32 *buf32, size_t buf32_size)
{
	if (buf32_size < RBF_ENCRYPTION_MODE_OFFSET + 1)
		return -EINVAL;

	/* Is the bitstream encrypted? */
	return ((buf32[RBF_ENCRYPTION_MODE_OFFSET] >> 2) & 3) != 0;
}

static int socfpga_a10_fpga_compressed(u32 *buf32, size_t buf32_size)
{
	if (buf32_size < RBF_DECOMPRESS_OFFSET + 1)
		return -EINVAL;

	/* Is the bitstream compressed? */
	return !((buf32[RBF_DECOMPRESS_OFFSET] >> 1) & 1);
}

static unsigned int socfpga_a10_fpga_get_cd_ratio(unsigned int cfg_width,
						  bool encrypt, bool compress)
{
	unsigned int cd_ratio;

	/*
	 * cd ratio is dependent on cfg width and whether the bitstream
	 * is encrypted and/or compressed.
	 *
	 * | width | encr. | compr. | cd ratio |
	 * |  16   |   0   |   0    |     1    |
	 * |  16   |   0   |   1    |     4    |
	 * |  16   |   1   |   0    |     2    |
	 * |  16   |   1   |   1    |     4    |
	 * |  32   |   0   |   0    |     1    |
	 * |  32   |   0   |   1    |     8    |
	 * |  32   |   1   |   0    |     4    |
	 * |  32   |   1   |   1    |     8    |
	 */
	if (!compress && !encrypt)
		return CDRATIO_x1;

	if (compress)
		cd_ratio = CDRATIO_x4;
	else
		cd_ratio = CDRATIO_x2;

	/* If 32 bit, double the cd ratio by incrementing the field  */
	if (cfg_width == CFGWDTH_32)
		cd_ratio += 1;

	return cd_ratio;
}

static int socfpga_a10_fpga_set_cdratio(struct fpga_manager *mgr,
					unsigned int cfg_width,
					const char *buf, size_t count)
{
	struct a10_fpga_priv *priv = mgr->priv;
	unsigned int cd_ratio;
	int encrypt, compress;

	encrypt = socfpga_a10_fpga_encrypted((u32 *)buf, count / 4);
	if (encrypt < 0)
		return -EINVAL;

	compress = socfpga_a10_fpga_compressed((u32 *)buf, count / 4);
	if (compress < 0)
		return -EINVAL;

	cd_ratio = socfpga_a10_fpga_get_cd_ratio(cfg_width, encrypt, compress);

	regmap_update_bits(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_02_OFST,
			   A10_FPGAMGR_IMGCFG_CTL_02_CDRATIO_MASK,
			   cd_ratio << A10_FPGAMGR_IMGCFG_CTL_02_CDRATIO_SHIFT);

	return 0;
}

static u32 socfpga_a10_fpga_read_stat(struct a10_fpga_priv *priv)
{
	u32 val;

	regmap_read(priv->regmap, A10_FPGAMGR_IMGCFG_STAT_OFST, &val);

	return val;
}

static int socfpga_a10_fpga_wait_for_pr_ready(struct a10_fpga_priv *priv)
{
	u32 reg, i;

	for (i = 0; i < 10 ; i++) {
		reg = socfpga_a10_fpga_read_stat(priv);

		if (reg & A10_FPGAMGR_IMGCFG_STAT_F2S_PR_ERROR)
			return -EINVAL;

		if (reg & A10_FPGAMGR_IMGCFG_STAT_F2S_PR_READY)
			return 0;
	}

	return -ETIMEDOUT;
}

static int socfpga_a10_fpga_wait_for_pr_done(struct a10_fpga_priv *priv)
{
	u32 reg, i;

	for (i = 0; i < 10 ; i++) {
		reg = socfpga_a10_fpga_read_stat(priv);

		if (reg & A10_FPGAMGR_IMGCFG_STAT_F2S_PR_ERROR)
			return -EINVAL;

		if (reg & A10_FPGAMGR_IMGCFG_STAT_F2S_PR_DONE)
			return 0;
	}

	return -ETIMEDOUT;
}

/* Start the FPGA programming by initialize the FPGA Manager */
static int socfpga_a10_fpga_write_init(struct fpga_manager *mgr,
				       struct fpga_image_info *info,
				       const char *buf, size_t count)
{
	struct a10_fpga_priv *priv = mgr->priv;
	unsigned int cfg_width;
	u32 msel, stat, mask;
	int ret;

	if (info->flags & FPGA_MGR_PARTIAL_RECONFIG)
		cfg_width = CFGWDTH_16;
	else
		return -EINVAL;

	/* Check for passive parallel (msel == 000 or 001) */
	msel = socfpga_a10_fpga_read_stat(priv);
	msel &= A10_FPGAMGR_IMGCFG_STAT_F2S_MSEL_MASK;
	msel >>= A10_FPGAMGR_IMGCFG_STAT_F2S_MSEL_SHIFT;
	if ((msel != 0) && (msel != 1)) {
		dev_dbg(&mgr->dev, "Fail: invalid msel=%d\n", msel);
		return -EINVAL;
	}

	/* Make sure no external devices are interfering */
	stat = socfpga_a10_fpga_read_stat(priv);
	mask = A10_FPGAMGR_IMGCFG_STAT_F2S_NCONFIG_PIN |
	       A10_FPGAMGR_IMGCFG_STAT_F2S_NSTATUS_PIN;
	if ((stat & mask) != mask)
		return -EINVAL;

	/* Set cfg width */
	socfpga_a10_fpga_set_cfg_width(priv, cfg_width);

	/* Determine cd ratio from bitstream header and set cd ratio */
	ret = socfpga_a10_fpga_set_cdratio(mgr, cfg_width, buf, count);
	if (ret)
		return ret;

	/*
	 * Clear s2f_nce to enable chip select.  Leave pr_request
	 * unasserted and override disabled.
	 */
	regmap_write(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_01_OFST,
		     A10_FPGAMGR_IMGCFG_CTL_01_S2F_NENABLE_CONFIG);

	/* Set cfg_ctrl to enable s2f dclk and data */
	regmap_update_bits(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_02_OFST,
			   A10_FPGAMGR_IMGCFG_CTL_02_EN_CFG_CTRL,
			   A10_FPGAMGR_IMGCFG_CTL_02_EN_CFG_CTRL);

	/*
	 * Disable overrides not needed for pr.
	 * s2f_config==1 leaves reset deasseted.
	 */
	regmap_write(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_00_OFST,
		     A10_FPGAMGR_IMGCFG_CTL_00_S2F_NENABLE_NCONFIG |
		     A10_FPGAMGR_IMGCFG_CTL_00_S2F_NENABLE_NSTATUS |
		     A10_FPGAMGR_IMGCFG_CTL_00_S2F_NENABLE_CONDONE |
		     A10_FPGAMGR_IMGCFG_CTL_00_S2F_NCONFIG);

	/* Enable override for data, dclk, nce, and pr_request to CSS */
	regmap_update_bits(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_01_OFST,
			   A10_FPGAMGR_IMGCFG_CTL_01_S2F_NENABLE_CONFIG, 0);

	/* Send some clocks to clear out any errors */
	socfpga_a10_fpga_generate_dclks(priv, 256);

	/* Assert pr_request */
	regmap_update_bits(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_01_OFST,
			   A10_FPGAMGR_IMGCFG_CTL_01_S2F_PR_REQUEST,
			   A10_FPGAMGR_IMGCFG_CTL_01_S2F_PR_REQUEST);

	/* Provide 2048 DCLKs before starting the config data streaming. */
	socfpga_a10_fpga_generate_dclks(priv, 0x7ff);

	/* Wait for pr_ready */
	return socfpga_a10_fpga_wait_for_pr_ready(priv);
}

/*
 * write data to the FPGA data register
 */
static int socfpga_a10_fpga_write(struct fpga_manager *mgr, const char *buf,
				  size_t count)
{
	struct a10_fpga_priv *priv = mgr->priv;
	u32 *buffer_32 = (u32 *)buf;
	size_t i = 0;

	if (count <= 0)
		return -EINVAL;

	/* Write out the complete 32-bit chunks */
	while (count >= sizeof(u32)) {
		writel(buffer_32[i++], priv->fpga_data_addr);
		count -= sizeof(u32);
	}

	/* Write out remaining non 32-bit chunks */
	switch (count) {
	case 3:
		writel(buffer_32[i++] & 0x00ffffff, priv->fpga_data_addr);
		break;
	case 2:
		writel(buffer_32[i++] & 0x0000ffff, priv->fpga_data_addr);
		break;
	case 1:
		writel(buffer_32[i++] & 0x000000ff, priv->fpga_data_addr);
		break;
	case 0:
		break;
	default:
		/* This will never happen */
		return -EFAULT;
	}

	return 0;
}

static int socfpga_a10_fpga_write_complete(struct fpga_manager *mgr,
					   struct fpga_image_info *info)
{
	struct a10_fpga_priv *priv = mgr->priv;
	u32 reg;
	int ret;

	/* Wait for pr_done */
	ret = socfpga_a10_fpga_wait_for_pr_done(priv);

	/* Clear pr_request */
	regmap_update_bits(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_01_OFST,
			   A10_FPGAMGR_IMGCFG_CTL_01_S2F_PR_REQUEST, 0);

	/* Send some clocks to clear out any errors */
	socfpga_a10_fpga_generate_dclks(priv, 256);

	/* Disable s2f dclk and data */
	regmap_update_bits(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_02_OFST,
			   A10_FPGAMGR_IMGCFG_CTL_02_EN_CFG_CTRL, 0);

	/* Deassert chip select */
	regmap_update_bits(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_01_OFST,
			   A10_FPGAMGR_IMGCFG_CTL_01_S2F_NCE,
			   A10_FPGAMGR_IMGCFG_CTL_01_S2F_NCE);

	/* Disable data, dclk, nce, and pr_request override to CSS */
	regmap_update_bits(priv->regmap, A10_FPGAMGR_IMGCFG_CTL_01_OFST,
			   A10_FPGAMGR_IMGCFG_CTL_01_S2F_NENABLE_CONFIG,
			   A10_FPGAMGR_IMGCFG_CTL_01_S2F_NENABLE_CONFIG);

	/* Return any errors regarding pr_done or pr_error */
	if (ret)
		return ret;

	/* Final check */
	reg = socfpga_a10_fpga_read_stat(priv);

	if (((reg & A10_FPGAMGR_IMGCFG_STAT_F2S_USERMODE) == 0) ||
	    ((reg & A10_FPGAMGR_IMGCFG_STAT_F2S_CONDONE_PIN) == 0) ||
	    ((reg & A10_FPGAMGR_IMGCFG_STAT_F2S_NSTATUS_PIN) == 0)) {
		dev_dbg(&mgr->dev,
			"Timeout in final check. Status=%08xf\n", reg);
		return -ETIMEDOUT;
	}

	return 0;
}

static enum fpga_mgr_states socfpga_a10_fpga_state(struct fpga_manager *mgr)
{
	struct a10_fpga_priv *priv = mgr->priv;
	u32 reg = socfpga_a10_fpga_read_stat(priv);

	if (reg & A10_FPGAMGR_IMGCFG_STAT_F2S_USERMODE)
		return FPGA_MGR_STATE_OPERATING;

	if (reg & A10_FPGAMGR_IMGCFG_STAT_F2S_PR_READY)
		return FPGA_MGR_STATE_WRITE;

	if (reg & A10_FPGAMGR_IMGCFG_STAT_F2S_CRC_ERROR)
		return FPGA_MGR_STATE_WRITE_COMPLETE_ERR;

	if ((reg & A10_FPGAMGR_IMGCFG_STAT_F2S_NSTATUS_PIN) == 0)
		return FPGA_MGR_STATE_RESET;

	return FPGA_MGR_STATE_UNKNOWN;
}

static const struct fpga_manager_ops socfpga_a10_fpga_mgr_ops = {
	.initial_header_size = (RBF_DECOMPRESS_OFFSET + 1) * 4,
	.state = socfpga_a10_fpga_state,
	.write_init = socfpga_a10_fpga_write_init,
	.write = socfpga_a10_fpga_write,
	.write_complete = socfpga_a10_fpga_write_complete,
};

static int socfpga_a10_fpga_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct a10_fpga_priv *priv;
	void __iomem *reg_base;
	struct fpga_manager *mgr;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* First mmio base is for register access */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);

	/* Second mmio base is for writing FPGA image data */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->fpga_data_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->fpga_data_addr))
		return PTR_ERR(priv->fpga_data_addr);

	/* regmap for register access */
	priv->regmap = devm_regmap_init_mmio(dev, reg_base,
					     &socfpga_a10_fpga_regmap_config);
	if (IS_ERR(priv->regmap))
		return -ENODEV;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "no clock specified\n");
		return PTR_ERR(priv->clk);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "could not enable clock\n");
		return -EBUSY;
	}

	mgr = devm_fpga_mgr_create(dev, "SoCFPGA Arria10 FPGA Manager",
				   &socfpga_a10_fpga_mgr_ops, priv);
	if (!mgr)
		return -ENOMEM;

	platform_set_drvdata(pdev, mgr);

	ret = fpga_mgr_register(mgr);
	if (ret) {
		clk_disable_unprepare(priv->clk);
		return ret;
	}

	return 0;
}

static int socfpga_a10_fpga_remove(struct platform_device *pdev)
{
	struct fpga_manager *mgr = platform_get_drvdata(pdev);
	struct a10_fpga_priv *priv = mgr->priv;

	fpga_mgr_unregister(mgr);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id socfpga_a10_fpga_of_match[] = {
	{ .compatible = "altr,socfpga-a10-fpga-mgr", },
	{},
};

MODULE_DEVICE_TABLE(of, socfpga_a10_fpga_of_match);

static struct platform_driver socfpga_a10_fpga_driver = {
	.probe = socfpga_a10_fpga_probe,
	.remove = socfpga_a10_fpga_remove,
	.driver = {
		.name	= "socfpga_a10_fpga_manager",
		.of_match_table = socfpga_a10_fpga_of_match,
	},
};

module_platform_driver(socfpga_a10_fpga_driver);

MODULE_AUTHOR("Alan Tull <atull@opensource.altera.com>");
MODULE_DESCRIPTION("SoCFPGA Arria10 FPGA Manager");
MODULE_LICENSE("GPL v2");
