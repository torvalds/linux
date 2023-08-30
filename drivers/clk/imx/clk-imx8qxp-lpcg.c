// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP
 *	Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "clk-scu.h"
#include "clk-imx8qxp-lpcg.h"

#include <dt-bindings/clock/imx8-clock.h>

/*
 * struct imx8qxp_lpcg_data - Description of one LPCG clock
 * @id: clock ID
 * @name: clock name
 * @parent: parent clock name
 * @flags: common clock flags
 * @offset: offset of this LPCG clock
 * @bit_idx: bit index of this LPCG clock
 * @hw_gate: whether supports HW autogate
 *
 * This structure describes one LPCG clock
 */
struct imx8qxp_lpcg_data {
	int id;
	char *name;
	char *parent;
	unsigned long flags;
	u32 offset;
	u8 bit_idx;
	bool hw_gate;
};

/*
 * struct imx8qxp_ss_lpcg - Description of one subsystem LPCG clocks
 * @lpcg: LPCG clocks array of one subsystem
 * @num_lpcg: the number of LPCG clocks
 * @num_max: the maximum number of LPCG clocks
 *
 * This structure describes each subsystem LPCG clocks information
 * which then will be used to create respective LPCGs clocks
 */
struct imx8qxp_ss_lpcg {
	const struct imx8qxp_lpcg_data *lpcg;
	u8 num_lpcg;
	u8 num_max;
};

static const struct imx8qxp_lpcg_data imx8qxp_lpcg_adma[] = {
	{ IMX_ADMA_LPCG_UART0_IPG_CLK, "uart0_lpcg_ipg_clk", "dma_ipg_clk_root", 0, ADMA_LPUART_0_LPCG, 16, 0, },
	{ IMX_ADMA_LPCG_UART0_BAUD_CLK, "uart0_lpcg_baud_clk", "uart0_clk", 0, ADMA_LPUART_0_LPCG, 0, 0, },
	{ IMX_ADMA_LPCG_UART1_IPG_CLK, "uart1_lpcg_ipg_clk", "dma_ipg_clk_root", 0, ADMA_LPUART_1_LPCG, 16, 0, },
	{ IMX_ADMA_LPCG_UART1_BAUD_CLK, "uart1_lpcg_baud_clk", "uart1_clk", 0, ADMA_LPUART_1_LPCG, 0, 0, },
	{ IMX_ADMA_LPCG_UART2_IPG_CLK, "uart2_lpcg_ipg_clk", "dma_ipg_clk_root", 0, ADMA_LPUART_2_LPCG, 16, 0, },
	{ IMX_ADMA_LPCG_UART2_BAUD_CLK, "uart2_lpcg_baud_clk", "uart2_clk", 0, ADMA_LPUART_2_LPCG, 0, 0, },
	{ IMX_ADMA_LPCG_UART3_IPG_CLK, "uart3_lpcg_ipg_clk", "dma_ipg_clk_root", 0, ADMA_LPUART_3_LPCG, 16, 0, },
	{ IMX_ADMA_LPCG_UART3_BAUD_CLK, "uart3_lpcg_baud_clk", "uart3_clk", 0, ADMA_LPUART_3_LPCG, 0, 0, },
	{ IMX_ADMA_LPCG_I2C0_IPG_CLK, "i2c0_lpcg_ipg_clk", "dma_ipg_clk_root", 0, ADMA_LPI2C_0_LPCG, 16, 0, },
	{ IMX_ADMA_LPCG_I2C0_CLK, "i2c0_lpcg_clk", "i2c0_clk", 0, ADMA_LPI2C_0_LPCG, 0, 0, },
	{ IMX_ADMA_LPCG_I2C1_IPG_CLK, "i2c1_lpcg_ipg_clk", "dma_ipg_clk_root", 0, ADMA_LPI2C_1_LPCG, 16, 0, },
	{ IMX_ADMA_LPCG_I2C1_CLK, "i2c1_lpcg_clk", "i2c1_clk", 0, ADMA_LPI2C_1_LPCG, 0, 0, },
	{ IMX_ADMA_LPCG_I2C2_IPG_CLK, "i2c2_lpcg_ipg_clk", "dma_ipg_clk_root", 0, ADMA_LPI2C_2_LPCG, 16, 0, },
	{ IMX_ADMA_LPCG_I2C2_CLK, "i2c2_lpcg_clk", "i2c2_clk", 0, ADMA_LPI2C_2_LPCG, 0, 0, },
	{ IMX_ADMA_LPCG_I2C3_IPG_CLK, "i2c3_lpcg_ipg_clk", "dma_ipg_clk_root", 0, ADMA_LPI2C_3_LPCG, 16, 0, },
	{ IMX_ADMA_LPCG_I2C3_CLK, "i2c3_lpcg_clk", "i2c3_clk", 0, ADMA_LPI2C_3_LPCG, 0, 0, },

	{ IMX_ADMA_LPCG_DSP_CORE_CLK, "dsp_lpcg_core_clk", "dma_ipg_clk_root", 0, ADMA_HIFI_LPCG, 28, 0, },
	{ IMX_ADMA_LPCG_DSP_IPG_CLK, "dsp_lpcg_ipg_clk", "dma_ipg_clk_root", 0, ADMA_HIFI_LPCG, 20, 0, },
	{ IMX_ADMA_LPCG_DSP_ADB_CLK, "dsp_lpcg_adb_clk", "dma_ipg_clk_root", 0, ADMA_HIFI_LPCG, 16, 0, },
	{ IMX_ADMA_LPCG_OCRAM_IPG_CLK, "ocram_lpcg_ipg_clk", "dma_ipg_clk_root", 0, ADMA_OCRAM_LPCG, 16, 0, },
};

static const struct imx8qxp_ss_lpcg imx8qxp_ss_adma = {
	.lpcg = imx8qxp_lpcg_adma,
	.num_lpcg = ARRAY_SIZE(imx8qxp_lpcg_adma),
	.num_max = IMX_ADMA_LPCG_CLK_END,
};

static const struct imx8qxp_lpcg_data imx8qxp_lpcg_conn[] = {
	{ IMX_CONN_LPCG_SDHC0_PER_CLK, "sdhc0_lpcg_per_clk", "sdhc0_clk", 0, CONN_USDHC_0_LPCG, 0, 0, },
	{ IMX_CONN_LPCG_SDHC0_IPG_CLK, "sdhc0_lpcg_ipg_clk", "conn_ipg_clk_root", 0, CONN_USDHC_0_LPCG, 16, 0, },
	{ IMX_CONN_LPCG_SDHC0_HCLK, "sdhc0_lpcg_ahb_clk", "conn_axi_clk_root", 0, CONN_USDHC_0_LPCG, 20, 0, },
	{ IMX_CONN_LPCG_SDHC1_PER_CLK, "sdhc1_lpcg_per_clk", "sdhc1_clk", 0, CONN_USDHC_1_LPCG, 0, 0, },
	{ IMX_CONN_LPCG_SDHC1_IPG_CLK, "sdhc1_lpcg_ipg_clk", "conn_ipg_clk_root", 0, CONN_USDHC_1_LPCG, 16, 0, },
	{ IMX_CONN_LPCG_SDHC1_HCLK, "sdhc1_lpcg_ahb_clk", "conn_axi_clk_root", 0, CONN_USDHC_1_LPCG, 20, 0, },
	{ IMX_CONN_LPCG_SDHC2_PER_CLK, "sdhc2_lpcg_per_clk", "sdhc2_clk", 0, CONN_USDHC_2_LPCG, 0, 0, },
	{ IMX_CONN_LPCG_SDHC2_IPG_CLK, "sdhc2_lpcg_ipg_clk", "conn_ipg_clk_root", 0, CONN_USDHC_2_LPCG, 16, 0, },
	{ IMX_CONN_LPCG_SDHC2_HCLK, "sdhc2_lpcg_ahb_clk", "conn_axi_clk_root", 0, CONN_USDHC_2_LPCG, 20, 0, },
	{ IMX_CONN_LPCG_ENET0_ROOT_CLK, "enet0_ipg_root_clk", "enet0_clk", 0, CONN_ENET_0_LPCG, 0, 0, },
	{ IMX_CONN_LPCG_ENET0_TX_CLK, "enet0_tx_clk", "enet0_clk", 0, CONN_ENET_0_LPCG, 4, 0, },
	{ IMX_CONN_LPCG_ENET0_AHB_CLK, "enet0_ahb_clk", "conn_axi_clk_root", 0, CONN_ENET_0_LPCG, 8, 0, },
	{ IMX_CONN_LPCG_ENET0_IPG_S_CLK, "enet0_ipg_s_clk", "conn_ipg_clk_root", 0, CONN_ENET_0_LPCG, 20, 0, },
	{ IMX_CONN_LPCG_ENET0_IPG_CLK, "enet0_ipg_clk", "enet0_ipg_s_clk", 0, CONN_ENET_0_LPCG, 16, 0, },
	{ IMX_CONN_LPCG_ENET1_ROOT_CLK, "enet1_ipg_root_clk", "enet1_clk", 0, CONN_ENET_1_LPCG, 0, 0, },
	{ IMX_CONN_LPCG_ENET1_TX_CLK, "enet1_tx_clk", "enet1_clk", 0, CONN_ENET_1_LPCG, 4, 0, },
	{ IMX_CONN_LPCG_ENET1_AHB_CLK, "enet1_ahb_clk", "conn_axi_clk_root", 0, CONN_ENET_1_LPCG, 8, 0, },
	{ IMX_CONN_LPCG_ENET1_IPG_S_CLK, "enet1_ipg_s_clk", "conn_ipg_clk_root", 0, CONN_ENET_1_LPCG, 20, 0, },
	{ IMX_CONN_LPCG_ENET1_IPG_CLK, "enet1_ipg_clk", "enet0_ipg_s_clk", 0, CONN_ENET_1_LPCG, 16, 0, },
};

static const struct imx8qxp_ss_lpcg imx8qxp_ss_conn = {
	.lpcg = imx8qxp_lpcg_conn,
	.num_lpcg = ARRAY_SIZE(imx8qxp_lpcg_conn),
	.num_max = IMX_CONN_LPCG_CLK_END,
};

static const struct imx8qxp_lpcg_data imx8qxp_lpcg_lsio[] = {
	{ IMX_LSIO_LPCG_PWM0_IPG_CLK, "pwm0_lpcg_ipg_clk", "pwm0_clk", 0, LSIO_PWM_0_LPCG, 0, 0, },
	{ IMX_LSIO_LPCG_PWM0_IPG_HF_CLK, "pwm0_lpcg_ipg_hf_clk", "pwm0_clk", 0, LSIO_PWM_0_LPCG, 4, 0, },
	{ IMX_LSIO_LPCG_PWM0_IPG_S_CLK, "pwm0_lpcg_ipg_s_clk", "pwm0_clk", 0, LSIO_PWM_0_LPCG, 16, 0, },
	{ IMX_LSIO_LPCG_PWM0_IPG_SLV_CLK, "pwm0_lpcg_ipg_slv_clk", "lsio_bus_clk_root", 0, LSIO_PWM_0_LPCG, 20, 0, },
	{ IMX_LSIO_LPCG_PWM0_IPG_MSTR_CLK, "pwm0_lpcg_ipg_mstr_clk", "pwm0_clk", 0, LSIO_PWM_0_LPCG, 24, 0, },
	{ IMX_LSIO_LPCG_PWM1_IPG_CLK, "pwm1_lpcg_ipg_clk", "pwm1_clk", 0, LSIO_PWM_1_LPCG, 0, 0, },
	{ IMX_LSIO_LPCG_PWM1_IPG_HF_CLK, "pwm1_lpcg_ipg_hf_clk", "pwm1_clk", 0, LSIO_PWM_1_LPCG, 4, 0, },
	{ IMX_LSIO_LPCG_PWM1_IPG_S_CLK, "pwm1_lpcg_ipg_s_clk", "pwm1_clk", 0, LSIO_PWM_1_LPCG, 16, 0, },
	{ IMX_LSIO_LPCG_PWM1_IPG_SLV_CLK, "pwm1_lpcg_ipg_slv_clk", "lsio_bus_clk_root", 0, LSIO_PWM_1_LPCG, 20, 0, },
	{ IMX_LSIO_LPCG_PWM1_IPG_MSTR_CLK, "pwm1_lpcg_ipg_mstr_clk", "pwm1_clk", 0, LSIO_PWM_1_LPCG, 24, 0, },
	{ IMX_LSIO_LPCG_PWM2_IPG_CLK, "pwm2_lpcg_ipg_clk", "pwm2_clk", 0, LSIO_PWM_2_LPCG, 0, 0, },
	{ IMX_LSIO_LPCG_PWM2_IPG_HF_CLK, "pwm2_lpcg_ipg_hf_clk", "pwm2_clk", 0, LSIO_PWM_2_LPCG, 4, 0, },
	{ IMX_LSIO_LPCG_PWM2_IPG_S_CLK, "pwm2_lpcg_ipg_s_clk", "pwm2_clk", 0, LSIO_PWM_2_LPCG, 16, 0, },
	{ IMX_LSIO_LPCG_PWM2_IPG_SLV_CLK, "pwm2_lpcg_ipg_slv_clk", "lsio_bus_clk_root", 0, LSIO_PWM_2_LPCG, 20, 0, },
	{ IMX_LSIO_LPCG_PWM2_IPG_MSTR_CLK, "pwm2_lpcg_ipg_mstr_clk", "pwm2_clk", 0, LSIO_PWM_2_LPCG, 24, 0, },
	{ IMX_LSIO_LPCG_PWM3_IPG_CLK, "pwm3_lpcg_ipg_clk", "pwm3_clk", 0, LSIO_PWM_3_LPCG, 0, 0, },
	{ IMX_LSIO_LPCG_PWM3_IPG_HF_CLK, "pwm3_lpcg_ipg_hf_clk", "pwm3_clk", 0, LSIO_PWM_3_LPCG, 4, 0, },
	{ IMX_LSIO_LPCG_PWM3_IPG_S_CLK, "pwm3_lpcg_ipg_s_clk", "pwm3_clk", 0, LSIO_PWM_3_LPCG, 16, 0, },
	{ IMX_LSIO_LPCG_PWM3_IPG_SLV_CLK, "pwm3_lpcg_ipg_slv_clk", "lsio_bus_clk_root", 0, LSIO_PWM_3_LPCG, 20, 0, },
	{ IMX_LSIO_LPCG_PWM3_IPG_MSTR_CLK, "pwm3_lpcg_ipg_mstr_clk", "pwm3_clk", 0, LSIO_PWM_3_LPCG, 24, 0, },
	{ IMX_LSIO_LPCG_PWM4_IPG_CLK, "pwm4_lpcg_ipg_clk", "pwm4_clk", 0, LSIO_PWM_4_LPCG, 0, 0, },
	{ IMX_LSIO_LPCG_PWM4_IPG_HF_CLK, "pwm4_lpcg_ipg_hf_clk", "pwm4_clk", 0, LSIO_PWM_4_LPCG, 4, 0, },
	{ IMX_LSIO_LPCG_PWM4_IPG_S_CLK, "pwm4_lpcg_ipg_s_clk", "pwm4_clk", 0, LSIO_PWM_4_LPCG, 16, 0, },
	{ IMX_LSIO_LPCG_PWM4_IPG_SLV_CLK, "pwm4_lpcg_ipg_slv_clk", "lsio_bus_clk_root", 0, LSIO_PWM_4_LPCG, 20, 0, },
	{ IMX_LSIO_LPCG_PWM4_IPG_MSTR_CLK, "pwm4_lpcg_ipg_mstr_clk", "pwm4_clk", 0, LSIO_PWM_4_LPCG, 24, 0, },
	{ IMX_LSIO_LPCG_PWM5_IPG_CLK, "pwm5_lpcg_ipg_clk", "pwm5_clk", 0, LSIO_PWM_5_LPCG, 0, 0, },
	{ IMX_LSIO_LPCG_PWM5_IPG_HF_CLK, "pwm5_lpcg_ipg_hf_clk", "pwm5_clk", 0, LSIO_PWM_5_LPCG, 4, 0, },
	{ IMX_LSIO_LPCG_PWM5_IPG_S_CLK, "pwm5_lpcg_ipg_s_clk", "pwm5_clk", 0, LSIO_PWM_5_LPCG, 16, 0, },
	{ IMX_LSIO_LPCG_PWM5_IPG_SLV_CLK, "pwm5_lpcg_ipg_slv_clk", "lsio_bus_clk_root", 0, LSIO_PWM_5_LPCG, 20, 0, },
	{ IMX_LSIO_LPCG_PWM5_IPG_MSTR_CLK, "pwm5_lpcg_ipg_mstr_clk", "pwm5_clk", 0, LSIO_PWM_5_LPCG, 24, 0, },
	{ IMX_LSIO_LPCG_PWM6_IPG_CLK, "pwm6_lpcg_ipg_clk", "pwm6_clk", 0, LSIO_PWM_6_LPCG, 0, 0, },
	{ IMX_LSIO_LPCG_PWM6_IPG_HF_CLK, "pwm6_lpcg_ipg_hf_clk", "pwm6_clk", 0, LSIO_PWM_6_LPCG, 4, 0, },
	{ IMX_LSIO_LPCG_PWM6_IPG_S_CLK, "pwm6_lpcg_ipg_s_clk", "pwm6_clk", 0, LSIO_PWM_6_LPCG, 16, 0, },
	{ IMX_LSIO_LPCG_PWM6_IPG_SLV_CLK, "pwm6_lpcg_ipg_slv_clk", "lsio_bus_clk_root", 0, LSIO_PWM_6_LPCG, 20, 0, },
	{ IMX_LSIO_LPCG_PWM6_IPG_MSTR_CLK, "pwm6_lpcg_ipg_mstr_clk", "pwm6_clk", 0, LSIO_PWM_6_LPCG, 24, 0, },
};

static const struct imx8qxp_ss_lpcg imx8qxp_ss_lsio = {
	.lpcg = imx8qxp_lpcg_lsio,
	.num_lpcg = ARRAY_SIZE(imx8qxp_lpcg_lsio),
	.num_max = IMX_LSIO_LPCG_CLK_END,
};

#define IMX_LPCG_MAX_CLKS	8

static struct clk_hw *imx_lpcg_of_clk_src_get(struct of_phandle_args *clkspec,
					      void *data)
{
	struct clk_hw_onecell_data *hw_data = data;
	unsigned int idx = clkspec->args[0] / 4;

	if (idx >= hw_data->num) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return hw_data->hws[idx];
}

static int imx_lpcg_parse_clks_from_dt(struct platform_device *pdev,
				       struct device_node *np)
{
	const char *output_names[IMX_LPCG_MAX_CLKS];
	const char *parent_names[IMX_LPCG_MAX_CLKS];
	unsigned int bit_offset[IMX_LPCG_MAX_CLKS];
	struct clk_hw_onecell_data *clk_data;
	struct clk_hw **clk_hws;
	struct resource *res;
	void __iomem *base;
	int count;
	int idx;
	int ret;
	int i;

	if (!of_device_is_compatible(np, "fsl,imx8qxp-lpcg"))
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	count = of_property_count_u32_elems(np, "clock-indices");
	if (count < 0) {
		dev_err(&pdev->dev, "failed to count clocks\n");
		return -EINVAL;
	}

	/*
	 * A trick here is that we set the num of clks to the MAX instead
	 * of the count from clock-indices because one LPCG supports up to
	 * 8 clock outputs which each of them is fixed to 4 bits. Then we can
	 * easily get the clock by clk-indices (bit-offset) / 4.
	 * And the cost is very limited few pointers.
	 */

	clk_data = devm_kzalloc(&pdev->dev, struct_size(clk_data, hws,
				IMX_LPCG_MAX_CLKS), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = IMX_LPCG_MAX_CLKS;
	clk_hws = clk_data->hws;

	ret = of_property_read_u32_array(np, "clock-indices", bit_offset,
					 count);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to read clock-indices\n");
		return -EINVAL;
	}

	ret = of_clk_parent_fill(np, parent_names, count);
	if (ret != count) {
		dev_err(&pdev->dev, "failed to get clock parent names\n");
		return count;
	}

	ret = of_property_read_string_array(np, "clock-output-names",
					    output_names, count);
	if (ret != count) {
		dev_err(&pdev->dev, "failed to read clock-output-names\n");
		return -EINVAL;
	}

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 500);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	for (i = 0; i < count; i++) {
		idx = bit_offset[i] / 4;
		if (idx >= IMX_LPCG_MAX_CLKS) {
			dev_warn(&pdev->dev, "invalid bit offset of clock %d\n",
				 i);
			ret = -EINVAL;
			goto unreg;
		}

		clk_hws[idx] = imx_clk_lpcg_scu_dev(&pdev->dev, output_names[i],
						    parent_names[i], 0, base,
						    bit_offset[i], false);
		if (IS_ERR(clk_hws[idx])) {
			dev_warn(&pdev->dev, "failed to register clock %d\n",
				 idx);
			ret = PTR_ERR(clk_hws[idx]);
			goto unreg;
		}
	}

	ret = devm_of_clk_add_hw_provider(&pdev->dev, imx_lpcg_of_clk_src_get,
					  clk_data);
	if (ret)
		goto unreg;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

unreg:
	while (--i >= 0) {
		idx = bit_offset[i] / 4;
		if (clk_hws[idx])
			imx_clk_lpcg_scu_unregister(clk_hws[idx]);
	}

	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int imx8qxp_lpcg_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct clk_hw_onecell_data *clk_data;
	const struct imx8qxp_ss_lpcg *ss_lpcg;
	const struct imx8qxp_lpcg_data *lpcg;
	struct resource *res;
	struct clk_hw **clks;
	void __iomem *base;
	int ret;
	int i;

	/* try new binding to parse clocks from device tree first */
	ret = imx_lpcg_parse_clks_from_dt(pdev, np);
	if (!ret)
		return 0;

	ss_lpcg = of_device_get_match_data(dev);
	if (!ss_lpcg)
		return -ENODEV;

	/*
	 * Please don't replace this with devm_platform_ioremap_resource.
	 *
	 * devm_platform_ioremap_resource calls devm_ioremap_resource which
	 * differs from devm_ioremap by also calling devm_request_mem_region
	 * and preventing other mappings in the same area.
	 *
	 * On imx8 the LPCG nodes map entire subsystems and overlap
	 * peripherals, this means that using devm_platform_ioremap_resource
	 * will cause many devices to fail to probe including serial ports.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	base = devm_ioremap(dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	clk_data = devm_kzalloc(&pdev->dev, struct_size(clk_data, hws,
				ss_lpcg->num_max), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = ss_lpcg->num_max;
	clks = clk_data->hws;

	for (i = 0; i < ss_lpcg->num_lpcg; i++) {
		lpcg = ss_lpcg->lpcg + i;
		clks[lpcg->id] = imx_clk_lpcg_scu(lpcg->name, lpcg->parent,
						  lpcg->flags, base + lpcg->offset,
						  lpcg->bit_idx, lpcg->hw_gate);
	}

	for (i = 0; i < clk_data->num; i++) {
		if (IS_ERR(clks[i]))
			pr_warn("i.MX clk %u: register failed with %ld\n",
				i, PTR_ERR(clks[i]));
	}

	return of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
}

static const struct of_device_id imx8qxp_lpcg_match[] = {
	{ .compatible = "fsl,imx8qxp-lpcg-adma", &imx8qxp_ss_adma, },
	{ .compatible = "fsl,imx8qxp-lpcg-conn", &imx8qxp_ss_conn, },
	{ .compatible = "fsl,imx8qxp-lpcg-lsio", &imx8qxp_ss_lsio, },
	{ .compatible = "fsl,imx8qxp-lpcg", NULL },
	{ /* sentinel */ }
};

static struct platform_driver imx8qxp_lpcg_clk_driver = {
	.driver = {
		.name = "imx8qxp-lpcg-clk",
		.of_match_table = imx8qxp_lpcg_match,
		.pm = &imx_clk_lpcg_scu_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe = imx8qxp_lpcg_clk_probe,
};

module_platform_driver(imx8qxp_lpcg_clk_driver);

MODULE_AUTHOR("Aisheng Dong <aisheng.dong@nxp.com>");
MODULE_DESCRIPTION("NXP i.MX8QXP LPCG clock driver");
MODULE_LICENSE("GPL v2");
