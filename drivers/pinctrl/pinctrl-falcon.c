// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/pinctrl/pinmux-falcon.c
 *  based on linux/drivers/pinctrl/pinmux-pxa910.c
 *
 *  Copyright (C) 2012 Thomas Langer <thomas.langer@lantiq.com>
 *  Copyright (C) 2012 John Crispin <john@phrozen.org>
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

#include "pinctrl-lantiq.h"

#include <lantiq_soc.h>

/* Multiplexer Control Register */
#define LTQ_PADC_MUX(x)         (x * 0x4)
/* Pull Up Enable Register */
#define LTQ_PADC_PUEN		0x80
/* Pull Down Enable Register */
#define LTQ_PADC_PDEN		0x84
/* Slew Rate Control Register */
#define LTQ_PADC_SRC		0x88
/* Drive Current Control Register */
#define LTQ_PADC_DCC		0x8C
/* Pad Control Availability Register */
#define LTQ_PADC_AVAIL          0xF0

#define pad_r32(p, reg)		ltq_r32(p + reg)
#define pad_w32(p, val, reg)	ltq_w32(val, p + reg)
#define pad_w32_mask(c, clear, set, reg) \
		pad_w32(c, (pad_r32(c, reg) & ~(clear)) | (set), reg)

#define pad_getbit(m, r, p)	(!!(ltq_r32(m + r) & (1 << p)))

#define PORTS			5
#define PINS			32
#define PORT(x)                 (x / PINS)
#define PORT_PIN(x)             (x % PINS)

#define MFP_FALCON(a, f0, f1, f2, f3)		\
{						\
	.name = #a,				\
	.pin = a,				\
	.func = {				\
		FALCON_MUX_##f0,		\
		FALCON_MUX_##f1,		\
		FALCON_MUX_##f2,		\
		FALCON_MUX_##f3,		\
	},					\
}

#define GRP_MUX(a, m, p)	\
{				\
	.name = a,		\
	.mux = FALCON_MUX_##m,	\
	.pins = p,		\
	.npins = ARRAY_SIZE(p),	\
}

enum falcon_mux {
	FALCON_MUX_GPIO = 0,
	FALCON_MUX_RST,
	FALCON_MUX_NTR,
	FALCON_MUX_PPS,
	FALCON_MUX_MDIO,
	FALCON_MUX_LED,
	FALCON_MUX_SPI,
	FALCON_MUX_ASC,
	FALCON_MUX_I2C,
	FALCON_MUX_HOSTIF,
	FALCON_MUX_SLIC,
	FALCON_MUX_JTAG,
	FALCON_MUX_PCM,
	FALCON_MUX_MII,
	FALCON_MUX_PHY,
	FALCON_MUX_NONE = 0xffff,
};

static struct pinctrl_pin_desc falcon_pads[PORTS * PINS];
static int pad_count[PORTS];

static void lantiq_load_pin_desc(struct pinctrl_pin_desc *d, int bank, int len)
{
	int base = bank * PINS;
	int i;

	for (i = 0; i < len; i++) {
		d[i].number = base + i;
		d[i].name = kasprintf(GFP_KERNEL, "io%d", base + i);
	}
	pad_count[bank] = len;
}

static struct ltq_mfp_pin falcon_mfp[] = {
	/*	pin		f0	f1	f2	f3 */
	MFP_FALCON(GPIO0,	RST,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO1,	GPIO,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO2,	GPIO,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO3,	GPIO,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO4,	NTR,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO5,	NTR,	GPIO,   PPS,    NONE),
	MFP_FALCON(GPIO6,	RST,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO7,	MDIO,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO8,	MDIO,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO9,	LED,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO10,	LED,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO11,	LED,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO12,	LED,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO13,	LED,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO14,	LED,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO32,	ASC,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO33,	ASC,	GPIO,   NONE,   NONE),
	MFP_FALCON(GPIO34,	SPI,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO35,	SPI,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO36,	SPI,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO37,	SPI,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO38,	SPI,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO39,	I2C,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO40,	I2C,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO41,	HOSTIF,	GPIO,	HOSTIF,	JTAG),
	MFP_FALCON(GPIO42,	HOSTIF,	GPIO,	HOSTIF,	NONE),
	MFP_FALCON(GPIO43,	SLIC,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO44,	SLIC,	GPIO,	PCM,	ASC),
	MFP_FALCON(GPIO45,	SLIC,	GPIO,	PCM,	ASC),
	MFP_FALCON(GPIO64,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO65,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO66,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO67,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO68,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO69,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO70,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO71,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO72,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO73,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO74,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO75,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO76,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO77,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO78,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO79,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO80,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO81,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO82,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO83,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO84,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO85,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO86,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO87,	MII,	GPIO,	NONE,	NONE),
	MFP_FALCON(GPIO88,	PHY,	GPIO,	NONE,	NONE),
};

static const unsigned pins_por[] = {GPIO0};
static const unsigned pins_ntr[] = {GPIO4};
static const unsigned pins_ntr8k[] = {GPIO5};
static const unsigned pins_pps[] = {GPIO5};
static const unsigned pins_hrst[] = {GPIO6};
static const unsigned pins_mdio[] = {GPIO7, GPIO8};
static const unsigned pins_bled[] = {GPIO9, GPIO10, GPIO11,
					GPIO12, GPIO13, GPIO14};
static const unsigned pins_asc0[] = {GPIO32, GPIO33};
static const unsigned pins_spi[] = {GPIO34, GPIO35, GPIO36};
static const unsigned pins_spi_cs0[] = {GPIO37};
static const unsigned pins_spi_cs1[] = {GPIO38};
static const unsigned pins_i2c[] = {GPIO39, GPIO40};
static const unsigned pins_jtag[] = {GPIO41};
static const unsigned pins_slic[] = {GPIO43, GPIO44, GPIO45};
static const unsigned pins_pcm[] = {GPIO44, GPIO45};
static const unsigned pins_asc1[] = {GPIO44, GPIO45};

static struct ltq_pin_group falcon_grps[] = {
	GRP_MUX("por", RST, pins_por),
	GRP_MUX("ntr", NTR, pins_ntr),
	GRP_MUX("ntr8k", NTR, pins_ntr8k),
	GRP_MUX("pps", PPS, pins_pps),
	GRP_MUX("hrst", RST, pins_hrst),
	GRP_MUX("mdio", MDIO, pins_mdio),
	GRP_MUX("bootled", LED, pins_bled),
	GRP_MUX("asc0", ASC, pins_asc0),
	GRP_MUX("spi", SPI, pins_spi),
	GRP_MUX("spi cs0", SPI, pins_spi_cs0),
	GRP_MUX("spi cs1", SPI, pins_spi_cs1),
	GRP_MUX("i2c", I2C, pins_i2c),
	GRP_MUX("jtag", JTAG, pins_jtag),
	GRP_MUX("slic", SLIC, pins_slic),
	GRP_MUX("pcm", PCM, pins_pcm),
	GRP_MUX("asc1", ASC, pins_asc1),
};

static const char * const ltq_rst_grps[] = {"por", "hrst"};
static const char * const ltq_ntr_grps[] = {"ntr", "ntr8k", "pps"};
static const char * const ltq_mdio_grps[] = {"mdio"};
static const char * const ltq_bled_grps[] = {"bootled"};
static const char * const ltq_asc_grps[] = {"asc0", "asc1"};
static const char * const ltq_spi_grps[] = {"spi", "spi cs0", "spi cs1"};
static const char * const ltq_i2c_grps[] = {"i2c"};
static const char * const ltq_jtag_grps[] = {"jtag"};
static const char * const ltq_slic_grps[] = {"slic"};
static const char * const ltq_pcm_grps[] = {"pcm"};

static struct ltq_pmx_func falcon_funcs[] = {
	{"rst",		ARRAY_AND_SIZE(ltq_rst_grps)},
	{"ntr",		ARRAY_AND_SIZE(ltq_ntr_grps)},
	{"mdio",	ARRAY_AND_SIZE(ltq_mdio_grps)},
	{"led",		ARRAY_AND_SIZE(ltq_bled_grps)},
	{"asc",		ARRAY_AND_SIZE(ltq_asc_grps)},
	{"spi",		ARRAY_AND_SIZE(ltq_spi_grps)},
	{"i2c",		ARRAY_AND_SIZE(ltq_i2c_grps)},
	{"jtag",	ARRAY_AND_SIZE(ltq_jtag_grps)},
	{"slic",	ARRAY_AND_SIZE(ltq_slic_grps)},
	{"pcm",		ARRAY_AND_SIZE(ltq_pcm_grps)},
};




/* ---------  pinconf related code --------- */
static int falcon_pinconf_group_get(struct pinctrl_dev *pctrldev,
				unsigned group, unsigned long *config)
{
	return -ENOTSUPP;
}

static int falcon_pinconf_group_set(struct pinctrl_dev *pctrldev,
				unsigned group, unsigned long *configs,
				unsigned num_configs)
{
	return -ENOTSUPP;
}

static int falcon_pinconf_get(struct pinctrl_dev *pctrldev,
				unsigned pin, unsigned long *config)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	enum ltq_pinconf_param param = LTQ_PINCONF_UNPACK_PARAM(*config);
	void __iomem *mem = info->membase[PORT(pin)];

	switch (param) {
	case LTQ_PINCONF_PARAM_DRIVE_CURRENT:
		*config = LTQ_PINCONF_PACK(param,
			!!pad_getbit(mem, LTQ_PADC_DCC, PORT_PIN(pin)));
		break;

	case LTQ_PINCONF_PARAM_SLEW_RATE:
		*config = LTQ_PINCONF_PACK(param,
			!!pad_getbit(mem, LTQ_PADC_SRC, PORT_PIN(pin)));
		break;

	case LTQ_PINCONF_PARAM_PULL:
		if (pad_getbit(mem, LTQ_PADC_PDEN, PORT_PIN(pin)))
			*config = LTQ_PINCONF_PACK(param, 1);
		else if (pad_getbit(mem, LTQ_PADC_PUEN, PORT_PIN(pin)))
			*config = LTQ_PINCONF_PACK(param, 2);
		else
			*config = LTQ_PINCONF_PACK(param, 0);

		break;

	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int falcon_pinconf_set(struct pinctrl_dev *pctrldev,
			unsigned pin, unsigned long *configs,
			unsigned num_configs)
{
	enum ltq_pinconf_param param;
	int arg;
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	void __iomem *mem = info->membase[PORT(pin)];
	u32 reg;
	int i;

	for (i = 0; i < num_configs; i++) {
		param = LTQ_PINCONF_UNPACK_PARAM(configs[i]);
		arg = LTQ_PINCONF_UNPACK_ARG(configs[i]);

		switch (param) {
		case LTQ_PINCONF_PARAM_DRIVE_CURRENT:
			reg = LTQ_PADC_DCC;
			break;

		case LTQ_PINCONF_PARAM_SLEW_RATE:
			reg = LTQ_PADC_SRC;
			break;

		case LTQ_PINCONF_PARAM_PULL:
			if (arg == 1)
				reg = LTQ_PADC_PDEN;
			else
				reg = LTQ_PADC_PUEN;
			break;

		default:
			pr_err("%s: Invalid config param %04x\n",
			pinctrl_dev_get_name(pctrldev), param);
			return -ENOTSUPP;
		}

		pad_w32(mem, BIT(PORT_PIN(pin)), reg);
		if (!(pad_r32(mem, reg) & BIT(PORT_PIN(pin))))
			return -ENOTSUPP;
	} /* for each config */

	return 0;
}

static void falcon_pinconf_dbg_show(struct pinctrl_dev *pctrldev,
			struct seq_file *s, unsigned offset)
{
	unsigned long config;
	struct pin_desc *desc;

	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	int port = PORT(offset);

	seq_printf(s, " (port %d) mux %d -- ", port,
		pad_r32(info->membase[port], LTQ_PADC_MUX(PORT_PIN(offset))));

	config = LTQ_PINCONF_PACK(LTQ_PINCONF_PARAM_PULL, 0);
	if (!falcon_pinconf_get(pctrldev, offset, &config))
		seq_printf(s, "pull %d ",
			(int)LTQ_PINCONF_UNPACK_ARG(config));

	config = LTQ_PINCONF_PACK(LTQ_PINCONF_PARAM_DRIVE_CURRENT, 0);
	if (!falcon_pinconf_get(pctrldev, offset, &config))
		seq_printf(s, "drive-current %d ",
			(int)LTQ_PINCONF_UNPACK_ARG(config));

	config = LTQ_PINCONF_PACK(LTQ_PINCONF_PARAM_SLEW_RATE, 0);
	if (!falcon_pinconf_get(pctrldev, offset, &config))
		seq_printf(s, "slew-rate %d ",
			(int)LTQ_PINCONF_UNPACK_ARG(config));

	desc = pin_desc_get(pctrldev, offset);
	if (desc) {
		if (desc->gpio_owner)
			seq_printf(s, " owner: %s", desc->gpio_owner);
	} else {
		seq_printf(s, " not registered");
	}
}

static void falcon_pinconf_group_dbg_show(struct pinctrl_dev *pctrldev,
			struct seq_file *s, unsigned selector)
{
}

static const struct pinconf_ops falcon_pinconf_ops = {
	.pin_config_get			= falcon_pinconf_get,
	.pin_config_set			= falcon_pinconf_set,
	.pin_config_group_get		= falcon_pinconf_group_get,
	.pin_config_group_set		= falcon_pinconf_group_set,
	.pin_config_dbg_show		= falcon_pinconf_dbg_show,
	.pin_config_group_dbg_show	= falcon_pinconf_group_dbg_show,
};

static struct pinctrl_desc falcon_pctrl_desc = {
	.owner		= THIS_MODULE,
	.pins		= falcon_pads,
	.confops	= &falcon_pinconf_ops,
};

static inline int falcon_mux_apply(struct pinctrl_dev *pctrldev,
			int mfp, int mux)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	int port = PORT(info->mfp[mfp].pin);

	if ((port >= PORTS) || (!info->membase[port]))
		return -ENODEV;

	pad_w32(info->membase[port], mux,
		LTQ_PADC_MUX(PORT_PIN(info->mfp[mfp].pin)));
	return 0;
}

static const struct ltq_cfg_param falcon_cfg_params[] = {
	{"lantiq,pull",			LTQ_PINCONF_PARAM_PULL},
	{"lantiq,drive-current",	LTQ_PINCONF_PARAM_DRIVE_CURRENT},
	{"lantiq,slew-rate",		LTQ_PINCONF_PARAM_SLEW_RATE},
};

static struct ltq_pinmux_info falcon_info = {
	.desc		= &falcon_pctrl_desc,
	.apply_mux	= falcon_mux_apply,
	.params		= falcon_cfg_params,
	.num_params	= ARRAY_SIZE(falcon_cfg_params),
};




/* --------- register the pinctrl layer --------- */

int pinctrl_falcon_get_range_size(int id)
{
	u32 avail;

	if ((id >= PORTS) || (!falcon_info.membase[id]))
		return -EINVAL;

	avail = pad_r32(falcon_info.membase[id], LTQ_PADC_AVAIL);

	return fls(avail);
}

void pinctrl_falcon_add_gpio_range(struct pinctrl_gpio_range *range)
{
	pinctrl_add_gpio_range(falcon_info.pctrl, range);
}

static int pinctrl_falcon_probe(struct platform_device *pdev)
{
	struct device_node *np;
	int pad_count = 0;
	int ret = 0;

	/* load and remap the pad resources of the different banks */
	for_each_compatible_node(np, NULL, "lantiq,pad-falcon") {
		struct platform_device *ppdev = of_find_device_by_node(np);
		const __be32 *bank = of_get_property(np, "lantiq,bank", NULL);
		struct resource res;
		u32 avail;
		int pins;

		if (!of_device_is_available(np))
			continue;

		if (!ppdev) {
			dev_err(&pdev->dev, "failed to find pad pdev\n");
			continue;
		}
		if (!bank || *bank >= PORTS)
			continue;
		if (of_address_to_resource(np, 0, &res))
			continue;
		falcon_info.clk[*bank] = clk_get(&ppdev->dev, NULL);
		if (IS_ERR(falcon_info.clk[*bank])) {
			dev_err(&ppdev->dev, "failed to get clock\n");
			of_node_put(np)
			return PTR_ERR(falcon_info.clk[*bank]);
		}
		falcon_info.membase[*bank] = devm_ioremap_resource(&pdev->dev,
								   &res);
		if (IS_ERR(falcon_info.membase[*bank])) {
			of_node_put(np);
			return PTR_ERR(falcon_info.membase[*bank]);
		}

		avail = pad_r32(falcon_info.membase[*bank],
					LTQ_PADC_AVAIL);
		pins = fls(avail);
		lantiq_load_pin_desc(&falcon_pads[pad_count], *bank, pins);
		pad_count += pins;
		clk_enable(falcon_info.clk[*bank]);
		dev_dbg(&pdev->dev, "found %s with %d pads\n",
				res.name, pins);
	}
	dev_dbg(&pdev->dev, "found a total of %d pads\n", pad_count);
	falcon_pctrl_desc.name	= dev_name(&pdev->dev);
	falcon_pctrl_desc.npins	= pad_count;

	falcon_info.mfp		= falcon_mfp;
	falcon_info.num_mfp	= ARRAY_SIZE(falcon_mfp);
	falcon_info.grps	= falcon_grps;
	falcon_info.num_grps	= ARRAY_SIZE(falcon_grps);
	falcon_info.funcs	= falcon_funcs;
	falcon_info.num_funcs	= ARRAY_SIZE(falcon_funcs);

	ret = ltq_pinctrl_register(pdev, &falcon_info);
	if (!ret)
		dev_info(&pdev->dev, "Init done\n");
	return ret;
}

static const struct of_device_id falcon_match[] = {
	{ .compatible = "lantiq,pinctrl-falcon" },
	{},
};
MODULE_DEVICE_TABLE(of, falcon_match);

static struct platform_driver pinctrl_falcon_driver = {
	.probe = pinctrl_falcon_probe,
	.driver = {
		.name = "pinctrl-falcon",
		.of_match_table = falcon_match,
	},
};

int __init pinctrl_falcon_init(void)
{
	return platform_driver_register(&pinctrl_falcon_driver);
}

core_initcall_sync(pinctrl_falcon_init);
