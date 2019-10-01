// SPDX-License-Identifier: GPL-2.0-only
/*
 * at91 pinctrl driver based on at91 pinmux core
 *
 * Copyright (C) 2011-2012 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
/* Since we request GPIOs from ourself */
#include <linux/pinctrl/consumer.h>

#include "pinctrl-at91.h"
#include "core.h"

#define MAX_GPIO_BANKS		5
#define MAX_NB_GPIO_PER_BANK	32

struct at91_pinctrl_mux_ops;

struct at91_gpio_chip {
	struct gpio_chip	chip;
	struct pinctrl_gpio_range range;
	struct at91_gpio_chip	*next;		/* Bank sharing same clock */
	int			pioc_hwirq;	/* PIO bank interrupt identifier on AIC */
	int			pioc_virq;	/* PIO bank Linux virtual interrupt */
	int			pioc_idx;	/* PIO bank index */
	void __iomem		*regbase;	/* PIO bank virtual address */
	struct clk		*clock;		/* associated clock */
	struct at91_pinctrl_mux_ops *ops;	/* ops */
};

static struct at91_gpio_chip *gpio_chips[MAX_GPIO_BANKS];

static int gpio_banks;

#define PULL_UP		(1 << 0)
#define MULTI_DRIVE	(1 << 1)
#define DEGLITCH	(1 << 2)
#define PULL_DOWN	(1 << 3)
#define DIS_SCHMIT	(1 << 4)
#define DRIVE_STRENGTH_SHIFT	5
#define DRIVE_STRENGTH_MASK		0x3
#define DRIVE_STRENGTH   (DRIVE_STRENGTH_MASK << DRIVE_STRENGTH_SHIFT)
#define OUTPUT		(1 << 7)
#define OUTPUT_VAL_SHIFT	8
#define OUTPUT_VAL	(0x1 << OUTPUT_VAL_SHIFT)
#define SLEWRATE_SHIFT	9
#define SLEWRATE_MASK	0x1
#define SLEWRATE	(SLEWRATE_MASK << SLEWRATE_SHIFT)
#define DEBOUNCE	(1 << 16)
#define DEBOUNCE_VAL_SHIFT	17
#define DEBOUNCE_VAL	(0x3fff << DEBOUNCE_VAL_SHIFT)

/**
 * These defines will translated the dt binding settings to our internal
 * settings. They are not necessarily the same value as the register setting.
 * The actual drive strength current of low, medium and high must be looked up
 * from the corresponding device datasheet. This value is different for pins
 * that are even in the same banks. It is also dependent on VCC.
 * DRIVE_STRENGTH_DEFAULT is just a placeholder to avoid changing the drive
 * strength when there is no dt config for it.
 */
enum drive_strength_bit {
	DRIVE_STRENGTH_BIT_DEF,
	DRIVE_STRENGTH_BIT_LOW,
	DRIVE_STRENGTH_BIT_MED,
	DRIVE_STRENGTH_BIT_HI,
};

#define DRIVE_STRENGTH_BIT_MSK(name)	(DRIVE_STRENGTH_BIT_##name << \
					 DRIVE_STRENGTH_SHIFT)

enum slewrate_bit {
	SLEWRATE_BIT_DIS,
	SLEWRATE_BIT_ENA,
};

#define SLEWRATE_BIT_MSK(name)		(SLEWRATE_BIT_##name << SLEWRATE_SHIFT)

/**
 * struct at91_pmx_func - describes AT91 pinmux functions
 * @name: the name of this specific function
 * @groups: corresponding pin groups
 * @ngroups: the number of groups
 */
struct at91_pmx_func {
	const char	*name;
	const char	**groups;
	unsigned	ngroups;
};

enum at91_mux {
	AT91_MUX_GPIO = 0,
	AT91_MUX_PERIPH_A = 1,
	AT91_MUX_PERIPH_B = 2,
	AT91_MUX_PERIPH_C = 3,
	AT91_MUX_PERIPH_D = 4,
};

/**
 * struct at91_pmx_pin - describes an At91 pin mux
 * @bank: the bank of the pin
 * @pin: the pin number in the @bank
 * @mux: the mux mode : gpio or periph_x of the pin i.e. alternate function.
 * @conf: the configuration of the pin: PULL_UP, MULTIDRIVE etc...
 */
struct at91_pmx_pin {
	uint32_t	bank;
	uint32_t	pin;
	enum at91_mux	mux;
	unsigned long	conf;
};

/**
 * struct at91_pin_group - describes an At91 pin group
 * @name: the name of this specific pin group
 * @pins_conf: the mux mode for each pin in this group. The size of this
 *	array is the same as pins.
 * @pins: an array of discrete physical pins used in this group, taken
 *	from the driver-local pin enumeration space
 * @npins: the number of pins in this group array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 */
struct at91_pin_group {
	const char		*name;
	struct at91_pmx_pin	*pins_conf;
	unsigned int		*pins;
	unsigned		npins;
};

/**
 * struct at91_pinctrl_mux_ops - describes an AT91 mux ops group
 * on new IP with support for periph C and D the way to mux in
 * periph A and B has changed
 * So provide the right call back
 * if not present means the IP does not support it
 * @get_periph: return the periph mode configured
 * @mux_A_periph: mux as periph A
 * @mux_B_periph: mux as periph B
 * @mux_C_periph: mux as periph C
 * @mux_D_periph: mux as periph D
 * @get_deglitch: get deglitch status
 * @set_deglitch: enable/disable deglitch
 * @get_debounce: get debounce status
 * @set_debounce: enable/disable debounce
 * @get_pulldown: get pulldown status
 * @set_pulldown: enable/disable pulldown
 * @get_schmitt_trig: get schmitt trigger status
 * @disable_schmitt_trig: disable schmitt trigger
 * @irq_type: return irq type
 */
struct at91_pinctrl_mux_ops {
	enum at91_mux (*get_periph)(void __iomem *pio, unsigned mask);
	void (*mux_A_periph)(void __iomem *pio, unsigned mask);
	void (*mux_B_periph)(void __iomem *pio, unsigned mask);
	void (*mux_C_periph)(void __iomem *pio, unsigned mask);
	void (*mux_D_periph)(void __iomem *pio, unsigned mask);
	bool (*get_deglitch)(void __iomem *pio, unsigned pin);
	void (*set_deglitch)(void __iomem *pio, unsigned mask, bool is_on);
	bool (*get_debounce)(void __iomem *pio, unsigned pin, u32 *div);
	void (*set_debounce)(void __iomem *pio, unsigned mask, bool is_on, u32 div);
	bool (*get_pulldown)(void __iomem *pio, unsigned pin);
	void (*set_pulldown)(void __iomem *pio, unsigned mask, bool is_on);
	bool (*get_schmitt_trig)(void __iomem *pio, unsigned pin);
	void (*disable_schmitt_trig)(void __iomem *pio, unsigned mask);
	unsigned (*get_drivestrength)(void __iomem *pio, unsigned pin);
	void (*set_drivestrength)(void __iomem *pio, unsigned pin,
					u32 strength);
	unsigned (*get_slewrate)(void __iomem *pio, unsigned pin);
	void (*set_slewrate)(void __iomem *pio, unsigned pin, u32 slewrate);
	/* irq */
	int (*irq_type)(struct irq_data *d, unsigned type);
};

static int gpio_irq_type(struct irq_data *d, unsigned type);
static int alt_gpio_irq_type(struct irq_data *d, unsigned type);

struct at91_pinctrl {
	struct device		*dev;
	struct pinctrl_dev	*pctl;

	int			nactive_banks;

	uint32_t		*mux_mask;
	int			nmux;

	struct at91_pmx_func	*functions;
	int			nfunctions;

	struct at91_pin_group	*groups;
	int			ngroups;

	struct at91_pinctrl_mux_ops *ops;
};

static inline const struct at91_pin_group *at91_pinctrl_find_group_by_name(
				const struct at91_pinctrl *info,
				const char *name)
{
	const struct at91_pin_group *grp = NULL;
	int i;

	for (i = 0; i < info->ngroups; i++) {
		if (strcmp(info->groups[i].name, name))
			continue;

		grp = &info->groups[i];
		dev_dbg(info->dev, "%s: %d 0:%d\n", name, grp->npins, grp->pins[0]);
		break;
	}

	return grp;
}

static int at91_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct at91_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->ngroups;
}

static const char *at91_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	struct at91_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->groups[selector].name;
}

static int at91_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			       const unsigned **pins,
			       unsigned *npins)
{
	struct at91_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	if (selector >= info->ngroups)
		return -EINVAL;

	*pins = info->groups[selector].pins;
	*npins = info->groups[selector].npins;

	return 0;
}

static void at91_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	seq_printf(s, "%s", dev_name(pctldev->dev));
}

static int at91_dt_node_to_map(struct pinctrl_dev *pctldev,
			struct device_node *np,
			struct pinctrl_map **map, unsigned *num_maps)
{
	struct at91_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const struct at91_pin_group *grp;
	struct pinctrl_map *new_map;
	struct device_node *parent;
	int map_num = 1;
	int i;

	/*
	 * first find the group of this node and check if we need to create
	 * config maps for pins
	 */
	grp = at91_pinctrl_find_group_by_name(info, np->name);
	if (!grp) {
		dev_err(info->dev, "unable to find group for node %pOFn\n",
			np);
		return -EINVAL;
	}

	map_num += grp->npins;
	new_map = devm_kcalloc(pctldev->dev, map_num, sizeof(*new_map),
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
		new_map[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
		new_map[i].data.configs.group_or_pin =
				pin_get_name(pctldev, grp->pins[i]);
		new_map[i].data.configs.configs = &grp->pins_conf[i].conf;
		new_map[i].data.configs.num_configs = 1;
	}

	dev_dbg(pctldev->dev, "maps: function %s group %s num %d\n",
		(*map)->data.mux.function, (*map)->data.mux.group, map_num);

	return 0;
}

static void at91_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, unsigned num_maps)
{
}

static const struct pinctrl_ops at91_pctrl_ops = {
	.get_groups_count	= at91_get_groups_count,
	.get_group_name		= at91_get_group_name,
	.get_group_pins		= at91_get_group_pins,
	.pin_dbg_show		= at91_pin_dbg_show,
	.dt_node_to_map		= at91_dt_node_to_map,
	.dt_free_map		= at91_dt_free_map,
};

static void __iomem *pin_to_controller(struct at91_pinctrl *info,
				 unsigned int bank)
{
	if (!gpio_chips[bank])
		return NULL;

	return gpio_chips[bank]->regbase;
}

static inline int pin_to_bank(unsigned pin)
{
	return pin /= MAX_NB_GPIO_PER_BANK;
}

static unsigned pin_to_mask(unsigned int pin)
{
	return 1 << pin;
}

static unsigned two_bit_pin_value_shift_amount(unsigned int pin)
{
	/* return the shift value for a pin for "two bit" per pin registers,
	 * i.e. drive strength */
	return 2*((pin >= MAX_NB_GPIO_PER_BANK/2)
			? pin - MAX_NB_GPIO_PER_BANK/2 : pin);
}

static unsigned sama5d3_get_drive_register(unsigned int pin)
{
	/* drive strength is split between two registers
	 * with two bits per pin */
	return (pin >= MAX_NB_GPIO_PER_BANK/2)
			? SAMA5D3_PIO_DRIVER2 : SAMA5D3_PIO_DRIVER1;
}

static unsigned at91sam9x5_get_drive_register(unsigned int pin)
{
	/* drive strength is split between two registers
	 * with two bits per pin */
	return (pin >= MAX_NB_GPIO_PER_BANK/2)
			? AT91SAM9X5_PIO_DRIVER2 : AT91SAM9X5_PIO_DRIVER1;
}

static void at91_mux_disable_interrupt(void __iomem *pio, unsigned mask)
{
	writel_relaxed(mask, pio + PIO_IDR);
}

static unsigned at91_mux_get_pullup(void __iomem *pio, unsigned pin)
{
	return !((readl_relaxed(pio + PIO_PUSR) >> pin) & 0x1);
}

static void at91_mux_set_pullup(void __iomem *pio, unsigned mask, bool on)
{
	if (on)
		writel_relaxed(mask, pio + PIO_PPDDR);

	writel_relaxed(mask, pio + (on ? PIO_PUER : PIO_PUDR));
}

static bool at91_mux_get_output(void __iomem *pio, unsigned int pin, bool *val)
{
	*val = (readl_relaxed(pio + PIO_ODSR) >> pin) & 0x1;
	return (readl_relaxed(pio + PIO_OSR) >> pin) & 0x1;
}

static void at91_mux_set_output(void __iomem *pio, unsigned int mask,
				bool is_on, bool val)
{
	writel_relaxed(mask, pio + (val ? PIO_SODR : PIO_CODR));
	writel_relaxed(mask, pio + (is_on ? PIO_OER : PIO_ODR));
}

static unsigned at91_mux_get_multidrive(void __iomem *pio, unsigned pin)
{
	return (readl_relaxed(pio + PIO_MDSR) >> pin) & 0x1;
}

static void at91_mux_set_multidrive(void __iomem *pio, unsigned mask, bool on)
{
	writel_relaxed(mask, pio + (on ? PIO_MDER : PIO_MDDR));
}

static void at91_mux_set_A_periph(void __iomem *pio, unsigned mask)
{
	writel_relaxed(mask, pio + PIO_ASR);
}

static void at91_mux_set_B_periph(void __iomem *pio, unsigned mask)
{
	writel_relaxed(mask, pio + PIO_BSR);
}

static void at91_mux_pio3_set_A_periph(void __iomem *pio, unsigned mask)
{

	writel_relaxed(readl_relaxed(pio + PIO_ABCDSR1) & ~mask,
						pio + PIO_ABCDSR1);
	writel_relaxed(readl_relaxed(pio + PIO_ABCDSR2) & ~mask,
						pio + PIO_ABCDSR2);
}

static void at91_mux_pio3_set_B_periph(void __iomem *pio, unsigned mask)
{
	writel_relaxed(readl_relaxed(pio + PIO_ABCDSR1) | mask,
						pio + PIO_ABCDSR1);
	writel_relaxed(readl_relaxed(pio + PIO_ABCDSR2) & ~mask,
						pio + PIO_ABCDSR2);
}

static void at91_mux_pio3_set_C_periph(void __iomem *pio, unsigned mask)
{
	writel_relaxed(readl_relaxed(pio + PIO_ABCDSR1) & ~mask, pio + PIO_ABCDSR1);
	writel_relaxed(readl_relaxed(pio + PIO_ABCDSR2) | mask, pio + PIO_ABCDSR2);
}

static void at91_mux_pio3_set_D_periph(void __iomem *pio, unsigned mask)
{
	writel_relaxed(readl_relaxed(pio + PIO_ABCDSR1) | mask, pio + PIO_ABCDSR1);
	writel_relaxed(readl_relaxed(pio + PIO_ABCDSR2) | mask, pio + PIO_ABCDSR2);
}

static enum at91_mux at91_mux_pio3_get_periph(void __iomem *pio, unsigned mask)
{
	unsigned select;

	if (readl_relaxed(pio + PIO_PSR) & mask)
		return AT91_MUX_GPIO;

	select = !!(readl_relaxed(pio + PIO_ABCDSR1) & mask);
	select |= (!!(readl_relaxed(pio + PIO_ABCDSR2) & mask) << 1);

	return select + 1;
}

static enum at91_mux at91_mux_get_periph(void __iomem *pio, unsigned mask)
{
	unsigned select;

	if (readl_relaxed(pio + PIO_PSR) & mask)
		return AT91_MUX_GPIO;

	select = readl_relaxed(pio + PIO_ABSR) & mask;

	return select + 1;
}

static bool at91_mux_get_deglitch(void __iomem *pio, unsigned pin)
{
	return (readl_relaxed(pio + PIO_IFSR) >> pin) & 0x1;
}

static void at91_mux_set_deglitch(void __iomem *pio, unsigned mask, bool is_on)
{
	writel_relaxed(mask, pio + (is_on ? PIO_IFER : PIO_IFDR));
}

static bool at91_mux_pio3_get_deglitch(void __iomem *pio, unsigned pin)
{
	if ((readl_relaxed(pio + PIO_IFSR) >> pin) & 0x1)
		return !((readl_relaxed(pio + PIO_IFSCSR) >> pin) & 0x1);

	return false;
}

static void at91_mux_pio3_set_deglitch(void __iomem *pio, unsigned mask, bool is_on)
{
	if (is_on)
		writel_relaxed(mask, pio + PIO_IFSCDR);
	at91_mux_set_deglitch(pio, mask, is_on);
}

static bool at91_mux_pio3_get_debounce(void __iomem *pio, unsigned pin, u32 *div)
{
	*div = readl_relaxed(pio + PIO_SCDR);

	return ((readl_relaxed(pio + PIO_IFSR) >> pin) & 0x1) &&
	       ((readl_relaxed(pio + PIO_IFSCSR) >> pin) & 0x1);
}

static void at91_mux_pio3_set_debounce(void __iomem *pio, unsigned mask,
				bool is_on, u32 div)
{
	if (is_on) {
		writel_relaxed(mask, pio + PIO_IFSCER);
		writel_relaxed(div & PIO_SCDR_DIV, pio + PIO_SCDR);
		writel_relaxed(mask, pio + PIO_IFER);
	} else
		writel_relaxed(mask, pio + PIO_IFSCDR);
}

static bool at91_mux_pio3_get_pulldown(void __iomem *pio, unsigned pin)
{
	return !((readl_relaxed(pio + PIO_PPDSR) >> pin) & 0x1);
}

static void at91_mux_pio3_set_pulldown(void __iomem *pio, unsigned mask, bool is_on)
{
	if (is_on)
		writel_relaxed(mask, pio + PIO_PUDR);

	writel_relaxed(mask, pio + (is_on ? PIO_PPDER : PIO_PPDDR));
}

static void at91_mux_pio3_disable_schmitt_trig(void __iomem *pio, unsigned mask)
{
	writel_relaxed(readl_relaxed(pio + PIO_SCHMITT) | mask, pio + PIO_SCHMITT);
}

static bool at91_mux_pio3_get_schmitt_trig(void __iomem *pio, unsigned pin)
{
	return (readl_relaxed(pio + PIO_SCHMITT) >> pin) & 0x1;
}

static inline u32 read_drive_strength(void __iomem *reg, unsigned pin)
{
	unsigned tmp = readl_relaxed(reg);

	tmp = tmp >> two_bit_pin_value_shift_amount(pin);

	return tmp & DRIVE_STRENGTH_MASK;
}

static unsigned at91_mux_sama5d3_get_drivestrength(void __iomem *pio,
							unsigned pin)
{
	unsigned tmp = read_drive_strength(pio +
					sama5d3_get_drive_register(pin), pin);

	/* SAMA5 strength is 1:1 with our defines,
	 * except 0 is equivalent to low per datasheet */
	if (!tmp)
		tmp = DRIVE_STRENGTH_BIT_MSK(LOW);

	return tmp;
}

static unsigned at91_mux_sam9x5_get_drivestrength(void __iomem *pio,
							unsigned pin)
{
	unsigned tmp = read_drive_strength(pio +
				at91sam9x5_get_drive_register(pin), pin);

	/* strength is inverse in SAM9x5s hardware with the pinctrl defines
	 * hardware: 0 = hi, 1 = med, 2 = low, 3 = rsvd */
	tmp = DRIVE_STRENGTH_BIT_MSK(HI) - tmp;

	return tmp;
}

static unsigned at91_mux_sam9x60_get_drivestrength(void __iomem *pio,
						   unsigned pin)
{
	unsigned tmp = readl_relaxed(pio + SAM9X60_PIO_DRIVER1);

	if (tmp & BIT(pin))
		return DRIVE_STRENGTH_BIT_HI;

	return DRIVE_STRENGTH_BIT_LOW;
}

static unsigned at91_mux_sam9x60_get_slewrate(void __iomem *pio, unsigned pin)
{
	unsigned tmp = readl_relaxed(pio + SAM9X60_PIO_SLEWR);

	if ((tmp & BIT(pin)))
		return SLEWRATE_BIT_ENA;

	return SLEWRATE_BIT_DIS;
}

static void set_drive_strength(void __iomem *reg, unsigned pin, u32 strength)
{
	unsigned tmp = readl_relaxed(reg);
	unsigned shift = two_bit_pin_value_shift_amount(pin);

	tmp &= ~(DRIVE_STRENGTH_MASK  <<  shift);
	tmp |= strength << shift;

	writel_relaxed(tmp, reg);
}

static void at91_mux_sama5d3_set_drivestrength(void __iomem *pio, unsigned pin,
						u32 setting)
{
	/* do nothing if setting is zero */
	if (!setting)
		return;

	/* strength is 1 to 1 with setting for SAMA5 */
	set_drive_strength(pio + sama5d3_get_drive_register(pin), pin, setting);
}

static void at91_mux_sam9x5_set_drivestrength(void __iomem *pio, unsigned pin,
						u32 setting)
{
	/* do nothing if setting is zero */
	if (!setting)
		return;

	/* strength is inverse on SAM9x5s with our defines
	 * 0 = hi, 1 = med, 2 = low, 3 = rsvd */
	setting = DRIVE_STRENGTH_BIT_MSK(HI) - setting;

	set_drive_strength(pio + at91sam9x5_get_drive_register(pin), pin,
				setting);
}

static void at91_mux_sam9x60_set_drivestrength(void __iomem *pio, unsigned pin,
					       u32 setting)
{
	unsigned int tmp;

	if (setting <= DRIVE_STRENGTH_BIT_DEF ||
	    setting == DRIVE_STRENGTH_BIT_MED ||
	    setting > DRIVE_STRENGTH_BIT_HI)
		return;

	tmp = readl_relaxed(pio + SAM9X60_PIO_DRIVER1);

	/* Strength is 0: low, 1: hi */
	if (setting == DRIVE_STRENGTH_BIT_LOW)
		tmp &= ~BIT(pin);
	else
		tmp |= BIT(pin);

	writel_relaxed(tmp, pio + SAM9X60_PIO_DRIVER1);
}

static void at91_mux_sam9x60_set_slewrate(void __iomem *pio, unsigned pin,
					  u32 setting)
{
	unsigned int tmp;

	if (setting < SLEWRATE_BIT_DIS || setting > SLEWRATE_BIT_ENA)
		return;

	tmp = readl_relaxed(pio + SAM9X60_PIO_SLEWR);

	if (setting == SLEWRATE_BIT_DIS)
		tmp &= ~BIT(pin);
	else
		tmp |= BIT(pin);

	writel_relaxed(tmp, pio + SAM9X60_PIO_SLEWR);
}

static struct at91_pinctrl_mux_ops at91rm9200_ops = {
	.get_periph	= at91_mux_get_periph,
	.mux_A_periph	= at91_mux_set_A_periph,
	.mux_B_periph	= at91_mux_set_B_periph,
	.get_deglitch	= at91_mux_get_deglitch,
	.set_deglitch	= at91_mux_set_deglitch,
	.irq_type	= gpio_irq_type,
};

static struct at91_pinctrl_mux_ops at91sam9x5_ops = {
	.get_periph	= at91_mux_pio3_get_periph,
	.mux_A_periph	= at91_mux_pio3_set_A_periph,
	.mux_B_periph	= at91_mux_pio3_set_B_periph,
	.mux_C_periph	= at91_mux_pio3_set_C_periph,
	.mux_D_periph	= at91_mux_pio3_set_D_periph,
	.get_deglitch	= at91_mux_pio3_get_deglitch,
	.set_deglitch	= at91_mux_pio3_set_deglitch,
	.get_debounce	= at91_mux_pio3_get_debounce,
	.set_debounce	= at91_mux_pio3_set_debounce,
	.get_pulldown	= at91_mux_pio3_get_pulldown,
	.set_pulldown	= at91_mux_pio3_set_pulldown,
	.get_schmitt_trig = at91_mux_pio3_get_schmitt_trig,
	.disable_schmitt_trig = at91_mux_pio3_disable_schmitt_trig,
	.get_drivestrength = at91_mux_sam9x5_get_drivestrength,
	.set_drivestrength = at91_mux_sam9x5_set_drivestrength,
	.irq_type	= alt_gpio_irq_type,
};

static const struct at91_pinctrl_mux_ops sam9x60_ops = {
	.get_periph	= at91_mux_pio3_get_periph,
	.mux_A_periph	= at91_mux_pio3_set_A_periph,
	.mux_B_periph	= at91_mux_pio3_set_B_periph,
	.mux_C_periph	= at91_mux_pio3_set_C_periph,
	.mux_D_periph	= at91_mux_pio3_set_D_periph,
	.get_deglitch	= at91_mux_pio3_get_deglitch,
	.set_deglitch	= at91_mux_pio3_set_deglitch,
	.get_debounce	= at91_mux_pio3_get_debounce,
	.set_debounce	= at91_mux_pio3_set_debounce,
	.get_pulldown	= at91_mux_pio3_get_pulldown,
	.set_pulldown	= at91_mux_pio3_set_pulldown,
	.get_schmitt_trig = at91_mux_pio3_get_schmitt_trig,
	.disable_schmitt_trig = at91_mux_pio3_disable_schmitt_trig,
	.get_drivestrength = at91_mux_sam9x60_get_drivestrength,
	.set_drivestrength = at91_mux_sam9x60_set_drivestrength,
	.get_slewrate   = at91_mux_sam9x60_get_slewrate,
	.set_slewrate   = at91_mux_sam9x60_set_slewrate,
	.irq_type	= alt_gpio_irq_type,

};

static struct at91_pinctrl_mux_ops sama5d3_ops = {
	.get_periph	= at91_mux_pio3_get_periph,
	.mux_A_periph	= at91_mux_pio3_set_A_periph,
	.mux_B_periph	= at91_mux_pio3_set_B_periph,
	.mux_C_periph	= at91_mux_pio3_set_C_periph,
	.mux_D_periph	= at91_mux_pio3_set_D_periph,
	.get_deglitch	= at91_mux_pio3_get_deglitch,
	.set_deglitch	= at91_mux_pio3_set_deglitch,
	.get_debounce	= at91_mux_pio3_get_debounce,
	.set_debounce	= at91_mux_pio3_set_debounce,
	.get_pulldown	= at91_mux_pio3_get_pulldown,
	.set_pulldown	= at91_mux_pio3_set_pulldown,
	.get_schmitt_trig = at91_mux_pio3_get_schmitt_trig,
	.disable_schmitt_trig = at91_mux_pio3_disable_schmitt_trig,
	.get_drivestrength = at91_mux_sama5d3_get_drivestrength,
	.set_drivestrength = at91_mux_sama5d3_set_drivestrength,
	.irq_type	= alt_gpio_irq_type,
};

static void at91_pin_dbg(const struct device *dev, const struct at91_pmx_pin *pin)
{
	if (pin->mux) {
		dev_dbg(dev, "pio%c%d configured as periph%c with conf = 0x%lx\n",
			pin->bank + 'A', pin->pin, pin->mux - 1 + 'A', pin->conf);
	} else {
		dev_dbg(dev, "pio%c%d configured as gpio with conf = 0x%lx\n",
			pin->bank + 'A', pin->pin, pin->conf);
	}
}

static int pin_check_config(struct at91_pinctrl *info, const char *name,
			    int index, const struct at91_pmx_pin *pin)
{
	int mux;

	/* check if it's a valid config */
	if (pin->bank >= gpio_banks) {
		dev_err(info->dev, "%s: pin conf %d bank_id %d >= nbanks %d\n",
			name, index, pin->bank, gpio_banks);
		return -EINVAL;
	}

	if (!gpio_chips[pin->bank]) {
		dev_err(info->dev, "%s: pin conf %d bank_id %d not enabled\n",
			name, index, pin->bank);
		return -ENXIO;
	}

	if (pin->pin >= MAX_NB_GPIO_PER_BANK) {
		dev_err(info->dev, "%s: pin conf %d pin_bank_id %d >= %d\n",
			name, index, pin->pin, MAX_NB_GPIO_PER_BANK);
		return -EINVAL;
	}

	if (!pin->mux)
		return 0;

	mux = pin->mux - 1;

	if (mux >= info->nmux) {
		dev_err(info->dev, "%s: pin conf %d mux_id %d >= nmux %d\n",
			name, index, mux, info->nmux);
		return -EINVAL;
	}

	if (!(info->mux_mask[pin->bank * info->nmux + mux] & 1 << pin->pin)) {
		dev_err(info->dev, "%s: pin conf %d mux_id %d not supported for pio%c%d\n",
			name, index, mux, pin->bank + 'A', pin->pin);
		return -EINVAL;
	}

	return 0;
}

static void at91_mux_gpio_disable(void __iomem *pio, unsigned mask)
{
	writel_relaxed(mask, pio + PIO_PDR);
}

static void at91_mux_gpio_enable(void __iomem *pio, unsigned mask, bool input)
{
	writel_relaxed(mask, pio + PIO_PER);
	writel_relaxed(mask, pio + (input ? PIO_ODR : PIO_OER));
}

static int at91_pmx_set(struct pinctrl_dev *pctldev, unsigned selector,
			unsigned group)
{
	struct at91_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const struct at91_pmx_pin *pins_conf = info->groups[group].pins_conf;
	const struct at91_pmx_pin *pin;
	uint32_t npins = info->groups[group].npins;
	int i, ret;
	unsigned mask;
	void __iomem *pio;

	dev_dbg(info->dev, "enable function %s group %s\n",
		info->functions[selector].name, info->groups[group].name);

	/* first check that all the pins of the group are valid with a valid
	 * parameter */
	for (i = 0; i < npins; i++) {
		pin = &pins_conf[i];
		ret = pin_check_config(info, info->groups[group].name, i, pin);
		if (ret)
			return ret;
	}

	for (i = 0; i < npins; i++) {
		pin = &pins_conf[i];
		at91_pin_dbg(info->dev, pin);
		pio = pin_to_controller(info, pin->bank);

		if (!pio)
			continue;

		mask = pin_to_mask(pin->pin);
		at91_mux_disable_interrupt(pio, mask);
		switch (pin->mux) {
		case AT91_MUX_GPIO:
			at91_mux_gpio_enable(pio, mask, 1);
			break;
		case AT91_MUX_PERIPH_A:
			info->ops->mux_A_periph(pio, mask);
			break;
		case AT91_MUX_PERIPH_B:
			info->ops->mux_B_periph(pio, mask);
			break;
		case AT91_MUX_PERIPH_C:
			if (!info->ops->mux_C_periph)
				return -EINVAL;
			info->ops->mux_C_periph(pio, mask);
			break;
		case AT91_MUX_PERIPH_D:
			if (!info->ops->mux_D_periph)
				return -EINVAL;
			info->ops->mux_D_periph(pio, mask);
			break;
		}
		if (pin->mux)
			at91_mux_gpio_disable(pio, mask);
	}

	return 0;
}

static int at91_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct at91_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->nfunctions;
}

static const char *at91_pmx_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned selector)
{
	struct at91_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->functions[selector].name;
}

static int at91_pmx_get_groups(struct pinctrl_dev *pctldev, unsigned selector,
			       const char * const **groups,
			       unsigned * const num_groups)
{
	struct at91_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	*groups = info->functions[selector].groups;
	*num_groups = info->functions[selector].ngroups;

	return 0;
}

static int at91_gpio_request_enable(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned offset)
{
	struct at91_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);
	struct at91_gpio_chip *at91_chip;
	struct gpio_chip *chip;
	unsigned mask;

	if (!range) {
		dev_err(npct->dev, "invalid range\n");
		return -EINVAL;
	}
	if (!range->gc) {
		dev_err(npct->dev, "missing GPIO chip in range\n");
		return -EINVAL;
	}
	chip = range->gc;
	at91_chip = gpiochip_get_data(chip);

	dev_dbg(npct->dev, "enable pin %u as GPIO\n", offset);

	mask = 1 << (offset - chip->base);

	dev_dbg(npct->dev, "enable pin %u as PIO%c%d 0x%x\n",
		offset, 'A' + range->id, offset - chip->base, mask);

	writel_relaxed(mask, at91_chip->regbase + PIO_PER);

	return 0;
}

static void at91_gpio_disable_free(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned offset)
{
	struct at91_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	dev_dbg(npct->dev, "disable pin %u as GPIO\n", offset);
	/* Set the pin to some default state, GPIO is usually default */
}

static const struct pinmux_ops at91_pmx_ops = {
	.get_functions_count	= at91_pmx_get_funcs_count,
	.get_function_name	= at91_pmx_get_func_name,
	.get_function_groups	= at91_pmx_get_groups,
	.set_mux		= at91_pmx_set,
	.gpio_request_enable	= at91_gpio_request_enable,
	.gpio_disable_free	= at91_gpio_disable_free,
};

static int at91_pinconf_get(struct pinctrl_dev *pctldev,
			     unsigned pin_id, unsigned long *config)
{
	struct at91_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	void __iomem *pio;
	unsigned pin;
	int div;
	bool out;

	*config = 0;
	dev_dbg(info->dev, "%s:%d, pin_id=%d", __func__, __LINE__, pin_id);
	pio = pin_to_controller(info, pin_to_bank(pin_id));

	if (!pio)
		return -EINVAL;

	pin = pin_id % MAX_NB_GPIO_PER_BANK;

	if (at91_mux_get_multidrive(pio, pin))
		*config |= MULTI_DRIVE;

	if (at91_mux_get_pullup(pio, pin))
		*config |= PULL_UP;

	if (info->ops->get_deglitch && info->ops->get_deglitch(pio, pin))
		*config |= DEGLITCH;
	if (info->ops->get_debounce && info->ops->get_debounce(pio, pin, &div))
		*config |= DEBOUNCE | (div << DEBOUNCE_VAL_SHIFT);
	if (info->ops->get_pulldown && info->ops->get_pulldown(pio, pin))
		*config |= PULL_DOWN;
	if (info->ops->get_schmitt_trig && info->ops->get_schmitt_trig(pio, pin))
		*config |= DIS_SCHMIT;
	if (info->ops->get_drivestrength)
		*config |= (info->ops->get_drivestrength(pio, pin)
				<< DRIVE_STRENGTH_SHIFT);
	if (info->ops->get_slewrate)
		*config |= (info->ops->get_slewrate(pio, pin) << SLEWRATE_SHIFT);
	if (at91_mux_get_output(pio, pin, &out))
		*config |= OUTPUT | (out << OUTPUT_VAL_SHIFT);

	return 0;
}

static int at91_pinconf_set(struct pinctrl_dev *pctldev,
			     unsigned pin_id, unsigned long *configs,
			     unsigned num_configs)
{
	struct at91_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	unsigned mask;
	void __iomem *pio;
	int i;
	unsigned long config;
	unsigned pin;

	for (i = 0; i < num_configs; i++) {
		config = configs[i];

		dev_dbg(info->dev,
			"%s:%d, pin_id=%d, config=0x%lx",
			__func__, __LINE__, pin_id, config);
		pio = pin_to_controller(info, pin_to_bank(pin_id));

		if (!pio)
			return -EINVAL;

		pin = pin_id % MAX_NB_GPIO_PER_BANK;
		mask = pin_to_mask(pin);

		if (config & PULL_UP && config & PULL_DOWN)
			return -EINVAL;

		at91_mux_set_output(pio, mask, config & OUTPUT,
				    (config & OUTPUT_VAL) >> OUTPUT_VAL_SHIFT);
		at91_mux_set_pullup(pio, mask, config & PULL_UP);
		at91_mux_set_multidrive(pio, mask, config & MULTI_DRIVE);
		if (info->ops->set_deglitch)
			info->ops->set_deglitch(pio, mask, config & DEGLITCH);
		if (info->ops->set_debounce)
			info->ops->set_debounce(pio, mask, config & DEBOUNCE,
				(config & DEBOUNCE_VAL) >> DEBOUNCE_VAL_SHIFT);
		if (info->ops->set_pulldown)
			info->ops->set_pulldown(pio, mask, config & PULL_DOWN);
		if (info->ops->disable_schmitt_trig && config & DIS_SCHMIT)
			info->ops->disable_schmitt_trig(pio, mask);
		if (info->ops->set_drivestrength)
			info->ops->set_drivestrength(pio, pin,
				(config & DRIVE_STRENGTH)
					>> DRIVE_STRENGTH_SHIFT);
		if (info->ops->set_slewrate)
			info->ops->set_slewrate(pio, pin,
				(config & SLEWRATE) >> SLEWRATE_SHIFT);

	} /* for each config */

	return 0;
}

#define DBG_SHOW_FLAG(flag) do {		\
	if (config & flag) {			\
		if (num_conf)			\
			seq_puts(s, "|");	\
		seq_puts(s, #flag);		\
		num_conf++;			\
	}					\
} while (0)

#define DBG_SHOW_FLAG_MASKED(mask, flag, name) do { \
	if ((config & mask) == flag) {		\
		if (num_conf)			\
			seq_puts(s, "|");	\
		seq_puts(s, #name);		\
		num_conf++;			\
	}					\
} while (0)

static void at91_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				   struct seq_file *s, unsigned pin_id)
{
	unsigned long config;
	int val, num_conf = 0;

	at91_pinconf_get(pctldev, pin_id, &config);

	DBG_SHOW_FLAG(MULTI_DRIVE);
	DBG_SHOW_FLAG(PULL_UP);
	DBG_SHOW_FLAG(PULL_DOWN);
	DBG_SHOW_FLAG(DIS_SCHMIT);
	DBG_SHOW_FLAG(DEGLITCH);
	DBG_SHOW_FLAG_MASKED(DRIVE_STRENGTH, DRIVE_STRENGTH_BIT_MSK(LOW),
			     DRIVE_STRENGTH_LOW);
	DBG_SHOW_FLAG_MASKED(DRIVE_STRENGTH, DRIVE_STRENGTH_BIT_MSK(MED),
			     DRIVE_STRENGTH_MED);
	DBG_SHOW_FLAG_MASKED(DRIVE_STRENGTH, DRIVE_STRENGTH_BIT_MSK(HI),
			     DRIVE_STRENGTH_HI);
	DBG_SHOW_FLAG(SLEWRATE);
	DBG_SHOW_FLAG(DEBOUNCE);
	if (config & DEBOUNCE) {
		val = config >> DEBOUNCE_VAL_SHIFT;
		seq_printf(s, "(%d)", val);
	}

	return;
}

static void at91_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					 struct seq_file *s, unsigned group)
{
}

static const struct pinconf_ops at91_pinconf_ops = {
	.pin_config_get			= at91_pinconf_get,
	.pin_config_set			= at91_pinconf_set,
	.pin_config_dbg_show		= at91_pinconf_dbg_show,
	.pin_config_group_dbg_show	= at91_pinconf_group_dbg_show,
};

static struct pinctrl_desc at91_pinctrl_desc = {
	.pctlops	= &at91_pctrl_ops,
	.pmxops		= &at91_pmx_ops,
	.confops	= &at91_pinconf_ops,
	.owner		= THIS_MODULE,
};

static const char *gpio_compat = "atmel,at91rm9200-gpio";

static void at91_pinctrl_child_count(struct at91_pinctrl *info,
				     struct device_node *np)
{
	struct device_node *child;

	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, gpio_compat)) {
			if (of_device_is_available(child))
				info->nactive_banks++;
		} else {
			info->nfunctions++;
			info->ngroups += of_get_child_count(child);
		}
	}
}

static int at91_pinctrl_mux_mask(struct at91_pinctrl *info,
				 struct device_node *np)
{
	int ret = 0;
	int size;
	const __be32 *list;

	list = of_get_property(np, "atmel,mux-mask", &size);
	if (!list) {
		dev_err(info->dev, "can not read the mux-mask of %d\n", size);
		return -EINVAL;
	}

	size /= sizeof(*list);
	if (!size || size % gpio_banks) {
		dev_err(info->dev, "wrong mux mask array should be by %d\n", gpio_banks);
		return -EINVAL;
	}
	info->nmux = size / gpio_banks;

	info->mux_mask = devm_kcalloc(info->dev, size, sizeof(u32),
				      GFP_KERNEL);
	if (!info->mux_mask)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "atmel,mux-mask",
					  info->mux_mask, size);
	if (ret)
		dev_err(info->dev, "can not read the mux-mask of %d\n", size);
	return ret;
}

static int at91_pinctrl_parse_groups(struct device_node *np,
				     struct at91_pin_group *grp,
				     struct at91_pinctrl *info, u32 index)
{
	struct at91_pmx_pin *pin;
	int size;
	const __be32 *list;
	int i, j;

	dev_dbg(info->dev, "group(%d): %pOFn\n", index, np);

	/* Initialise group */
	grp->name = np->name;

	/*
	 * the binding format is atmel,pins = <bank pin mux CONFIG ...>,
	 * do sanity check and calculate pins number
	 */
	list = of_get_property(np, "atmel,pins", &size);
	/* we do not check return since it's safe node passed down */
	size /= sizeof(*list);
	if (!size || size % 4) {
		dev_err(info->dev, "wrong pins number or pins and configs should be by 4\n");
		return -EINVAL;
	}

	grp->npins = size / 4;
	pin = grp->pins_conf = devm_kcalloc(info->dev,
					    grp->npins,
					    sizeof(struct at91_pmx_pin),
					    GFP_KERNEL);
	grp->pins = devm_kcalloc(info->dev, grp->npins, sizeof(unsigned int),
				 GFP_KERNEL);
	if (!grp->pins_conf || !grp->pins)
		return -ENOMEM;

	for (i = 0, j = 0; i < size; i += 4, j++) {
		pin->bank = be32_to_cpu(*list++);
		pin->pin = be32_to_cpu(*list++);
		grp->pins[j] = pin->bank * MAX_NB_GPIO_PER_BANK + pin->pin;
		pin->mux = be32_to_cpu(*list++);
		pin->conf = be32_to_cpu(*list++);

		at91_pin_dbg(info->dev, pin);
		pin++;
	}

	return 0;
}

static int at91_pinctrl_parse_functions(struct device_node *np,
					struct at91_pinctrl *info, u32 index)
{
	struct device_node *child;
	struct at91_pmx_func *func;
	struct at91_pin_group *grp;
	int ret;
	static u32 grp_index;
	u32 i = 0;

	dev_dbg(info->dev, "parse function(%d): %pOFn\n", index, np);

	func = &info->functions[index];

	/* Initialise function */
	func->name = np->name;
	func->ngroups = of_get_child_count(np);
	if (func->ngroups == 0) {
		dev_err(info->dev, "no groups defined\n");
		return -EINVAL;
	}
	func->groups = devm_kcalloc(info->dev,
			func->ngroups, sizeof(char *), GFP_KERNEL);
	if (!func->groups)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		func->groups[i] = child->name;
		grp = &info->groups[grp_index++];
		ret = at91_pinctrl_parse_groups(child, grp, info, i++);
		if (ret) {
			of_node_put(child);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id at91_pinctrl_of_match[] = {
	{ .compatible = "atmel,sama5d3-pinctrl", .data = &sama5d3_ops },
	{ .compatible = "atmel,at91sam9x5-pinctrl", .data = &at91sam9x5_ops },
	{ .compatible = "atmel,at91rm9200-pinctrl", .data = &at91rm9200_ops },
	{ .compatible = "microchip,sam9x60-pinctrl", .data = &sam9x60_ops },
	{ /* sentinel */ }
};

static int at91_pinctrl_probe_dt(struct platform_device *pdev,
				 struct at91_pinctrl *info)
{
	int ret = 0;
	int i, j;
	uint32_t *tmp;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;

	if (!np)
		return -ENODEV;

	info->dev = &pdev->dev;
	info->ops = (struct at91_pinctrl_mux_ops *)
		of_match_device(at91_pinctrl_of_match, &pdev->dev)->data;
	at91_pinctrl_child_count(info, np);

	if (gpio_banks < 1) {
		dev_err(&pdev->dev, "you need to specify at least one gpio-controller\n");
		return -EINVAL;
	}

	ret = at91_pinctrl_mux_mask(info, np);
	if (ret)
		return ret;

	dev_dbg(&pdev->dev, "nmux = %d\n", info->nmux);

	dev_dbg(&pdev->dev, "mux-mask\n");
	tmp = info->mux_mask;
	for (i = 0; i < gpio_banks; i++) {
		for (j = 0; j < info->nmux; j++, tmp++) {
			dev_dbg(&pdev->dev, "%d:%d\t0x%x\n", i, j, tmp[0]);
		}
	}

	dev_dbg(&pdev->dev, "nfunctions = %d\n", info->nfunctions);
	dev_dbg(&pdev->dev, "ngroups = %d\n", info->ngroups);
	info->functions = devm_kcalloc(&pdev->dev,
					info->nfunctions,
					sizeof(struct at91_pmx_func),
					GFP_KERNEL);
	if (!info->functions)
		return -ENOMEM;

	info->groups = devm_kcalloc(&pdev->dev,
					info->ngroups,
					sizeof(struct at91_pin_group),
					GFP_KERNEL);
	if (!info->groups)
		return -ENOMEM;

	dev_dbg(&pdev->dev, "nbanks = %d\n", gpio_banks);
	dev_dbg(&pdev->dev, "nfunctions = %d\n", info->nfunctions);
	dev_dbg(&pdev->dev, "ngroups = %d\n", info->ngroups);

	i = 0;

	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, gpio_compat))
			continue;
		ret = at91_pinctrl_parse_functions(child, info, i++);
		if (ret) {
			dev_err(&pdev->dev, "failed to parse function\n");
			of_node_put(child);
			return ret;
		}
	}

	return 0;
}

static int at91_pinctrl_probe(struct platform_device *pdev)
{
	struct at91_pinctrl *info;
	struct pinctrl_pin_desc *pdesc;
	int ret, i, j, k, ngpio_chips_enabled = 0;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret = at91_pinctrl_probe_dt(pdev, info);
	if (ret)
		return ret;

	/*
	 * We need all the GPIO drivers to probe FIRST, or we will not be able
	 * to obtain references to the struct gpio_chip * for them, and we
	 * need this to proceed.
	 */
	for (i = 0; i < gpio_banks; i++)
		if (gpio_chips[i])
			ngpio_chips_enabled++;

	if (ngpio_chips_enabled < info->nactive_banks) {
		dev_warn(&pdev->dev,
			 "All GPIO chips are not registered yet (%d/%d)\n",
			 ngpio_chips_enabled, info->nactive_banks);
		devm_kfree(&pdev->dev, info);
		return -EPROBE_DEFER;
	}

	at91_pinctrl_desc.name = dev_name(&pdev->dev);
	at91_pinctrl_desc.npins = gpio_banks * MAX_NB_GPIO_PER_BANK;
	at91_pinctrl_desc.pins = pdesc =
		devm_kcalloc(&pdev->dev,
			     at91_pinctrl_desc.npins, sizeof(*pdesc),
			     GFP_KERNEL);

	if (!at91_pinctrl_desc.pins)
		return -ENOMEM;

	for (i = 0, k = 0; i < gpio_banks; i++) {
		for (j = 0; j < MAX_NB_GPIO_PER_BANK; j++, k++) {
			pdesc->number = k;
			pdesc->name = kasprintf(GFP_KERNEL, "pio%c%d", i + 'A', j);
			pdesc++;
		}
	}

	platform_set_drvdata(pdev, info);
	info->pctl = devm_pinctrl_register(&pdev->dev, &at91_pinctrl_desc,
					   info);

	if (IS_ERR(info->pctl)) {
		dev_err(&pdev->dev, "could not register AT91 pinctrl driver\n");
		return PTR_ERR(info->pctl);
	}

	/* We will handle a range of GPIO pins */
	for (i = 0; i < gpio_banks; i++)
		if (gpio_chips[i])
			pinctrl_add_gpio_range(info->pctl, &gpio_chips[i]->range);

	dev_info(&pdev->dev, "initialized AT91 pinctrl driver\n");

	return 0;
}

static int at91_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct at91_gpio_chip *at91_gpio = gpiochip_get_data(chip);
	void __iomem *pio = at91_gpio->regbase;
	unsigned mask = 1 << offset;
	u32 osr;

	osr = readl_relaxed(pio + PIO_OSR);
	return !(osr & mask);
}

static int at91_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct at91_gpio_chip *at91_gpio = gpiochip_get_data(chip);
	void __iomem *pio = at91_gpio->regbase;
	unsigned mask = 1 << offset;

	writel_relaxed(mask, pio + PIO_ODR);
	return 0;
}

static int at91_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct at91_gpio_chip *at91_gpio = gpiochip_get_data(chip);
	void __iomem *pio = at91_gpio->regbase;
	unsigned mask = 1 << offset;
	u32 pdsr;

	pdsr = readl_relaxed(pio + PIO_PDSR);
	return (pdsr & mask) != 0;
}

static void at91_gpio_set(struct gpio_chip *chip, unsigned offset,
				int val)
{
	struct at91_gpio_chip *at91_gpio = gpiochip_get_data(chip);
	void __iomem *pio = at91_gpio->regbase;
	unsigned mask = 1 << offset;

	writel_relaxed(mask, pio + (val ? PIO_SODR : PIO_CODR));
}

static void at91_gpio_set_multiple(struct gpio_chip *chip,
				      unsigned long *mask, unsigned long *bits)
{
	struct at91_gpio_chip *at91_gpio = gpiochip_get_data(chip);
	void __iomem *pio = at91_gpio->regbase;

#define BITS_MASK(bits) (((bits) == 32) ? ~0U : (BIT(bits) - 1))
	/* Mask additionally to ngpio as not all GPIO controllers have 32 pins */
	uint32_t set_mask = (*mask & *bits) & BITS_MASK(chip->ngpio);
	uint32_t clear_mask = (*mask & ~(*bits)) & BITS_MASK(chip->ngpio);

	writel_relaxed(set_mask, pio + PIO_SODR);
	writel_relaxed(clear_mask, pio + PIO_CODR);
}

static int at91_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				int val)
{
	struct at91_gpio_chip *at91_gpio = gpiochip_get_data(chip);
	void __iomem *pio = at91_gpio->regbase;
	unsigned mask = 1 << offset;

	writel_relaxed(mask, pio + (val ? PIO_SODR : PIO_CODR));
	writel_relaxed(mask, pio + PIO_OER);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void at91_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	enum at91_mux mode;
	int i;
	struct at91_gpio_chip *at91_gpio = gpiochip_get_data(chip);
	void __iomem *pio = at91_gpio->regbase;

	for (i = 0; i < chip->ngpio; i++) {
		unsigned mask = pin_to_mask(i);
		const char *gpio_label;

		gpio_label = gpiochip_is_requested(chip, i);
		if (!gpio_label)
			continue;
		mode = at91_gpio->ops->get_periph(pio, mask);
		seq_printf(s, "[%s] GPIO%s%d: ",
			   gpio_label, chip->label, i);
		if (mode == AT91_MUX_GPIO) {
			seq_printf(s, "[gpio] ");
			seq_printf(s, "%s ",
				      readl_relaxed(pio + PIO_OSR) & mask ?
				      "output" : "input");
			seq_printf(s, "%s\n",
				      readl_relaxed(pio + PIO_PDSR) & mask ?
				      "set" : "clear");
		} else {
			seq_printf(s, "[periph %c]\n",
				   mode + 'A' - 1);
		}
	}
}
#else
#define at91_gpio_dbg_show	NULL
#endif

/* Several AIC controller irqs are dispatched through this GPIO handler.
 * To use any AT91_PIN_* as an externally triggered IRQ, first call
 * at91_set_gpio_input() then maybe enable its glitch filter.
 * Then just request_irq() with the pin ID; it works like any ARM IRQ
 * handler.
 * First implementation always triggers on rising and falling edges
 * whereas the newer PIO3 can be additionally configured to trigger on
 * level, edge with any polarity.
 *
 * Alternatively, certain pins may be used directly as IRQ0..IRQ6 after
 * configuring them with at91_set_a_periph() or at91_set_b_periph().
 * IRQ0..IRQ6 should be configurable, e.g. level vs edge triggering.
 */

static void gpio_irq_mask(struct irq_data *d)
{
	struct at91_gpio_chip *at91_gpio = irq_data_get_irq_chip_data(d);
	void __iomem	*pio = at91_gpio->regbase;
	unsigned	mask = 1 << d->hwirq;

	if (pio)
		writel_relaxed(mask, pio + PIO_IDR);
}

static void gpio_irq_unmask(struct irq_data *d)
{
	struct at91_gpio_chip *at91_gpio = irq_data_get_irq_chip_data(d);
	void __iomem	*pio = at91_gpio->regbase;
	unsigned	mask = 1 << d->hwirq;

	if (pio)
		writel_relaxed(mask, pio + PIO_IER);
}

static int gpio_irq_type(struct irq_data *d, unsigned type)
{
	switch (type) {
	case IRQ_TYPE_NONE:
	case IRQ_TYPE_EDGE_BOTH:
		return 0;
	default:
		return -EINVAL;
	}
}

/* Alternate irq type for PIO3 support */
static int alt_gpio_irq_type(struct irq_data *d, unsigned type)
{
	struct at91_gpio_chip *at91_gpio = irq_data_get_irq_chip_data(d);
	void __iomem	*pio = at91_gpio->regbase;
	unsigned	mask = 1 << d->hwirq;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		irq_set_handler_locked(d, handle_simple_irq);
		writel_relaxed(mask, pio + PIO_ESR);
		writel_relaxed(mask, pio + PIO_REHLSR);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_set_handler_locked(d, handle_simple_irq);
		writel_relaxed(mask, pio + PIO_ESR);
		writel_relaxed(mask, pio + PIO_FELLSR);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_set_handler_locked(d, handle_level_irq);
		writel_relaxed(mask, pio + PIO_LSR);
		writel_relaxed(mask, pio + PIO_FELLSR);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irq_set_handler_locked(d, handle_level_irq);
		writel_relaxed(mask, pio + PIO_LSR);
		writel_relaxed(mask, pio + PIO_REHLSR);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		/*
		 * disable additional interrupt modes:
		 * fall back to default behavior
		 */
		irq_set_handler_locked(d, handle_simple_irq);
		writel_relaxed(mask, pio + PIO_AIMDR);
		return 0;
	case IRQ_TYPE_NONE:
	default:
		pr_warn("AT91: No type for GPIO irq offset %d\n", d->irq);
		return -EINVAL;
	}

	/* enable additional interrupt modes */
	writel_relaxed(mask, pio + PIO_AIMER);

	return 0;
}

static void gpio_irq_ack(struct irq_data *d)
{
	/* the interrupt is already cleared before by reading ISR */
}

#ifdef CONFIG_PM

static u32 wakeups[MAX_GPIO_BANKS];
static u32 backups[MAX_GPIO_BANKS];

static int gpio_irq_set_wake(struct irq_data *d, unsigned state)
{
	struct at91_gpio_chip *at91_gpio = irq_data_get_irq_chip_data(d);
	unsigned	bank = at91_gpio->pioc_idx;
	unsigned mask = 1 << d->hwirq;

	if (unlikely(bank >= MAX_GPIO_BANKS))
		return -EINVAL;

	if (state)
		wakeups[bank] |= mask;
	else
		wakeups[bank] &= ~mask;

	irq_set_irq_wake(at91_gpio->pioc_virq, state);

	return 0;
}

void at91_pinctrl_gpio_suspend(void)
{
	int i;

	for (i = 0; i < gpio_banks; i++) {
		void __iomem  *pio;

		if (!gpio_chips[i])
			continue;

		pio = gpio_chips[i]->regbase;

		backups[i] = readl_relaxed(pio + PIO_IMR);
		writel_relaxed(backups[i], pio + PIO_IDR);
		writel_relaxed(wakeups[i], pio + PIO_IER);

		if (!wakeups[i])
			clk_disable_unprepare(gpio_chips[i]->clock);
		else
			printk(KERN_DEBUG "GPIO-%c may wake for %08x\n",
			       'A'+i, wakeups[i]);
	}
}

void at91_pinctrl_gpio_resume(void)
{
	int i;

	for (i = 0; i < gpio_banks; i++) {
		void __iomem  *pio;

		if (!gpio_chips[i])
			continue;

		pio = gpio_chips[i]->regbase;

		if (!wakeups[i])
			clk_prepare_enable(gpio_chips[i]->clock);

		writel_relaxed(wakeups[i], pio + PIO_IDR);
		writel_relaxed(backups[i], pio + PIO_IER);
	}
}

#else
#define gpio_irq_set_wake	NULL
#endif /* CONFIG_PM */

static void gpio_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct gpio_chip *gpio_chip = irq_desc_get_handler_data(desc);
	struct at91_gpio_chip *at91_gpio = gpiochip_get_data(gpio_chip);
	void __iomem	*pio = at91_gpio->regbase;
	unsigned long	isr;
	int		n;

	chained_irq_enter(chip, desc);
	for (;;) {
		/* Reading ISR acks pending (edge triggered) GPIO interrupts.
		 * When there are none pending, we're finished unless we need
		 * to process multiple banks (like ID_PIOCDE on sam9263).
		 */
		isr = readl_relaxed(pio + PIO_ISR) & readl_relaxed(pio + PIO_IMR);
		if (!isr) {
			if (!at91_gpio->next)
				break;
			at91_gpio = at91_gpio->next;
			pio = at91_gpio->regbase;
			gpio_chip = &at91_gpio->chip;
			continue;
		}

		for_each_set_bit(n, &isr, BITS_PER_LONG) {
			generic_handle_irq(irq_find_mapping(
					   gpio_chip->irq.domain, n));
		}
	}
	chained_irq_exit(chip, desc);
	/* now it may re-trigger */
}

static int at91_gpio_of_irq_setup(struct platform_device *pdev,
				  struct at91_gpio_chip *at91_gpio)
{
	struct gpio_chip	*gpiochip_prev = NULL;
	struct at91_gpio_chip   *prev = NULL;
	struct irq_data		*d = irq_get_irq_data(at91_gpio->pioc_virq);
	struct irq_chip		*gpio_irqchip;
	struct gpio_irq_chip	*girq;
	int i;

	gpio_irqchip = devm_kzalloc(&pdev->dev, sizeof(*gpio_irqchip),
				    GFP_KERNEL);
	if (!gpio_irqchip)
		return -ENOMEM;

	at91_gpio->pioc_hwirq = irqd_to_hwirq(d);

	gpio_irqchip->name = "GPIO";
	gpio_irqchip->irq_ack = gpio_irq_ack;
	gpio_irqchip->irq_disable = gpio_irq_mask;
	gpio_irqchip->irq_mask = gpio_irq_mask;
	gpio_irqchip->irq_unmask = gpio_irq_unmask;
	gpio_irqchip->irq_set_wake = gpio_irq_set_wake,
	gpio_irqchip->irq_set_type = at91_gpio->ops->irq_type;

	/* Disable irqs of this PIO controller */
	writel_relaxed(~0, at91_gpio->regbase + PIO_IDR);

	/*
	 * Let the generic code handle this edge IRQ, the the chained
	 * handler will perform the actual work of handling the parent
	 * interrupt.
	 */
	girq = &at91_gpio->chip.irq;
	girq->chip = gpio_irqchip;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_edge_irq;

	/*
	 * The top level handler handles one bank of GPIOs, except
	 * on some SoC it can handle up to three...
	 * We only set up the handler for the first of the list.
	 */
	gpiochip_prev = irq_get_handler_data(at91_gpio->pioc_virq);
	if (!gpiochip_prev) {
		girq->parent_handler = gpio_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(&pdev->dev, 1,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->parents[0] = at91_gpio->pioc_virq;
		return 0;
	}

	prev = gpiochip_get_data(gpiochip_prev);
	/* we can only have 2 banks before */
	for (i = 0; i < 2; i++) {
		if (prev->next) {
			prev = prev->next;
		} else {
			prev->next = at91_gpio;
			return 0;
		}
	}

	return -EINVAL;
}

/* This structure is replicated for each GPIO block allocated at probe time */
static const struct gpio_chip at91_gpio_template = {
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,
	.get_direction		= at91_gpio_get_direction,
	.direction_input	= at91_gpio_direction_input,
	.get			= at91_gpio_get,
	.direction_output	= at91_gpio_direction_output,
	.set			= at91_gpio_set,
	.set_multiple		= at91_gpio_set_multiple,
	.dbg_show		= at91_gpio_dbg_show,
	.can_sleep		= false,
	.ngpio			= MAX_NB_GPIO_PER_BANK,
};

static const struct of_device_id at91_gpio_of_match[] = {
	{ .compatible = "atmel,at91sam9x5-gpio", .data = &at91sam9x5_ops, },
	{ .compatible = "atmel,at91rm9200-gpio", .data = &at91rm9200_ops },
	{ .compatible = "microchip,sam9x60-gpio", .data = &sam9x60_ops },
	{ /* sentinel */ }
};

static int at91_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct at91_gpio_chip *at91_chip = NULL;
	struct gpio_chip *chip;
	struct pinctrl_gpio_range *range;
	int ret = 0;
	int irq, i;
	int alias_idx = of_alias_get_id(np, "gpio");
	uint32_t ngpio;
	char **names;

	BUG_ON(alias_idx >= ARRAY_SIZE(gpio_chips));
	if (gpio_chips[alias_idx]) {
		ret = -EBUSY;
		goto err;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err;
	}

	at91_chip = devm_kzalloc(&pdev->dev, sizeof(*at91_chip), GFP_KERNEL);
	if (!at91_chip) {
		ret = -ENOMEM;
		goto err;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	at91_chip->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(at91_chip->regbase)) {
		ret = PTR_ERR(at91_chip->regbase);
		goto err;
	}

	at91_chip->ops = (struct at91_pinctrl_mux_ops *)
		of_match_device(at91_gpio_of_match, &pdev->dev)->data;
	at91_chip->pioc_virq = irq;
	at91_chip->pioc_idx = alias_idx;

	at91_chip->clock = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(at91_chip->clock)) {
		dev_err(&pdev->dev, "failed to get clock, ignoring.\n");
		ret = PTR_ERR(at91_chip->clock);
		goto err;
	}

	ret = clk_prepare_enable(at91_chip->clock);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare and enable clock, ignoring.\n");
		goto clk_enable_err;
	}

	at91_chip->chip = at91_gpio_template;

	chip = &at91_chip->chip;
	chip->of_node = np;
	chip->label = dev_name(&pdev->dev);
	chip->parent = &pdev->dev;
	chip->owner = THIS_MODULE;
	chip->base = alias_idx * MAX_NB_GPIO_PER_BANK;

	if (!of_property_read_u32(np, "#gpio-lines", &ngpio)) {
		if (ngpio >= MAX_NB_GPIO_PER_BANK)
			pr_err("at91_gpio.%d, gpio-nb >= %d failback to %d\n",
			       alias_idx, MAX_NB_GPIO_PER_BANK, MAX_NB_GPIO_PER_BANK);
		else
			chip->ngpio = ngpio;
	}

	names = devm_kcalloc(&pdev->dev, chip->ngpio, sizeof(char *),
			     GFP_KERNEL);

	if (!names) {
		ret = -ENOMEM;
		goto clk_enable_err;
	}

	for (i = 0; i < chip->ngpio; i++)
		names[i] = kasprintf(GFP_KERNEL, "pio%c%d", alias_idx + 'A', i);

	chip->names = (const char *const *)names;

	range = &at91_chip->range;
	range->name = chip->label;
	range->id = alias_idx;
	range->pin_base = range->base = range->id * MAX_NB_GPIO_PER_BANK;

	range->npins = chip->ngpio;
	range->gc = chip;

	ret = at91_gpio_of_irq_setup(pdev, at91_chip);
	if (ret)
		goto gpiochip_add_err;

	ret = gpiochip_add_data(chip, at91_chip);
	if (ret)
		goto gpiochip_add_err;

	gpio_chips[alias_idx] = at91_chip;
	gpio_banks = max(gpio_banks, alias_idx + 1);

	dev_info(&pdev->dev, "at address %p\n", at91_chip->regbase);

	return 0;

gpiochip_add_err:
clk_enable_err:
	clk_disable_unprepare(at91_chip->clock);
err:
	dev_err(&pdev->dev, "Failure %i for GPIO %i\n", ret, alias_idx);

	return ret;
}

static struct platform_driver at91_gpio_driver = {
	.driver = {
		.name = "gpio-at91",
		.of_match_table = at91_gpio_of_match,
	},
	.probe = at91_gpio_probe,
};

static struct platform_driver at91_pinctrl_driver = {
	.driver = {
		.name = "pinctrl-at91",
		.of_match_table = at91_pinctrl_of_match,
	},
	.probe = at91_pinctrl_probe,
};

static struct platform_driver * const drivers[] = {
	&at91_gpio_driver,
	&at91_pinctrl_driver,
};

static int __init at91_pinctrl_init(void)
{
	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}
arch_initcall(at91_pinctrl_init);
