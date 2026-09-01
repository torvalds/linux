/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 * Author: Benjamin Larsson <benjamin.larsson@genexis.eu>
 * Author: Markus Gothe <markus.gothe@genexis.eu>
 */

#ifndef __AIROHA_COMMON_HEADER__
#define __AIROHA_COMMON_HEADER__

#include <dt-bindings/pinctrl/mt65xx.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"

#define AIROHA_NUM_PINS				64
#define AIROHA_PIN_BANK_SIZE			(AIROHA_NUM_PINS / 2)
#define AIROHA_REG_GPIOCTRL_NUM_PIN		(AIROHA_NUM_PINS / 4)

#define PINCTRL_PIN_GROUP(id, table)					\
	PINCTRL_PINGROUP(id, table##_pins, ARRAY_SIZE(table##_pins))

#define PINCTRL_FUNC_DESC(id, table)					\
	{								\
		.desc = PINCTRL_PINFUNCTION(id, table##_groups,	\
					    ARRAY_SIZE(table##_groups)),\
		.groups = table##_func_group,				\
		.group_size = ARRAY_SIZE(table##_func_group),		\
	}

#define PINCTRL_CONF_DESC(p, offset, mask)				\
	{								\
		.pin = p,						\
		.reg = { offset, mask },				\
	}

struct airoha_pinctrl_reg {
	u32 offset;
	u32 mask;
};

enum airoha_pinctrl_mux_func {
	AIROHA_FUNC_MUX,
	AIROHA_FUNC_PWM_MUX,
	AIROHA_FUNC_PWM_EXT_MUX,
};

struct airoha_pinctrl_func_group {
	const char *name;
	struct {
		enum airoha_pinctrl_mux_func mux;
		u32 offset;
		u32 mask;
		u32 val;
	} regmap[2];
	int regmap_size;
};

struct airoha_pinctrl_func {
	const struct pinfunction desc;
	const struct airoha_pinctrl_func_group *groups;
	u8 group_size;
};

struct airoha_pinctrl_conf {
	u32 pin;
	struct airoha_pinctrl_reg reg;
};

struct airoha_gpiochip_regs {
	/* gpio */
	const u32 *data;
	const u32 *dir;
	const u32 *out;
	/* irq */
	const u32 *status;
	const u32 *level;
	const u32 *edge;
};

struct airoha_pinctrl_confs_info {
	const struct airoha_pinctrl_conf *confs;
	unsigned int num_confs;
};

enum airoha_pinctrl_confs_type {
	AIROHA_PINCTRL_CONFS_PULLUP,
	AIROHA_PINCTRL_CONFS_PULLDOWN,
	AIROHA_PINCTRL_CONFS_DRIVE_E2,
	AIROHA_PINCTRL_CONFS_DRIVE_E4,
	AIROHA_PINCTRL_CONFS_PCIE_RST_OD,

	AIROHA_PINCTRL_CONFS_MAX,
};

struct airoha_pinctrl {
	struct pinctrl_dev *ctrl;

	struct pinctrl_desc desc;
	const struct pingroup *grps;
	const struct airoha_pinctrl_func *funcs;
	const struct airoha_pinctrl_confs_info *confs_info;

	struct regmap *chip_scu;
	struct regmap *regmap;

	struct gpio_chip gpiochip;
	struct airoha_gpiochip_regs *gpio_regs;
};

struct airoha_pinctrl_match_data {
	const char *chip_scu_compatible;
	const char *pinctrl_name;
	struct module *pinctrl_owner;
	const struct pinctrl_pin_desc *pins;
	const unsigned int num_pins;
	const struct pingroup *grps;
	const unsigned int num_grps;
	const struct airoha_pinctrl_func *funcs;
	const unsigned int num_funcs;
	const struct airoha_pinctrl_confs_info confs_info[AIROHA_PINCTRL_CONFS_MAX];
};

int airoha_pinctrl_probe(struct platform_device *pdev);

#endif
