/*
 * Pinctrl driver for Rockchip SoCs
 *
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * With some ideas taken from pinctrl-samsung:
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2012 Linaro Ltd
 *		http://www.linaro.org
 *
 * and pinctrl-at91:
 * Copyright (C) 2011-2012 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/clk.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/pinctrl/rockchip.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>


#include "core.h"
#include "pinconf.h"


#if 1
#define DBG_PINCTRL(x...)  if(atomic_read(&info->debug_flag) == 1) printk(x)
#else
#define DBG_PINCTRL(x...)
#endif


/* GPIO control registers */
#define GPIO_SWPORT_DR		0x00
#define GPIO_SWPORT_DDR		0x04
#define GPIO_INTEN			0x30
#define GPIO_INTMASK		0x34
#define GPIO_INTTYPE_LEVEL	0x38
#define GPIO_INT_POLARITY	0x3c
#define GPIO_INT_STATUS		0x40
#define GPIO_INT_RAWSTATUS	0x44
#define GPIO_DEBOUNCE		0x48
#define GPIO_PORTS_EOI		0x4c
#define GPIO_EXT_PORT		0x50
#define GPIO_LS_SYNC		0x60

enum rockchip_pinctrl_type {
	RK2928,
	RK3066B,
	RK3188,
	RK3288,
};

enum rockchip_pin_bank_type {
	COMMON_BANK,
	RK3188_BANK0,
};

/**
 * @reg_base: register base of the gpio bank
 * @reg_pull: optional separate register for additional pull settings
 * @clk: clock of the gpio bank
 * @irq: interrupt of the gpio bank
 * @pin_base: first pin number
 * @nr_pins: number of pins in this bank
 * @name: name of the bank
 * @bank_num: number of the bank, to account for holes
 * @valid: are all necessary informations present
 * @of_node: dt node of this bank
 * @drvdata: common pinctrl basedata
 * @domain: irqdomain of the gpio bank
 * @gpio_chip: gpiolib chip
 * @grange: gpio range
 * @slock: spinlock for the gpio bank
 */
struct rockchip_pin_bank {
	void __iomem			*reg_base;
	void __iomem			*reg_pull;
	struct clk			*clk;
	int				irq;
	u32				pin_base;
	u8				nr_pins;
	char				*name;
	u8				bank_num;
	enum rockchip_pin_bank_type	bank_type;
	bool				valid;
	struct device_node		*of_node;
	struct rockchip_pinctrl		*drvdata;
	struct irq_domain		*domain;
	struct gpio_chip		gpio_chip;
	struct pinctrl_gpio_range	grange;
	spinlock_t			slock;
	u32				toggle_edge_mode;
	u32 				suspend_wakeup;
	u32 				saved_wakeup;	
};

#define PIN_BANK(id, pins, label)			\
	{						\
		.bank_num	= id,			\
		.nr_pins	= pins,			\
		.name		= label,		\
	}

/**
 */
struct rockchip_pin_ctrl {
	struct rockchip_pin_bank	*pin_banks;
	u32				nr_banks;
	u32				nr_pins;
	char				*label;
	enum rockchip_pinctrl_type	type;
	int				mux_offset;
	void	(*pull_calc_reg)(struct rockchip_pin_bank *bank, int pin_num,
				 void __iomem **reg, u8 *bit);
};

struct rockchip_pin_config {
	unsigned int		func;
	unsigned long		*configs;
	unsigned int		nconfigs;
};

/**
 * struct rockchip_pin_group: represent group of pins of a pinmux function.
 * @name: name of the pin group, used to lookup the group.
 * @pins: the pins included in this group.
 * @npins: number of pins included in this group.
 * @func: the mux function number to be programmed when selected.
 * @configs: the config values to be set for each pin
 * @nconfigs: number of configs for each pin
 */
struct rockchip_pin_group {
	const char			*name;	
	const char			*func_name;
	unsigned int			npins;
	unsigned int			*pins;
	struct rockchip_pin_config	*data;
};

/**
 * struct rockchip_pmx_func: represent a pin function.
 * @name: name of the pin function, used to lookup the function.
 * @groups: one or more names of pin groups that provide this function.
 * @num_groups: number of groups included in @groups.
 */
struct rockchip_pmx_func {
	const char		*name;
	const char		**groups;
	u8			ngroups;
};

struct rockchip_pinctrl {
	void __iomem			*reg_base;
	void __iomem			*reg_pull;
	struct device			*dev;
	struct rockchip_pin_ctrl	*ctrl;
	struct pinctrl_desc		pctl;
	struct pinctrl_dev		*pctl_dev;
	struct rockchip_pin_group	*groups;
	unsigned int			ngroups;
	struct rockchip_pmx_func	*functions;
	unsigned int			nfunctions;
	
	atomic_t			debug_flag;	
};

struct iomux_mode{
        unsigned int mode:4,
                     off:4,
                     goff:4,
                     bank:4,
                     reserve:16;
};

struct union_mode{
        union{
                struct iomux_mode mux;
                unsigned int mode;
        };
};

struct func_to_reg_offset {
	unsigned int reg_type;
	const char *vol_name;
	const char *func_name;
	const char *group_name;
	unsigned int reg_offset;
	unsigned int bit_offset;
	unsigned int bit_mask;
};

#define FUNC_GROUP_TO_REG_OFFSET(type, vol, func, group, reg, bit, mask)	\
	{						\
		.reg_type	= type,		\
		.vol_name	= vol,		\
		.func_name	= func,		\
		.group_name	= group,	\
		.reg_offset	= reg,		\
		.bit_offset	= bit,		\
		.bit_mask	= mask,		\
	}



static inline struct rockchip_pin_bank *gc_to_pin_bank(struct gpio_chip *gc)
{
	return container_of(gc, struct rockchip_pin_bank, gpio_chip);
}

static const inline struct rockchip_pin_group *pinctrl_name_to_group(
					const struct rockchip_pinctrl *info,
					const char *name)
{
	int i;

	for (i = 0; i < info->ngroups; i++) {
		if (!strcmp(info->groups[i].name, name))
			return &info->groups[i];
	}

	return NULL;
}

/*
 * given a pin number that is local to a pin controller, find out the pin bank
 * and the register base of the pin bank.
 */
static struct rockchip_pin_bank *pin_to_bank(struct rockchip_pinctrl *info,
								unsigned pin)
{
	struct rockchip_pin_bank *b = info->ctrl->pin_banks;

	while (pin >= (b->pin_base + b->nr_pins))
		b++;

	return b;
}

static struct rockchip_pin_bank *bank_num_to_bank(
					struct rockchip_pinctrl *info,
					unsigned num)
{
	struct rockchip_pin_bank *b = info->ctrl->pin_banks;
	int i;

	for (i = 0; i < info->ctrl->nr_banks; i++, b++) {
		if (b->bank_num == num)
			return b;
	}

	return ERR_PTR(-EINVAL);
}

/*
 * Pinctrl_ops handling
 */

static int rockchip_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->ngroups;
}

static const char *rockchip_get_group_name(struct pinctrl_dev *pctldev,
							unsigned selector)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->groups[selector].name;
}

static int rockchip_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned selector, const unsigned **pins,
				      unsigned *npins)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	if (selector >= info->ngroups)
		return -EINVAL;

	*pins = info->groups[selector].pins;
	*npins = info->groups[selector].npins;

	return 0;
}

static int rockchip_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const struct rockchip_pin_group *grp;
	struct pinctrl_map *new_map;
	struct device_node *parent;
	int map_num = 1;
	int i;

	/*
	 * first find the group of this node and check if we need to create
	 * config maps for pins
	 */
	grp = pinctrl_name_to_group(info, np->name);
	if (!grp) {
		dev_err(info->dev, "unable to find group for node %s\n",
			np->name);
		return -EINVAL;
	}

	map_num += grp->npins;
	new_map = devm_kzalloc(pctldev->dev, sizeof(*new_map) * map_num,
								GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	*map = new_map;
	*num_maps = map_num;

	/* create mux map */
	parent = of_get_parent(np);
	if (!parent) {
		devm_kfree(pctldev->dev, new_map);
		return -EINVAL;
	}
	new_map[0].type = PIN_MAP_TYPE_MUX_GROUP;
	new_map[0].data.mux.function = parent->name;
	new_map[0].data.mux.group = np->name;
	of_node_put(parent);

	/* create config map */
	new_map++;
	for (i = 0; i < grp->npins; i++) {
		new_map[i].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		new_map[i].data.configs.group_or_pin = grp->name;
				//pin_get_name(pctldev, grp->pins[i]);
		new_map[i].data.configs.configs = grp->data[i].configs;
		new_map[i].data.configs.num_configs = grp->data[i].nconfigs;
	}

	dev_dbg(pctldev->dev, "maps: function %s group %s num %d\n",
		(*map)->data.mux.function, (*map)->data.mux.group, map_num);

	return 0;
}

static void rockchip_dt_free_map(struct pinctrl_dev *pctldev,
				    struct pinctrl_map *map, unsigned num_maps)
{
}

static const struct pinctrl_ops rockchip_pctrl_ops = {
	.get_groups_count	= rockchip_get_groups_count,
	.get_group_name		= rockchip_get_group_name,
	.get_group_pins		= rockchip_get_group_pins,
	.dt_node_to_map		= rockchip_dt_node_to_map,
	.dt_free_map		= rockchip_dt_free_map,
};



/*
 * Set a new mux function for a pin.
 *
 * The register is divided into the upper and lower 16 bit. When changing
 * a value, the previous register value is not read and changed. Instead
 * it seems the changed bits are marked in the upper 16 bit, while the
 * changed value gets set in the same offset in the lower 16 bit.
 * All pin settings seem to be 2 bit wide in both the upper and lower
 * parts.
 * @bank: pin bank to change
 * @pin: pin to change
 * @mux: new mux function to set
 */
static void rockchip_set_mux(struct rockchip_pin_bank *bank, int pin, int mux)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	void __iomem *reg = info->reg_base + info->ctrl->mux_offset;
	unsigned long flags;
	u8 bit;
	u32 data;
	struct union_mode m;
	
	/* GPIO0_C */
	/*GPIO0_C0 = 0x0c00, NAND_D8, */
	/*GPIO0_C1 = 0x0c10, NAND_D9, */

	if(bank->bank_num == 15)
	{
		printk("%s:error bank num %d is out of range\n",__func__, bank->bank_num);
		return ;
	}
    
        m.mode = mux;
	
	if((m.mux.bank != bank->bank_num) || (((m.mux.goff - 0x0A) * 8 + m.mux.off ) != pin))
	{
		printk("%s:error:mux_bank(%d) != gpio_bank(%d), mux_offset(%d) != gpio_offset(%d)\n",__func__,
		m.mux.bank, bank->bank_num, ((m.mux.goff - 0x0A) * 8 + m.mux.off ), pin);
		return;
	}

	/* get basic quadrupel of mux registers and the correct reg inside */
	reg += bank->bank_num * 0x10;
	reg += (pin / 8) * 4;
	bit = (pin % 8) * 2;

	spin_lock_irqsave(&bank->slock, flags);

	data = (3 << (bit + 16));
	data |= (mux & 3) << bit;
	writel(data, reg);

	spin_unlock_irqrestore(&bank->slock, flags);

	DBG_PINCTRL("%s:setting GPIO%d-%d to mux:0x%x\n", __func__, m.mux.bank, ((m.mux.goff - 0x0A) * 8 + m.mux.off ), mux);
		
	//DBG_PINCTRL("%s:setting mux of GPIO%d-%d to %d\n", __func__, bank->bank_num, pin, mux&0x3);
}


/*
 * Pinmux_ops handling
 */

static int rockchip_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->nfunctions;
}

static const char *rockchip_pmx_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned selector)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->functions[selector].name;
}

static int rockchip_pmx_get_groups(struct pinctrl_dev *pctldev,
				unsigned selector, const char * const **groups,
				unsigned * const num_groups)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	*groups = info->functions[selector].groups;
	*num_groups = info->functions[selector].ngroups;

	return 0;
}

static int rockchip_pmx_enable(struct pinctrl_dev *pctldev, unsigned selector,
							    unsigned group)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const unsigned int *pins = info->groups[group].pins;
	const struct rockchip_pin_config *data = info->groups[group].data;
	struct rockchip_pin_bank *bank;
	int cnt;

	DBG_PINCTRL("%s:enable function %s group %s\n",
		__func__, info->functions[selector].name, info->groups[group].name);

	/*
	 * for each pin in the pin group selected, program the correspoding pin
	 * pin function number in the config register.
	 */
	for (cnt = 0; cnt < info->groups[group].npins; cnt++) {
		bank = pin_to_bank(info, pins[cnt]);
		rockchip_set_mux(bank, pins[cnt] - bank->pin_base,
				 data[cnt].func);
	}

	return 0;
}

static void rockchip_pmx_disable(struct pinctrl_dev *pctldev,
					unsigned selector, unsigned group)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const unsigned int *pins = info->groups[group].pins;
	const struct rockchip_pin_config *data = info->groups[group].data;
	struct rockchip_pin_bank *bank;
	int cnt;

	DBG_PINCTRL("%s:disable function %s group %s\n",
		__func__, info->functions[selector].name, info->groups[group].name);

	for (cnt = 0; cnt < info->groups[group].npins; cnt++) {
		bank = pin_to_bank(info, pins[cnt]);
		rockchip_set_mux(bank, pins[cnt] - bank->pin_base, FUNC_TO_GPIO(data[cnt].func));
	}
}

/*
 * The calls to gpio_direction_output() and gpio_direction_input()
 * leads to this function call (via the pinctrl_gpio_direction_{input|output}()
 * function called from the gpiolib interface).
 */
static int rockchip_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
					      struct pinctrl_gpio_range *range,
					      unsigned offset, bool input)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct rockchip_pin_bank *bank;
	struct gpio_chip *chip;
	int pin;
	u32 data;
	u32 mux;

	chip = range->gc;
	bank = gc_to_pin_bank(chip);
	pin = offset - chip->base;

	DBG_PINCTRL("%s:gpio_direction for pin %u as %s-%d to %s\n",
		 __func__, offset, range->name, pin, input ? "input" : "output");

	mux = (bank->bank_num << 12) | (((pin / 8) + 0x0A) << 8) | ((pin % 8)<< 4) | RK_FUNC_GPIO;

	rockchip_set_mux(bank, pin, mux);

	data = readl_relaxed(bank->reg_base + GPIO_SWPORT_DDR);
	/* set bit to 1 for output, 0 for input */
	if (!input)
		data |= BIT(pin);
	else
		data &= ~BIT(pin);
	writel_relaxed(data, bank->reg_base + GPIO_SWPORT_DDR);

	return 0;
}

static const struct pinmux_ops rockchip_pmx_ops = {
	.get_functions_count	= rockchip_pmx_get_funcs_count,
	.get_function_name	= rockchip_pmx_get_func_name,
	.get_function_groups	= rockchip_pmx_get_groups,
	.enable			= rockchip_pmx_enable,
	//.disable		= rockchip_pmx_disable,
	.gpio_set_direction	= rockchip_pmx_gpio_set_direction,
};



/*
 * Hardware access
 */

static struct func_to_reg_offset rk3188_func_to_drv_reg_offset[] = 
{
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "NULL",  "reserve" ,"NULL", RK3188_GRF_IO_CON0, 0 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "flash-vcc", "gpio0_flash" ,"NULL", RK3188_GRF_IO_CON0, 2, 3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "flash-vcc", "gpio0_flash" ,"NULL", RK3188_GRF_IO_CON0, 4 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "flash-vcc", "gpio0_d" ,"NULL", RK3188_GRF_IO_CON0, 6 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "ap0-vcc", "gpio1_uart0" ,"NULL", RK3188_GRF_IO_CON0, 8 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "ap1-vcc", "gpio1_uart1" ,"NULL", RK3188_GRF_IO_CON0, 10 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "ap1-vcc", "gpio1_spi0" ,"NULL", RK3188_GRF_IO_CON0, 10 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "NULL", "gpio1_uart2" ,"NULL", RK3188_GRF_IO_CON0, 12 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "vccio0-vcc", "gpio1_uart3" ,"NULL", RK3188_GRF_IO_CON0, 14 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "vccio0-vcc", "gpio1_spi1" ,"NULL", RK3188_GRF_IO_CON0, 14 ,3),

	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "ap1-vcc", "gpio1_i2s0" ,"NULL", RK3188_GRF_IO_CON1, 0 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "ap1-vcc", "gpio1_i2s0" ,"NULL", RK3188_GRF_IO_CON1, 2 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "ap1-vcc", "gpio1_c" ,"NULL", RK3188_GRF_IO_CON1, 4 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "vccio1-vcc", "gpio1_i2c0" ,"NULL", RK3188_GRF_IO_CON1, 6 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "vccio1-vcc", "gpio1_i2c1" ,"NULL", RK3188_GRF_IO_CON1, 8 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "vccio1-vcc", "gpio1_i2c2" ,"NULL", RK3188_GRF_IO_CON1, 10 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "ap1-vcc", "gpio1_i2c4" ,"NULL", RK3188_GRF_IO_CON1, 12 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "lcdc1-vcc", "gpio2_lcdc1" ,"NULL", RK3188_GRF_IO_CON1, 14 ,3),

	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "lcdc1-vcc", "gpio2_lcdc1" ,"NULL", RK3188_GRF_IO_CON2, 0 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "lcdc1-vcc", "gpio2_smc" ,"NULL", RK3188_GRF_IO_CON2, 2 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "NULL", "reserve" ,"NULL", RK3188_GRF_IO_CON2, 4 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "ap0-vcc", "gpio3_sdmmc" ,"NULL", RK3188_GRF_IO_CON2, 6 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "ap0-vcc", "gpio3_sdmmc" ,"NULL", RK3188_GRF_IO_CON2, 8 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "cif-vcc", "gpio3_b" ,"NULL", RK3188_GRF_IO_CON2, 10 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "cif-vcc", "gpio3_cif" ,"NULL", RK3188_GRF_IO_CON2, 12 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "cif-vcc", "gpio3_cif" ,"NULL", RK3188_GRF_IO_CON2, 14 ,3),

	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "vccio0-vcc", "gpio3_sdio" ,"NULL", RK3188_GRF_IO_CON3, 0 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "vccio0-vcc", "gpio3_sdio" ,"NULL", RK3188_GRF_IO_CON3, 2 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "vccio0-vcc", "gpio3_pwm" ,"NULL", RK3188_GRF_IO_CON3, 4 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "vccio0-vcc", "gpio3_d" ,"NULL", RK3188_GRF_IO_CON3, 6 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "flash-vcc", "flash" ,"NULL", RK3188_GRF_IO_CON3, 8 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "flash-vcc", "flash" ,"NULL", RK3188_GRF_IO_CON3, 10 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "cif-vcc", "cif" ,"NULL", RK3188_GRF_IO_CON3, 12 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "lcdc0-vcc", "lcdc0" ,"NULL", RK3188_GRF_IO_CON3, 14 ,3),

	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "lcdc0-vcc", "lcdc0" ,"NULL", RK3188_GRF_IO_CON4, 0 ,3),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "vccio0-vcc", "jtag" ,"NULL", RK3188_GRF_IO_CON4, 2 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "NULL", "misc" ,"NULL", RK3188_GRF_IO_CON4, 4 ,3),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_DRV_REG, "NULL", "reserve" ,"NULL", RK3188_GRF_IO_CON4, 6 ,3),	
		
};

static struct func_to_reg_offset rk3188_func_to_vol_reg_offset[] = 
{
	FUNC_GROUP_TO_REG_OFFSET(TYPE_VOL_REG, "ap0-vcc", "vol_domain", "ap0-vcc", RK3188_GRF_IO_CON4, 8 ,1),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_VOL_REG, "ap1-vcc", "vol_domain", "ap1-vcc", RK3188_GRF_IO_CON4, 9 ,1),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_VOL_REG, "cif-vcc", "vol_domain", "cif-vcc", RK3188_GRF_IO_CON4, 10 ,1),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_VOL_REG, "flash-vcc", "vol_domain", "flash-vcc", RK3188_GRF_IO_CON4, 11 ,1),	
	FUNC_GROUP_TO_REG_OFFSET(TYPE_VOL_REG, "vccio0-vcc", "vol_domain", "vccio0-vcc", RK3188_GRF_IO_CON4, 12 ,1),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_VOL_REG, "vccio1-vcc", "vol_domain", "vccio1-vcc", RK3188_GRF_IO_CON4, 13 ,1),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_VOL_REG, "lcdc0-vcc", "vol_domain", "lcdc0-vcc", RK3188_GRF_IO_CON4, 14 ,1),
	FUNC_GROUP_TO_REG_OFFSET(TYPE_VOL_REG, "lcdc1-vcc", "vol_domain", "lcdc1-vcc", RK3188_GRF_IO_CON4, 15 ,1),
};

static void rk2928_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
				    int pin_num, void __iomem **reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	*reg = info->reg_base + RK2928_PULL_OFFSET;
	*reg += bank->bank_num * RK2928_PULL_BANK_STRIDE;
	*reg += (pin_num / RK2928_PULL_PINS_PER_REG) * 4;

	*bit = pin_num % RK2928_PULL_PINS_PER_REG;

	DBG_PINCTRL("%s:GPIO%d-%d, pull_reg=0x%p, bit=%d\n", __func__, bank->bank_num, pin_num, *reg, *bit);
};


static void rk3188_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
				    int pin_num, void __iomem **reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 12 pins of the first bank are located elsewhere */
	if (bank->bank_type == RK3188_BANK0 && pin_num < 12) {
		*reg = bank->reg_pull +
				((pin_num / RK3188_PULL_PINS_PER_REG) * 4);
		*bit = pin_num % RK3188_PULL_PINS_PER_REG;
		*bit *= RK3188_PULL_BITS_PER_PIN;
	} else {
		*reg = info->reg_pull - 4;
		*reg += bank->bank_num * RK3188_PULL_BANK_STRIDE;
		*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);

		/*
		 * The bits in these registers have an inverse ordering
		 * with the lowest pin being in bits 15:14 and the highest
		 * pin in bits 1:0
		 */
		*bit = 7 - (pin_num % RK3188_PULL_PINS_PER_REG);
		*bit *= RK3188_PULL_BITS_PER_PIN;
	}

	DBG_PINCTRL("%s:GPIO%d-%d, pull_reg=0x%p, bit=%d\n", __func__, bank->bank_num, pin_num, *reg, *bit);
}

static int rockchip_get_pull(struct rockchip_pin_bank *bank, int pin_num)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	void __iomem *reg;
	u8 bit;
	u32 data;

	/* rk3066b does support any pulls */
	if (ctrl->type == RK3066B)
		return PIN_CONFIG_BIAS_DISABLE;

	ctrl->pull_calc_reg(bank, pin_num, &reg, &bit);

	switch (ctrl->type) {
	case RK2928:
		return !(readl_relaxed(reg) & BIT(bit))
				? PIN_CONFIG_BIAS_PULL_PIN_DEFAULT
				: PIN_CONFIG_BIAS_DISABLE;
	case RK3188:
		data = readl_relaxed(reg) >> bit;
		data &= (1 << RK3188_PULL_BITS_PER_PIN) - 1;

		switch (data) {
		case 0:
			return PIN_CONFIG_BIAS_DISABLE;
		case 1:
			return PIN_CONFIG_BIAS_PULL_UP;
		case 2:
			return PIN_CONFIG_BIAS_PULL_DOWN;
		case 3:
			return PIN_CONFIG_BIAS_BUS_HOLD;
		}

		dev_err(info->dev, "unknown pull setting\n");
		return -EIO;
	default:
		dev_err(info->dev, "unsupported pinctrl type\n");
		return -EINVAL;
	};

	DBG_PINCTRL("%s:GPIO%d-%d pull is 0x%x\n", __func__, bank->bank_num, pin_num, data);
}

static int rockchip_set_pull(struct rockchip_pin_bank *bank,
					int pin_num, int pull)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	void __iomem *reg;
	unsigned long flags;
	u8 bit;
	u32 data;


	/* rk3066b does support any pulls */
	if (ctrl->type == RK3066B)
		return pull ? -EINVAL : 0;

	ctrl->pull_calc_reg(bank, pin_num, &reg, &bit);

	switch (ctrl->type) {
	case RK2928:
		spin_lock_irqsave(&bank->slock, flags);

		data = BIT(bit + 16);
		if (pull == PIN_CONFIG_BIAS_DISABLE)
			data |= BIT(bit);
		writel(data, reg);

		spin_unlock_irqrestore(&bank->slock, flags);
		break;
	case RK3188:
		spin_lock_irqsave(&bank->slock, flags);

		/* enable the write to the equivalent lower bits */
		data = ((1 << RK3188_PULL_BITS_PER_PIN) - 1) << (bit + 16);

		switch (pull) {
		case PIN_CONFIG_BIAS_DISABLE:
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			data |= (1 << bit);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			data |= (2 << bit);
			break;
		case PIN_CONFIG_BIAS_BUS_HOLD:
			data |= (3 << bit);
			break;
		default:
			dev_err(info->dev, "unsupported pull setting %d\n",
				pull);
			return -EINVAL;
		}

		writel(data, reg);

		spin_unlock_irqrestore(&bank->slock, flags);
		break;
	default:
		dev_err(info->dev, "unsupported pinctrl type\n");
		return -EINVAL;
	}


	DBG_PINCTRL("%s:GPIO%d-%d pull is 0x%x\n", __func__, bank->bank_num, pin_num, data);

	return 0;
}

/*
 * Pinconf_ops handling
 */

static bool rockchip_pinconf_pull_valid(struct rockchip_pin_ctrl *ctrl,
					enum pin_config_param pull)
{
	switch (ctrl->type) {
	case RK2928:
		return (pull == PIN_CONFIG_BIAS_PULL_PIN_DEFAULT ||
					pull == PIN_CONFIG_BIAS_DISABLE);
	case RK3066B:
		return pull ? false : true;
	case RK3188:
		return (pull != PIN_CONFIG_BIAS_PULL_PIN_DEFAULT);
	}

	return false;
}

static int _rockchip_pinconf_get(struct rockchip_pin_bank *bank,
					int pin_num, unsigned long *config, int config_type, unsigned group)
{
	struct rockchip_pinctrl *info = bank->drvdata;	
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct func_to_reg_offset reg_offset[4];//same name count should be less four
	struct func_to_reg_offset *func_to_reg;
	int i = 0, ii = 0, j = 0, jj = 0, num = 0;		
	int data = 0;
	unsigned long flags;	
	void __iomem *reg;

	
	if(group < 0)
		return -1;

	DBG_PINCTRL("%s:GPIO%d-%d,group=%s, function=%s, type=%d\n", __func__, bank->bank_num, pin_num, info->groups[group].name, info->groups[group].func_name, config_type);

	switch(ctrl->type)
	{
		case RK2928:
		//to do
		break;

		case RK3066B:
			
		break;
		
		case RK3188:
		
		switch(config_type)
		{		
			case TYPE_PULL_REG:
			//compare pin num
			
			break;
			
			case TYPE_VOL_REG:
			//compare group_name
			func_to_reg = rk3188_func_to_vol_reg_offset;
			num = ARRAY_SIZE(rk3188_func_to_vol_reg_offset);
			for(i = 0; i < num; i++)
			{
				if(!strcmp(info->groups[group].name, func_to_reg[i].group_name))
				{
					reg_offset[j++] = func_to_reg[i];
					DBG_PINCTRL("%s:select \"%s\"\n",__func__, func_to_reg[i].group_name);
				}
			}

			if(j == 0)
			{
				func_to_reg = rk3188_func_to_drv_reg_offset;	
				num = ARRAY_SIZE(rk3188_func_to_drv_reg_offset);
				for(i = 0; i < num; i++)
				{
					if(!strcmp(info->groups[group].func_name, func_to_reg[i].func_name))
					{
						
						func_to_reg = rk3188_func_to_vol_reg_offset;
						num = ARRAY_SIZE(rk3188_func_to_vol_reg_offset);
						for(ii = 0; ii < num; ii++)
						{
							if(!strcmp(func_to_reg[i].vol_name, func_to_reg[ii].group_name))
							{
								reg_offset[jj++] = func_to_reg[ii];
								DBG_PINCTRL("%s:select \"%s\"\n",__func__, func_to_reg[ii].group_name);
							}
						}
						
					}
				}


				j = jj;

			}


			break;
				
			case TYPE_DRV_REG:
			//compare func_name
			func_to_reg = rk3188_func_to_drv_reg_offset;	
			num = ARRAY_SIZE(rk3188_func_to_drv_reg_offset);
			for(i = 0; i < num; i++)
			{
				if(!strcmp(info->groups[group].func_name, func_to_reg[i].func_name))
				{
					reg_offset[j++] = func_to_reg[i];
					DBG_PINCTRL("%s:select \"%s\"\n",__func__, func_to_reg[i].func_name);
				}
			}

			break;
			
			case TYPE_TRI_REG:
			break;

			default:
			break;

		}
		
		break;

		default:
		break;
	}

	if(j <= 0)
	{
		printk("%s:could find config register for PIN%d-%d,type=%d,num=%d\n",__func__, bank->bank_num, pin_num, config_type, num);
		return -1;
	}
	
			
	for(i=0; i < j; i++)
	{
		reg = info->reg_base + reg_offset[i].reg_offset;
		spin_lock_irqsave(&bank->slock, flags);
		data = readl_relaxed(reg) >> reg_offset[i].bit_offset;
		data &= reg_offset[i].bit_mask;			
		spin_unlock_irqrestore(&bank->slock, flags);
		
		*config = data;

		DBG_PINCTRL("%s:reg_offset[%d]=0x%x,,bit_offset[%d]=0x%x,data[%d]=0x%x\n",__func__, i, reg_offset[i].reg_offset, i, reg_offset[i].bit_offset, i, data);			
	}
	
	
	return 0;
}



static int _rockchip_pinconf_set(struct rockchip_pin_bank *bank,
					int pin_num, int param, int config_type, unsigned group)
{
	struct rockchip_pinctrl *info = bank->drvdata;	
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct func_to_reg_offset reg_offset[4];//same name count should be less four
	struct func_to_reg_offset *func_to_reg;
	int i = 0, ii = 0, j = 0, jj = 0, num = 0;	
	int data = 0;
	unsigned long flags;	
	void __iomem *reg;

	
	if(group < 0)
		return -1;

	DBG_PINCTRL("%s:GPIO%d-%d,group=%s, function=%s, type=%d\n", __func__, bank->bank_num, pin_num, info->groups[group].name, info->groups[group].func_name, config_type);

	switch(ctrl->type)
	{
		case RK2928:
		//to do
		break;

		case RK3066B:
			
		break;
		
		case RK3188:
		
		switch(config_type)
		{		
			case TYPE_PULL_REG:
			//compare pin num
			
			break;
			
			case TYPE_VOL_REG:
			//compare group_name
			func_to_reg = rk3188_func_to_vol_reg_offset;
			num = ARRAY_SIZE(rk3188_func_to_vol_reg_offset);
			for(i = 0; i < num; i++)
			{
				if(!strcmp(info->groups[group].name, func_to_reg[i].group_name))
				{
					reg_offset[j++] = func_to_reg[i];
					DBG_PINCTRL("%s:select \"%s\"\n",__func__, func_to_reg[i].group_name);
				}
			}

			if(j == 0)
			{
				func_to_reg = rk3188_func_to_drv_reg_offset;	
				num = ARRAY_SIZE(rk3188_func_to_drv_reg_offset);
				for(i = 0; i < num; i++)
				{
					if(!strcmp(info->groups[group].func_name, func_to_reg[i].func_name))
					{
						
						func_to_reg = rk3188_func_to_vol_reg_offset;
						num = ARRAY_SIZE(rk3188_func_to_vol_reg_offset);
						for(ii = 0; ii < num; ii++)
						{
							if(!strcmp(rk3188_func_to_drv_reg_offset[i].vol_name, func_to_reg[ii].group_name))
							{
								reg_offset[jj++] = func_to_reg[ii];
								DBG_PINCTRL("%s:select \"%s\"\n",__func__, func_to_reg[ii].group_name);
							}
						}
						
					}
				}


				j = jj;

			}


			break;
				
			case TYPE_DRV_REG:
			//compare func_name
			func_to_reg = rk3188_func_to_drv_reg_offset;	
			num = ARRAY_SIZE(rk3188_func_to_drv_reg_offset);
			for(i = 0; i < num; i++)
			{
				if(!strcmp(info->groups[group].func_name, func_to_reg[i].func_name))
				{
					reg_offset[j++] = func_to_reg[i];
					DBG_PINCTRL("%s:select \"%s\"\n",__func__, func_to_reg[i].func_name);
				}
			}

			break;
			
			case TYPE_TRI_REG:
			break;

			default:
			break;

		}
		
		break;

		default:
		break;
	}

	if(j <= 0)
	{
		printk("%s:could find config register for PIN%d-%d,type=%d,num=%d\n",__func__, bank->bank_num, pin_num, config_type, num);
		return -1;
	}
			
	for(i=0; i < j; i++)
	{
		reg = info->reg_base + reg_offset[i].reg_offset;
		data |= ((param & reg_offset[i].bit_mask) << (16 + reg_offset[i].bit_offset));
		data |= ((param & reg_offset[i].bit_mask) << reg_offset[i].bit_offset);
		spin_lock_irqsave(&bank->slock, flags);
		//writel_relaxed(data, reg);
		spin_unlock_irqrestore(&bank->slock, flags);

		DBG_PINCTRL("%s:reg_offset[%d]=0x%x,,bit_offset[%d]=0x%x,value[%d]=0x%x\n",__func__, i, reg_offset[i].reg_offset, i, reg_offset[i].bit_offset, i, data);			
	}
	
	
	return 0;
}





/* set the pin config settings for a specified pin */
static int rockchip_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long configs, unsigned group)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct rockchip_pin_bank *bank = pin_to_bank(info, pin);
	enum pin_config_param param;
	u16 arg;
	int rc;


	param = pinconf_to_config_param(configs);
	arg = pinconf_to_config_argument(configs);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		rc =  rockchip_set_pull(bank, pin - bank->pin_base,
			param);
		if (rc)
			return rc;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
	case PIN_CONFIG_BIAS_BUS_HOLD:
		if (!rockchip_pinconf_pull_valid(info->ctrl, param))
			return -ENOTSUPP;

		if (!arg)
			return -EINVAL;

		rc = rockchip_set_pull(bank, pin - bank->pin_base,
			param);
		if (rc)
			return rc;
		break;
		
	case PIN_CONFIG_POWER_SOURCE:
		rc = _rockchip_pinconf_set(bank, pin - bank->pin_base, param, TYPE_VOL_REG, group);
		if (rc)
		return rc;
		break;

	case PIN_CONFIG_DRIVE_STRENGTH:
		rc = _rockchip_pinconf_set(bank, pin - bank->pin_base, param, TYPE_DRV_REG, group);
		if (rc)
		return rc;
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		rc = _rockchip_pinconf_set(bank, pin - bank->pin_base, param, TYPE_TRI_REG, group);
		if (rc)
		return rc;
		break;
	default:
		return -ENOTSUPP;
		break;
	}
	
	DBG_PINCTRL("%s,pin=%d,param=%d\n",__func__,pin, param);

	return 0;
}

/* get the pin config settings for a specified pin */
static int rockchip_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
							unsigned long *config, unsigned group)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct rockchip_pin_bank *bank = pin_to_bank(info, pin);
	enum pin_config_param param = pinconf_to_config_param(*config);
	int rc;
	
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (rockchip_get_pull(bank, pin - bank->pin_base) != param)
			return -EINVAL;

		*config = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
	case PIN_CONFIG_BIAS_BUS_HOLD:
		if (!rockchip_pinconf_pull_valid(info->ctrl, param))
			return -ENOTSUPP;

		if (rockchip_get_pull(bank, pin - bank->pin_base) != param)
			return -EINVAL;

		*config = 1;
		break;
	
	case PIN_CONFIG_POWER_SOURCE:
		rc = _rockchip_pinconf_get(bank, pin - bank->pin_base, config, TYPE_VOL_REG, group);
		if (rc)
		return rc;
		break;

	case PIN_CONFIG_DRIVE_STRENGTH:
		rc = _rockchip_pinconf_get(bank, pin - bank->pin_base, config, TYPE_DRV_REG, group);
		if (rc)
		return rc;
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		rc = _rockchip_pinconf_get(bank, pin - bank->pin_base, config, TYPE_TRI_REG, group);
		if (rc)
		return rc;
		break;
		
	default:
		return -ENOTSUPP;
		break;
	}

	
	DBG_PINCTRL("%s:pin=%d, param=%d\n",__func__, pin, param);

	return 0;
}

/* set the pin config settings for a specified pin group */
static int rockchip_pinconf_group_set(struct pinctrl_dev *pctldev,
			unsigned group, unsigned long config)
{	
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const unsigned int *pins;
	unsigned int cnt;
	
	DBG_PINCTRL("%s:group[%d]:%s\n",__func__, group, info->groups[group].name);
	pins = info->groups[group].pins;

	for (cnt = 0; cnt < info->groups[group].npins; cnt++)
		rockchip_pinconf_set(pctldev, pins[cnt], config, group);
	
	return 0;
}

/* get the pin config settings for a specified pin group */
static int rockchip_pinconf_group_get(struct pinctrl_dev *pctldev,
				unsigned int group, unsigned long *config)
{	
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const unsigned int *pins;

	pins = info->groups[group].pins;
	rockchip_pinconf_get(pctldev, pins[0], config, group);

	DBG_PINCTRL("%s:group[%d]:%s\n",__func__, group, info->groups[group].name);
	return 0;
}

static void rockchip_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				 struct seq_file *s, unsigned pin)
{
	//to do
}

static void rockchip_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
				       struct seq_file *s, unsigned group)
{
	//to do
}


static const struct pinconf_ops rockchip_pinconf_ops = {
	.pin_config_group_get		= rockchip_pinconf_group_get,
	.pin_config_group_set		= rockchip_pinconf_group_set,
	.pin_config_dbg_show		= rockchip_pinconf_dbg_show,
	.pin_config_group_dbg_show	= rockchip_pinconf_group_dbg_show,
};

static const struct of_device_id rockchip_bank_match[] = {
	{ .compatible = "rockchip,gpio-bank" },
	{ .compatible = "rockchip,rk3188-gpio-bank0" },
	{},
};

static void rockchip_pinctrl_child_count(struct rockchip_pinctrl *info,
						struct device_node *np)
{
	struct device_node *child;

	for_each_child_of_node(np, child) {
		if (of_match_node(rockchip_bank_match, child))
			continue;

		info->nfunctions++;
		info->ngroups += of_get_child_count(child);
	}
}

static int rockchip_pinctrl_parse_groups(struct device_node *np,
                                              struct rockchip_pin_group *grp,
                                              struct rockchip_pinctrl *info,
                                              u32 index)
{
        struct rockchip_pin_bank *bank;
        int size;
        const __be32 *list;
        int i, j;
	struct union_mode m;
	int configlen = 0;	
	unsigned long *pinconfig;	
	u32 val;
	
	DBG_PINCTRL("%s:group(%d): %s\n", __func__, index, np->name);

        /* Initialise group */
        grp->name = np->name;

        /*
         * the binding format is rockchip,pins = <mux>,
         * do sanity check and calculate pins number
         */
        list = of_get_property(np, "rockchip,pins", &size);
        /* we do not check return since it's safe node passed down */
        size /= sizeof(*list);
        if (!size || size % 1) {
                dev_err(info->dev, "wrong pins number or pins and configs should be by 1\n");
                return -EINVAL;
        }

        grp->npins = size / 1;

        grp->pins = devm_kzalloc(info->dev, grp->npins * sizeof(unsigned int),
                                                GFP_KERNEL);
        grp->data = devm_kzalloc(info->dev, grp->npins *
                                          sizeof(struct rockchip_pin_config),
                                        GFP_KERNEL);
        if (!grp->pins || !grp->data)
                return -ENOMEM;

	pinconfig = kzalloc(configlen * sizeof(*pinconfig), GFP_KERNEL);

        for (i = 0; i < size; i++) {
		m.mode = be32_to_cpu(*list++);
		
		bank = bank_num_to_bank(info, m.mux.bank);
		if (IS_ERR(bank))
			return PTR_ERR(bank);
		
		grp->pins[i] = bank->pin_base + (m.mux.goff - 0x0A) * 8 + m.mux.off;
		grp->data[i].func = m.mode;

		
		j = 0;
		configlen = 0;
		
		if (of_find_property(np, "rockchip,pull", NULL))
			configlen++;
		if (of_find_property(np, "rockchip,voltage", NULL))
			configlen++;	
		if (of_find_property(np, "rockchip,drive", NULL))
			configlen++;
		if (of_find_property(np, "rockchip,tristate", NULL))
			configlen++;
		
		pinconfig = kzalloc(configlen * sizeof(*pinconfig), GFP_KERNEL);	
			
		if (!of_property_read_u32(np, "rockchip,pull", &val)) {
			enum pin_config_param pull = PIN_CONFIG_END;
			if (val == 1)
				pull = PIN_CONFIG_BIAS_PULL_UP;
			else if (val == 2)
				pull = PIN_CONFIG_BIAS_PULL_DOWN;
			
			pinconfig[j++] = pinconf_to_config_packed(pull, 0);
		}

		if (!of_property_read_u32(np, "rockchip,voltage", &val)) {
			pinconfig[j++] =
				pinconf_to_config_packed(PIN_CONFIG_POWER_SOURCE,
							 val);
		}

		if (!of_property_read_u32(np, "rockchip,drive", &val)) {
			pinconfig[j++] =
				pinconf_to_config_packed(PIN_CONFIG_DRIVE_STRENGTH,
							 val);
		}

		if (!of_property_read_u32(np, "rockchip,tristate", &val)) {
			pinconfig[j++] =
				pinconf_to_config_packed(PIN_CONFIG_BIAS_HIGH_IMPEDANCE,
							 val);
		}

		grp->data[i].configs = pinconfig;
		grp->data[i].nconfigs = configlen;

        }

        return 0;
}


static int rockchip_pinctrl_parse_functions(struct device_node *np,
						struct rockchip_pinctrl *info,
						u32 index)
{
	struct device_node *child;
	struct rockchip_pmx_func *func;
	struct rockchip_pin_group *grp;
	int ret;
	static u32 grp_index;
	u32 i = 0;

	DBG_PINCTRL("%s:parse function(%d): %s\n", __func__, index, np->name);

	func = &info->functions[index];

	/* Initialise function */
	func->name = np->name;
	func->ngroups = of_get_child_count(np);
	if (func->ngroups <= 0)
		return 0;

	func->groups = devm_kzalloc(info->dev,
			func->ngroups * sizeof(char *), GFP_KERNEL);
	if (!func->groups)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		func->groups[i] = child->name;
		grp = &info->groups[grp_index++];	
		grp->func_name = np->name;
		ret = rockchip_pinctrl_parse_groups(child, grp, info, i++);
		if (ret)
			return ret;	
		
		//DBG_PINCTRL("%s:grp->func_name(%d): %s\n", __func__, grp_index, grp->func_name);
	}

	return 0;
}

static int rockchip_pinctrl_parse_dt(struct platform_device *pdev,
					      struct rockchip_pinctrl *info)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int ret;
	int i;

	rockchip_pinctrl_child_count(info, np);

	dev_dbg(&pdev->dev, "nfunctions = %d\n", info->nfunctions);
	dev_dbg(&pdev->dev, "ngroups = %d\n", info->ngroups);

	info->functions = devm_kzalloc(dev, info->nfunctions *
					      sizeof(struct rockchip_pmx_func),
					      GFP_KERNEL);
	if (!info->functions) {
		dev_err(dev, "failed to allocate memory for function list\n");
		return -EINVAL;
	}

	info->groups = devm_kzalloc(dev, info->ngroups *
					    sizeof(struct rockchip_pin_group),
					    GFP_KERNEL);
	if (!info->groups) {
		dev_err(dev, "failed allocate memory for ping group list\n");
		return -EINVAL;
	}

	i = 0;

	for_each_child_of_node(np, child) {
		if (of_match_node(rockchip_bank_match, child))
			continue;

		ret = rockchip_pinctrl_parse_functions(child, info, i++);
		if (ret) {
			dev_err(&pdev->dev, "failed to parse function\n");
			return ret;
		}
	}

	return 0;
}

static int rockchip_pinctrl_register(struct platform_device *pdev,
					struct rockchip_pinctrl *info)
{
	struct pinctrl_desc *ctrldesc = &info->pctl;
	struct pinctrl_pin_desc *pindesc, *pdesc;
	struct rockchip_pin_bank *pin_bank;
	int pin, bank, ret;
	int k;

	ctrldesc->name = "rockchip-pinctrl";
	ctrldesc->owner = THIS_MODULE;
	ctrldesc->pctlops = &rockchip_pctrl_ops;
	ctrldesc->pmxops = &rockchip_pmx_ops;
	ctrldesc->confops = &rockchip_pinconf_ops;

	pindesc = devm_kzalloc(&pdev->dev, sizeof(*pindesc) *
			info->ctrl->nr_pins, GFP_KERNEL);
	if (!pindesc) {
		dev_err(&pdev->dev, "mem alloc for pin descriptors failed\n");
		return -ENOMEM;
	}
	ctrldesc->pins = pindesc;
	ctrldesc->npins = info->ctrl->nr_pins;

	pdesc = pindesc;
	for (bank = 0 , k = 0; bank < info->ctrl->nr_banks; bank++) {
		pin_bank = &info->ctrl->pin_banks[bank];
		for (pin = 0; pin < pin_bank->nr_pins; pin++, k++) {
			pdesc->number = k;
			pdesc->name = kasprintf(GFP_KERNEL, "%s-%d",
						pin_bank->name, pin);
			pdesc++;
		}
	}

	info->pctl_dev = pinctrl_register(ctrldesc, &pdev->dev, info);
	if (!info->pctl_dev) {
		dev_err(&pdev->dev, "could not register pinctrl driver\n");
		return -EINVAL;
	}

	for (bank = 0; bank < info->ctrl->nr_banks; ++bank) {
		pin_bank = &info->ctrl->pin_banks[bank];
		pin_bank->grange.name = pin_bank->name;
		pin_bank->grange.id = bank;
		pin_bank->grange.pin_base = pin_bank->pin_base;
		pin_bank->grange.base = pin_bank->gpio_chip.base;
		pin_bank->grange.npins = pin_bank->gpio_chip.ngpio;
		pin_bank->grange.gc = &pin_bank->gpio_chip;
		pinctrl_add_gpio_range(info->pctl_dev, &pin_bank->grange);
	}

	ret = rockchip_pinctrl_parse_dt(pdev, info);
	if (ret) {
		pinctrl_unregister(info->pctl_dev);
		return ret;
	}

	return 0;
}

/*
 * GPIO handling
 */

static int rockchip_gpio_request(struct gpio_chip *chip, unsigned offset)
{	
	struct rockchip_pin_bank *bank = gc_to_pin_bank(chip);		
	struct rockchip_pinctrl *info = bank->drvdata;
	
	if(bank->bank_num == 15)
	{
		printk("%s:error bank num %d is out of range\n",__func__, bank->bank_num);
		return -1;
	}

		
	DBG_PINCTRL("%s:GPIO%d-%d\n", __func__, bank->bank_num, offset);
	return pinctrl_request_gpio(chip->base + offset);
}

static void rockchip_gpio_free(struct gpio_chip *chip, unsigned offset)
{		
	struct rockchip_pin_bank *bank = gc_to_pin_bank(chip);	
	struct rockchip_pinctrl *info = bank->drvdata;
	if(bank->bank_num == 15)
	{
		printk("%s:error bank num %d is out of range\n",__func__, bank->bank_num);
		return ;
	}
	DBG_PINCTRL("%s:GPIO%d-%d\n", __func__, bank->bank_num, offset);
	pinctrl_free_gpio(chip->base + offset);
}

static void rockchip_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct rockchip_pin_bank *bank = gc_to_pin_bank(gc);	
	struct rockchip_pinctrl *info = bank->drvdata;
	void __iomem *reg = bank->reg_base + GPIO_SWPORT_DR;
	unsigned long flags;
	u32 data;

	spin_lock_irqsave(&bank->slock, flags);

	data = readl(reg);
	data &= ~BIT(offset);
	if (value)
		data |= BIT(offset);
	writel(data, reg);

	spin_unlock_irqrestore(&bank->slock, flags);
	
	DBG_PINCTRL("%s:GPIO%d-%d level = %d\n", __func__, bank->bank_num, offset, value);
}

/*
 * Returns the level of the pin for input direction and setting of the DR
 * register for output gpios.
 */
static int rockchip_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct rockchip_pin_bank *bank = gc_to_pin_bank(gc);	
	struct rockchip_pinctrl *info = bank->drvdata;
	u32 data;

	data = readl(bank->reg_base + GPIO_EXT_PORT);
	data >>= offset;
	data &= 1;
	
	DBG_PINCTRL("%s:GPIO%d-%d level = %d\n", __func__, bank->bank_num, offset, data);
	return data;
}

/*
 * gpiolib gpio_direction_input callback function. The setting of the pin
 * mux function as 'gpio input' will be handled by the pinctrl susbsystem
 * interface.
 */
static int rockchip_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct rockchip_pin_bank *bank = gc_to_pin_bank(gc);
	struct rockchip_pinctrl *info = bank->drvdata;
	
	if(bank->bank_num == 15)
	{
		printk("%s:error bank num %d is out of range\n",__func__, bank->bank_num);
		return -1;
	}

	DBG_PINCTRL("%s:GPIO%d-%d\n", __func__, bank->bank_num, offset);
	return pinctrl_gpio_direction_input(gc->base + offset);
}

/*
 * gpiolib gpio_direction_output callback function. The setting of the pin
 * mux function as 'gpio output' will be handled by the pinctrl susbsystem
 * interface.
 */
static int rockchip_gpio_direction_output(struct gpio_chip *gc,
					  unsigned offset, int value)
{
	struct rockchip_pin_bank *bank = gc_to_pin_bank(gc);	
	struct rockchip_pinctrl *info = bank->drvdata;

	if(bank->bank_num == 15)
	{
		printk("%s:error bank num %d is out of range\n",__func__, bank->bank_num);
		return -1;
	}

	rockchip_gpio_set(gc, offset, value);
	
	DBG_PINCTRL("%s:set GPIO%d-%d level %d\n", __func__, bank->bank_num, offset, value);	
	return pinctrl_gpio_direction_output(gc->base + offset);
}

/*
 * gpiolib gpio_to_irq callback function. Creates a mapping between a GPIO pin
 * and a virtual IRQ, if not already present.
 */
static int rockchip_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct rockchip_pin_bank *bank = gc_to_pin_bank(gc);
	struct rockchip_pinctrl *info = bank->drvdata;
	
	unsigned int virq;

	if(bank->bank_num == 15)
	{
		printk("%s:error bank num %d is out of range\n",__func__, bank->bank_num);
		return -1;
	}
	
	if (!bank->domain)
		return -ENXIO;

	virq = irq_create_mapping(bank->domain, offset);

	DBG_PINCTRL("%s:virq=%d, GPIO%d-%d\n", __func__, virq, bank->bank_num, offset);

	return (virq) ? : -ENXIO;
}

static const struct gpio_chip rockchip_gpiolib_chip = {
	.request = rockchip_gpio_request,
	.free = rockchip_gpio_free,
	.set = rockchip_gpio_set,
	.get = rockchip_gpio_get,
	.direction_input = rockchip_gpio_direction_input,
	.direction_output = rockchip_gpio_direction_output,
	.to_irq = rockchip_gpio_to_irq,
	.owner = THIS_MODULE,
};

/*
 * Interrupt handling
 */

static void rockchip_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_get_chip(irq);
	struct rockchip_pin_bank *bank = irq_get_handler_data(irq);	
	struct rockchip_pinctrl *info = bank->drvdata;
	
	u32 polarity = 0, data = 0;
	u32 pend;
	bool edge_changed = false;

	DBG_PINCTRL("%s:got irq for bank %s\n", __func__, bank->name);

	chained_irq_enter(chip, desc);

	pend = readl_relaxed(bank->reg_base + GPIO_INT_STATUS);

	if (bank->toggle_edge_mode) {
		polarity = readl_relaxed(bank->reg_base +
					 GPIO_INT_POLARITY);
		data = readl_relaxed(bank->reg_base + GPIO_EXT_PORT);
	}

	while (pend) {
		unsigned int virq;

		irq = __ffs(pend);
		pend &= ~BIT(irq);
		virq = irq_linear_revmap(bank->domain, irq);

		if (!virq) {
			dev_err(bank->drvdata->dev, "unmapped irq %d\n", irq);
			continue;
		}

		dev_dbg(bank->drvdata->dev, "handling irq %d\n", irq);

		/*
		 * Triggering IRQ on both rising and falling edge
		 * needs manual intervention.
		 */
		if (bank->toggle_edge_mode & BIT(irq)) {
			if (data & BIT(irq))
				polarity &= ~BIT(irq);
			else
				polarity |= BIT(irq);

			edge_changed = true;
		}

		generic_handle_irq(virq);

		
		DBG_PINCTRL("%s:irq=%d,%s\n",__func__, irq, bank->name);
	}

	if (bank->toggle_edge_mode && edge_changed) {
		/* Interrupt params should only be set with ints disabled */
		data = readl_relaxed(bank->reg_base + GPIO_INTEN);
		writel_relaxed(0, bank->reg_base + GPIO_INTEN);
		writel(polarity, bank->reg_base + GPIO_INT_POLARITY);
		writel(data, bank->reg_base + GPIO_INTEN);
	}

	chained_irq_exit(chip, desc);
}

static int rockchip_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct rockchip_pin_bank *bank = irq_data_get_irq_chip_data(d);	
	struct rockchip_pinctrl *info = bank->drvdata;
	u32 mask = BIT(d->hwirq);
	u32 polarity;
	u32 level;
	u32 data;	
	u32 mux;
	unsigned long flags;
	
	DBG_PINCTRL("%s:type=%d,irq=%d,hwirq=%d\n",__func__,type, d->irq, (int)d->hwirq);
	
	/* make sure the pin is configured as gpio input */
	mux = (bank->bank_num << 12) | (((d->hwirq / 8) + 0x0A) << 8) | ((d->hwirq % 8)<< 4) | RK_FUNC_GPIO;
	rockchip_set_mux(bank, d->hwirq, mux);

	data = readl_relaxed(bank->reg_base + GPIO_SWPORT_DDR);
	data &= ~mask;
	writel_relaxed(data, bank->reg_base + GPIO_SWPORT_DDR);

	if (type & IRQ_TYPE_EDGE_BOTH)
		__irq_set_handler_locked(d->irq, handle_edge_irq);
	else
		__irq_set_handler_locked(d->irq, handle_level_irq);
	
	spin_lock_irqsave(&bank->slock, flags);
	
	level = readl_relaxed(bank->reg_base + GPIO_INTTYPE_LEVEL);
	polarity = readl_relaxed(bank->reg_base + GPIO_INT_POLARITY);

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		bank->toggle_edge_mode |= mask;
		level |= mask;

		/*
		 * Determine gpio state. If 1 next interrupt should be falling
		 * otherwise rising.
		 */
		data = readl(bank->reg_base + GPIO_EXT_PORT);
		if (data & mask)
			polarity &= ~mask;
		else
			polarity |= mask;
		break;
	case IRQ_TYPE_EDGE_RISING:
		bank->toggle_edge_mode &= ~mask;
		level |= mask;
		polarity |= mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		bank->toggle_edge_mode &= ~mask;
		level |= mask;
		polarity &= ~mask;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		bank->toggle_edge_mode &= ~mask;
		level &= ~mask;
		polarity |= mask;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		bank->toggle_edge_mode &= ~mask;
		level &= ~mask;
		polarity &= ~mask;
		break;
	default:
		//spin_unlock_irqrestore(&bank->slock, flags);
		return -EINVAL;
	}

	writel_relaxed(level, bank->reg_base + GPIO_INTTYPE_LEVEL);
	writel_relaxed(polarity, bank->reg_base + GPIO_INT_POLARITY);
	
	spin_unlock_irqrestore(&bank->slock, flags);
	
	DBG_PINCTRL("%s:type=%d,irq=%d,hwirq=%d,ok\n",__func__,type, d->irq, (int)d->hwirq);
	return 0;
}


static inline void rockchip_gpio_bit_op(void __iomem *reg_base, unsigned int offset, u32 bit, unsigned char flag)
{
	u32 val = __raw_readl(reg_base + offset);
	if (flag)
		val |= BIT(bit);
	else
		val &= ~BIT(bit);

	
	__raw_writel(val, reg_base + offset);
}

static inline unsigned gpio_to_bit(struct rockchip_pin_bank *bank, unsigned gpio)
{
	while (gpio >= (bank->pin_base + bank->nr_pins))
		bank++;

	return gpio - bank->pin_base;
}

static inline unsigned offset_to_bit(unsigned offset)
{
	return 1u << offset;
}

static void GPIOEnableIntr(void __iomem *reg_base, unsigned int bit)
{
	rockchip_gpio_bit_op(reg_base, GPIO_INTEN, bit, 1);
}

static void GPIODisableIntr(void __iomem *reg_base, unsigned int bit)
{
	rockchip_gpio_bit_op(reg_base, GPIO_INTEN, bit, 0);
}

static void GPIOAckIntr(void __iomem *reg_base, unsigned int bit)
{
	rockchip_gpio_bit_op(reg_base, GPIO_PORTS_EOI, bit, 1);
}

static int rockchip_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct rockchip_pin_bank *bank = irq_data_get_irq_chip_data(d);	
	struct rockchip_pinctrl *info = bank->drvdata;
	
	//u32 bit = gpio_to_bit(bank, d->irq);	
	u32 bit = d->hwirq;
	unsigned long flags;

	spin_lock_irqsave(&bank->slock, flags);
	
	if (on)
	{
		bank->suspend_wakeup |= BIT(bit);
	}
	else
	{
		bank->suspend_wakeup &= ~BIT(bit);			
	}
	spin_unlock_irqrestore(&bank->slock, flags);
	
	DBG_PINCTRL("%s:irq=%d,hwirq=%d,bank->reg_base=0x%x,bit=%d\n",__func__,d->irq, (int)d->hwirq, (int)bank->reg_base,bit);
	return 0;
}

static void rockchip_gpio_irq_unmask(struct irq_data *d)
{
	struct rockchip_pin_bank *bank = irq_data_get_irq_chip_data(d);
	struct rockchip_pinctrl *info = bank->drvdata;
	//u32 bit = gpio_to_bit(bank, d->irq);	
	u32 bit = d->hwirq;
	unsigned long flags;

	spin_lock_irqsave(&bank->slock, flags);
	GPIOEnableIntr(bank->reg_base, bit);
	spin_unlock_irqrestore(&bank->slock, flags);

	DBG_PINCTRL("%s:irq=%d,hwirq=%d,bank->reg_base=0x%x,bit=%d\n",__func__,d->irq, (int)d->hwirq, (int)bank->reg_base,bit);
}

static void rockchip_gpio_irq_mask(struct irq_data *d)
{
	struct rockchip_pin_bank *bank = irq_data_get_irq_chip_data(d);	
	struct rockchip_pinctrl *info = bank->drvdata;
	//u32 bit = gpio_to_bit(bank, d->irq);
	u32 bit = d->hwirq;
	unsigned long flags;

	spin_lock_irqsave(&bank->slock, flags);
	GPIODisableIntr(bank->reg_base, bit);
	spin_unlock_irqrestore(&bank->slock, flags);
	
	DBG_PINCTRL("%s:irq=%d,hwirq=%d,bank->reg_base=0x%x,bit=%d\n",__func__,d->irq, (int)d->hwirq, (int)bank->reg_base,bit);
}

static void rockchip_gpio_irq_ack(struct irq_data *d)
{
	struct rockchip_pin_bank *bank = irq_data_get_irq_chip_data(d);	
	struct rockchip_pinctrl *info = bank->drvdata;
	//u32 bit = gpio_to_bit(bank, d->irq);
	u32 bit = d->hwirq;

	GPIOAckIntr(bank->reg_base, bit);
	
	DBG_PINCTRL("%s:irq=%d,hwirq=%d,bank->reg_base=0x%x,bit=%d\n",__func__,d->irq, (int)d->hwirq, (int)bank->reg_base,bit);
}


static struct irq_chip rockchip_gpio_irq_chip = {
	.name		= "GPIO",
	.irq_ack 	= rockchip_gpio_irq_ack,
	.irq_disable	= rockchip_gpio_irq_mask,
	.irq_mask	= rockchip_gpio_irq_mask,
	.irq_unmask	= rockchip_gpio_irq_unmask,
	.irq_set_type	= rockchip_gpio_irq_set_type,
	.irq_set_wake	= rockchip_gpio_irq_set_wake,
};


static int rockchip_gpio_irq_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hwirq)
{
	struct rockchip_pin_bank *bank = d->host_data;		
	struct rockchip_pinctrl *info = bank->drvdata;
	struct irq_data *irq_data = irq_get_irq_data(irq);
	
	if (!bank)
	{
		printk("%s:bank=0x%p,irq=%d\n",__func__,bank, irq);
		return -EINVAL;
	}
	
	irq_set_chip_and_handler(irq, &rockchip_gpio_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, bank);
	set_irq_flags(irq, IRQF_VALID);
	
	irq_data->hwirq = hwirq;
	irq_data->irq = irq;
		
	DBG_PINCTRL("%s:irq=%d\n",__func__,irq);
	return 0;
}

const struct irq_domain_ops rockchip_gpio_irq_ops = {
	.map = rockchip_gpio_irq_map,
	.xlate = irq_domain_xlate_twocell,
};


static int rockchip_interrupts_register(struct platform_device *pdev,
						struct rockchip_pinctrl *info)
{
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct rockchip_pin_bank *bank = ctrl->pin_banks;
	//unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	int i;

	for (i = 0; i < ctrl->nr_banks; ++i, ++bank) {
		if (!bank->valid) {
			dev_warn(&pdev->dev, "bank %s is not valid\n",
				 bank->name);
			continue;
		}
		
		__raw_writel(0, bank->reg_base + GPIO_INTEN);
		
		bank->drvdata = info;
		bank->domain = irq_domain_add_linear(bank->of_node, 32,
				&rockchip_gpio_irq_ops, bank);
		if (!bank->domain) {
			dev_warn(&pdev->dev, "could not initialize irq domain for bank %s\n",
				 bank->name);
			continue;
		}

		
		DBG_PINCTRL("%s:bank=%d\n",__func__,i);

		irq_set_handler_data(bank->irq, bank);
		irq_set_chained_handler(bank->irq, rockchip_irq_demux);
	}


	return 0;
}


static int rockchip_gpiolib_register(struct platform_device *pdev,
						struct rockchip_pinctrl *info)
{
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct rockchip_pin_bank *bank = ctrl->pin_banks;
	struct gpio_chip *gc;
	int ret;
	int i;

	for (i = 0; i < ctrl->nr_banks; ++i, ++bank) {
		if (!bank->valid) {
			dev_warn(&pdev->dev, "bank %s is not valid\n",
				 bank->name);
			continue;
		}

		bank->gpio_chip = rockchip_gpiolib_chip;

		gc = &bank->gpio_chip;
		gc->base = bank->pin_base;
		gc->ngpio = bank->nr_pins;
		gc->dev = &pdev->dev;
		gc->of_node = bank->of_node;
		gc->label = bank->name;

		ret = gpiochip_add(gc);
		if (ret) {
			dev_err(&pdev->dev, "failed to register gpio_chip %s, error code: %d\n",
							gc->label, ret);
			goto fail;
		}
	}

	rockchip_interrupts_register(pdev, info);

	return 0;

fail:
	for (--i, --bank; i >= 0; --i, --bank) {
		if (!bank->valid)
			continue;

		if (gpiochip_remove(&bank->gpio_chip))
			dev_err(&pdev->dev, "gpio chip %s remove failed\n",
							bank->gpio_chip.label);
	}
	return ret;
}

static int rockchip_gpiolib_unregister(struct platform_device *pdev,
						struct rockchip_pinctrl *info)
{
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct rockchip_pin_bank *bank = ctrl->pin_banks;
	int ret = 0;
	int i;

	for (i = 0; !ret && i < ctrl->nr_banks; ++i, ++bank) {
		if (!bank->valid)
			continue;

		ret = gpiochip_remove(&bank->gpio_chip);
	}

	if (ret)
		dev_err(&pdev->dev, "gpio chip remove failed\n");

	return ret;
}

static int rockchip_get_bank_data(struct rockchip_pin_bank *bank,
				  struct device *dev)
{
	struct resource res;

	if (of_address_to_resource(bank->of_node, 0, &res)) {
		dev_err(dev, "cannot find IO resource for bank %s\n", bank->name);
		return -ENOENT;
	}

	bank->reg_base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(bank->reg_base))
		return PTR_ERR(bank->reg_base);

	/*
	 * special case, where parts of the pull setting-registers are
	 * part of the PMU register space
	 */
	if (of_device_is_compatible(bank->of_node,
				    "rockchip,rk3188-gpio-bank0")) {
		bank->bank_type = RK3188_BANK0;

		if (of_address_to_resource(bank->of_node, 1, &res)) {
			dev_err(dev, "cannot find IO resource for bank %s\n", bank->name);
			return -ENOENT;
		}

		bank->reg_pull = devm_ioremap_resource(dev, &res);
		if (IS_ERR(bank->reg_pull))
			return PTR_ERR(bank->reg_pull);
	} else {
		bank->bank_type = COMMON_BANK;
	}

	bank->irq = irq_of_parse_and_map(bank->of_node, 0);

	bank->clk = of_clk_get(bank->of_node, 0);
	if (IS_ERR(bank->clk)) {
		dev_warn(dev, "failed to get clk for bank %s\n", bank->name);
		bank->clk = NULL;
	}
	clk_prepare_enable(bank->clk);
	return 0;
}

static const struct of_device_id rockchip_pinctrl_dt_match[];

/* retrieve the soc specific data */
static struct rockchip_pin_ctrl *rockchip_pinctrl_get_soc_data(
						struct rockchip_pinctrl *d,
						struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *np;
	struct rockchip_pin_ctrl *ctrl;
	struct rockchip_pin_bank *bank;
	int i;

	match = of_match_node(rockchip_pinctrl_dt_match, node);
	ctrl = (struct rockchip_pin_ctrl *)match->data;

	for_each_child_of_node(node, np) {
		if (!of_find_property(np, "gpio-controller", NULL))
			continue;

		bank = ctrl->pin_banks;
		for (i = 0; i < ctrl->nr_banks; ++i, ++bank) {
			if (!strcmp(bank->name, np->name)) {
				bank->of_node = np;

				if (!rockchip_get_bank_data(bank, &pdev->dev))
					bank->valid = true;
				break;
			}
		}
	}

	bank = ctrl->pin_banks;
	for (i = 0; i < ctrl->nr_banks; ++i, ++bank) {
		spin_lock_init(&bank->slock);
		bank->drvdata = d;
		bank->pin_base = ctrl->nr_pins;
		ctrl->nr_pins += bank->nr_pins;
	}

	return ctrl;
}

static struct rockchip_pin_ctrl rk3188_pin_ctrl;

#ifdef CONFIG_PM
static int rockchip_pinctrl_suspend(void)
{
	struct rockchip_pin_ctrl *ctrl = &rk3188_pin_ctrl;
	struct rockchip_pin_bank *bank = ctrl->pin_banks;
	int n, i = 0;
	//int value = 0;
		
	for(n=0; n<ctrl->nr_banks-1; n++)
	{
#if 0
		for(i=0; i<0x60; i=i+4)
		{
			value = readl_relaxed(bank->reg_base + i);
			printk("%s:bank_num=%d,reg[0x%x+0x%x]=0x%x,bank_name=%s\n",__func__,bank->bank_num, bank->reg_base, i, value, bank->name);
		}
#endif		
		bank->saved_wakeup = __raw_readl(bank->reg_base + GPIO_INTEN);
         __raw_writel(bank->suspend_wakeup, bank->reg_base + GPIO_INTEN);

		if (!bank->suspend_wakeup)
		clk_disable_unprepare(bank->clk);
				
		printk("%s:bank_num=%d, suspend_wakeup=0x%x\n",__func__, bank->bank_num, bank->suspend_wakeup);
		bank++;
	}

	
	return 0;
	
}

static int rockchip_pinctrl_resume(void)
{
	struct rockchip_pin_ctrl *ctrl = &rk3188_pin_ctrl;
	struct rockchip_pin_bank *bank = ctrl->pin_banks;
	int n, i = 0;
	int value = 0;	
	u32 isr;

	for(n=0; n<ctrl->nr_banks-1; n++)
	{
#if 0
		for(i=0; i<0x60; i=i+4)
		{
			value = readl_relaxed(bank->reg_base + i);
			printk("%s:bank_num=%d,reg[0x%x+0x%x]=0x%x,bank_name=%s\n",__func__,bank->bank_num, bank->reg_base, i, value, bank->name);
		}
#endif		
		if (!bank->suspend_wakeup)
		clk_prepare_enable(bank->clk);

		/* keep enable for resume irq */
		 isr = __raw_readl(bank->reg_base + GPIO_INT_STATUS);
        __raw_writel(bank->saved_wakeup | (bank->suspend_wakeup & isr), bank->reg_base + GPIO_INTEN);

		printk("%s:bank_num=%d, suspend_wakeup=0x%x\n",__func__, bank->bank_num, bank->saved_wakeup | (bank->suspend_wakeup & isr));

		bank++;
	}
              
	return 0;
}

#endif

static int rockchip_pinctrl_probe(struct platform_device *pdev)
{
	struct rockchip_pinctrl *info;
	struct device *dev = &pdev->dev;
	struct rockchip_pin_ctrl *ctrl;
	struct resource *res;
	int ret;
	
	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENODEV;
	}

	info = devm_kzalloc(dev, sizeof(struct rockchip_pinctrl), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ctrl = rockchip_pinctrl_get_soc_data(info, pdev);
	if (!ctrl) {
		dev_err(dev, "driver data not available\n");
		return -EINVAL;
	}
	info->ctrl = ctrl;
	info->dev = dev;
	
	atomic_set(&info->debug_flag, 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(info->reg_base))
		return PTR_ERR(info->reg_base);

	/* The RK3188 has its pull registers in a separate place */
	if (ctrl->type == RK3188) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		info->reg_pull = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(info->reg_base))
			return PTR_ERR(info->reg_base);
	}

	ret = rockchip_gpiolib_register(pdev, info);
	if (ret)
		return ret;

	ret = rockchip_pinctrl_register(pdev, info);
	if (ret) {
		rockchip_gpiolib_unregister(pdev, info);
		return ret;
	}

	platform_set_drvdata(pdev, info);

	printk("%s:init ok\n",__func__);
	return 0;
}

static struct rockchip_pin_bank rk2928_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
	
	PIN_BANK(15, 32, "gpio15"),//virtual bank
};

static struct rockchip_pin_ctrl rk2928_pin_ctrl = {
		.pin_banks		= rk2928_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk2928_pin_banks),
		.label			= "RK2928-GPIO",
		.type			= RK2928,
		.mux_offset		= 0xa8,
		.pull_calc_reg		= rk2928_calc_pull_reg_and_bit,
};

static struct rockchip_pin_bank rk3066a_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
	PIN_BANK(4, 32, "gpio4"),
	PIN_BANK(6, 16, "gpio6"),
	PIN_BANK(15, 32, "gpio15"),//virtual bank
};

static struct rockchip_pin_ctrl rk3066a_pin_ctrl = {
		.pin_banks		= rk3066a_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3066a_pin_banks),
		.label			= "RK3066a-GPIO",
		.type			= RK2928,
		.mux_offset		= 0xa8,
		.pull_calc_reg		= rk2928_calc_pull_reg_and_bit,
};

static struct rockchip_pin_bank rk3066b_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
	
	PIN_BANK(15, 32, "gpio15"),//virtual bank
};

static struct rockchip_pin_ctrl rk3066b_pin_ctrl = {
		.pin_banks	= rk3066b_pin_banks,
		.nr_banks	= ARRAY_SIZE(rk3066b_pin_banks),
		.label		= "RK3066b-GPIO",
		.type		= RK3066B,
		.mux_offset	= 0x60,
};

static struct rockchip_pin_bank rk3188_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
	
	PIN_BANK(15, 32, "gpio15"),//virtual bank
};

static struct rockchip_pin_ctrl rk3188_pin_ctrl = {
		.pin_banks		= rk3188_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3188_pin_banks),
		.label			= "RK3188-GPIO",
		.type			= RK3188,
		.mux_offset		= 0x60,
		.pull_calc_reg	= rk3188_calc_pull_reg_and_bit,
};


static struct rockchip_pin_bank rk3288_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
	PIN_BANK(4, 32, "gpio4"),
	PIN_BANK(5, 32, "gpio5"),
	PIN_BANK(6, 32, "gpio6"),
	PIN_BANK(7, 32, "gpio7"),
	PIN_BANK(8, 32, "gpio8"),

	
	PIN_BANK(15, 32, "gpio15"),//virtual bank
};

static struct rockchip_pin_ctrl rk3288_pin_ctrl = {
		.pin_banks		= rk3288_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3288_pin_banks),
		.label			= "RK3288-GPIO",
		.type			= RK3288,
		.mux_offset		= 0x60,
};


static const struct of_device_id rockchip_pinctrl_dt_match[] = {
	{ .compatible = "rockchip,rk2928-pinctrl",
		.data = (void *)&rk2928_pin_ctrl },
	{ .compatible = "rockchip,rk3066a-pinctrl",
		.data = (void *)&rk3066a_pin_ctrl },
	{ .compatible = "rockchip,rk3066b-pinctrl",
		.data = (void *)&rk3066b_pin_ctrl },
	{ .compatible = "rockchip,rk3188-pinctrl",
		.data = (void *)&rk3188_pin_ctrl },
	{ .compatible = "rockchip,rk3288-pinctrl",
		.data = (void *)&rk3288_pin_ctrl },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_pinctrl_dt_match);

static struct platform_driver rockchip_pinctrl_driver = {
	.probe		= rockchip_pinctrl_probe,
	.driver = {
		.name	= "rockchip-pinctrl",
		.owner	= THIS_MODULE,
		.of_match_table = rockchip_pinctrl_dt_match,
	},
};

#ifdef CONFIG_PM
static struct syscore_ops rockchip_gpio_syscore_ops = {
        .suspend        = rockchip_pinctrl_suspend,
        .resume         = rockchip_pinctrl_resume,
};
#endif

static int __init rockchip_pinctrl_drv_register(void)
{
#ifdef CONFIG_PM
	register_syscore_ops(&rockchip_gpio_syscore_ops);
#endif
	return platform_driver_register(&rockchip_pinctrl_driver);
}
postcore_initcall(rockchip_pinctrl_drv_register);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("Rockchip pinctrl driver");
MODULE_LICENSE("GPL v2");
