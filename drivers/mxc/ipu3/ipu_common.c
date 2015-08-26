/*
 * Copyright 2005-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file ipu_common.c
 *
 * @brief This file contains the IPU driver common API functions.
 *
 * @ingroup IPU
 */
#include <linux/busfreq-imx.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ipu-v3.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/cacheflush.h>

#include "ipu_param_mem.h"
#include "ipu_regs.h"

static struct ipu_soc ipu_array[MXC_IPU_MAX_NUM];

/* Static functions */
static irqreturn_t ipu_sync_irq_handler(int irq, void *desc);
static irqreturn_t ipu_err_irq_handler(int irq, void *desc);

static inline uint32_t channel_2_dma(ipu_channel_t ch, ipu_buffer_t type)
{
	return ((uint32_t) ch >> (6 * type)) & 0x3F;
};

static inline int _ipu_is_ic_chan(uint32_t dma_chan)
{
	return (((dma_chan >= 11) && (dma_chan <= 22) && (dma_chan != 17) &&
		(dma_chan != 18)));
}

static inline int _ipu_is_vdi_out_chan(uint32_t dma_chan)
{
	return (dma_chan == 5);
}

static inline int _ipu_is_ic_graphic_chan(uint32_t dma_chan)
{
	return (dma_chan == 14 || dma_chan == 15);
}

/* Either DP BG or DP FG can be graphic window */
static inline int _ipu_is_dp_graphic_chan(uint32_t dma_chan)
{
	return (dma_chan == 23 || dma_chan == 27);
}

static inline int _ipu_is_irt_chan(uint32_t dma_chan)
{
	return ((dma_chan >= 45) && (dma_chan <= 50));
}

static inline int _ipu_is_dmfc_chan(uint32_t dma_chan)
{
	return ((dma_chan >= 23) && (dma_chan <= 29));
}

static inline int _ipu_is_trb_chan(struct ipu_soc *ipu, uint32_t dma_chan)
{
	return (((dma_chan == 8) || (dma_chan == 9) ||
		 (dma_chan == 10) || (dma_chan == 13) ||
		 (dma_chan == 21) || (dma_chan == 23) ||
		 (dma_chan == 27) || (dma_chan == 28)) &&
		(ipu->devtype >= IPUv3EX));
}

/*
 * We usually use IDMAC 23 as full plane and IDMAC 27 as partial
 * plane.
 * IDMAC 23/24/28/41 can drive a display respectively - primary
 * IDMAC 27 depends on IDMAC 23 - nonprimary
 */
static inline int _ipu_is_primary_disp_chan(uint32_t dma_chan)
{
	return ((dma_chan == 23) || (dma_chan == 24) ||
		(dma_chan == 28) || (dma_chan == 41));
}

static inline int _ipu_is_sync_irq(uint32_t irq)
{
	/* sync interrupt register number */
	int reg_num = irq / 32 + 1;

	return ((reg_num == 1)  || (reg_num == 2)  || (reg_num == 3)  ||
		(reg_num == 4)  || (reg_num == 7)  || (reg_num == 8)  ||
		(reg_num == 11) || (reg_num == 12) || (reg_num == 13) ||
		(reg_num == 14) || (reg_num == 15));
}

#define idma_is_valid(ch)	(ch != NO_DMA)
#define idma_mask(ch)		(idma_is_valid(ch) ? (1UL << (ch & 0x1F)) : 0)
#define tri_cur_buf_mask(ch)	(idma_mask(ch*2) * 3)
#define tri_cur_buf_shift(ch)	(ffs(idma_mask(ch*2)) - 1)

static inline bool idma_is_set(struct ipu_soc *ipu, uint32_t reg, uint32_t dma)
{
	return !!(ipu_idmac_read(ipu, reg) & idma_mask(dma));
}

static int ipu_clk_setup_enable(struct ipu_soc *ipu)
{
	char pixel_clk_0[] = "ipu1_pclk_0";
	char pixel_clk_1[] = "ipu1_pclk_1";
	char pixel_clk_0_sel[] = "ipu1_pclk0_sel";
	char pixel_clk_1_sel[] = "ipu1_pclk1_sel";
	char pixel_clk_0_div[] = "ipu1_pclk0_div";
	char pixel_clk_1_div[] = "ipu1_pclk1_div";
	char *ipu_pixel_clk_sel[] = { "ipu1", "ipu1_di0", "ipu1_di1", };
	char *pclk_sel;
	struct clk *clk;
	int ret;
	int i;

	pixel_clk_0[3] += ipu->id;
	pixel_clk_1[3] += ipu->id;
	pixel_clk_0_sel[3] += ipu->id;
	pixel_clk_1_sel[3] += ipu->id;
	pixel_clk_0_div[3] += ipu->id;
	pixel_clk_1_div[3] += ipu->id;
	for (i = 0; i < ARRAY_SIZE(ipu_pixel_clk_sel); i++) {
		pclk_sel = ipu_pixel_clk_sel[i];
		pclk_sel[3] += ipu->id;
	}
	dev_dbg(ipu->dev, "ipu_clk = %lu\n", clk_get_rate(ipu->ipu_clk));

	clk = clk_register_mux_pix_clk(ipu->dev, pixel_clk_0_sel,
			(const char **)ipu_pixel_clk_sel,
			ARRAY_SIZE(ipu_pixel_clk_sel),
			0, ipu->id, 0, 0);
	if (IS_ERR(clk)) {
		dev_err(ipu->dev, "clk_register mux di0 failed");
		return PTR_ERR(clk);
	}
	ipu->pixel_clk_sel[0] = clk;
	clk = clk_register_mux_pix_clk(ipu->dev, pixel_clk_1_sel,
			(const char **)ipu_pixel_clk_sel,
			ARRAY_SIZE(ipu_pixel_clk_sel),
			0, ipu->id, 1, 0);
	if (IS_ERR(clk)) {
		dev_err(ipu->dev, "clk_register mux di1 failed");
		return PTR_ERR(clk);
	}
	ipu->pixel_clk_sel[1] = clk;

	clk = clk_register_div_pix_clk(ipu->dev, pixel_clk_0_div,
				pixel_clk_0_sel, 0, ipu->id, 0, 0);
	if (IS_ERR(clk)) {
		dev_err(ipu->dev, "clk register di0 div failed");
		return PTR_ERR(clk);
	}
	clk = clk_register_div_pix_clk(ipu->dev, pixel_clk_1_div,
			pixel_clk_1_sel, CLK_SET_RATE_PARENT, ipu->id, 1, 0);
	if (IS_ERR(clk)) {
		dev_err(ipu->dev, "clk register di1 div failed");
		return PTR_ERR(clk);
	}

	ipu->pixel_clk[0] = clk_register_gate_pix_clk(ipu->dev, pixel_clk_0,
				pixel_clk_0_div, CLK_SET_RATE_PARENT,
				ipu->id, 0, 0);
	if (IS_ERR(ipu->pixel_clk[0])) {
		dev_err(ipu->dev, "clk register di0 gate failed");
		return PTR_ERR(ipu->pixel_clk[0]);
	}
	ipu->pixel_clk[1] = clk_register_gate_pix_clk(ipu->dev, pixel_clk_1,
				pixel_clk_1_div, CLK_SET_RATE_PARENT,
				ipu->id, 1, 0);
	if (IS_ERR(ipu->pixel_clk[1])) {
		dev_err(ipu->dev, "clk register di1 gate failed");
		return PTR_ERR(ipu->pixel_clk[1]);
	}

	ret = clk_set_parent(ipu->pixel_clk_sel[0], ipu->ipu_clk);
	if (ret) {
		dev_err(ipu->dev, "clk set parent failed");
		return ret;
	}

	ret = clk_set_parent(ipu->pixel_clk_sel[1], ipu->ipu_clk);
	if (ret) {
		dev_err(ipu->dev, "clk set parent failed");
		return ret;
	}

	ipu->di_clk[0] = devm_clk_get(ipu->dev, "di0");
	if (IS_ERR(ipu->di_clk[0])) {
		dev_err(ipu->dev, "clk_get di0 failed");
		return PTR_ERR(ipu->di_clk[0]);
	}
	ipu->di_clk[1] = devm_clk_get(ipu->dev, "di1");
	if (IS_ERR(ipu->di_clk[1])) {
		dev_err(ipu->dev, "clk_get di1 failed");
		return PTR_ERR(ipu->di_clk[1]);
	}

	ipu->di_clk_sel[0] = devm_clk_get(ipu->dev, "di0_sel");
	if (IS_ERR(ipu->di_clk_sel[0])) {
		dev_err(ipu->dev, "clk_get di0_sel failed");
		return PTR_ERR(ipu->di_clk_sel[0]);
	}
	ipu->di_clk_sel[1] = devm_clk_get(ipu->dev, "di1_sel");
	if (IS_ERR(ipu->di_clk_sel[1])) {
		dev_err(ipu->dev, "clk_get di1_sel failed");
		return PTR_ERR(ipu->di_clk_sel[1]);
	}

	return 0;
}

static int ipu_mem_reset(struct ipu_soc *ipu)
{
	int timeout = 1000;

	ipu_cm_write(ipu, 0x807FFFFF, IPU_MEM_RST);

	while (ipu_cm_read(ipu, IPU_MEM_RST) & 0x80000000) {
		if (!timeout--)
			return -ETIME;
		msleep(1);
	}

	return 0;
}

struct ipu_soc *ipu_get_soc(int id)
{
	if (id >= MXC_IPU_MAX_NUM)
		return ERR_PTR(-ENODEV);
	else if (!ipu_array[id].online)
		return ERR_PTR(-ENODEV);
	else
		return &(ipu_array[id]);
}
EXPORT_SYMBOL_GPL(ipu_get_soc);

void _ipu_get(struct ipu_soc *ipu)
{
	int ret;

	ret = clk_enable(ipu->ipu_clk);
	if (ret < 0)
		BUG();
}

void _ipu_put(struct ipu_soc *ipu)
{
	clk_disable(ipu->ipu_clk);
}

void ipu_disable_hsp_clk(struct ipu_soc *ipu)
{
	_ipu_put(ipu);
}
EXPORT_SYMBOL(ipu_disable_hsp_clk);

struct ipu_devtype {
	const char *name;
	unsigned long cm_ofs;
	unsigned long idmac_ofs;
	unsigned long ic_ofs;
	unsigned long csi0_ofs;
	unsigned long csi1_ofs;
	unsigned long di0_ofs;
	unsigned long di1_ofs;
	unsigned long smfc_ofs;
	unsigned long dc_ofs;
	unsigned long dmfc_ofs;
	unsigned long vdi_ofs;
	unsigned long cpmem_ofs;
	unsigned long srm_ofs;
	unsigned long tpm_ofs;
	unsigned long dc_tmpl_ofs;
	enum ipuv3_type type;
	bool idmac_used_bufs_present;
};

struct ipu_platform_type {
	struct ipu_devtype devtype;
	unsigned int ch0123_axi;
	unsigned int ch23_axi;
	unsigned int ch27_axi;
	unsigned int ch28_axi;
	unsigned int normal_axi;
	bool smfc_idmac_12bit_3planar_bs_fixup;	/* workaround little stripes */
	bool idmac_used_bufs_en_r;
	bool idmac_used_bufs_en_w;
	unsigned int idmac_used_bufs_max_r;
	unsigned int idmac_used_bufs_max_w;
};

static struct ipu_platform_type ipu_type_imx51 = {
	.devtype = {
		.name =		"IPUv3EX",
		.cm_ofs =	0x1E000000,
		.idmac_ofs =	0x1E008000,
		.ic_ofs =	0x1E020000,
		.csi0_ofs =	0x1E030000,
		.csi1_ofs =	0x1E038000,
		.di0_ofs =	0x1E040000,
		.di1_ofs =	0x1E048000,
		.smfc_ofs =	0x1E050000,
		.dc_ofs =	0x1E058000,
		.dmfc_ofs =	0x1E060000,
		.vdi_ofs =	0x1E068000,
		.cpmem_ofs =	0x1F000000,
		.srm_ofs =	0x1F040000,
		.tpm_ofs =	0x1F060000,
		.dc_tmpl_ofs =	0x1F080000,
		.type =		IPUv3EX,
		.idmac_used_bufs_present = false,
	},
	.ch0123_axi = 1,
	.ch23_axi = 1,
	.ch23_axi = 1,
	.ch27_axi = 1,
	.ch28_axi = 1,
	.normal_axi = 0,
	.smfc_idmac_12bit_3planar_bs_fixup = false,
};

static struct ipu_platform_type ipu_type_imx53 = {
	.devtype = {
		.name =		"IPUv3M",
		.cm_ofs =	0x06000000,
		.idmac_ofs =	0x06008000,
		.ic_ofs =	0x06020000,
		.csi0_ofs =	0x06030000,
		.csi1_ofs =	0x06038000,
		.di0_ofs =	0x06040000,
		.di1_ofs =	0x06048000,
		.smfc_ofs =	0x06050000,
		.dc_ofs =	0x06058000,
		.dmfc_ofs =	0x06060000,
		.vdi_ofs =	0x06068000,
		.cpmem_ofs =	0x07000000,
		.srm_ofs =	0x07040000,
		.tpm_ofs =	0x07060000,
		.dc_tmpl_ofs =	0x07080000,
		.type =		IPUv3M,
		.idmac_used_bufs_present = true,
	},
	.ch0123_axi = 1,
	.ch23_axi = 1,
	.ch27_axi = 1,
	.ch28_axi = 1,
	.normal_axi = 0,
	.idmac_used_bufs_en_r = false,
	.idmac_used_bufs_en_w = false,
	.smfc_idmac_12bit_3planar_bs_fixup = false,
};

static struct ipu_platform_type ipu_type_imx6q = {
	.devtype = {
		.name = "IPUv3H",
		.cm_ofs =	0x00200000,
		.idmac_ofs =	0x00208000,
		.ic_ofs =	0x00220000,
		.csi0_ofs =	0x00230000,
		.csi1_ofs =	0x00238000,
		.di0_ofs =	0x00240000,
		.di1_ofs =	0x00248000,
		.smfc_ofs =	0x00250000,
		.dc_ofs =	0x00258000,
		.dmfc_ofs =	0x00260000,
		.vdi_ofs =	0x00268000,
		.cpmem_ofs =	0x00300000,
		.srm_ofs =	0x00340000,
		.tpm_ofs =	0x00360000,
		.dc_tmpl_ofs =	0x00380000,
		.type =		IPUv3H,
		.idmac_used_bufs_present = true,
	},
	.ch0123_axi = 0,
	.ch23_axi = 0,
	.ch27_axi = 0,
	.ch28_axi = 0,
	.normal_axi = 1,
	.idmac_used_bufs_en_r = false,
	.idmac_used_bufs_en_w = false,
	.smfc_idmac_12bit_3planar_bs_fixup = false,
};

static struct ipu_platform_type ipu_type_imx6qp = {
	.devtype = {
		.name = "IPUv3H",
		.cm_ofs =	0x00200000,
		.idmac_ofs =	0x00208000,
		.ic_ofs =	0x00220000,
		.csi0_ofs =	0x00230000,
		.csi1_ofs =	0x00238000,
		.di0_ofs =	0x00240000,
		.di1_ofs =	0x00248000,
		.smfc_ofs =	0x00250000,
		.dc_ofs =	0x00258000,
		.dmfc_ofs =	0x00260000,
		.vdi_ofs =	0x00268000,
		.cpmem_ofs =	0x00300000,
		.srm_ofs =	0x00340000,
		.tpm_ofs =	0x00360000,
		.dc_tmpl_ofs =	0x00380000,
		.type =		IPUv3H,
		.idmac_used_bufs_present = true,
	},
	.ch0123_axi = 0,
	.ch23_axi = 0,
	.ch27_axi = 2,
	.ch28_axi = 3,
	.normal_axi = 1,
	.idmac_used_bufs_en_r = true,
	.idmac_used_bufs_en_w = true,
	.idmac_used_bufs_max_r = 0x3,
	.idmac_used_bufs_max_w = 0x3,
	.smfc_idmac_12bit_3planar_bs_fixup = true,
 };

static const struct of_device_id imx_ipuv3_dt_ids[] = {
	{ .compatible = "fsl,imx51-ipu", .data = &ipu_type_imx51, },
	{ .compatible = "fsl,imx53-ipu", .data = &ipu_type_imx53, },
	{ .compatible = "fsl,imx6q-ipu", .data = &ipu_type_imx6q, },
	{ .compatible = "fsl,imx6qp-ipu", .data = &ipu_type_imx6qp, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_ipuv3_dt_ids);

/*!
 * This function is called by the driver framework to initialize the IPU
 * hardware.
 *
 * @param	dev	The device structure for the IPU passed in by the
 *			driver framework.
 *
 * @return      Returns 0 on success or negative error code on error
 */
static int ipu_probe(struct platform_device *pdev)
{
	struct ipu_soc *ipu;
	struct resource *res;
	unsigned long ipu_base;
	const struct of_device_id *of_id =
			of_match_device(imx_ipuv3_dt_ids, &pdev->dev);
	const struct ipu_platform_type *iputype = of_id->data;
	const struct ipu_devtype *devtype = &iputype->devtype;
	int ret = 0, id;
	u32 bypass_reset, reg;

	dev_dbg(&pdev->dev, "<%s>\n", __func__);

	ret = of_property_read_u32(pdev->dev.of_node,
					"bypass_reset", &bypass_reset);
	if (ret < 0) {
		dev_dbg(&pdev->dev, "can not get bypass_reset\n");
		return ret;
	}

	id = of_alias_get_id(pdev->dev.of_node, "ipu");
	if (id < 0) {
		dev_dbg(&pdev->dev, "can not get alias id\n");
		return id;
	}

	ipu = &ipu_array[id];
	memset(ipu, 0, sizeof(struct ipu_soc));
	ipu->bypass_reset = (bool)bypass_reset;
	ipu->dev = &pdev->dev;
	ipu->id = id;
	ipu->devtype = devtype->type;
	ipu->ch0123_axi = iputype->ch0123_axi;
	ipu->ch23_axi = iputype->ch23_axi;
	ipu->ch27_axi = iputype->ch27_axi;
	ipu->ch28_axi = iputype->ch28_axi;
	ipu->normal_axi = iputype->normal_axi;
	ipu->smfc_idmac_12bit_3planar_bs_fixup =
			iputype->smfc_idmac_12bit_3planar_bs_fixup;
	spin_lock_init(&ipu->int_reg_spin_lock);
	spin_lock_init(&ipu->rdy_reg_spin_lock);
	mutex_init(&ipu->mutex_lock);

	dev_dbg(&pdev->dev, "revision is %s\n", devtype->name);

	ipu->irq_sync = platform_get_irq(pdev, 0);
	ipu->irq_err = platform_get_irq(pdev, 1);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res || ipu->irq_sync < 0 || ipu->irq_err < 0) {
		dev_err(&pdev->dev, "can't get device resources\n");
		return -ENODEV;
	}

	if (!devm_request_mem_region(&pdev->dev, res->start,
				     resource_size(res), pdev->name))
		return -EBUSY;

	ret = devm_request_irq(&pdev->dev, ipu->irq_sync,
			ipu_sync_irq_handler, 0, pdev->name, ipu);
	if (ret) {
		dev_err(ipu->dev, "request SYNC interrupt failed\n");
		return ret;
	}
	ret = devm_request_irq(&pdev->dev, ipu->irq_err,
			ipu_err_irq_handler, 0, pdev->name, ipu);
	if (ret) {
		dev_err(ipu->dev, "request ERR interrupt failed\n");
		return ret;
	}

	ipu_base = res->start;

	ipu->cm_reg = devm_ioremap(&pdev->dev,
				ipu_base + devtype->cm_ofs, PAGE_SIZE);
	ipu->ic_reg = devm_ioremap(&pdev->dev,
				ipu_base + devtype->ic_ofs, PAGE_SIZE);
	ipu->idmac_reg = devm_ioremap(&pdev->dev,
				ipu_base + devtype->idmac_ofs, PAGE_SIZE);
	/* DP Registers are accessed thru the SRM */
	ipu->dp_reg = devm_ioremap(&pdev->dev,
				ipu_base + devtype->srm_ofs, PAGE_SIZE);
	ipu->dc_reg = devm_ioremap(&pdev->dev,
				ipu_base + devtype->dc_ofs, PAGE_SIZE);
	ipu->dmfc_reg = devm_ioremap(&pdev->dev,
				ipu_base + devtype->dmfc_ofs, PAGE_SIZE);
	ipu->di_reg[0] = devm_ioremap(&pdev->dev,
				ipu_base + devtype->di0_ofs, PAGE_SIZE);
	ipu->di_reg[1] = devm_ioremap(&pdev->dev,
				ipu_base + devtype->di1_ofs, PAGE_SIZE);
	ipu->smfc_reg = devm_ioremap(&pdev->dev,
				ipu_base + devtype->smfc_ofs, PAGE_SIZE);
	ipu->csi_reg[0] = devm_ioremap(&pdev->dev,
				ipu_base + devtype->csi0_ofs, PAGE_SIZE);
	ipu->csi_reg[1] = devm_ioremap(&pdev->dev,
				ipu_base + devtype->csi1_ofs, PAGE_SIZE);
	ipu->cpmem_base = devm_ioremap(&pdev->dev,
				ipu_base + devtype->cpmem_ofs, SZ_128K);
	ipu->tpmem_base = devm_ioremap(&pdev->dev,
				ipu_base + devtype->tpm_ofs, SZ_64K);
	ipu->dc_tmpl_reg = devm_ioremap(&pdev->dev,
				ipu_base + devtype->dc_tmpl_ofs, SZ_128K);
	ipu->vdi_reg = devm_ioremap(&pdev->dev,
				ipu_base + devtype->vdi_ofs, PAGE_SIZE);
	if (!ipu->cm_reg || !ipu->ic_reg || !ipu->idmac_reg ||
		!ipu->dp_reg || !ipu->dc_reg || !ipu->dmfc_reg ||
		!ipu->di_reg[0] || !ipu->di_reg[1] || !ipu->smfc_reg ||
		!ipu->csi_reg[0] || !ipu->csi_reg[1] || !ipu->cpmem_base ||
		!ipu->tpmem_base || !ipu->dc_tmpl_reg || !ipu->vdi_reg)
		return -ENOMEM;

	dev_dbg(ipu->dev, "IPU CM Regs = %p\n", ipu->cm_reg);
	dev_dbg(ipu->dev, "IPU IC Regs = %p\n", ipu->ic_reg);
	dev_dbg(ipu->dev, "IPU IDMAC Regs = %p\n", ipu->idmac_reg);
	dev_dbg(ipu->dev, "IPU DP Regs = %p\n", ipu->dp_reg);
	dev_dbg(ipu->dev, "IPU DC Regs = %p\n", ipu->dc_reg);
	dev_dbg(ipu->dev, "IPU DMFC Regs = %p\n", ipu->dmfc_reg);
	dev_dbg(ipu->dev, "IPU DI0 Regs = %p\n", ipu->di_reg[0]);
	dev_dbg(ipu->dev, "IPU DI1 Regs = %p\n", ipu->di_reg[1]);
	dev_dbg(ipu->dev, "IPU SMFC Regs = %p\n", ipu->smfc_reg);
	dev_dbg(ipu->dev, "IPU CSI0 Regs = %p\n", ipu->csi_reg[0]);
	dev_dbg(ipu->dev, "IPU CSI1 Regs = %p\n", ipu->csi_reg[1]);
	dev_dbg(ipu->dev, "IPU CPMem = %p\n", ipu->cpmem_base);
	dev_dbg(ipu->dev, "IPU TPMem = %p\n", ipu->tpmem_base);
	dev_dbg(ipu->dev, "IPU DC Template Mem = %p\n", ipu->dc_tmpl_reg);
	dev_dbg(ipu->dev, "IPU VDI Regs = %p\n", ipu->vdi_reg);

	ipu->ipu_clk = devm_clk_get(ipu->dev, "bus");
	if (IS_ERR(ipu->ipu_clk)) {
		dev_err(ipu->dev, "clk_get ipu failed");
		return PTR_ERR(ipu->ipu_clk);
	}

	/* ipu_clk is always prepared */
	ret = clk_prepare_enable(ipu->ipu_clk);
	if (ret < 0) {
		dev_err(ipu->dev, "ipu clk enable failed\n");
		return ret;
	}

	ipu->prg_clk = devm_clk_get(ipu->dev, "prg");
	if (IS_ERR(ipu->prg_clk))
		ipu->prg_clk = NULL;

	ipu->online = true;

	platform_set_drvdata(pdev, ipu);

	if (!bypass_reset) {
		ret = device_reset(&pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "failed to reset: %d\n", ret);
			return ret;
		}

		ipu_mem_reset(ipu);

		ipu_disp_init(ipu);

		/* Set MCU_T to divide MCU access window into 2 */
		ipu_cm_write(ipu, 0x00400000L | (IPU_MCU_T_DEFAULT << 18),
			     IPU_DISP_GEN);
	}

	/* setup ipu clk tree after ipu reset  */
	ret = ipu_clk_setup_enable(ipu);
	if (ret < 0) {
		dev_err(ipu->dev, "ipu clk setup failed\n");
		ipu->online = false;
		return ret;
	}

	if (devtype->idmac_used_bufs_present) {
		reg = ipu_idmac_read(ipu, IDMAC_CONF);
		if (iputype->idmac_used_bufs_en_r)
			reg |= IDMAC_CONF_USED_BUFS_EN_R;
		else
			reg &= ~IDMAC_CONF_USED_BUFS_EN_R;
		if (iputype->idmac_used_bufs_en_w)
			reg |= IDMAC_CONF_USED_BUFS_EN_W;
		else
			reg &= ~IDMAC_CONF_USED_BUFS_EN_W;

		reg &= ~IDMAC_CONF_USED_BUFS_MAX_R_MASK;
		reg |= (iputype->idmac_used_bufs_max_r <<
			IDMAC_CONF_USED_BUFS_MAX_R_OFFSET);
		reg &= ~IDMAC_CONF_USED_BUFS_MAX_W_MASK;
		reg |= (iputype->idmac_used_bufs_max_w <<
			IDMAC_CONF_USED_BUFS_MAX_W_OFFSET);
		ipu_idmac_write(ipu, reg, IDMAC_CONF);
	}

	/* Set sync refresh channels and CSI->mem channel as high priority */
	ipu_idmac_write(ipu, 0x18800003L, IDMAC_CHA_PRI(0));

	/* Enable error interrupts by default */
	ipu_cm_write(ipu, 0xFFFFFFFF, IPU_INT_CTRL(5));
	ipu_cm_write(ipu, 0xFFFFFFFF, IPU_INT_CTRL(6));
	ipu_cm_write(ipu, 0xFFFFFFFF, IPU_INT_CTRL(9));
	ipu_cm_write(ipu, 0xFFFFFFFF, IPU_INT_CTRL(10));

	if (!bypass_reset)
		clk_disable(ipu->ipu_clk);

	register_ipu_device(ipu, id);

	pm_runtime_enable(&pdev->dev);

	return ret;
}

int ipu_remove(struct platform_device *pdev)
{
	struct ipu_soc *ipu = platform_get_drvdata(pdev);

	unregister_ipu_device(ipu, ipu->id);

	clk_put(ipu->ipu_clk);

	return 0;
}

void ipu_dump_registers(struct ipu_soc *ipu)
{
	dev_dbg(ipu->dev, "IPU_CONF = \t0x%08X\n", ipu_cm_read(ipu, IPU_CONF));
	dev_dbg(ipu->dev, "IDMAC_CONF = \t0x%08X\n", ipu_idmac_read(ipu, IDMAC_CONF));
	dev_dbg(ipu->dev, "IDMAC_CHA_EN1 = \t0x%08X\n",
	       ipu_idmac_read(ipu, IDMAC_CHA_EN(0)));
	dev_dbg(ipu->dev, "IDMAC_CHA_EN2 = \t0x%08X\n",
	       ipu_idmac_read(ipu, IDMAC_CHA_EN(32)));
	dev_dbg(ipu->dev, "IDMAC_CHA_PRI1 = \t0x%08X\n",
	       ipu_idmac_read(ipu, IDMAC_CHA_PRI(0)));
	dev_dbg(ipu->dev, "IDMAC_CHA_PRI2 = \t0x%08X\n",
	       ipu_idmac_read(ipu, IDMAC_CHA_PRI(32)));
	dev_dbg(ipu->dev, "IDMAC_BAND_EN1 = \t0x%08X\n",
	       ipu_idmac_read(ipu, IDMAC_BAND_EN(ipu->devtype, 0)));
	dev_dbg(ipu->dev, "IDMAC_BAND_EN2 = \t0x%08X\n",
	       ipu_idmac_read(ipu, IDMAC_BAND_EN(ipu->devtype, 32)));
	dev_dbg(ipu->dev, "IPU_CHA_DB_MODE_SEL0 = \t0x%08X\n",
	       ipu_cm_read(ipu, IPU_CHA_DB_MODE_SEL(0)));
	dev_dbg(ipu->dev, "IPU_CHA_DB_MODE_SEL1 = \t0x%08X\n",
	       ipu_cm_read(ipu, IPU_CHA_DB_MODE_SEL(32)));
	if (ipu->devtype >= IPUv3EX) {
		dev_dbg(ipu->dev, "IPU_CHA_TRB_MODE_SEL0 = \t0x%08X\n",
		       ipu_cm_read(ipu, IPU_CHA_TRB_MODE_SEL(ipu->devtype, 0)));
		dev_dbg(ipu->dev, "IPU_CHA_TRB_MODE_SEL1 = \t0x%08X\n",
		       ipu_cm_read(ipu,
				IPU_CHA_TRB_MODE_SEL(ipu->devtype, 32)));
	}
	dev_dbg(ipu->dev, "DMFC_WR_CHAN = \t0x%08X\n",
	       ipu_dmfc_read(ipu, DMFC_WR_CHAN));
	dev_dbg(ipu->dev, "DMFC_WR_CHAN_DEF = \t0x%08X\n",
	       ipu_dmfc_read(ipu, DMFC_WR_CHAN_DEF));
	dev_dbg(ipu->dev, "DMFC_DP_CHAN = \t0x%08X\n",
	       ipu_dmfc_read(ipu, DMFC_DP_CHAN));
	dev_dbg(ipu->dev, "DMFC_DP_CHAN_DEF = \t0x%08X\n",
	       ipu_dmfc_read(ipu, DMFC_DP_CHAN_DEF));
	dev_dbg(ipu->dev, "DMFC_IC_CTRL = \t0x%08X\n",
	       ipu_dmfc_read(ipu, DMFC_IC_CTRL));
	dev_dbg(ipu->dev, "IPU_FS_PROC_FLOW1 = \t0x%08X\n",
	       ipu_cm_read(ipu, IPU_FS_PROC_FLOW1));
	dev_dbg(ipu->dev, "IPU_FS_PROC_FLOW2 = \t0x%08X\n",
	       ipu_cm_read(ipu, IPU_FS_PROC_FLOW2));
	dev_dbg(ipu->dev, "IPU_FS_PROC_FLOW3 = \t0x%08X\n",
	       ipu_cm_read(ipu, IPU_FS_PROC_FLOW3));
	dev_dbg(ipu->dev, "IPU_FS_DISP_FLOW1 = \t0x%08X\n",
	       ipu_cm_read(ipu, IPU_FS_DISP_FLOW1));
	dev_dbg(ipu->dev, "IPU_VDIC_VDI_FSIZE = \t0x%08X\n",
	       ipu_vdi_read(ipu, VDI_FSIZE));
	dev_dbg(ipu->dev, "IPU_VDIC_VDI_C = \t0x%08X\n",
	       ipu_vdi_read(ipu, VDI_C));
	dev_dbg(ipu->dev, "IPU_IC_CONF = \t0x%08X\n",
	       ipu_ic_read(ipu, IC_CONF));
}

/*!
 * This function is called to initialize a logical IPU channel.
 *
 * @param	ipu	ipu handler
 * @param       channel Input parameter for the logical channel ID to init.
 *
 * @param       params  Input parameter containing union of channel
 *                      initialization parameters.
 *
 * @return      Returns 0 on success or negative error code on fail
 */
int32_t ipu_init_channel(struct ipu_soc *ipu, ipu_channel_t channel, ipu_channel_params_t *params)
{
	int ret = 0;
	bool bad_pixfmt;
	uint32_t ipu_conf, reg, in_g_pixel_fmt, sec_dma;

	dev_dbg(ipu->dev, "init channel = %d\n", IPU_CHAN_ID(channel));

	ret = pm_runtime_get_sync(ipu->dev);
	if (ret < 0) {
		dev_err(ipu->dev, "ch = %d, pm_runtime_get failed:%d!\n",
				IPU_CHAN_ID(channel), ret);
		dump_stack();
		return ret;
	}
	/*
	 * Here, ret could be 1 if the device's runtime PM status was
	 * already 'active', so clear it to be 0.
	 */
	ret = 0;

	_ipu_get(ipu);

	mutex_lock(&ipu->mutex_lock);

	/* Re-enable error interrupts every time a channel is initialized */
	ipu_cm_write(ipu, 0xFFFFFFFF, IPU_INT_CTRL(5));
	ipu_cm_write(ipu, 0xFFFFFFFF, IPU_INT_CTRL(6));
	ipu_cm_write(ipu, 0xFFFFFFFF, IPU_INT_CTRL(9));
	ipu_cm_write(ipu, 0xFFFFFFFF, IPU_INT_CTRL(10));

	if (ipu->channel_init_mask & (1L << IPU_CHAN_ID(channel))) {
		dev_warn(ipu->dev, "Warning: channel already initialized %d\n",
			IPU_CHAN_ID(channel));
	}

	ipu_conf = ipu_cm_read(ipu, IPU_CONF);

	switch (channel) {
	case CSI_MEM0:
	case CSI_MEM1:
	case CSI_MEM2:
	case CSI_MEM3:
		if (params->csi_mem.csi > 1) {
			ret = -EINVAL;
			goto err;
		}

		if (params->csi_mem.interlaced)
			ipu->chan_is_interlaced[channel_2_dma(channel,
				IPU_OUTPUT_BUFFER)] = true;
		else
			ipu->chan_is_interlaced[channel_2_dma(channel,
				IPU_OUTPUT_BUFFER)] = false;

		ipu->smfc_use_count++;
		ipu->csi_channel[params->csi_mem.csi] = channel;

		/*SMFC setting*/
		if (params->csi_mem.mipi_en) {
			ipu_conf |= (1 << (IPU_CONF_CSI0_DATA_SOURCE_OFFSET +
				params->csi_mem.csi));
			_ipu_smfc_init(ipu, channel, params->csi_mem.mipi_vc,
				params->csi_mem.csi);
			_ipu_csi_set_mipi_di(ipu, params->csi_mem.mipi_vc,
				params->csi_mem.mipi_id, params->csi_mem.csi);
		} else {
			ipu_conf &= ~(1 << (IPU_CONF_CSI0_DATA_SOURCE_OFFSET +
				params->csi_mem.csi));
			_ipu_smfc_init(ipu, channel, 0, params->csi_mem.csi);
		}

		/*CSI data (include compander) dest*/
		_ipu_csi_init(ipu, channel, params->csi_mem.csi);
		break;
	case CSI_PRP_ENC_MEM:
		if (params->csi_prp_enc_mem.csi > 1) {
			ret = -EINVAL;
			goto err;
		}
		if ((ipu->using_ic_dirct_ch == MEM_VDI_PRP_VF_MEM) ||
			(ipu->using_ic_dirct_ch == MEM_VDI_MEM)) {
			ret = -EINVAL;
			goto err;
		}
		ipu->using_ic_dirct_ch = CSI_PRP_ENC_MEM;

		ipu->ic_use_count++;
		ipu->csi_channel[params->csi_prp_enc_mem.csi] = channel;

		if (params->csi_prp_enc_mem.mipi_en) {
			ipu_conf |= (1 << (IPU_CONF_CSI0_DATA_SOURCE_OFFSET +
				params->csi_prp_enc_mem.csi));
			_ipu_csi_set_mipi_di(ipu,
				params->csi_prp_enc_mem.mipi_vc,
				params->csi_prp_enc_mem.mipi_id,
				params->csi_prp_enc_mem.csi);
		} else
			ipu_conf &= ~(1 << (IPU_CONF_CSI0_DATA_SOURCE_OFFSET +
				params->csi_prp_enc_mem.csi));

		/*CSI0/1 feed into IC*/
		ipu_conf &= ~IPU_CONF_IC_INPUT;
		if (params->csi_prp_enc_mem.csi)
			ipu_conf |= IPU_CONF_CSI_SEL;
		else
			ipu_conf &= ~IPU_CONF_CSI_SEL;

		/*PRP skip buffer in memory, only valid when RWS_EN is true*/
		reg = ipu_cm_read(ipu, IPU_FS_PROC_FLOW1);
		ipu_cm_write(ipu, reg & ~FS_ENC_IN_VALID, IPU_FS_PROC_FLOW1);

		/*CSI data (include compander) dest*/
		_ipu_csi_init(ipu, channel, params->csi_prp_enc_mem.csi);
		_ipu_ic_init_prpenc(ipu, params, true);
		break;
	case CSI_PRP_VF_MEM:
		if (params->csi_prp_vf_mem.csi > 1) {
			ret = -EINVAL;
			goto err;
		}
		if ((ipu->using_ic_dirct_ch == MEM_VDI_PRP_VF_MEM) ||
			(ipu->using_ic_dirct_ch == MEM_VDI_MEM)) {
			ret = -EINVAL;
			goto err;
		}
		ipu->using_ic_dirct_ch = CSI_PRP_VF_MEM;

		ipu->ic_use_count++;
		ipu->csi_channel[params->csi_prp_vf_mem.csi] = channel;

		if (params->csi_prp_vf_mem.mipi_en) {
			ipu_conf |= (1 << (IPU_CONF_CSI0_DATA_SOURCE_OFFSET +
				params->csi_prp_vf_mem.csi));
			_ipu_csi_set_mipi_di(ipu,
				params->csi_prp_vf_mem.mipi_vc,
				params->csi_prp_vf_mem.mipi_id,
				params->csi_prp_vf_mem.csi);
		} else
			ipu_conf &= ~(1 << (IPU_CONF_CSI0_DATA_SOURCE_OFFSET +
				params->csi_prp_vf_mem.csi));

		/*CSI0/1 feed into IC*/
		ipu_conf &= ~IPU_CONF_IC_INPUT;
		if (params->csi_prp_vf_mem.csi)
			ipu_conf |= IPU_CONF_CSI_SEL;
		else
			ipu_conf &= ~IPU_CONF_CSI_SEL;

		/*PRP skip buffer in memory, only valid when RWS_EN is true*/
		reg = ipu_cm_read(ipu, IPU_FS_PROC_FLOW1);
		ipu_cm_write(ipu, reg & ~FS_VF_IN_VALID, IPU_FS_PROC_FLOW1);

		/*CSI data (include compander) dest*/
		_ipu_csi_init(ipu, channel, params->csi_prp_vf_mem.csi);
		_ipu_ic_init_prpvf(ipu, params, true);
		break;
	case MEM_PRP_VF_MEM:
		if (params->mem_prp_vf_mem.graphics_combine_en) {
			sec_dma = channel_2_dma(channel, IPU_GRAPH_IN_BUFFER);
			in_g_pixel_fmt = params->mem_prp_vf_mem.in_g_pixel_fmt;
			bad_pixfmt =
				_ipu_ch_param_bad_alpha_pos(in_g_pixel_fmt);

			if (params->mem_prp_vf_mem.alpha_chan_en) {
				if (bad_pixfmt) {
					dev_err(ipu->dev, "bad pixel format "
						"for graphics plane from "
						"ch%d\n", sec_dma);
					ret = -EINVAL;
					goto err;
				}
				ipu->thrd_chan_en[IPU_CHAN_ID(channel)] = true;
			}
			ipu->sec_chan_en[IPU_CHAN_ID(channel)] = true;
		}

		reg = ipu_cm_read(ipu, IPU_FS_PROC_FLOW1);
		ipu_cm_write(ipu, reg | FS_VF_IN_VALID, IPU_FS_PROC_FLOW1);

		_ipu_ic_init_prpvf(ipu, params, false);
		ipu->ic_use_count++;
		break;
	case MEM_VDI_PRP_VF_MEM:
		if ((ipu->using_ic_dirct_ch == CSI_PRP_VF_MEM) ||
			(ipu->using_ic_dirct_ch == MEM_VDI_MEM) ||
		     (ipu->using_ic_dirct_ch == CSI_PRP_ENC_MEM)) {
			ret = -EINVAL;
			goto err;
		}
		ipu->using_ic_dirct_ch = MEM_VDI_PRP_VF_MEM;
		ipu->ic_use_count++;
		ipu->vdi_use_count++;
		reg = ipu_cm_read(ipu, IPU_FS_PROC_FLOW1);
		reg &= ~FS_VDI_SRC_SEL_MASK;
		ipu_cm_write(ipu, reg , IPU_FS_PROC_FLOW1);

		if (params->mem_prp_vf_mem.graphics_combine_en)
			ipu->sec_chan_en[IPU_CHAN_ID(channel)] = true;
		_ipu_ic_init_prpvf(ipu, params, false);
		_ipu_vdi_init(ipu, channel, params);
		break;
	case MEM_VDI_PRP_VF_MEM_P:
	case MEM_VDI_PRP_VF_MEM_N:
	case MEM_VDI_MEM_P:
	case MEM_VDI_MEM_N:
		_ipu_vdi_init(ipu, channel, params);
		break;
	case MEM_VDI_MEM:
		if ((ipu->using_ic_dirct_ch == CSI_PRP_VF_MEM) ||
			(ipu->using_ic_dirct_ch == MEM_VDI_PRP_VF_MEM) ||
		     (ipu->using_ic_dirct_ch == CSI_PRP_ENC_MEM)) {
			ret = -EINVAL;
			goto err;
		}
		ipu->using_ic_dirct_ch = MEM_VDI_MEM;
		ipu->ic_use_count++;
		ipu->vdi_use_count++;
		_ipu_vdi_init(ipu, channel, params);
		break;
	case MEM_ROT_VF_MEM:
		ipu->ic_use_count++;
		ipu->rot_use_count++;
		_ipu_ic_init_rotate_vf(ipu, params);
		break;
	case MEM_PRP_ENC_MEM:
		ipu->ic_use_count++;
		reg = ipu_cm_read(ipu, IPU_FS_PROC_FLOW1);
		ipu_cm_write(ipu, reg | FS_ENC_IN_VALID, IPU_FS_PROC_FLOW1);
		_ipu_ic_init_prpenc(ipu, params, false);
		break;
	case MEM_ROT_ENC_MEM:
		ipu->ic_use_count++;
		ipu->rot_use_count++;
		_ipu_ic_init_rotate_enc(ipu, params);
		break;
	case MEM_PP_MEM:
		if (params->mem_pp_mem.graphics_combine_en) {
			sec_dma = channel_2_dma(channel, IPU_GRAPH_IN_BUFFER);
			in_g_pixel_fmt = params->mem_pp_mem.in_g_pixel_fmt;
			bad_pixfmt =
				_ipu_ch_param_bad_alpha_pos(in_g_pixel_fmt);

			if (params->mem_pp_mem.alpha_chan_en) {
				if (bad_pixfmt) {
					dev_err(ipu->dev, "bad pixel format "
						"for graphics plane from "
						"ch%d\n", sec_dma);
					ret = -EINVAL;
					goto err;
				}
				ipu->thrd_chan_en[IPU_CHAN_ID(channel)] = true;
			}

			ipu->sec_chan_en[IPU_CHAN_ID(channel)] = true;
		}

		_ipu_ic_init_pp(ipu, params);
		ipu->ic_use_count++;
		break;
	case MEM_ROT_PP_MEM:
		_ipu_ic_init_rotate_pp(ipu, params);
		ipu->ic_use_count++;
		ipu->rot_use_count++;
		break;
	case MEM_DC_SYNC:
		if (params->mem_dc_sync.di > 1) {
			ret = -EINVAL;
			goto err;
		}

		ipu->dc_di_assignment[1] = params->mem_dc_sync.di;
		_ipu_dc_init(ipu, 1, params->mem_dc_sync.di,
			     params->mem_dc_sync.interlaced,
			     params->mem_dc_sync.out_pixel_fmt);
		ipu->di_use_count[params->mem_dc_sync.di]++;
		ipu->dc_use_count++;
		ipu->dmfc_use_count++;
		break;
	case MEM_BG_SYNC:
		if (params->mem_dp_bg_sync.di > 1) {
			ret = -EINVAL;
			goto err;
		}

		if (params->mem_dp_bg_sync.alpha_chan_en)
			ipu->thrd_chan_en[IPU_CHAN_ID(channel)] = true;

		ipu->dc_di_assignment[5] = params->mem_dp_bg_sync.di;
		_ipu_dp_init(ipu, channel, params->mem_dp_bg_sync.in_pixel_fmt,
			     params->mem_dp_bg_sync.out_pixel_fmt);
		_ipu_dc_init(ipu, 5, params->mem_dp_bg_sync.di,
			     params->mem_dp_bg_sync.interlaced,
			     params->mem_dp_bg_sync.out_pixel_fmt);
		ipu->di_use_count[params->mem_dp_bg_sync.di]++;
		ipu->dc_use_count++;
		ipu->dp_use_count++;
		ipu->dmfc_use_count++;
		break;
	case MEM_FG_SYNC:
		_ipu_dp_init(ipu, channel, params->mem_dp_fg_sync.in_pixel_fmt,
			     params->mem_dp_fg_sync.out_pixel_fmt);

		if (params->mem_dp_fg_sync.alpha_chan_en)
			ipu->thrd_chan_en[IPU_CHAN_ID(channel)] = true;

		ipu->dc_use_count++;
		ipu->dp_use_count++;
		ipu->dmfc_use_count++;
		break;
	case DIRECT_ASYNC0:
		if (params->direct_async.di > 1) {
			ret = -EINVAL;
			goto err;
		}

		ipu->dc_di_assignment[8] = params->direct_async.di;
		_ipu_dc_init(ipu, 8, params->direct_async.di, false, IPU_PIX_FMT_GENERIC);
		ipu->di_use_count[params->direct_async.di]++;
		ipu->dc_use_count++;
		break;
	case DIRECT_ASYNC1:
		if (params->direct_async.di > 1) {
			ret = -EINVAL;
			goto err;
		}

		ipu->dc_di_assignment[9] = params->direct_async.di;
		_ipu_dc_init(ipu, 9, params->direct_async.di, false, IPU_PIX_FMT_GENERIC);
		ipu->di_use_count[params->direct_async.di]++;
		ipu->dc_use_count++;
		break;
	default:
		dev_err(ipu->dev, "Missing channel initialization\n");
		break;
	}

	ipu->channel_init_mask |= 1L << IPU_CHAN_ID(channel);

	ipu_cm_write(ipu, ipu_conf, IPU_CONF);

err:
	mutex_unlock(&ipu->mutex_lock);
	return ret;
}
EXPORT_SYMBOL(ipu_init_channel);

/*!
 * This function is called to uninitialize a logical IPU channel.
 *
 * @param	ipu	ipu handler
 * @param       channel Input parameter for the logical channel ID to uninit.
 */
void ipu_uninit_channel(struct ipu_soc *ipu, ipu_channel_t channel)
{
	uint32_t reg;
	uint32_t in_dma, out_dma = 0;
	uint32_t ipu_conf;
	uint32_t dc_chan = 0;
	int ret;

	mutex_lock(&ipu->mutex_lock);

	if ((ipu->channel_init_mask & (1L << IPU_CHAN_ID(channel))) == 0) {
		dev_dbg(ipu->dev, "Channel already uninitialized %d\n",
			IPU_CHAN_ID(channel));
		mutex_unlock(&ipu->mutex_lock);
		return;
	}

	/* Make sure channel is disabled */
	/* Get input and output dma channels */
	in_dma = channel_2_dma(channel, IPU_VIDEO_IN_BUFFER);
	out_dma = channel_2_dma(channel, IPU_OUTPUT_BUFFER);

	if (idma_is_set(ipu, IDMAC_CHA_EN(in_dma), in_dma) ||
	    idma_is_set(ipu, IDMAC_CHA_EN(out_dma), out_dma)) {
		dev_err(ipu->dev,
			"Channel %d is not disabled, disable first\n",
			IPU_CHAN_ID(channel));
		mutex_unlock(&ipu->mutex_lock);
		return;
	}

	ipu_conf = ipu_cm_read(ipu, IPU_CONF);

	/* Reset the double buffer */
	reg = ipu_cm_read(ipu, IPU_CHA_DB_MODE_SEL(in_dma));
	ipu_cm_write(ipu, reg & ~idma_mask(in_dma), IPU_CHA_DB_MODE_SEL(in_dma));
	reg = ipu_cm_read(ipu, IPU_CHA_DB_MODE_SEL(out_dma));
	ipu_cm_write(ipu, reg & ~idma_mask(out_dma), IPU_CHA_DB_MODE_SEL(out_dma));

	/* Reset the triple buffer */
	reg = ipu_cm_read(ipu, IPU_CHA_TRB_MODE_SEL(ipu->devtype, in_dma));
	ipu_cm_write(ipu, reg & ~idma_mask(in_dma),
				IPU_CHA_TRB_MODE_SEL(ipu->devtype, in_dma));
	reg = ipu_cm_read(ipu, IPU_CHA_TRB_MODE_SEL(ipu->devtype, out_dma));
	ipu_cm_write(ipu, reg & ~idma_mask(out_dma),
				IPU_CHA_TRB_MODE_SEL(ipu->devtype, out_dma));

	if (_ipu_is_ic_chan(in_dma) || _ipu_is_dp_graphic_chan(in_dma)) {
		ipu->sec_chan_en[IPU_CHAN_ID(channel)] = false;
		ipu->thrd_chan_en[IPU_CHAN_ID(channel)] = false;
	}

	switch (channel) {
	case CSI_MEM0:
	case CSI_MEM1:
	case CSI_MEM2:
	case CSI_MEM3:
		ipu->smfc_use_count--;
		if (ipu->csi_channel[0] == channel) {
			ipu->csi_channel[0] = CHAN_NONE;
		} else if (ipu->csi_channel[1] == channel) {
			ipu->csi_channel[1] = CHAN_NONE;
		}
		break;
	case CSI_PRP_ENC_MEM:
		ipu->ic_use_count--;
		if (ipu->using_ic_dirct_ch == CSI_PRP_ENC_MEM)
			ipu->using_ic_dirct_ch = 0;
		_ipu_ic_uninit_prpenc(ipu);
		if (ipu->csi_channel[0] == channel) {
			ipu->csi_channel[0] = CHAN_NONE;
		} else if (ipu->csi_channel[1] == channel) {
			ipu->csi_channel[1] = CHAN_NONE;
		}
		break;
	case CSI_PRP_VF_MEM:
		ipu->ic_use_count--;
		if (ipu->using_ic_dirct_ch == CSI_PRP_VF_MEM)
			ipu->using_ic_dirct_ch = 0;
		_ipu_ic_uninit_prpvf(ipu);
		if (ipu->csi_channel[0] == channel) {
			ipu->csi_channel[0] = CHAN_NONE;
		} else if (ipu->csi_channel[1] == channel) {
			ipu->csi_channel[1] = CHAN_NONE;
		}
		break;
	case MEM_PRP_VF_MEM:
		ipu->ic_use_count--;
		_ipu_ic_uninit_prpvf(ipu);
		reg = ipu_cm_read(ipu, IPU_FS_PROC_FLOW1);
		ipu_cm_write(ipu, reg & ~FS_VF_IN_VALID, IPU_FS_PROC_FLOW1);
		break;
	case MEM_VDI_PRP_VF_MEM:
		ipu->ic_use_count--;
		ipu->vdi_use_count--;
		if (ipu->using_ic_dirct_ch == MEM_VDI_PRP_VF_MEM)
			ipu->using_ic_dirct_ch = 0;
		_ipu_ic_uninit_prpvf(ipu);
		_ipu_vdi_uninit(ipu);
		reg = ipu_cm_read(ipu, IPU_FS_PROC_FLOW1);
		ipu_cm_write(ipu, reg & ~FS_VF_IN_VALID, IPU_FS_PROC_FLOW1);
		break;
	case MEM_VDI_MEM:
		ipu->ic_use_count--;
		ipu->vdi_use_count--;
		if (ipu->using_ic_dirct_ch == MEM_VDI_MEM)
			ipu->using_ic_dirct_ch = 0;
		_ipu_vdi_uninit(ipu);
		break;
	case MEM_VDI_PRP_VF_MEM_P:
	case MEM_VDI_PRP_VF_MEM_N:
	case MEM_VDI_MEM_P:
	case MEM_VDI_MEM_N:
		break;
	case MEM_ROT_VF_MEM:
		ipu->rot_use_count--;
		ipu->ic_use_count--;
		_ipu_ic_uninit_rotate_vf(ipu);
		break;
	case MEM_PRP_ENC_MEM:
		ipu->ic_use_count--;
		_ipu_ic_uninit_prpenc(ipu);
		reg = ipu_cm_read(ipu, IPU_FS_PROC_FLOW1);
		ipu_cm_write(ipu, reg & ~FS_ENC_IN_VALID, IPU_FS_PROC_FLOW1);
		break;
	case MEM_ROT_ENC_MEM:
		ipu->rot_use_count--;
		ipu->ic_use_count--;
		_ipu_ic_uninit_rotate_enc(ipu);
		break;
	case MEM_PP_MEM:
		ipu->ic_use_count--;
		_ipu_ic_uninit_pp(ipu);
		break;
	case MEM_ROT_PP_MEM:
		ipu->rot_use_count--;
		ipu->ic_use_count--;
		_ipu_ic_uninit_rotate_pp(ipu);
		break;
	case MEM_DC_SYNC:
		dc_chan = 1;
		_ipu_dc_uninit(ipu, 1);
		ipu->di_use_count[ipu->dc_di_assignment[1]]--;
		ipu->dc_use_count--;
		ipu->dmfc_use_count--;
		break;
	case MEM_BG_SYNC:
		dc_chan = 5;
		_ipu_dp_uninit(ipu, channel);
		_ipu_dc_uninit(ipu, 5);
		ipu->di_use_count[ipu->dc_di_assignment[5]]--;
		ipu->dc_use_count--;
		ipu->dp_use_count--;
		ipu->dmfc_use_count--;
		break;
	case MEM_FG_SYNC:
		_ipu_dp_uninit(ipu, channel);
		ipu->dc_use_count--;
		ipu->dp_use_count--;
		ipu->dmfc_use_count--;
		break;
	case DIRECT_ASYNC0:
		dc_chan = 8;
		_ipu_dc_uninit(ipu, 8);
		ipu->di_use_count[ipu->dc_di_assignment[8]]--;
		ipu->dc_use_count--;
		break;
	case DIRECT_ASYNC1:
		dc_chan = 9;
		_ipu_dc_uninit(ipu, 9);
		ipu->di_use_count[ipu->dc_di_assignment[9]]--;
		ipu->dc_use_count--;
		break;
	default:
		break;
	}

	if (ipu->ic_use_count == 0)
		ipu_conf &= ~IPU_CONF_IC_EN;
	if (ipu->vdi_use_count == 0) {
		ipu_conf &= ~IPU_CONF_ISP_EN;
		ipu_conf &= ~IPU_CONF_VDI_EN;
		ipu_conf &= ~IPU_CONF_IC_INPUT;
	}
	if (ipu->rot_use_count == 0)
		ipu_conf &= ~IPU_CONF_ROT_EN;
	if (ipu->dc_use_count == 0)
		ipu_conf &= ~IPU_CONF_DC_EN;
	if (ipu->dp_use_count == 0)
		ipu_conf &= ~IPU_CONF_DP_EN;
	if (ipu->dmfc_use_count == 0)
		ipu_conf &= ~IPU_CONF_DMFC_EN;
	if (ipu->di_use_count[0] == 0) {
		ipu_conf &= ~IPU_CONF_DI0_EN;
	}
	if (ipu->di_use_count[1] == 0) {
		ipu_conf &= ~IPU_CONF_DI1_EN;
	}
	if (ipu->smfc_use_count == 0)
		ipu_conf &= ~IPU_CONF_SMFC_EN;

	ipu_cm_write(ipu, ipu_conf, IPU_CONF);

	ipu->channel_init_mask &= ~(1L << IPU_CHAN_ID(channel));

	/*
	 * Disable pixel clk and its parent clock(if the parent clock
	 * usecount is 1) after clearing DC/DP/DI bits in IPU_CONF
	 * register to prevent LVDS display channel starvation.
	 */
	if (_ipu_is_primary_disp_chan(in_dma) &&
	    ipu->pixel_clk_en[ipu->dc_di_assignment[dc_chan]]) {
		clk_disable_unprepare(ipu->pixel_clk[ipu->dc_di_assignment[dc_chan]]);
		ipu->pixel_clk_en[ipu->dc_di_assignment[dc_chan]] = false;
	}

	mutex_unlock(&ipu->mutex_lock);

	_ipu_put(ipu);

	ret = pm_runtime_put_sync_suspend(ipu->dev);
	if (ret < 0) {
		dev_err(ipu->dev, "ch = %d, pm_runtime_put failed:%d!\n",
				IPU_CHAN_ID(channel), ret);
		dump_stack();
	}

	WARN_ON(ipu->ic_use_count < 0);
	WARN_ON(ipu->vdi_use_count < 0);
	WARN_ON(ipu->rot_use_count < 0);
	WARN_ON(ipu->dc_use_count < 0);
	WARN_ON(ipu->dp_use_count < 0);
	WARN_ON(ipu->dmfc_use_count < 0);
	WARN_ON(ipu->smfc_use_count < 0);
}
EXPORT_SYMBOL(ipu_uninit_channel);

/*!
 * This function is called to initialize buffer(s) for logical IPU channel.
 *
 * @param	ipu		ipu handler
 *
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       type            Input parameter which buffer to initialize.
 *
 * @param       pixel_fmt       Input parameter for pixel format of buffer.
 *                              Pixel format is a FOURCC ASCII code.
 *
 * @param       width           Input parameter for width of buffer in pixels.
 *
 * @param       height          Input parameter for height of buffer in pixels.
 *
 * @param       stride          Input parameter for stride length of buffer
 *                              in pixels.
 *
 * @param       rot_mode        Input parameter for rotation setting of buffer.
 *                              A rotation setting other than
 *                              IPU_ROTATE_VERT_FLIP
 *                              should only be used for input buffers of
 *                              rotation channels.
 *
 * @param       phyaddr_0       Input parameter buffer 0 physical address.
 *
 * @param       phyaddr_1       Input parameter buffer 1 physical address.
 *                              Setting this to a value other than NULL enables
 *                              double buffering mode.
 *
 * @param       phyaddr_2       Input parameter buffer 2 physical address.
 *                              Setting this to a value other than NULL enables
 *                              triple buffering mode, phyaddr_1 should not be
 *                              NULL then.
 *
 * @param       u		private u offset for additional cropping,
 *				zero if not used.
 *
 * @param       v		private v offset for additional cropping,
 *				zero if not used.
 *
 * @return      Returns 0 on success or negative error code on fail
 */
int32_t ipu_init_channel_buffer(struct ipu_soc *ipu, ipu_channel_t channel,
				ipu_buffer_t type,
				uint32_t pixel_fmt,
				uint16_t width, uint16_t height,
				uint32_t stride,
				ipu_rotate_mode_t rot_mode,
				dma_addr_t phyaddr_0, dma_addr_t phyaddr_1,
				dma_addr_t phyaddr_2,
				uint32_t u, uint32_t v)
{
	uint32_t reg;
	uint32_t dma_chan;
	uint32_t burst_size;

	dma_chan = channel_2_dma(channel, type);
	if (!idma_is_valid(dma_chan))
		return -EINVAL;

	if (stride < width * bytes_per_pixel(pixel_fmt))
		stride = width * bytes_per_pixel(pixel_fmt);

	if (stride % 4) {
		dev_err(ipu->dev,
			"Stride not 32-bit aligned, stride = %d\n", stride);
		return -EINVAL;
	}
	/* IC & IRT channels' width must be multiple of 8 pixels */
	if ((_ipu_is_ic_chan(dma_chan) || _ipu_is_irt_chan(dma_chan))
		&& (width % 8)) {
		dev_err(ipu->dev, "Width must be 8 pixel multiple\n");
		return -EINVAL;
	}

	if (_ipu_is_vdi_out_chan(dma_chan) &&
		((width < 16) || (height < 16) || (width % 2) || (height % 4))) {
		dev_err(ipu->dev, "vdi width/height limited err\n");
		return -EINVAL;
	}

	/* IPUv3EX and IPUv3M support triple buffer */
	if ((!_ipu_is_trb_chan(ipu, dma_chan)) && phyaddr_2) {
		dev_err(ipu->dev, "Chan%d doesn't support triple buffer "
				   "mode\n", dma_chan);
		return -EINVAL;
	}
	if (!phyaddr_1 && phyaddr_2) {
		dev_err(ipu->dev, "Chan%d's buf1 physical addr is NULL for "
				   "triple buffer mode\n", dma_chan);
		return -EINVAL;
	}

	mutex_lock(&ipu->mutex_lock);

	/* Build parameter memory data for DMA channel */
	_ipu_ch_param_init(ipu, dma_chan, pixel_fmt, width, height, stride, u, v, 0,
			   phyaddr_0, phyaddr_1, phyaddr_2);

	/* Set correlative channel parameter of local alpha channel */
	if ((_ipu_is_ic_graphic_chan(dma_chan) ||
	     _ipu_is_dp_graphic_chan(dma_chan)) &&
	    (ipu->thrd_chan_en[IPU_CHAN_ID(channel)] == true)) {
		_ipu_ch_param_set_alpha_use_separate_channel(ipu, dma_chan, true);
		_ipu_ch_param_set_alpha_buffer_memory(ipu, dma_chan);
		_ipu_ch_param_set_alpha_condition_read(ipu, dma_chan);
		/* fix alpha width as 8 and burst size as 16*/
		_ipu_ch_params_set_alpha_width(ipu, dma_chan, 8);
		_ipu_ch_param_set_burst_size(ipu, dma_chan, 16);
	} else if (_ipu_is_ic_graphic_chan(dma_chan) &&
		   ipu_pixel_format_has_alpha(pixel_fmt))
		_ipu_ch_param_set_alpha_use_separate_channel(ipu, dma_chan, false);

	if (rot_mode)
		_ipu_ch_param_set_rotation(ipu, dma_chan, rot_mode);

	/* IC and ROT channels have restriction of 8 or 16 pix burst length */
	if (_ipu_is_ic_chan(dma_chan) || _ipu_is_vdi_out_chan(dma_chan)) {
		if ((width % 16) == 0)
			_ipu_ch_param_set_burst_size(ipu, dma_chan, 16);
		else
			_ipu_ch_param_set_burst_size(ipu, dma_chan, 8);
	} else if (_ipu_is_irt_chan(dma_chan)) {
		_ipu_ch_param_set_burst_size(ipu, dma_chan, 8);
		_ipu_ch_param_set_block_mode(ipu, dma_chan);
	} else if (_ipu_is_dmfc_chan(dma_chan)) {
		burst_size = _ipu_ch_param_get_burst_size(ipu, dma_chan);
		_ipu_dmfc_set_wait4eot(ipu, dma_chan, width);
		_ipu_dmfc_set_burst_size(ipu, dma_chan, burst_size);
	}

	if (_ipu_disp_chan_is_interlaced(ipu, channel) ||
		ipu->chan_is_interlaced[dma_chan])
		_ipu_ch_param_set_interlaced_scan(ipu, dma_chan);

	if (_ipu_is_ic_chan(dma_chan) || _ipu_is_irt_chan(dma_chan) ||
		_ipu_is_vdi_out_chan(dma_chan)) {
		burst_size = _ipu_ch_param_get_burst_size(ipu, dma_chan);
		_ipu_ic_idma_init(ipu, dma_chan, width, height, burst_size,
			rot_mode);
	} else if (_ipu_is_smfc_chan(dma_chan)) {
		burst_size = _ipu_ch_param_get_burst_size(ipu, dma_chan);
		/*
		 * This is different from IPUv3 spec, but it is confirmed
		 * in IPUforum that SMFC burst size should be NPB[6:3]
		 * when IDMAC works in 16-bit generic data mode.
		 */
		if (pixel_fmt == IPU_PIX_FMT_GENERIC)
			/* 8 bits per pixel */
			burst_size = burst_size >> 4;
		else if (pixel_fmt == IPU_PIX_FMT_GENERIC_16)
			/* 16 bits per pixel */
			burst_size = burst_size >> 3;
		else
			burst_size = burst_size >> 2;
		_ipu_smfc_set_burst_size(ipu, channel, burst_size-1);
	}

	switch (dma_chan) {
	case 0:
	case 1:
	case 2:
	case 3:
		_ipu_ch_param_set_axi_id(ipu, dma_chan, ipu->ch0123_axi);
		break;
	case 23:
		_ipu_ch_param_set_axi_id(ipu, dma_chan, ipu->ch23_axi);
		break;
	case 27:
		_ipu_ch_param_set_axi_id(ipu, dma_chan, ipu->ch27_axi);
		break;
	case 28:
		_ipu_ch_param_set_axi_id(ipu, dma_chan, ipu->ch28_axi);
		break;
	default:
		_ipu_ch_param_set_axi_id(ipu, dma_chan, ipu->normal_axi);
		break;
	}

	if (idma_is_set(ipu, IDMAC_CHA_PRI(dma_chan), dma_chan) &&
	    ipu->devtype == IPUv3H) {
		uint32_t reg = IDMAC_CH_LOCK_EN_1(ipu->devtype);
		uint32_t value = 0;

		switch (dma_chan) {
		case 5:
			value = 0x3;
			break;
		case 11:
			value = 0x3 << 2;
			break;
		case 12:
			value = 0x3 << 4;
			break;
		case 14:
			value = 0x3 << 6;
			break;
		case 15:
			value = 0x3 << 8;
			break;
		case 20:
			value = 0x3 << 10;
			break;
		case 21:
			value = 0x3 << 12;
			break;
		case 22:
			value = 0x3 << 14;
			break;
		case 23:
			value = 0x3 << 16;
			break;
		case 27:
			value = 0x3 << 18;
			break;
		case 28:
			value = 0x3 << 20;
			break;
		case 45:
			reg = IDMAC_CH_LOCK_EN_2(ipu->devtype);
			value = 0x3 << 0;
			break;
		case 46:
			reg = IDMAC_CH_LOCK_EN_2(ipu->devtype);
			value = 0x3 << 2;
			break;
		case 47:
			reg = IDMAC_CH_LOCK_EN_2(ipu->devtype);
			value = 0x3 << 4;
			break;
		case 48:
			reg = IDMAC_CH_LOCK_EN_2(ipu->devtype);
			value = 0x3 << 6;
			break;
		case 49:
			reg = IDMAC_CH_LOCK_EN_2(ipu->devtype);
			value = 0x3 << 8;
			break;
		case 50:
			reg = IDMAC_CH_LOCK_EN_2(ipu->devtype);
			value = 0x3 << 10;
			break;
		default:
			break;
		}
		value |= ipu_idmac_read(ipu, reg);
		ipu_idmac_write(ipu, value, reg);
	}

	_ipu_ch_param_dump(ipu, dma_chan);

	if (phyaddr_2 && ipu->devtype >= IPUv3EX) {
		reg = ipu_cm_read(ipu, IPU_CHA_DB_MODE_SEL(dma_chan));
		reg &= ~idma_mask(dma_chan);
		ipu_cm_write(ipu, reg, IPU_CHA_DB_MODE_SEL(dma_chan));

		reg = ipu_cm_read(ipu,
				IPU_CHA_TRB_MODE_SEL(ipu->devtype, dma_chan));
		reg |= idma_mask(dma_chan);
		ipu_cm_write(ipu, reg,
				IPU_CHA_TRB_MODE_SEL(ipu->devtype, dma_chan));

		/* Set IDMAC third buffer's cpmem number */
		/* See __ipu_ch_get_third_buf_cpmem_num() for mapping */
		ipu_idmac_write(ipu, 0x00444047L,
				IDMAC_SUB_ADDR_4(ipu->devtype));
		ipu_idmac_write(ipu, 0x46004241L,
				IDMAC_SUB_ADDR_3(ipu->devtype));
		ipu_idmac_write(ipu, 0x00000045L,
				IDMAC_SUB_ADDR_1(ipu->devtype));

		/* Reset to buffer 0 */
		ipu_cm_write(ipu, tri_cur_buf_mask(dma_chan),
				IPU_CHA_TRIPLE_CUR_BUF(ipu->devtype, dma_chan));
	} else {
		reg = ipu_cm_read(ipu,
				IPU_CHA_TRB_MODE_SEL(ipu->devtype, dma_chan));
		reg &= ~idma_mask(dma_chan);
		ipu_cm_write(ipu, reg,
				IPU_CHA_TRB_MODE_SEL(ipu->devtype, dma_chan));

		reg = ipu_cm_read(ipu, IPU_CHA_DB_MODE_SEL(dma_chan));
		if (phyaddr_1)
			reg |= idma_mask(dma_chan);
		else
			reg &= ~idma_mask(dma_chan);
		ipu_cm_write(ipu, reg, IPU_CHA_DB_MODE_SEL(dma_chan));

		/* Reset to buffer 0 */
		ipu_cm_write(ipu, idma_mask(dma_chan),
				IPU_CHA_CUR_BUF(ipu->devtype, dma_chan));

	}

	mutex_unlock(&ipu->mutex_lock);

	return 0;
}
EXPORT_SYMBOL(ipu_init_channel_buffer);

/*!
 * This function is called to update the physical address of a buffer for
 * a logical IPU channel.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       type            Input parameter which buffer to initialize.
 *
 * @param       bufNum          Input parameter for buffer number to update.
 *                              0 or 1 are the only valid values.
 *
 * @param       phyaddr         Input parameter buffer physical address.
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail. This function will fail if the buffer is set to ready.
 */
int32_t ipu_update_channel_buffer(struct ipu_soc *ipu, ipu_channel_t channel,
				ipu_buffer_t type, uint32_t bufNum, dma_addr_t phyaddr)
{
	uint32_t reg;
	int ret = 0;
	uint32_t dma_chan = channel_2_dma(channel, type);
	unsigned long lock_flags;

	if (dma_chan == IDMA_CHAN_INVALID)
		return -EINVAL;

	spin_lock_irqsave(&ipu->rdy_reg_spin_lock, lock_flags);
	if (bufNum == 0)
		reg = ipu_cm_read(ipu,
				IPU_CHA_BUF0_RDY(ipu->devtype, dma_chan));
	else if (bufNum == 1)
		reg = ipu_cm_read(ipu,
				IPU_CHA_BUF1_RDY(ipu->devtype, dma_chan));
	else
		reg = ipu_cm_read(ipu,
				IPU_CHA_BUF2_RDY(ipu->devtype, dma_chan));

	if ((reg & idma_mask(dma_chan)) == 0)
		_ipu_ch_param_set_buffer(ipu, dma_chan, bufNum, phyaddr);
	else
		ret = -EACCES;
	spin_unlock_irqrestore(&ipu->rdy_reg_spin_lock, lock_flags);

	return ret;
}
EXPORT_SYMBOL(ipu_update_channel_buffer);

/*!
 * This function is called to update the band mode setting for
 * a logical IPU channel.
 *
 * @param	ipu		ipu handler
 *
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       type            Input parameter which buffer to initialize.
 *
 * @param       band_height     Input parameter for band lines:
 *				shoule be log2(4/8/16/32/64/128/256).
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail.
 */
int32_t ipu_set_channel_bandmode(struct ipu_soc *ipu, ipu_channel_t channel,
				 ipu_buffer_t type, uint32_t band_height)
{
	uint32_t reg;
	int ret = 0;
	uint32_t dma_chan = channel_2_dma(channel, type);

	if ((2 > band_height) || (8 < band_height))
		return -EINVAL;

	mutex_lock(&ipu->mutex_lock);

	reg = ipu_idmac_read(ipu, IDMAC_BAND_EN(ipu->devtype, dma_chan));
	reg |= 1 << (dma_chan % 32);
	ipu_idmac_write(ipu, reg, IDMAC_BAND_EN(ipu->devtype, dma_chan));

	_ipu_ch_param_set_bandmode(ipu, dma_chan, band_height);
	dev_dbg(ipu->dev, "dma_chan:%d, band_height:%d.\n\n",
				dma_chan, 1 << band_height);
	mutex_unlock(&ipu->mutex_lock);

	return ret;
}
EXPORT_SYMBOL(ipu_set_channel_bandmode);

/*!
 * This function is called to initialize a buffer for logical IPU channel.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       type            Input parameter which buffer to initialize.
 *
 * @param       pixel_fmt       Input parameter for pixel format of buffer.
 *                              Pixel format is a FOURCC ASCII code.
 *
 * @param       width           Input parameter for width of buffer in pixels.
 *
 * @param       height          Input parameter for height of buffer in pixels.
 *
 * @param       stride          Input parameter for stride length of buffer
 *                              in pixels.
 *
 * @param       u		predefined private u offset for additional cropping,
 *								zero if not used.
 *
 * @param       v		predefined private v offset for additional cropping,
 *								zero if not used.
 *
 * @param			vertical_offset vertical offset for Y coordinate
 * 								in the existed frame
 *
 *
 * @param			horizontal_offset horizontal offset for X coordinate
 * 								in the existed frame
 *
 *
 * @return      Returns 0 on success or negative error code on fail
 *              This function will fail if any buffer is set to ready.
 */

int32_t ipu_update_channel_offset(struct ipu_soc *ipu,
				ipu_channel_t channel, ipu_buffer_t type,
				uint32_t pixel_fmt,
				uint16_t width, uint16_t height,
				uint32_t stride,
				uint32_t u, uint32_t v,
				uint32_t vertical_offset, uint32_t horizontal_offset)
{
	int ret = 0;
	uint32_t dma_chan = channel_2_dma(channel, type);
	unsigned long lock_flags;

	if (dma_chan == IDMA_CHAN_INVALID)
		return -EINVAL;

	spin_lock_irqsave(&ipu->rdy_reg_spin_lock, lock_flags);
	if ((ipu_cm_read(ipu, IPU_CHA_BUF0_RDY(ipu->devtype, dma_chan)) &
	     idma_mask(dma_chan)) ||
	    (ipu_cm_read(ipu, IPU_CHA_BUF1_RDY(ipu->devtype, dma_chan)) &
	     idma_mask(dma_chan)) ||
	    ((ipu_cm_read(ipu, IPU_CHA_BUF2_RDY(ipu->devtype, dma_chan)) &
	     idma_mask(dma_chan)) &&
	     (ipu_cm_read(ipu, IPU_CHA_TRB_MODE_SEL(ipu->devtype, dma_chan)) &
	      idma_mask(dma_chan)) && _ipu_is_trb_chan(ipu, dma_chan)))
		ret = -EACCES;
	else
		_ipu_ch_offset_update(ipu, dma_chan, pixel_fmt, width, height, stride,
				      u, v, 0, vertical_offset, horizontal_offset);
	spin_unlock_irqrestore(&ipu->rdy_reg_spin_lock, lock_flags);

	return ret;
}
EXPORT_SYMBOL(ipu_update_channel_offset);

int32_t ipu_get_channel_offset(uint32_t pixel_fmt,
			       uint16_t width, uint16_t height,
			       uint32_t stride,
			       uint32_t u, uint32_t v,
			       uint32_t vertical_offset, uint32_t horizontal_offset,
			       uint32_t *u_offset, uint32_t *v_offset)
{
	return __ipu_ch_offset_calc(pixel_fmt, width, height, stride,
				    u, v, 0,
				    vertical_offset, horizontal_offset,
				    u_offset, v_offset);
}
EXPORT_SYMBOL(ipu_get_channel_offset);

/*!
 * This function is called to set a channel's buffer as ready.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       type            Input parameter which buffer to initialize.
 *
 * @param       bufNum          Input parameter for which buffer number set to
 *                              ready state.
 *
 * @return      Returns 0 on success or negative error code on fail
 */
int32_t ipu_select_buffer(struct ipu_soc *ipu, ipu_channel_t channel,
			ipu_buffer_t type, uint32_t bufNum)
{
	uint32_t dma_chan = channel_2_dma(channel, type);
	unsigned long lock_flags;

	if (dma_chan == IDMA_CHAN_INVALID)
		return -EINVAL;

	spin_lock_irqsave(&ipu->rdy_reg_spin_lock, lock_flags);
	/* Mark buffer to be ready. */
	if (bufNum == 0)
		ipu_cm_write(ipu, idma_mask(dma_chan),
			     IPU_CHA_BUF0_RDY(ipu->devtype, dma_chan));
	else if (bufNum == 1)
		ipu_cm_write(ipu, idma_mask(dma_chan),
			     IPU_CHA_BUF1_RDY(ipu->devtype, dma_chan));
	else
		ipu_cm_write(ipu, idma_mask(dma_chan),
			     IPU_CHA_BUF2_RDY(ipu->devtype, dma_chan));
	spin_unlock_irqrestore(&ipu->rdy_reg_spin_lock, lock_flags);

	return 0;
}
EXPORT_SYMBOL(ipu_select_buffer);

/*!
 * This function is called to set a channel's buffer as ready.
 *
 * @param	ipu		ipu handler
 * @param       bufNum          Input parameter for which buffer number set to
 *                              ready state.
 *
 * @return      Returns 0 on success or negative error code on fail
 */
int32_t ipu_select_multi_vdi_buffer(struct ipu_soc *ipu, uint32_t bufNum)
{

	uint32_t dma_chan = channel_2_dma(MEM_VDI_PRP_VF_MEM, IPU_INPUT_BUFFER);
	uint32_t mask_bit =
		idma_mask(channel_2_dma(MEM_VDI_PRP_VF_MEM_P, IPU_INPUT_BUFFER))|
		idma_mask(dma_chan)|
		idma_mask(channel_2_dma(MEM_VDI_PRP_VF_MEM_N, IPU_INPUT_BUFFER));
	unsigned long lock_flags;

	spin_lock_irqsave(&ipu->rdy_reg_spin_lock, lock_flags);
	/* Mark buffers to be ready. */
	if (bufNum == 0)
		ipu_cm_write(ipu, mask_bit,
				IPU_CHA_BUF0_RDY(ipu->devtype, dma_chan));
	else
		ipu_cm_write(ipu, mask_bit,
				IPU_CHA_BUF1_RDY(ipu->devtype, dma_chan));
	spin_unlock_irqrestore(&ipu->rdy_reg_spin_lock, lock_flags);

	return 0;
}
EXPORT_SYMBOL(ipu_select_multi_vdi_buffer);

#define NA	-1
static int proc_dest_sel[] = {
	0, 1, 1, 3, 5, 5, 4, 7, 8, 9, 10, 11, 12, 14, 15, 16,
	0, 1, 1, 5, 5, 5, 5, 5, 7, 8, 9, 10, 11, 12, 14, 31 };
static int proc_src_sel[] = { 0, 6, 7, 6, 7, 8, 5, NA, NA, NA,
  NA, NA, NA, NA, NA,  1,  2,  3,  4,  7,  8, NA, 8, NA };
static int disp_src_sel[] = { 0, 6, 7, 8, 3, 4, 5, NA, NA, NA,
  NA, NA, NA, NA, NA,  1, NA,  2, NA,  3,  4,  4,  4,  4 };


/*!
 * This function links 2 channels together for automatic frame
 * synchronization. The output of the source channel is linked to the input of
 * the destination channel.
 *
 * @param	ipu		ipu handler
 * @param       src_ch          Input parameter for the logical channel ID of
 *                              the source channel.
 *
 * @param       dest_ch         Input parameter for the logical channel ID of
 *                              the destination channel.
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail.
 */
int32_t ipu_link_channels(struct ipu_soc *ipu, ipu_channel_t src_ch, ipu_channel_t dest_ch)
{
	int retval = 0;
	uint32_t fs_proc_flow1;
	uint32_t fs_proc_flow2;
	uint32_t fs_proc_flow3;
	uint32_t fs_disp_flow1;

	mutex_lock(&ipu->mutex_lock);

	fs_proc_flow1 = ipu_cm_read(ipu, IPU_FS_PROC_FLOW1);
	fs_proc_flow2 = ipu_cm_read(ipu, IPU_FS_PROC_FLOW2);
	fs_proc_flow3 = ipu_cm_read(ipu, IPU_FS_PROC_FLOW3);
	fs_disp_flow1 = ipu_cm_read(ipu, IPU_FS_DISP_FLOW1);

	switch (src_ch) {
	case CSI_MEM0:
		fs_proc_flow3 &= ~FS_SMFC0_DEST_SEL_MASK;
		fs_proc_flow3 |=
			proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
			FS_SMFC0_DEST_SEL_OFFSET;
		break;
	case CSI_MEM1:
		fs_proc_flow3 &= ~FS_SMFC1_DEST_SEL_MASK;
		fs_proc_flow3 |=
			proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
			FS_SMFC1_DEST_SEL_OFFSET;
		break;
	case CSI_MEM2:
		fs_proc_flow3 &= ~FS_SMFC2_DEST_SEL_MASK;
		fs_proc_flow3 |=
			proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
			FS_SMFC2_DEST_SEL_OFFSET;
		break;
	case CSI_MEM3:
		fs_proc_flow3 &= ~FS_SMFC3_DEST_SEL_MASK;
		fs_proc_flow3 |=
			proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
			FS_SMFC3_DEST_SEL_OFFSET;
		break;
	case CSI_PRP_ENC_MEM:
		fs_proc_flow2 &= ~FS_PRPENC_DEST_SEL_MASK;
		fs_proc_flow2 |=
			proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
			FS_PRPENC_DEST_SEL_OFFSET;
		break;
	case CSI_PRP_VF_MEM:
		fs_proc_flow2 &= ~FS_PRPVF_DEST_SEL_MASK;
		fs_proc_flow2 |=
			proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
			FS_PRPVF_DEST_SEL_OFFSET;
		break;
	case MEM_PP_MEM:
		fs_proc_flow2 &= ~FS_PP_DEST_SEL_MASK;
		fs_proc_flow2 |=
		    proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
		    FS_PP_DEST_SEL_OFFSET;
		break;
	case MEM_ROT_PP_MEM:
		fs_proc_flow2 &= ~FS_PP_ROT_DEST_SEL_MASK;
		fs_proc_flow2 |=
		    proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
		    FS_PP_ROT_DEST_SEL_OFFSET;
		break;
	case MEM_PRP_ENC_MEM:
		fs_proc_flow2 &= ~FS_PRPENC_DEST_SEL_MASK;
		fs_proc_flow2 |=
		    proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
		    FS_PRPENC_DEST_SEL_OFFSET;
		break;
	case MEM_ROT_ENC_MEM:
		fs_proc_flow2 &= ~FS_PRPENC_ROT_DEST_SEL_MASK;
		fs_proc_flow2 |=
		    proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
		    FS_PRPENC_ROT_DEST_SEL_OFFSET;
		break;
	case MEM_PRP_VF_MEM:
		fs_proc_flow2 &= ~FS_PRPVF_DEST_SEL_MASK;
		fs_proc_flow2 |=
		    proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
		    FS_PRPVF_DEST_SEL_OFFSET;
		break;
	case MEM_VDI_PRP_VF_MEM:
		fs_proc_flow2 &= ~FS_PRPVF_DEST_SEL_MASK;
		fs_proc_flow2 |=
		    proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
		    FS_PRPVF_DEST_SEL_OFFSET;
		break;
	case MEM_ROT_VF_MEM:
		fs_proc_flow2 &= ~FS_PRPVF_ROT_DEST_SEL_MASK;
		fs_proc_flow2 |=
		    proc_dest_sel[IPU_CHAN_ID(dest_ch)] <<
		    FS_PRPVF_ROT_DEST_SEL_OFFSET;
		break;
	case MEM_VDOA_MEM:
		fs_proc_flow3 &= ~FS_VDOA_DEST_SEL_MASK;
		if (MEM_VDI_MEM == dest_ch)
			fs_proc_flow3 |= FS_VDOA_DEST_SEL_VDI;
		else if (MEM_PP_MEM == dest_ch)
			fs_proc_flow3 |= FS_VDOA_DEST_SEL_IC;
		else {
			retval = -EINVAL;
			goto err;
		}
		break;
	default:
		retval = -EINVAL;
		goto err;
	}

	switch (dest_ch) {
	case MEM_PP_MEM:
		fs_proc_flow1 &= ~FS_PP_SRC_SEL_MASK;
		if (MEM_VDOA_MEM == src_ch)
			fs_proc_flow1 |= FS_PP_SRC_SEL_VDOA;
		else
			fs_proc_flow1 |= proc_src_sel[IPU_CHAN_ID(src_ch)] <<
						FS_PP_SRC_SEL_OFFSET;
		break;
	case MEM_ROT_PP_MEM:
		fs_proc_flow1 &= ~FS_PP_ROT_SRC_SEL_MASK;
		fs_proc_flow1 |=
		    proc_src_sel[IPU_CHAN_ID(src_ch)] <<
		    FS_PP_ROT_SRC_SEL_OFFSET;
		break;
	case MEM_PRP_ENC_MEM:
		fs_proc_flow1 &= ~FS_PRP_SRC_SEL_MASK;
		fs_proc_flow1 |=
		    proc_src_sel[IPU_CHAN_ID(src_ch)] << FS_PRP_SRC_SEL_OFFSET;
		break;
	case MEM_ROT_ENC_MEM:
		fs_proc_flow1 &= ~FS_PRPENC_ROT_SRC_SEL_MASK;
		fs_proc_flow1 |=
		    proc_src_sel[IPU_CHAN_ID(src_ch)] <<
		    FS_PRPENC_ROT_SRC_SEL_OFFSET;
		break;
	case MEM_PRP_VF_MEM:
		fs_proc_flow1 &= ~FS_PRP_SRC_SEL_MASK;
		fs_proc_flow1 |=
		    proc_src_sel[IPU_CHAN_ID(src_ch)] << FS_PRP_SRC_SEL_OFFSET;
		break;
	case MEM_VDI_PRP_VF_MEM:
		fs_proc_flow1 &= ~FS_PRP_SRC_SEL_MASK;
		fs_proc_flow1 |=
		    proc_src_sel[IPU_CHAN_ID(src_ch)] << FS_PRP_SRC_SEL_OFFSET;
		break;
	case MEM_ROT_VF_MEM:
		fs_proc_flow1 &= ~FS_PRPVF_ROT_SRC_SEL_MASK;
		fs_proc_flow1 |=
		    proc_src_sel[IPU_CHAN_ID(src_ch)] <<
		    FS_PRPVF_ROT_SRC_SEL_OFFSET;
		break;
	case MEM_DC_SYNC:
		fs_disp_flow1 &= ~FS_DC1_SRC_SEL_MASK;
		fs_disp_flow1 |=
		    disp_src_sel[IPU_CHAN_ID(src_ch)] << FS_DC1_SRC_SEL_OFFSET;
		break;
	case MEM_BG_SYNC:
		fs_disp_flow1 &= ~FS_DP_SYNC0_SRC_SEL_MASK;
		fs_disp_flow1 |=
		    disp_src_sel[IPU_CHAN_ID(src_ch)] <<
		    FS_DP_SYNC0_SRC_SEL_OFFSET;
		break;
	case MEM_FG_SYNC:
		fs_disp_flow1 &= ~FS_DP_SYNC1_SRC_SEL_MASK;
		fs_disp_flow1 |=
		    disp_src_sel[IPU_CHAN_ID(src_ch)] <<
		    FS_DP_SYNC1_SRC_SEL_OFFSET;
		break;
	case MEM_DC_ASYNC:
		fs_disp_flow1 &= ~FS_DC2_SRC_SEL_MASK;
		fs_disp_flow1 |=
		    disp_src_sel[IPU_CHAN_ID(src_ch)] << FS_DC2_SRC_SEL_OFFSET;
		break;
	case MEM_BG_ASYNC0:
		fs_disp_flow1 &= ~FS_DP_ASYNC0_SRC_SEL_MASK;
		fs_disp_flow1 |=
		    disp_src_sel[IPU_CHAN_ID(src_ch)] <<
		    FS_DP_ASYNC0_SRC_SEL_OFFSET;
		break;
	case MEM_FG_ASYNC0:
		fs_disp_flow1 &= ~FS_DP_ASYNC1_SRC_SEL_MASK;
		fs_disp_flow1 |=
		    disp_src_sel[IPU_CHAN_ID(src_ch)] <<
		    FS_DP_ASYNC1_SRC_SEL_OFFSET;
		break;
	case MEM_VDI_MEM:
		fs_proc_flow1 &= ~FS_VDI_SRC_SEL_MASK;
		if (MEM_VDOA_MEM == src_ch)
			fs_proc_flow1 |= FS_VDI_SRC_SEL_VDOA;
		else {
			retval = -EINVAL;
			goto err;
		}
		break;
	default:
		retval = -EINVAL;
		goto err;
	}

	ipu_cm_write(ipu, fs_proc_flow1, IPU_FS_PROC_FLOW1);
	ipu_cm_write(ipu, fs_proc_flow2, IPU_FS_PROC_FLOW2);
	ipu_cm_write(ipu, fs_proc_flow3, IPU_FS_PROC_FLOW3);
	ipu_cm_write(ipu, fs_disp_flow1, IPU_FS_DISP_FLOW1);

err:
	mutex_unlock(&ipu->mutex_lock);
	return retval;
}
EXPORT_SYMBOL(ipu_link_channels);

/*!
 * This function unlinks 2 channels and disables automatic frame
 * synchronization.
 *
 * @param	ipu		ipu handler
 * @param       src_ch          Input parameter for the logical channel ID of
 *                              the source channel.
 *
 * @param       dest_ch         Input parameter for the logical channel ID of
 *                              the destination channel.
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail.
 */
int32_t ipu_unlink_channels(struct ipu_soc *ipu, ipu_channel_t src_ch, ipu_channel_t dest_ch)
{
	int retval = 0;
	uint32_t fs_proc_flow1;
	uint32_t fs_proc_flow2;
	uint32_t fs_proc_flow3;
	uint32_t fs_disp_flow1;

	mutex_lock(&ipu->mutex_lock);

	fs_proc_flow1 = ipu_cm_read(ipu, IPU_FS_PROC_FLOW1);
	fs_proc_flow2 = ipu_cm_read(ipu, IPU_FS_PROC_FLOW2);
	fs_proc_flow3 = ipu_cm_read(ipu, IPU_FS_PROC_FLOW3);
	fs_disp_flow1 = ipu_cm_read(ipu, IPU_FS_DISP_FLOW1);

	switch (src_ch) {
	case CSI_MEM0:
		fs_proc_flow3 &= ~FS_SMFC0_DEST_SEL_MASK;
		break;
	case CSI_MEM1:
		fs_proc_flow3 &= ~FS_SMFC1_DEST_SEL_MASK;
		break;
	case CSI_MEM2:
		fs_proc_flow3 &= ~FS_SMFC2_DEST_SEL_MASK;
		break;
	case CSI_MEM3:
		fs_proc_flow3 &= ~FS_SMFC3_DEST_SEL_MASK;
		break;
	case CSI_PRP_ENC_MEM:
		fs_proc_flow2 &= ~FS_PRPENC_DEST_SEL_MASK;
		break;
	case CSI_PRP_VF_MEM:
		fs_proc_flow2 &= ~FS_PRPVF_DEST_SEL_MASK;
		break;
	case MEM_PP_MEM:
		fs_proc_flow2 &= ~FS_PP_DEST_SEL_MASK;
		break;
	case MEM_ROT_PP_MEM:
		fs_proc_flow2 &= ~FS_PP_ROT_DEST_SEL_MASK;
		break;
	case MEM_PRP_ENC_MEM:
		fs_proc_flow2 &= ~FS_PRPENC_DEST_SEL_MASK;
		break;
	case MEM_ROT_ENC_MEM:
		fs_proc_flow2 &= ~FS_PRPENC_ROT_DEST_SEL_MASK;
		break;
	case MEM_PRP_VF_MEM:
		fs_proc_flow2 &= ~FS_PRPVF_DEST_SEL_MASK;
		break;
	case MEM_VDI_PRP_VF_MEM:
		fs_proc_flow2 &= ~FS_PRPVF_DEST_SEL_MASK;
		break;
	case MEM_ROT_VF_MEM:
		fs_proc_flow2 &= ~FS_PRPVF_ROT_DEST_SEL_MASK;
		break;
	case MEM_VDOA_MEM:
		fs_proc_flow3 &= ~FS_VDOA_DEST_SEL_MASK;
		break;
	default:
		retval = -EINVAL;
		goto err;
	}

	switch (dest_ch) {
	case MEM_PP_MEM:
		fs_proc_flow1 &= ~FS_PP_SRC_SEL_MASK;
		break;
	case MEM_ROT_PP_MEM:
		fs_proc_flow1 &= ~FS_PP_ROT_SRC_SEL_MASK;
		break;
	case MEM_PRP_ENC_MEM:
		fs_proc_flow1 &= ~FS_PRP_SRC_SEL_MASK;
		break;
	case MEM_ROT_ENC_MEM:
		fs_proc_flow1 &= ~FS_PRPENC_ROT_SRC_SEL_MASK;
		break;
	case MEM_PRP_VF_MEM:
		fs_proc_flow1 &= ~FS_PRP_SRC_SEL_MASK;
		break;
	case MEM_VDI_PRP_VF_MEM:
		fs_proc_flow1 &= ~FS_PRP_SRC_SEL_MASK;
		break;
	case MEM_ROT_VF_MEM:
		fs_proc_flow1 &= ~FS_PRPVF_ROT_SRC_SEL_MASK;
		break;
	case MEM_DC_SYNC:
		fs_disp_flow1 &= ~FS_DC1_SRC_SEL_MASK;
		break;
	case MEM_BG_SYNC:
		fs_disp_flow1 &= ~FS_DP_SYNC0_SRC_SEL_MASK;
		break;
	case MEM_FG_SYNC:
		fs_disp_flow1 &= ~FS_DP_SYNC1_SRC_SEL_MASK;
		break;
	case MEM_DC_ASYNC:
		fs_disp_flow1 &= ~FS_DC2_SRC_SEL_MASK;
		break;
	case MEM_BG_ASYNC0:
		fs_disp_flow1 &= ~FS_DP_ASYNC0_SRC_SEL_MASK;
		break;
	case MEM_FG_ASYNC0:
		fs_disp_flow1 &= ~FS_DP_ASYNC1_SRC_SEL_MASK;
		break;
	case MEM_VDI_MEM:
		fs_proc_flow1 &= ~FS_VDI_SRC_SEL_MASK;
		break;
	default:
		retval = -EINVAL;
		goto err;
	}

	ipu_cm_write(ipu, fs_proc_flow1, IPU_FS_PROC_FLOW1);
	ipu_cm_write(ipu, fs_proc_flow2, IPU_FS_PROC_FLOW2);
	ipu_cm_write(ipu, fs_proc_flow3, IPU_FS_PROC_FLOW3);
	ipu_cm_write(ipu, fs_disp_flow1, IPU_FS_DISP_FLOW1);

err:
	mutex_unlock(&ipu->mutex_lock);
	return retval;
}
EXPORT_SYMBOL(ipu_unlink_channels);

/*!
 * This function check whether a logical channel was enabled.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @return      This function returns 1 while request channel is enabled or
 *              0 for not enabled.
 */
int32_t ipu_is_channel_busy(struct ipu_soc *ipu, ipu_channel_t channel)
{
	uint32_t reg;
	uint32_t in_dma;
	uint32_t out_dma;

	out_dma = channel_2_dma(channel, IPU_OUTPUT_BUFFER);
	in_dma = channel_2_dma(channel, IPU_VIDEO_IN_BUFFER);

	reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(in_dma));
	if (reg & idma_mask(in_dma))
		return 1;
	reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(out_dma));
	if (reg & idma_mask(out_dma))
		return 1;
	return 0;
}
EXPORT_SYMBOL(ipu_is_channel_busy);

/*!
 * This function enables a logical channel.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail.
 */
int32_t ipu_enable_channel(struct ipu_soc *ipu, ipu_channel_t channel)
{
	uint32_t reg;
	uint32_t ipu_conf;
	uint32_t in_dma;
	uint32_t out_dma;
	uint32_t sec_dma;
	uint32_t thrd_dma;

	mutex_lock(&ipu->mutex_lock);

	if (ipu->channel_enable_mask & (1L << IPU_CHAN_ID(channel))) {
		dev_err(ipu->dev, "Warning: channel already enabled %d\n",
			IPU_CHAN_ID(channel));
		mutex_unlock(&ipu->mutex_lock);
		return -EACCES;
	}

	/* Get input and output dma channels */
	out_dma = channel_2_dma(channel, IPU_OUTPUT_BUFFER);
	in_dma = channel_2_dma(channel, IPU_VIDEO_IN_BUFFER);

	ipu_conf = ipu_cm_read(ipu, IPU_CONF);
	if (ipu->di_use_count[0] > 0) {
		ipu_conf |= IPU_CONF_DI0_EN;
	}
	if (ipu->di_use_count[1] > 0) {
		ipu_conf |= IPU_CONF_DI1_EN;
	}
	if (ipu->dp_use_count > 0)
		ipu_conf |= IPU_CONF_DP_EN;
	if (ipu->dc_use_count > 0)
		ipu_conf |= IPU_CONF_DC_EN;
	if (ipu->dmfc_use_count > 0)
		ipu_conf |= IPU_CONF_DMFC_EN;
	if (ipu->ic_use_count > 0)
		ipu_conf |= IPU_CONF_IC_EN;
	if (ipu->vdi_use_count > 0) {
		ipu_conf |= IPU_CONF_ISP_EN;
		ipu_conf |= IPU_CONF_VDI_EN;
		ipu_conf |= IPU_CONF_IC_INPUT;
	}
	if (ipu->rot_use_count > 0)
		ipu_conf |= IPU_CONF_ROT_EN;
	if (ipu->smfc_use_count > 0)
		ipu_conf |= IPU_CONF_SMFC_EN;
	ipu_cm_write(ipu, ipu_conf, IPU_CONF);

	if (idma_is_valid(in_dma)) {
		reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(in_dma));
		ipu_idmac_write(ipu, reg | idma_mask(in_dma), IDMAC_CHA_EN(in_dma));
	}
	if (idma_is_valid(out_dma)) {
		reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(out_dma));
		ipu_idmac_write(ipu, reg | idma_mask(out_dma), IDMAC_CHA_EN(out_dma));
	}

	if ((ipu->sec_chan_en[IPU_CHAN_ID(channel)]) &&
		((channel == MEM_PP_MEM) || (channel == MEM_PRP_VF_MEM) ||
		 (channel == MEM_VDI_PRP_VF_MEM))) {
		sec_dma = channel_2_dma(channel, IPU_GRAPH_IN_BUFFER);
		reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(sec_dma));
		ipu_idmac_write(ipu, reg | idma_mask(sec_dma), IDMAC_CHA_EN(sec_dma));
	}
	if ((ipu->thrd_chan_en[IPU_CHAN_ID(channel)]) &&
		((channel == MEM_PP_MEM) || (channel == MEM_PRP_VF_MEM))) {
		thrd_dma = channel_2_dma(channel, IPU_ALPHA_IN_BUFFER);
		reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(thrd_dma));
		ipu_idmac_write(ipu, reg | idma_mask(thrd_dma), IDMAC_CHA_EN(thrd_dma));

		sec_dma = channel_2_dma(channel, IPU_GRAPH_IN_BUFFER);
		reg = ipu_idmac_read(ipu, IDMAC_SEP_ALPHA);
		ipu_idmac_write(ipu, reg | idma_mask(sec_dma), IDMAC_SEP_ALPHA);
	} else if ((ipu->thrd_chan_en[IPU_CHAN_ID(channel)]) &&
		   ((channel == MEM_BG_SYNC) || (channel == MEM_FG_SYNC))) {
		thrd_dma = channel_2_dma(channel, IPU_ALPHA_IN_BUFFER);
		reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(thrd_dma));
		ipu_idmac_write(ipu, reg | idma_mask(thrd_dma), IDMAC_CHA_EN(thrd_dma));
		reg = ipu_idmac_read(ipu, IDMAC_SEP_ALPHA);
		ipu_idmac_write(ipu, reg | idma_mask(in_dma), IDMAC_SEP_ALPHA);
	}

	if ((channel == MEM_DC_SYNC) || (channel == MEM_BG_SYNC) ||
	    (channel == MEM_FG_SYNC)) {
		reg = ipu_idmac_read(ipu, IDMAC_WM_EN(in_dma));
		ipu_idmac_write(ipu, reg | idma_mask(in_dma), IDMAC_WM_EN(in_dma));

		_ipu_dp_dc_enable(ipu, channel);
	}

	if (_ipu_is_ic_chan(in_dma) || _ipu_is_ic_chan(out_dma) ||
		_ipu_is_irt_chan(in_dma) || _ipu_is_irt_chan(out_dma) ||
		_ipu_is_vdi_out_chan(out_dma))
		_ipu_ic_enable_task(ipu, channel);

	ipu->channel_enable_mask |= 1L << IPU_CHAN_ID(channel);

	if (ipu->prg_clk)
		clk_prepare_enable(ipu->prg_clk);

	mutex_unlock(&ipu->mutex_lock);

	return 0;
}
EXPORT_SYMBOL(ipu_enable_channel);

/*!
 * This function check buffer ready for a logical channel.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       type            Input parameter which buffer to clear.
 *
 * @param       bufNum          Input parameter for which buffer number clear
 * 				ready state.
 *
 */
int32_t ipu_check_buffer_ready(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type,
		uint32_t bufNum)
{
	uint32_t dma_chan = channel_2_dma(channel, type);
	uint32_t reg;
	unsigned long lock_flags;

	if (dma_chan == IDMA_CHAN_INVALID)
		return -EINVAL;

	spin_lock_irqsave(&ipu->rdy_reg_spin_lock, lock_flags);
	if (bufNum == 0)
		reg = ipu_cm_read(ipu,
				IPU_CHA_BUF0_RDY(ipu->devtype, dma_chan));
	else if (bufNum == 1)
		reg = ipu_cm_read(ipu,
				IPU_CHA_BUF1_RDY(ipu->devtype, dma_chan));
	else
		reg = ipu_cm_read(ipu,
				IPU_CHA_BUF2_RDY(ipu->devtype, dma_chan));
	spin_unlock_irqrestore(&ipu->rdy_reg_spin_lock, lock_flags);

	if (reg & idma_mask(dma_chan))
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(ipu_check_buffer_ready);

/*!
 * This function clear buffer ready for a logical channel.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       type            Input parameter which buffer to clear.
 *
 * @param       bufNum          Input parameter for which buffer number clear
 * 				ready state.
 *
 */
void _ipu_clear_buffer_ready(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type,
		uint32_t bufNum)
{
	uint32_t dma_ch = channel_2_dma(channel, type);

	if (!idma_is_valid(dma_ch))
		return;

	ipu_cm_write(ipu, 0xF0300000, IPU_GPR); /* write one to clear */
	if (bufNum == 0)
		ipu_cm_write(ipu, idma_mask(dma_ch),
				IPU_CHA_BUF0_RDY(ipu->devtype, dma_ch));
	else if (bufNum == 1)
		ipu_cm_write(ipu, idma_mask(dma_ch),
				IPU_CHA_BUF1_RDY(ipu->devtype, dma_ch));
	else
		ipu_cm_write(ipu, idma_mask(dma_ch),
				IPU_CHA_BUF2_RDY(ipu->devtype, dma_ch));
	ipu_cm_write(ipu, 0x0, IPU_GPR); /* write one to set */
}

void ipu_clear_buffer_ready(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type,
		uint32_t bufNum)
{
	unsigned long lock_flags;

	spin_lock_irqsave(&ipu->rdy_reg_spin_lock, lock_flags);
	_ipu_clear_buffer_ready(ipu, channel, type, bufNum);
	spin_unlock_irqrestore(&ipu->rdy_reg_spin_lock, lock_flags);
}
EXPORT_SYMBOL(ipu_clear_buffer_ready);

/*!
 * This function disables a logical channel.
 *
 * @param	ipu		ipu handler
 * @param       channel         Input parameter for the logical channel ID.
 *
 * @param       wait_for_stop   Flag to set whether to wait for channel end
 *                              of frame or return immediately.
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail.
 */
int32_t ipu_disable_channel(struct ipu_soc *ipu, ipu_channel_t channel, bool wait_for_stop)
{
	uint32_t reg;
	uint32_t in_dma;
	uint32_t out_dma;
	uint32_t sec_dma = NO_DMA;
	uint32_t thrd_dma = NO_DMA;
	uint16_t fg_pos_x, fg_pos_y;
	unsigned long lock_flags;

	mutex_lock(&ipu->mutex_lock);

	if ((ipu->channel_enable_mask & (1L << IPU_CHAN_ID(channel))) == 0) {
		dev_dbg(ipu->dev, "Channel already disabled %d\n",
			IPU_CHAN_ID(channel));
		mutex_unlock(&ipu->mutex_lock);
		return -EACCES;
	}

	/* Get input and output dma channels */
	out_dma = channel_2_dma(channel, IPU_OUTPUT_BUFFER);
	in_dma = channel_2_dma(channel, IPU_VIDEO_IN_BUFFER);

	if ((idma_is_valid(in_dma) &&
		!idma_is_set(ipu, IDMAC_CHA_EN(in_dma), in_dma))
		&& (idma_is_valid(out_dma) &&
		!idma_is_set(ipu, IDMAC_CHA_EN(out_dma), out_dma))) {
		mutex_unlock(&ipu->mutex_lock);
		return -EINVAL;
	}

	if (ipu->sec_chan_en[IPU_CHAN_ID(channel)])
		sec_dma = channel_2_dma(channel, IPU_GRAPH_IN_BUFFER);
	if (ipu->thrd_chan_en[IPU_CHAN_ID(channel)]) {
		sec_dma = channel_2_dma(channel, IPU_GRAPH_IN_BUFFER);
		thrd_dma = channel_2_dma(channel, IPU_ALPHA_IN_BUFFER);
	}

	if ((channel == MEM_BG_SYNC) || (channel == MEM_FG_SYNC) ||
	    (channel == MEM_DC_SYNC)) {
		if (channel == MEM_FG_SYNC) {
			_ipu_disp_get_window_pos(ipu, channel, &fg_pos_x, &fg_pos_y);
			_ipu_disp_set_window_pos(ipu, channel, 0, 0);
		}

		_ipu_dp_dc_disable(ipu, channel, false);

		/*
		 * wait for BG channel EOF then disable FG-IDMAC,
		 * it avoid FG NFB4EOF error.
		 */
		if ((channel == MEM_FG_SYNC) && (ipu_is_channel_busy(ipu, MEM_BG_SYNC))) {
			int timeout = 50;

			ipu_cm_write(ipu, IPUIRQ_2_MASK(IPU_IRQ_BG_SYNC_EOF),
					IPUIRQ_2_STATREG(ipu->devtype,
							IPU_IRQ_BG_SYNC_EOF));
			while ((ipu_cm_read(ipu,
				IPUIRQ_2_STATREG(ipu->devtype,
				IPU_IRQ_BG_SYNC_EOF)) &
				IPUIRQ_2_MASK(IPU_IRQ_BG_SYNC_EOF)) == 0) {
				msleep(10);
				timeout -= 10;
				if (timeout <= 0) {
					dev_err(ipu->dev, "warning: wait for bg sync eof timeout\n");
					break;
				}
			}
		}
	} else if (wait_for_stop && !_ipu_is_smfc_chan(out_dma) &&
		   channel != CSI_PRP_VF_MEM && channel != CSI_PRP_ENC_MEM) {
		while (idma_is_set(ipu, IDMAC_CHA_BUSY(ipu->devtype, in_dma),
					in_dma) ||
		       idma_is_set(ipu, IDMAC_CHA_BUSY(ipu->devtype, out_dma),
					out_dma) ||
			(ipu->sec_chan_en[IPU_CHAN_ID(channel)] &&
			idma_is_set(ipu, IDMAC_CHA_BUSY(ipu->devtype, sec_dma),
					sec_dma)) ||
			(ipu->thrd_chan_en[IPU_CHAN_ID(channel)] &&
			idma_is_set(ipu, IDMAC_CHA_BUSY(ipu->devtype, thrd_dma),
					thrd_dma))) {
			uint32_t irq = 0xffffffff;
			int timeout = 50000;

			if (idma_is_set(ipu, IDMAC_CHA_BUSY(ipu->devtype,
					out_dma), out_dma))
				irq = out_dma;
			if (ipu->sec_chan_en[IPU_CHAN_ID(channel)] &&
				idma_is_set(ipu, IDMAC_CHA_BUSY(ipu->devtype,
						sec_dma), sec_dma))
				irq = sec_dma;
			if (ipu->thrd_chan_en[IPU_CHAN_ID(channel)] &&
				idma_is_set(ipu, IDMAC_CHA_BUSY(ipu->devtype,
						thrd_dma), thrd_dma))
				irq = thrd_dma;
			if (idma_is_set(ipu, IDMAC_CHA_BUSY(ipu->devtype,
						in_dma), in_dma))
				irq = in_dma;

			if (irq == 0xffffffff) {
				dev_dbg(ipu->dev, "warning: no channel busy, break\n");
				break;
			}

			ipu_cm_write(ipu, IPUIRQ_2_MASK(irq),
					IPUIRQ_2_STATREG(ipu->devtype, irq));

			dev_dbg(ipu->dev, "warning: channel %d busy, need wait\n", irq);

			while (((ipu_cm_read(ipu,
					IPUIRQ_2_STATREG(ipu->devtype, irq))
				& IPUIRQ_2_MASK(irq)) == 0) &&
				(idma_is_set(ipu, IDMAC_CHA_BUSY(ipu->devtype,
							irq), irq))) {
				udelay(10);
				timeout -= 10;
				if (timeout <= 0) {
					ipu_dump_registers(ipu);
					dev_err(ipu->dev, "warning: disable ipu dma channel %d during its busy state\n", irq);
					break;
				}
			}
			dev_dbg(ipu->dev, "wait_time:%d\n", 50000 - timeout);

		}
	}

	if ((channel == MEM_BG_SYNC) || (channel == MEM_FG_SYNC) ||
	    (channel == MEM_DC_SYNC)) {
		reg = ipu_idmac_read(ipu, IDMAC_WM_EN(in_dma));
		ipu_idmac_write(ipu, reg & ~idma_mask(in_dma), IDMAC_WM_EN(in_dma));
	}

	/* Disable IC task */
	if (_ipu_is_ic_chan(in_dma) || _ipu_is_ic_chan(out_dma) ||
		_ipu_is_irt_chan(in_dma) || _ipu_is_irt_chan(out_dma) ||
		_ipu_is_vdi_out_chan(out_dma))
		_ipu_ic_disable_task(ipu, channel);

	/* Disable DMA channel(s) */
	if (idma_is_valid(in_dma)) {
		reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(in_dma));
		ipu_idmac_write(ipu, reg & ~idma_mask(in_dma), IDMAC_CHA_EN(in_dma));
		ipu_cm_write(ipu, idma_mask(in_dma),
				IPU_CHA_CUR_BUF(ipu->devtype, in_dma));
		ipu_cm_write(ipu, tri_cur_buf_mask(in_dma),
				IPU_CHA_TRIPLE_CUR_BUF(ipu->devtype, in_dma));
	}
	if (idma_is_valid(out_dma)) {
		reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(out_dma));
		ipu_idmac_write(ipu, reg & ~idma_mask(out_dma), IDMAC_CHA_EN(out_dma));
		ipu_cm_write(ipu, idma_mask(out_dma),
				IPU_CHA_CUR_BUF(ipu->devtype, out_dma));
		ipu_cm_write(ipu, tri_cur_buf_mask(out_dma),
				IPU_CHA_TRIPLE_CUR_BUF(ipu->devtype, out_dma));
	}
	if (ipu->sec_chan_en[IPU_CHAN_ID(channel)] && idma_is_valid(sec_dma)) {
		reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(sec_dma));
		ipu_idmac_write(ipu, reg & ~idma_mask(sec_dma), IDMAC_CHA_EN(sec_dma));
		ipu_cm_write(ipu, idma_mask(sec_dma),
				IPU_CHA_CUR_BUF(ipu->devtype, sec_dma));
	}
	if (ipu->thrd_chan_en[IPU_CHAN_ID(channel)] && idma_is_valid(thrd_dma)) {
		reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(thrd_dma));
		ipu_idmac_write(ipu, reg & ~idma_mask(thrd_dma), IDMAC_CHA_EN(thrd_dma));
		if (channel == MEM_BG_SYNC || channel == MEM_FG_SYNC) {
			reg = ipu_idmac_read(ipu, IDMAC_SEP_ALPHA);
			ipu_idmac_write(ipu, reg & ~idma_mask(in_dma), IDMAC_SEP_ALPHA);
		} else {
			reg = ipu_idmac_read(ipu, IDMAC_SEP_ALPHA);
			ipu_idmac_write(ipu, reg & ~idma_mask(sec_dma), IDMAC_SEP_ALPHA);
		}
		ipu_cm_write(ipu, idma_mask(thrd_dma),
				IPU_CHA_CUR_BUF(ipu->devtype, thrd_dma));
	}

	if (channel == MEM_FG_SYNC)
		_ipu_disp_set_window_pos(ipu, channel, fg_pos_x, fg_pos_y);

	spin_lock_irqsave(&ipu->rdy_reg_spin_lock, lock_flags);
	/* Set channel buffers NOT to be ready */
	if (idma_is_valid(in_dma)) {
		_ipu_clear_buffer_ready(ipu, channel, IPU_VIDEO_IN_BUFFER, 0);
		_ipu_clear_buffer_ready(ipu, channel, IPU_VIDEO_IN_BUFFER, 1);
		_ipu_clear_buffer_ready(ipu, channel, IPU_VIDEO_IN_BUFFER, 2);
	}
	if (idma_is_valid(out_dma)) {
		_ipu_clear_buffer_ready(ipu, channel, IPU_OUTPUT_BUFFER, 0);
		_ipu_clear_buffer_ready(ipu, channel, IPU_OUTPUT_BUFFER, 1);
	}
	if (ipu->sec_chan_en[IPU_CHAN_ID(channel)] && idma_is_valid(sec_dma)) {
		_ipu_clear_buffer_ready(ipu, channel, IPU_GRAPH_IN_BUFFER, 0);
		_ipu_clear_buffer_ready(ipu, channel, IPU_GRAPH_IN_BUFFER, 1);
	}
	if (ipu->thrd_chan_en[IPU_CHAN_ID(channel)] && idma_is_valid(thrd_dma)) {
		_ipu_clear_buffer_ready(ipu, channel, IPU_ALPHA_IN_BUFFER, 0);
		_ipu_clear_buffer_ready(ipu, channel, IPU_ALPHA_IN_BUFFER, 1);
	}
	spin_unlock_irqrestore(&ipu->rdy_reg_spin_lock, lock_flags);

	ipu->channel_enable_mask &= ~(1L << IPU_CHAN_ID(channel));

	if (ipu->prg_clk)
		clk_disable_unprepare(ipu->prg_clk);

	mutex_unlock(&ipu->mutex_lock);

	return 0;
}
EXPORT_SYMBOL(ipu_disable_channel);

/*!
 * This function enables CSI.
 *
 * @param	ipu		ipu handler
 * @param       csi	csi num 0 or 1
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail.
 */
int32_t ipu_enable_csi(struct ipu_soc *ipu, uint32_t csi)
{
	uint32_t reg;

	if (csi > 1) {
		dev_err(ipu->dev, "Wrong csi num_%d\n", csi);
		return -EINVAL;
	}

	_ipu_get(ipu);
	mutex_lock(&ipu->mutex_lock);
	ipu->csi_use_count[csi]++;

	if (ipu->csi_use_count[csi] == 1) {
		reg = ipu_cm_read(ipu, IPU_CONF);
		if (csi == 0)
			ipu_cm_write(ipu, reg | IPU_CONF_CSI0_EN, IPU_CONF);
		else
			ipu_cm_write(ipu, reg | IPU_CONF_CSI1_EN, IPU_CONF);
	}
	mutex_unlock(&ipu->mutex_lock);
	_ipu_put(ipu);
	return 0;
}
EXPORT_SYMBOL(ipu_enable_csi);

/*!
 * This function disables CSI.
 *
 * @param	ipu		ipu handler
 * @param       csi	csi num 0 or 1
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail.
 */
int32_t ipu_disable_csi(struct ipu_soc *ipu, uint32_t csi)
{
	uint32_t reg;

	if (csi > 1) {
		dev_err(ipu->dev, "Wrong csi num_%d\n", csi);
		return -EINVAL;
	}
	_ipu_get(ipu);
	mutex_lock(&ipu->mutex_lock);
	ipu->csi_use_count[csi]--;
	if (ipu->csi_use_count[csi] == 0) {
		_ipu_csi_wait4eof(ipu, ipu->csi_channel[csi]);
		reg = ipu_cm_read(ipu, IPU_CONF);
		if (csi == 0)
			ipu_cm_write(ipu, reg & ~IPU_CONF_CSI0_EN, IPU_CONF);
		else
			ipu_cm_write(ipu, reg & ~IPU_CONF_CSI1_EN, IPU_CONF);
	}
	mutex_unlock(&ipu->mutex_lock);
	_ipu_put(ipu);
	return 0;
}
EXPORT_SYMBOL(ipu_disable_csi);

static irqreturn_t ipu_sync_irq_handler(int irq, void *desc)
{
	struct ipu_soc *ipu = desc;
	int i;
	uint32_t line, bit, int_stat, int_ctrl;
	irqreturn_t result = IRQ_NONE;
	const int int_reg[] = { 1, 2, 3, 4, 11, 12, 13, 14, 15, 0 };

	spin_lock(&ipu->int_reg_spin_lock);

	for (i = 0; int_reg[i] != 0; i++) {
		int_stat = ipu_cm_read(ipu,
				IPU_INT_STAT(ipu->devtype, int_reg[i]));
		int_ctrl = ipu_cm_read(ipu, IPU_INT_CTRL(int_reg[i]));
		int_stat &= int_ctrl;
		ipu_cm_write(ipu, int_stat,
				IPU_INT_STAT(ipu->devtype, int_reg[i]));
		while ((line = ffs(int_stat)) != 0) {
			bit = --line;
			int_stat &= ~(1UL << line);
			line += (int_reg[i] - 1) * 32;
			result |=
			    ipu->irq_list[line].handler(line,
						       ipu->irq_list[line].
						       dev_id);
			if (ipu->irq_list[line].flags & IPU_IRQF_ONESHOT) {
				int_ctrl &= ~(1UL << bit);
				ipu_cm_write(ipu, int_ctrl,
						IPU_INT_CTRL(int_reg[i]));
			}
		}
	}

	spin_unlock(&ipu->int_reg_spin_lock);

	return result;
}

static irqreturn_t ipu_err_irq_handler(int irq, void *desc)
{
	struct ipu_soc *ipu = desc;
	int i;
	uint32_t int_stat;
	const int err_reg[] = { 5, 6, 9, 10, 0 };

	spin_lock(&ipu->int_reg_spin_lock);

	for (i = 0; err_reg[i] != 0; i++) {
		int_stat = ipu_cm_read(ipu,
				IPU_INT_STAT(ipu->devtype, err_reg[i]));
		int_stat &= ipu_cm_read(ipu, IPU_INT_CTRL(err_reg[i]));
		if (int_stat) {
			ipu_cm_write(ipu, int_stat,
				IPU_INT_STAT(ipu->devtype, err_reg[i]));
			dev_warn(ipu->dev,
				"IPU Warning - IPU_INT_STAT_%d = 0x%08X\n",
				err_reg[i], int_stat);
			/* Disable interrupts so we only get error once */
			int_stat = ipu_cm_read(ipu, IPU_INT_CTRL(err_reg[i])) &
					~int_stat;
			ipu_cm_write(ipu, int_stat, IPU_INT_CTRL(err_reg[i]));
		}
	}

	spin_unlock(&ipu->int_reg_spin_lock);

	return IRQ_HANDLED;
}

/*!
 * This function enables the interrupt for the specified interrupt line.
 * The interrupt lines are defined in \b ipu_irq_line enum.
 *
 * @param	ipu		ipu handler
 * @param       irq             Interrupt line to enable interrupt for.
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail.
 */
int ipu_enable_irq(struct ipu_soc *ipu, uint32_t irq)
{
	uint32_t reg;
	unsigned long lock_flags;
	int ret = 0;

	_ipu_get(ipu);

	spin_lock_irqsave(&ipu->int_reg_spin_lock, lock_flags);

	/*
	 * Check sync interrupt handler only, since we do nothing for
	 * error interrupts but than print out register values in the
	 * error interrupt source handler.
	 */
	if (_ipu_is_sync_irq(irq) && (ipu->irq_list[irq].handler == NULL)) {
		dev_err(ipu->dev, "handler hasn't been registered on sync "
				  "irq %d\n", irq);
		ret = -EACCES;
		goto out;
	}

	reg = ipu_cm_read(ipu, IPUIRQ_2_CTRLREG(irq));
	reg |= IPUIRQ_2_MASK(irq);
	ipu_cm_write(ipu, reg, IPUIRQ_2_CTRLREG(irq));
out:
	spin_unlock_irqrestore(&ipu->int_reg_spin_lock, lock_flags);

	_ipu_put(ipu);

	return ret;
}
EXPORT_SYMBOL(ipu_enable_irq);

/*!
 * This function disables the interrupt for the specified interrupt line.
 * The interrupt lines are defined in \b ipu_irq_line enum.
 *
 * @param	ipu		ipu handler
 * @param       irq             Interrupt line to disable interrupt for.
 *
 */
void ipu_disable_irq(struct ipu_soc *ipu, uint32_t irq)
{
	uint32_t reg;
	unsigned long lock_flags;

	_ipu_get(ipu);

	spin_lock_irqsave(&ipu->int_reg_spin_lock, lock_flags);

	reg = ipu_cm_read(ipu, IPUIRQ_2_CTRLREG(irq));
	reg &= ~IPUIRQ_2_MASK(irq);
	ipu_cm_write(ipu, reg, IPUIRQ_2_CTRLREG(irq));

	spin_unlock_irqrestore(&ipu->int_reg_spin_lock, lock_flags);

	_ipu_put(ipu);
}
EXPORT_SYMBOL(ipu_disable_irq);

/*!
 * This function clears the interrupt for the specified interrupt line.
 * The interrupt lines are defined in \b ipu_irq_line enum.
 *
 * @param	ipu		ipu handler
 * @param       irq             Interrupt line to clear interrupt for.
 *
 */
void ipu_clear_irq(struct ipu_soc *ipu, uint32_t irq)
{
	unsigned long lock_flags;

	_ipu_get(ipu);

	spin_lock_irqsave(&ipu->int_reg_spin_lock, lock_flags);

	ipu_cm_write(ipu, IPUIRQ_2_MASK(irq),
			IPUIRQ_2_STATREG(ipu->devtype, irq));

	spin_unlock_irqrestore(&ipu->int_reg_spin_lock, lock_flags);

	_ipu_put(ipu);
}
EXPORT_SYMBOL(ipu_clear_irq);

/*!
 * This function returns the current interrupt status for the specified
 * interrupt line. The interrupt lines are defined in \b ipu_irq_line enum.
 *
 * @param	ipu		ipu handler
 * @param       irq             Interrupt line to get status for.
 *
 * @return      Returns true if the interrupt is pending/asserted or false if
 *              the interrupt is not pending.
 */
bool ipu_get_irq_status(struct ipu_soc *ipu, uint32_t irq)
{
	uint32_t reg;
	unsigned long lock_flags;

	_ipu_get(ipu);

	spin_lock_irqsave(&ipu->int_reg_spin_lock, lock_flags);
	reg = ipu_cm_read(ipu, IPUIRQ_2_STATREG(ipu->devtype, irq));
	spin_unlock_irqrestore(&ipu->int_reg_spin_lock, lock_flags);

	_ipu_put(ipu);

	if (reg & IPUIRQ_2_MASK(irq))
		return true;
	else
		return false;
}
EXPORT_SYMBOL(ipu_get_irq_status);

/*!
 * This function registers an interrupt handler function for the specified
 * interrupt line. The interrupt lines are defined in \b ipu_irq_line enum.
 *
 * @param	ipu		ipu handler
 * @param       irq             Interrupt line to get status for.
 *
 * @param       handler         Input parameter for address of the handler
 *                              function.
 *
 * @param       irq_flags       Flags for interrupt mode. Currently not used.
 *
 * @param       devname         Input parameter for string name of driver
 *                              registering the handler.
 *
 * @param       dev_id          Input parameter for pointer of data to be
 *                              passed to the handler.
 *
 * @return      This function returns 0 on success or negative error code on
 *              fail.
 */
int ipu_request_irq(struct ipu_soc *ipu, uint32_t irq,
		    irqreturn_t(*handler) (int, void *),
		    uint32_t irq_flags, const char *devname, void *dev_id)
{
	uint32_t reg;
	unsigned long lock_flags;
	int ret = 0;

	BUG_ON(irq >= IPU_IRQ_COUNT);

	_ipu_get(ipu);

	spin_lock_irqsave(&ipu->int_reg_spin_lock, lock_flags);

	if (ipu->irq_list[irq].handler != NULL) {
		dev_err(ipu->dev,
			"handler already installed on irq %d\n", irq);
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Check sync interrupt handler only, since we do nothing for
	 * error interrupts but than print out register values in the
	 * error interrupt source handler.
	 */
	if (_ipu_is_sync_irq(irq) && (handler == NULL)) {
		dev_err(ipu->dev, "handler is NULL for sync irq %d\n", irq);
		ret = -EINVAL;
		goto out;
	}

	ipu->irq_list[irq].handler = handler;
	ipu->irq_list[irq].flags = irq_flags;
	ipu->irq_list[irq].dev_id = dev_id;
	ipu->irq_list[irq].name = devname;

	/* clear irq stat for previous use */
	ipu_cm_write(ipu, IPUIRQ_2_MASK(irq),
			IPUIRQ_2_STATREG(ipu->devtype, irq));
	/* enable the interrupt */
	reg = ipu_cm_read(ipu, IPUIRQ_2_CTRLREG(irq));
	reg |= IPUIRQ_2_MASK(irq);
	ipu_cm_write(ipu, reg, IPUIRQ_2_CTRLREG(irq));
out:
	spin_unlock_irqrestore(&ipu->int_reg_spin_lock, lock_flags);

	_ipu_put(ipu);

	return ret;
}
EXPORT_SYMBOL(ipu_request_irq);

/*!
 * This function unregisters an interrupt handler for the specified interrupt
 * line. The interrupt lines are defined in \b ipu_irq_line enum.
 *
 * @param	ipu		ipu handler
 * @param       irq             Interrupt line to get status for.
 *
 * @param       dev_id          Input parameter for pointer of data to be passed
 *                              to the handler. This must match value passed to
 *                              ipu_request_irq().
 *
 */
void ipu_free_irq(struct ipu_soc *ipu, uint32_t irq, void *dev_id)
{
	uint32_t reg;
	unsigned long lock_flags;

	_ipu_get(ipu);

	spin_lock_irqsave(&ipu->int_reg_spin_lock, lock_flags);

	/* disable the interrupt */
	reg = ipu_cm_read(ipu, IPUIRQ_2_CTRLREG(irq));
	reg &= ~IPUIRQ_2_MASK(irq);
	ipu_cm_write(ipu, reg, IPUIRQ_2_CTRLREG(irq));
	if (ipu->irq_list[irq].dev_id == dev_id)
		memset(&ipu->irq_list[irq], 0, sizeof(ipu->irq_list[irq]));

	spin_unlock_irqrestore(&ipu->int_reg_spin_lock, lock_flags);

	_ipu_put(ipu);
}
EXPORT_SYMBOL(ipu_free_irq);

uint32_t ipu_get_cur_buffer_idx(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type)
{
	uint32_t reg, dma_chan;

	dma_chan = channel_2_dma(channel, type);
	if (!idma_is_valid(dma_chan))
		return -EINVAL;

	reg = ipu_cm_read(ipu, IPU_CHA_TRB_MODE_SEL(ipu->devtype, dma_chan));
	if ((reg & idma_mask(dma_chan)) && _ipu_is_trb_chan(ipu, dma_chan)) {
		reg = ipu_cm_read(ipu,
				IPU_CHA_TRIPLE_CUR_BUF(ipu->devtype, dma_chan));
		return (reg & tri_cur_buf_mask(dma_chan)) >>
				tri_cur_buf_shift(dma_chan);
	} else {
		reg = ipu_cm_read(ipu, IPU_CHA_CUR_BUF(ipu->devtype, dma_chan));
		if (reg & idma_mask(dma_chan))
			return 1;
		else
			return 0;
	}
}
EXPORT_SYMBOL(ipu_get_cur_buffer_idx);

uint32_t _ipu_channel_status(struct ipu_soc *ipu, ipu_channel_t channel)
{
	uint32_t stat = 0;
	uint32_t task_stat_reg = ipu_cm_read(ipu,
					IPU_PROC_TASK_STAT(ipu->devtype));

	switch (channel) {
	case MEM_PRP_VF_MEM:
		stat = (task_stat_reg & TSTAT_VF_MASK) >> TSTAT_VF_OFFSET;
		break;
	case MEM_VDI_PRP_VF_MEM:
		stat = (task_stat_reg & TSTAT_VF_MASK) >> TSTAT_VF_OFFSET;
		break;
	case MEM_ROT_VF_MEM:
		stat =
		    (task_stat_reg & TSTAT_VF_ROT_MASK) >> TSTAT_VF_ROT_OFFSET;
		break;
	case MEM_PRP_ENC_MEM:
		stat = (task_stat_reg & TSTAT_ENC_MASK) >> TSTAT_ENC_OFFSET;
		break;
	case MEM_ROT_ENC_MEM:
		stat =
		    (task_stat_reg & TSTAT_ENC_ROT_MASK) >>
		    TSTAT_ENC_ROT_OFFSET;
		break;
	case MEM_PP_MEM:
		stat = (task_stat_reg & TSTAT_PP_MASK) >> TSTAT_PP_OFFSET;
		break;
	case MEM_ROT_PP_MEM:
		stat =
		    (task_stat_reg & TSTAT_PP_ROT_MASK) >> TSTAT_PP_ROT_OFFSET;
		break;

	default:
		stat = TASK_STAT_IDLE;
		break;
	}
	return stat;
}

/*!
 * This function check for  a logical channel status
 *
 * @param	ipu		ipu handler
 * @param	channel         Input parameter for the logical channel ID.
 *
 * @return      This function returns 0 on idle and 1 on busy.
 *
 */
uint32_t ipu_channel_status(struct ipu_soc *ipu, ipu_channel_t channel)
{
	uint32_t dma_status;

	_ipu_get(ipu);
	mutex_lock(&ipu->mutex_lock);
	dma_status = ipu_is_channel_busy(ipu, channel);
	mutex_unlock(&ipu->mutex_lock);
	_ipu_put(ipu);

	dev_dbg(ipu->dev, "%s, dma_status:%d.\n", __func__, dma_status);

	return dma_status;
}
EXPORT_SYMBOL(ipu_channel_status);

int32_t ipu_swap_channel(struct ipu_soc *ipu, ipu_channel_t from_ch, ipu_channel_t to_ch)
{
	uint32_t reg;
	unsigned long lock_flags;
	int from_dma = channel_2_dma(from_ch, IPU_INPUT_BUFFER);
	int to_dma = channel_2_dma(to_ch, IPU_INPUT_BUFFER);

	mutex_lock(&ipu->mutex_lock);

	/* enable target channel */
	reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(to_dma));
	ipu_idmac_write(ipu, reg | idma_mask(to_dma), IDMAC_CHA_EN(to_dma));

	ipu->channel_enable_mask |= 1L << IPU_CHAN_ID(to_ch);

	/* switch dp dc */
	_ipu_dp_dc_disable(ipu, from_ch, true);

	/* disable source channel */
	reg = ipu_idmac_read(ipu, IDMAC_CHA_EN(from_dma));
	ipu_idmac_write(ipu, reg & ~idma_mask(from_dma), IDMAC_CHA_EN(from_dma));
	ipu_cm_write(ipu, idma_mask(from_dma),
				IPU_CHA_CUR_BUF(ipu->devtype, from_dma));
	ipu_cm_write(ipu, tri_cur_buf_mask(from_dma),
				IPU_CHA_TRIPLE_CUR_BUF(ipu->devtype, from_dma));

	ipu->channel_enable_mask &= ~(1L << IPU_CHAN_ID(from_ch));

	spin_lock_irqsave(&ipu->rdy_reg_spin_lock, lock_flags);
	_ipu_clear_buffer_ready(ipu, from_ch, IPU_VIDEO_IN_BUFFER, 0);
	_ipu_clear_buffer_ready(ipu, from_ch, IPU_VIDEO_IN_BUFFER, 1);
	_ipu_clear_buffer_ready(ipu, from_ch, IPU_VIDEO_IN_BUFFER, 2);
	spin_unlock_irqrestore(&ipu->rdy_reg_spin_lock, lock_flags);

	mutex_unlock(&ipu->mutex_lock);

	return 0;
}
EXPORT_SYMBOL(ipu_swap_channel);

uint32_t bytes_per_pixel(uint32_t fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_GENERIC:	/*generic data */
	case IPU_PIX_FMT_RGB332:
	case IPU_PIX_FMT_YUV420P:
	case IPU_PIX_FMT_YVU420P:
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_YUV444P:
	case IPU_PIX_FMT_NV12:
	case PRE_PIX_FMT_NV21:
	case IPU_PIX_FMT_NV16:
	case PRE_PIX_FMT_NV61:
		return 1;
		break;
	case IPU_PIX_FMT_GENERIC_16:	/* generic data */
	case IPU_PIX_FMT_RGB565:
	case IPU_PIX_FMT_BGRA4444:
	case IPU_PIX_FMT_BGRA5551:
	case IPU_PIX_FMT_YUYV:
	case IPU_PIX_FMT_UYVY:
	case IPU_PIX_FMT_GPU16_SB_ST:
	case IPU_PIX_FMT_GPU16_SB_SRT:
	case IPU_PIX_FMT_GPU16_ST:
	case IPU_PIX_FMT_GPU16_SRT:
		return 2;
		break;
	case IPU_PIX_FMT_BGR24:
	case IPU_PIX_FMT_RGB24:
	case IPU_PIX_FMT_YUV444:
		return 3;
		break;
	case IPU_PIX_FMT_GENERIC_32:	/*generic data */
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_RGB32:
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_ABGR32:
	case IPU_PIX_FMT_GPU32_SB_ST:
	case IPU_PIX_FMT_GPU32_SB_SRT:
	case IPU_PIX_FMT_GPU32_ST:
	case IPU_PIX_FMT_GPU32_SRT:
	case IPU_PIX_FMT_AYUV:
		return 4;
		break;
	default:
		return 1;
		break;
	}
	return 0;
}
EXPORT_SYMBOL(bytes_per_pixel);

ipu_color_space_t format_to_colorspace(uint32_t fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_RGB666:
	case IPU_PIX_FMT_RGB565:
	case IPU_PIX_FMT_BGRA4444:
	case IPU_PIX_FMT_BGRA5551:
	case IPU_PIX_FMT_BGR24:
	case IPU_PIX_FMT_RGB24:
	case IPU_PIX_FMT_GBR24:
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_RGB32:
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_ABGR32:
	case IPU_PIX_FMT_LVDS666:
	case IPU_PIX_FMT_LVDS888:
	case IPU_PIX_FMT_GPU32_SB_ST:
	case IPU_PIX_FMT_GPU32_SB_SRT:
	case IPU_PIX_FMT_GPU32_ST:
	case IPU_PIX_FMT_GPU32_SRT:
	case IPU_PIX_FMT_GPU16_SB_ST:
	case IPU_PIX_FMT_GPU16_SB_SRT:
	case IPU_PIX_FMT_GPU16_ST:
	case IPU_PIX_FMT_GPU16_SRT:
		return RGB;
		break;

	default:
		return YCbCr;
		break;
	}
	return RGB;
}
EXPORT_SYMBOL(format_to_colorspace);

bool ipu_pixel_format_has_alpha(uint32_t fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_ABGR32:
		return true;
		break;
	default:
		return false;
		break;
	}
	return false;
}

bool ipu_ch_param_bad_alpha_pos(uint32_t pixel_fmt)
{
	return _ipu_ch_param_bad_alpha_pos(pixel_fmt);
}
EXPORT_SYMBOL(ipu_ch_param_bad_alpha_pos);

bool ipu_pixel_format_is_gpu_tile(uint32_t fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_GPU32_SB_ST:
	case IPU_PIX_FMT_GPU32_SB_SRT:
	case IPU_PIX_FMT_GPU32_ST:
	case IPU_PIX_FMT_GPU32_SRT:
	case IPU_PIX_FMT_GPU16_SB_ST:
	case IPU_PIX_FMT_GPU16_SB_SRT:
	case IPU_PIX_FMT_GPU16_ST:
	case IPU_PIX_FMT_GPU16_SRT:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL(ipu_pixel_format_is_gpu_tile);

bool ipu_pixel_format_is_split_gpu_tile(uint32_t fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_GPU32_SB_ST:
	case IPU_PIX_FMT_GPU32_SB_SRT:
	case IPU_PIX_FMT_GPU16_SB_ST:
	case IPU_PIX_FMT_GPU16_SB_SRT:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL(ipu_pixel_format_is_split_gpu_tile);

bool ipu_pixel_format_is_pre_yuv(uint32_t fmt)
{
	switch (fmt) {
	case PRE_PIX_FMT_NV21:
	case PRE_PIX_FMT_NV61:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL(ipu_pixel_format_is_pre_yuv);

bool ipu_pixel_format_is_multiplanar_yuv(uint32_t fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_YVU410P:
	case IPU_PIX_FMT_YUV420P:
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YVU420P:
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_YVU422P:
	case IPU_PIX_FMT_YUV444P:
	case IPU_PIX_FMT_NV12:
	case PRE_PIX_FMT_NV21:
	case IPU_PIX_FMT_NV16:
	case PRE_PIX_FMT_NV61:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL(ipu_pixel_format_is_multiplanar_yuv);

int ipu_ch_param_get_axi_id(struct ipu_soc *ipu, ipu_channel_t channel, ipu_buffer_t type)
{
	uint32_t dma_chan = channel_2_dma(channel, type);
	int axi_id;

	if (!idma_is_valid(dma_chan))
		return -EINVAL;

	_ipu_get(ipu);
	mutex_lock(&ipu->mutex_lock);
	axi_id = _ipu_ch_param_get_axi_id(ipu, dma_chan);
	mutex_unlock(&ipu->mutex_lock);
	_ipu_put(ipu);

	return axi_id;
}
EXPORT_SYMBOL(ipu_ch_param_get_axi_id);

#ifdef CONFIG_PM
int ipu_runtime_suspend(struct device *dev)
{
	release_bus_freq(BUS_FREQ_HIGH);
	dev_dbg(dev, "ipu busfreq high release.\n");

	return 0;
}

int ipu_runtime_resume(struct device *dev)
{
	request_bus_freq(BUS_FREQ_HIGH);
	dev_dbg(dev, "ipu busfreq high requst.\n");

	return 0;
}

static const struct dev_pm_ops ipu_pm_ops = {
	SET_RUNTIME_PM_OPS(ipu_runtime_suspend, ipu_runtime_resume, NULL)
};
#endif

/*!
 * This structure contains pointers to the power management callback functions.
 */
static struct platform_driver mxcipu_driver = {
	.driver = {
			.name		= "imx-ipuv3",
			.of_match_table	= imx_ipuv3_dt_ids,
		#ifdef CONFIG_PM
			.pm	= &ipu_pm_ops,
		#endif
	},
	.probe		= ipu_probe,
	.remove		= ipu_remove,
};

int32_t __init ipu_gen_init(void)
{
	int32_t ret;

	ret = platform_driver_register(&mxcipu_driver);
	return 0;
}

subsys_initcall(ipu_gen_init);

static void __exit ipu_gen_uninit(void)
{
	platform_driver_unregister(&mxcipu_driver);
}

module_exit(ipu_gen_uninit);
