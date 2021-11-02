// SPDX-License-Identifier: GPL-2.0-only
/*
 * Reset controller portions for the U8500 PRCC
 * Copyright (C) 2021 Linus Walleij <linus.walleij@linaro.org>
 */
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/reset-controller.h>
#include <linux/bits.h>
#include <linux/delay.h>

#include "prcc.h"
#include "reset-prcc.h"

#define to_u8500_prcc_reset(p) container_of((p), struct u8500_prcc_reset, rcdev)

/* This macro flattens the 2-dimensional PRCC numberspace */
#define PRCC_RESET_LINE(prcc_num, bit) \
	(((prcc_num) * PRCC_PERIPHS_PER_CLUSTER) + (bit))

/*
 * Reset registers in each PRCC - the reset lines are active low
 * so what you need to do is write a bit for the peripheral you
 * want to put into reset into the CLEAR register, this will assert
 * the reset by pulling the line low. SET take the device out of
 * reset. The status reflects the actual state of the line.
 */
#define PRCC_K_SOFTRST_SET		0x018
#define PRCC_K_SOFTRST_CLEAR		0x01c
#define PRCC_K_RST_STATUS		0x020

static int prcc_num_to_index(unsigned int num)
{
	switch (num) {
	case 1:
		return CLKRST1_INDEX;
	case 2:
		return CLKRST2_INDEX;
	case 3:
		return CLKRST3_INDEX;
	case 5:
		return CLKRST5_INDEX;
	case 6:
		return CLKRST6_INDEX;
	}
	return -EINVAL;
}

static void __iomem *u8500_prcc_reset_base(struct u8500_prcc_reset *ur,
					   unsigned long id)
{
	unsigned int prcc_num, index;

	prcc_num = id / PRCC_PERIPHS_PER_CLUSTER;
	index = prcc_num_to_index(prcc_num);

	if (index > ARRAY_SIZE(ur->base))
		return NULL;

	return ur->base[index];
}

static int u8500_prcc_reset(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	struct u8500_prcc_reset *ur = to_u8500_prcc_reset(rcdev);
	void __iomem *base = u8500_prcc_reset_base(ur, id);
	unsigned int bit = id % PRCC_PERIPHS_PER_CLUSTER;

	pr_debug("PRCC cycle reset id %lu, bit %u\n", id, bit);

	/*
	 * Assert reset and then release it. The one microsecond
	 * delay is found in the vendor reference code.
	 */
	writel(BIT(bit), base + PRCC_K_SOFTRST_CLEAR);
	udelay(1);
	writel(BIT(bit), base + PRCC_K_SOFTRST_SET);
	udelay(1);

	return 0;
}

static int u8500_prcc_reset_assert(struct reset_controller_dev *rcdev,
				   unsigned long id)
{
	struct u8500_prcc_reset *ur = to_u8500_prcc_reset(rcdev);
	void __iomem *base = u8500_prcc_reset_base(ur, id);
	unsigned int bit = id % PRCC_PERIPHS_PER_CLUSTER;

	pr_debug("PRCC assert reset id %lu, bit %u\n", id, bit);
	writel(BIT(bit), base + PRCC_K_SOFTRST_CLEAR);

	return 0;
}

static int u8500_prcc_reset_deassert(struct reset_controller_dev *rcdev,
				     unsigned long id)
{
	struct u8500_prcc_reset *ur = to_u8500_prcc_reset(rcdev);
	void __iomem *base = u8500_prcc_reset_base(ur, id);
	unsigned int bit = id % PRCC_PERIPHS_PER_CLUSTER;

	pr_debug("PRCC deassert reset id %lu, bit %u\n", id, bit);
	writel(BIT(bit), base + PRCC_K_SOFTRST_SET);

	return 0;
}

static int u8500_prcc_reset_status(struct reset_controller_dev *rcdev,
				   unsigned long id)
{
	struct u8500_prcc_reset *ur = to_u8500_prcc_reset(rcdev);
	void __iomem *base = u8500_prcc_reset_base(ur, id);
	unsigned int bit = id % PRCC_PERIPHS_PER_CLUSTER;
	u32 val;

	pr_debug("PRCC check status on reset line id %lu, bit %u\n", id, bit);
	val = readl(base + PRCC_K_RST_STATUS);

	/* Active low so return the inverse value of the bit */
	return !(val & BIT(bit));
}

static const struct reset_control_ops u8500_prcc_reset_ops = {
	.reset = u8500_prcc_reset,
	.assert = u8500_prcc_reset_assert,
	.deassert = u8500_prcc_reset_deassert,
	.status = u8500_prcc_reset_status,
};

static int u8500_prcc_reset_xlate(struct reset_controller_dev *rcdev,
				  const struct of_phandle_args *reset_spec)
{
	unsigned int prcc_num, bit;

	if (reset_spec->args_count != 2)
		return -EINVAL;

	prcc_num = reset_spec->args[0];
	bit = reset_spec->args[1];

	if (prcc_num != 1 && prcc_num != 2 && prcc_num != 3 &&
	    prcc_num != 5 && prcc_num != 6) {
		pr_err("%s: invalid PRCC %d\n", __func__, prcc_num);
		return -EINVAL;
	}

	pr_debug("located reset line %d at PRCC %d bit %d\n",
		 PRCC_RESET_LINE(prcc_num, bit), prcc_num, bit);

	return PRCC_RESET_LINE(prcc_num, bit);
}

void u8500_prcc_reset_init(struct device_node *np, struct u8500_prcc_reset *ur)
{
	struct reset_controller_dev *rcdev = &ur->rcdev;
	int ret;
	int i;

	for (i = 0; i < CLKRST_MAX; i++) {
		ur->base[i] = ioremap(ur->phy_base[i], SZ_4K);
		if (!ur->base[i])
			pr_err("PRCC failed to remap for reset base %d (%08x)\n",
			       i, ur->phy_base[i]);
	}

	rcdev->owner = THIS_MODULE;
	rcdev->ops = &u8500_prcc_reset_ops;
	rcdev->of_node = np;
	rcdev->of_reset_n_cells = 2;
	rcdev->of_xlate = u8500_prcc_reset_xlate;

	ret = reset_controller_register(rcdev);
	if (ret)
		pr_err("PRCC failed to register reset controller\n");
}
