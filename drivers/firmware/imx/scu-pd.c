// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
 *	Dong Aisheng <aisheng.dong@nxp.com>
 *
 * Implementation of the SCU based Power Domains
 *
 * NOTE: a better implementation suggested by Ulf Hansson is using a
 * single global power domain and implement the ->attach|detach_dev()
 * callback for the genpd and use the regular of_genpd_add_provider_simple().
 * From within the ->attach_dev(), we could get the OF node for
 * the device that is being attached and then parse the power-domain
 * cell containing the "resource id" and store that in the per device
 * struct generic_pm_domain_data (we have void pointer there for
 * storing these kind of things).
 *
 * Additionally, we need to implement the ->stop() and ->start()
 * callbacks of genpd, which is where you "power on/off" devices,
 * rather than using the above ->power_on|off() callbacks.
 *
 * However, there're two known issues:
 * 1. The ->attach_dev() of power domain infrastructure still does
 *    not support multi domains case as the struct device *dev passed
 *    in is a virtual PD device, it does not help for parsing the real
 *    device resource id from device tree, so it's unware of which
 *    real sub power domain of device should be attached.
 *
 *    The framework needs some proper extension to support multi power
 *    domain cases.
 *
 * 2. It also breaks most of current drivers as the driver probe sequence
 *    behavior changed if removing ->power_on|off() callback and use
 *    ->start() and ->stop() instead. genpd_dev_pm_attach will only power
 *    up the domain and attach device, but will not call .start() which
 *    relies on device runtime pm. That means the device power is still
 *    not up before running driver probe function. For SCU enabled
 *    platforms, all device drivers accessing registers/clock without power
 *    domain enabled will trigger a HW access error. That means we need fix
 *    most drivers probe sequence with proper runtime pm.
 *
 * In summary, we need fix above two issue before being able to switch to
 * the "single global power domain" way.
 *
 */

#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/firmware/imx/sci.h>
#include <linux/firmware/imx/svc/rm.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>

/* SCU Power Mode Protocol definition */
struct imx_sc_msg_req_set_resource_power_mode {
	struct imx_sc_rpc_msg hdr;
	u16 resource;
	u8 mode;
} __packed __aligned(4);

#define IMX_SCU_PD_NAME_SIZE 20
struct imx_sc_pm_domain {
	struct generic_pm_domain pd;
	char name[IMX_SCU_PD_NAME_SIZE];
	u32 rsrc;
};

struct imx_sc_pd_range {
	char *name;
	u32 rsrc;
	u8 num;

	/* add domain index */
	bool postfix;
	u8 start_from;
};

struct imx_sc_pd_soc {
	const struct imx_sc_pd_range *pd_ranges;
	u8 num_ranges;
};

static const struct imx_sc_pd_range imx8qxp_scu_pd_ranges[] = {
	/* LSIO SS */
	{ "pwm", IMX_SC_R_PWM_0, 8, true, 0 },
	{ "gpio", IMX_SC_R_GPIO_0, 8, true, 0 },
	{ "gpt", IMX_SC_R_GPT_0, 5, true, 0 },
	{ "kpp", IMX_SC_R_KPP, 1, false, 0 },
	{ "fspi", IMX_SC_R_FSPI_0, 2, true, 0 },
	{ "mu_a", IMX_SC_R_MU_0A, 14, true, 0 },
	{ "mu_b", IMX_SC_R_MU_5B, 9, true, 5 },

	/* CONN SS */
	{ "usb", IMX_SC_R_USB_0, 2, true, 0 },
	{ "usb0phy", IMX_SC_R_USB_0_PHY, 1, false, 0 },
	{ "usb2", IMX_SC_R_USB_2, 1, false, 0 },
	{ "usb2phy", IMX_SC_R_USB_2_PHY, 1, false, 0 },
	{ "sdhc", IMX_SC_R_SDHC_0, 3, true, 0 },
	{ "enet", IMX_SC_R_ENET_0, 2, true, 0 },
	{ "nand", IMX_SC_R_NAND, 1, false, 0 },
	{ "mlb", IMX_SC_R_MLB_0, 1, true, 0 },

	/* AUDIO SS */
	{ "audio-pll0", IMX_SC_R_AUDIO_PLL_0, 1, false, 0 },
	{ "audio-pll1", IMX_SC_R_AUDIO_PLL_1, 1, false, 0 },
	{ "audio-clk-0", IMX_SC_R_AUDIO_CLK_0, 1, false, 0 },
	{ "audio-clk-1", IMX_SC_R_AUDIO_CLK_1, 1, false, 0 },
	{ "dma0-ch", IMX_SC_R_DMA_0_CH0, 16, true, 0 },
	{ "dma1-ch", IMX_SC_R_DMA_1_CH0, 16, true, 0 },
	{ "dma2-ch", IMX_SC_R_DMA_2_CH0, 5, true, 0 },
	{ "asrc0", IMX_SC_R_ASRC_0, 1, false, 0 },
	{ "asrc1", IMX_SC_R_ASRC_1, 1, false, 0 },
	{ "esai0", IMX_SC_R_ESAI_0, 1, false, 0 },
	{ "spdif0", IMX_SC_R_SPDIF_0, 1, false, 0 },
	{ "spdif1", IMX_SC_R_SPDIF_1, 1, false, 0 },
	{ "sai", IMX_SC_R_SAI_0, 3, true, 0 },
	{ "sai3", IMX_SC_R_SAI_3, 1, false, 0 },
	{ "sai4", IMX_SC_R_SAI_4, 1, false, 0 },
	{ "sai5", IMX_SC_R_SAI_5, 1, false, 0 },
	{ "sai6", IMX_SC_R_SAI_6, 1, false, 0 },
	{ "sai7", IMX_SC_R_SAI_7, 1, false, 0 },
	{ "amix", IMX_SC_R_AMIX, 1, false, 0 },
	{ "mqs0", IMX_SC_R_MQS_0, 1, false, 0 },
	{ "dsp", IMX_SC_R_DSP, 1, false, 0 },
	{ "dsp-ram", IMX_SC_R_DSP_RAM, 1, false, 0 },

	/* DMA SS */
	{ "can", IMX_SC_R_CAN_0, 3, true, 0 },
	{ "ftm", IMX_SC_R_FTM_0, 2, true, 0 },
	{ "lpi2c", IMX_SC_R_I2C_0, 4, true, 0 },
	{ "adc", IMX_SC_R_ADC_0, 1, true, 0 },
	{ "lcd", IMX_SC_R_LCD_0, 1, true, 0 },
	{ "lcd0-pwm", IMX_SC_R_LCD_0_PWM_0, 1, true, 0 },
	{ "lpuart", IMX_SC_R_UART_0, 4, true, 0 },
	{ "lpspi", IMX_SC_R_SPI_0, 4, true, 0 },
	{ "irqstr_dsp", IMX_SC_R_IRQSTR_DSP, 1, false, 0 },

	/* VPU SS */
	{ "vpu", IMX_SC_R_VPU, 1, false, 0 },
	{ "vpu-pid", IMX_SC_R_VPU_PID0, 8, true, 0 },
	{ "vpu-dec0", IMX_SC_R_VPU_DEC_0, 1, false, 0 },
	{ "vpu-enc0", IMX_SC_R_VPU_ENC_0, 1, false, 0 },

	/* GPU SS */
	{ "gpu0-pid", IMX_SC_R_GPU_0_PID0, 4, true, 0 },

	/* HSIO SS */
	{ "pcie-b", IMX_SC_R_PCIE_B, 1, false, 0 },
	{ "serdes-1", IMX_SC_R_SERDES_1, 1, false, 0 },
	{ "hsio-gpio", IMX_SC_R_HSIO_GPIO, 1, false, 0 },

	/* MIPI SS */
	{ "mipi0", IMX_SC_R_MIPI_0, 1, false, 0 },
	{ "mipi0-pwm0", IMX_SC_R_MIPI_0_PWM_0, 1, false, 0 },
	{ "mipi0-i2c", IMX_SC_R_MIPI_0_I2C_0, 2, true, 0 },

	{ "mipi1", IMX_SC_R_MIPI_1, 1, false, 0 },
	{ "mipi1-pwm0", IMX_SC_R_MIPI_1_PWM_0, 1, false, 0 },
	{ "mipi1-i2c", IMX_SC_R_MIPI_1_I2C_0, 2, true, 0 },

	/* LVDS SS */
	{ "lvds0", IMX_SC_R_LVDS_0, 1, false, 0 },
	{ "lvds1", IMX_SC_R_LVDS_1, 1, false, 0 },

	/* DC SS */
	{ "dc0", IMX_SC_R_DC_0, 1, false, 0 },
	{ "dc0-pll", IMX_SC_R_DC_0_PLL_0, 2, true, 0 },
	{ "dc0-video", IMX_SC_R_DC_0_VIDEO0, 2, true, 0 },

	/* CM40 SS */
	{ "cm40-i2c", IMX_SC_R_M4_0_I2C, 1, false, 0 },
	{ "cm40-intmux", IMX_SC_R_M4_0_INTMUX, 1, false, 0 },
	{ "cm40-pid", IMX_SC_R_M4_0_PID0, 5, true, 0},
	{ "cm40-mu-a1", IMX_SC_R_M4_0_MU_1A, 1, false, 0},
	{ "cm40-lpuart", IMX_SC_R_M4_0_UART, 1, false, 0},

	/* CM41 SS */
	{ "cm41-i2c", IMX_SC_R_M4_1_I2C, 1, false, 0 },
	{ "cm41-intmux", IMX_SC_R_M4_1_INTMUX, 1, false, 0 },
	{ "cm41-pid", IMX_SC_R_M4_1_PID0, 5, true, 0},
	{ "cm41-mu-a1", IMX_SC_R_M4_1_MU_1A, 1, false, 0},
	{ "cm41-lpuart", IMX_SC_R_M4_1_UART, 1, false, 0},

	/* IMAGE SS */
	{ "img-jpegdec-mp", IMX_SC_R_MJPEG_DEC_MP, 1, false, 0 },
	{ "img-jpegdec-s0", IMX_SC_R_MJPEG_DEC_S0, 4, true, 0 },
	{ "img-jpegenc-mp", IMX_SC_R_MJPEG_ENC_MP, 1, false, 0 },
	{ "img-jpegenc-s0", IMX_SC_R_MJPEG_ENC_S0, 4, true, 0 },
};

static const struct imx_sc_pd_soc imx8qxp_scu_pd = {
	.pd_ranges = imx8qxp_scu_pd_ranges,
	.num_ranges = ARRAY_SIZE(imx8qxp_scu_pd_ranges),
};

static struct imx_sc_ipc *pm_ipc_handle;

static inline struct imx_sc_pm_domain *
to_imx_sc_pd(struct generic_pm_domain *genpd)
{
	return container_of(genpd, struct imx_sc_pm_domain, pd);
}

static int imx_sc_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct imx_sc_msg_req_set_resource_power_mode msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	struct imx_sc_pm_domain *pd;
	int ret;

	pd = to_imx_sc_pd(domain);

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_PM;
	hdr->func = IMX_SC_PM_FUNC_SET_RESOURCE_POWER_MODE;
	hdr->size = 2;

	msg.resource = pd->rsrc;
	msg.mode = power_on ? IMX_SC_PM_PW_MODE_ON : IMX_SC_PM_PW_MODE_LP;

	ret = imx_scu_call_rpc(pm_ipc_handle, &msg, true);
	if (ret)
		dev_err(&domain->dev, "failed to power %s resource %d ret %d\n",
			power_on ? "up" : "off", pd->rsrc, ret);

	return ret;
}

static int imx_sc_pd_power_on(struct generic_pm_domain *domain)
{
	return imx_sc_pd_power(domain, true);
}

static int imx_sc_pd_power_off(struct generic_pm_domain *domain)
{
	return imx_sc_pd_power(domain, false);
}

static struct generic_pm_domain *imx_scu_pd_xlate(struct of_phandle_args *spec,
						  void *data)
{
	struct generic_pm_domain *domain = ERR_PTR(-ENOENT);
	struct genpd_onecell_data *pd_data = data;
	unsigned int i;

	for (i = 0; i < pd_data->num_domains; i++) {
		struct imx_sc_pm_domain *sc_pd;

		sc_pd = to_imx_sc_pd(pd_data->domains[i]);
		if (sc_pd->rsrc == spec->args[0]) {
			domain = &sc_pd->pd;
			break;
		}
	}

	return domain;
}

static struct imx_sc_pm_domain *
imx_scu_add_pm_domain(struct device *dev, int idx,
		      const struct imx_sc_pd_range *pd_ranges)
{
	struct imx_sc_pm_domain *sc_pd;
	int ret;

	if (!imx_sc_rm_is_resource_owned(pm_ipc_handle, pd_ranges->rsrc + idx))
		return NULL;

	sc_pd = devm_kzalloc(dev, sizeof(*sc_pd), GFP_KERNEL);
	if (!sc_pd)
		return ERR_PTR(-ENOMEM);

	sc_pd->rsrc = pd_ranges->rsrc + idx;
	sc_pd->pd.power_off = imx_sc_pd_power_off;
	sc_pd->pd.power_on = imx_sc_pd_power_on;

	if (pd_ranges->postfix)
		snprintf(sc_pd->name, sizeof(sc_pd->name),
			 "%s%i", pd_ranges->name, pd_ranges->start_from + idx);
	else
		snprintf(sc_pd->name, sizeof(sc_pd->name),
			 "%s", pd_ranges->name);

	sc_pd->pd.name = sc_pd->name;

	if (sc_pd->rsrc >= IMX_SC_R_LAST) {
		dev_warn(dev, "invalid pd %s rsrc id %d found",
			 sc_pd->name, sc_pd->rsrc);

		devm_kfree(dev, sc_pd);
		return NULL;
	}

	ret = pm_genpd_init(&sc_pd->pd, NULL, true);
	if (ret) {
		dev_warn(dev, "failed to init pd %s rsrc id %d",
			 sc_pd->name, sc_pd->rsrc);
		devm_kfree(dev, sc_pd);
		return NULL;
	}

	return sc_pd;
}

static int imx_scu_init_pm_domains(struct device *dev,
				    const struct imx_sc_pd_soc *pd_soc)
{
	const struct imx_sc_pd_range *pd_ranges = pd_soc->pd_ranges;
	struct generic_pm_domain **domains;
	struct genpd_onecell_data *pd_data;
	struct imx_sc_pm_domain *sc_pd;
	u32 count = 0;
	int i, j;

	for (i = 0; i < pd_soc->num_ranges; i++)
		count += pd_ranges[i].num;

	domains = devm_kcalloc(dev, count, sizeof(*domains), GFP_KERNEL);
	if (!domains)
		return -ENOMEM;

	pd_data = devm_kzalloc(dev, sizeof(*pd_data), GFP_KERNEL);
	if (!pd_data)
		return -ENOMEM;

	count = 0;
	for (i = 0; i < pd_soc->num_ranges; i++) {
		for (j = 0; j < pd_ranges[i].num; j++) {
			sc_pd = imx_scu_add_pm_domain(dev, j, &pd_ranges[i]);
			if (IS_ERR_OR_NULL(sc_pd))
				continue;

			domains[count++] = &sc_pd->pd;
			dev_dbg(dev, "added power domain %s\n", sc_pd->pd.name);
		}
	}

	pd_data->domains = domains;
	pd_data->num_domains = count;
	pd_data->xlate = imx_scu_pd_xlate;

	of_genpd_add_provider_onecell(dev->of_node, pd_data);

	return 0;
}

static int imx_sc_pd_probe(struct platform_device *pdev)
{
	const struct imx_sc_pd_soc *pd_soc;
	int ret;

	ret = imx_scu_get_handle(&pm_ipc_handle);
	if (ret)
		return ret;

	pd_soc = of_device_get_match_data(&pdev->dev);
	if (!pd_soc)
		return -ENODEV;

	return imx_scu_init_pm_domains(&pdev->dev, pd_soc);
}

static const struct of_device_id imx_sc_pd_match[] = {
	{ .compatible = "fsl,imx8qxp-scu-pd", &imx8qxp_scu_pd},
	{ .compatible = "fsl,scu-pd", &imx8qxp_scu_pd},
	{ /* sentinel */ }
};

static struct platform_driver imx_sc_pd_driver = {
	.driver = {
		.name = "imx-scu-pd",
		.of_match_table = imx_sc_pd_match,
	},
	.probe = imx_sc_pd_probe,
};
builtin_platform_driver(imx_sc_pd_driver);

MODULE_AUTHOR("Dong Aisheng <aisheng.dong@nxp.com>");
MODULE_DESCRIPTION("IMX SCU Power Domain driver");
MODULE_LICENSE("GPL v2");
