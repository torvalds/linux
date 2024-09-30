// SPDX-License-Identifier: GPL-2.0+
/*
 * SoC driver for Cirrus EP93xx chips.
 * Copyright (C) 2022 Nikita Shubin <nikita.shubin@maquefel.me>
 *
 * Based on a rewrite of arch/arm/mach-ep93xx/core.c
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 * Copyright (C) 2007 Herbert Valerio Riedel <hvr@gnu.org>
 *
 * Thanks go to Michael Burian and Ray Lehtiniemi for their key
 * role in the ep93xx Linux community.
 */

#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sys_soc.h>

#include <linux/soc/cirrus/ep93xx.h>

#define EP93XX_SYSCON_DEVCFG		0x80

#define EP93XX_SWLOCK_MAGICK		0xaa
#define EP93XX_SYSCON_SWLOCK		0xc0
#define EP93XX_SYSCON_SYSCFG		0x9c
#define EP93XX_SYSCON_SYSCFG_REV_MASK	GENMASK(31, 28)
#define EP93XX_SYSCON_SYSCFG_REV_SHIFT	28

struct ep93xx_map_info {
	spinlock_t lock;
	void __iomem *base;
	struct regmap *map;
};

/*
 * EP93xx System Controller software locked register write
 *
 * Logic safeguards are included to condition the control signals for
 * power connection to the matrix to prevent part damage. In addition, a
 * software lock register is included that must be written with 0xAA
 * before each register write to change the values of the four switch
 * matrix control registers.
 */
static void ep93xx_regmap_write(struct regmap *map, spinlock_t *lock,
				 unsigned int reg, unsigned int val)
{
	guard(spinlock_irqsave)(lock);

	regmap_write(map, EP93XX_SYSCON_SWLOCK, EP93XX_SWLOCK_MAGICK);
	regmap_write(map, reg, val);
}

static void ep93xx_regmap_update_bits(struct regmap *map, spinlock_t *lock,
				      unsigned int reg, unsigned int mask,
				      unsigned int val)
{
	guard(spinlock_irqsave)(lock);

	regmap_write(map, EP93XX_SYSCON_SWLOCK, EP93XX_SWLOCK_MAGICK);
	/* force write is required to clear swlock if no changes are made */
	regmap_update_bits_base(map, reg, mask, val, NULL, false, true);
}

static void ep93xx_unregister_adev(void *_adev)
{
	struct auxiliary_device *adev = _adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static void ep93xx_adev_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);
	struct ep93xx_regmap_adev *rdev = to_ep93xx_regmap_adev(adev);

	kfree(rdev);
}

static struct auxiliary_device __init *ep93xx_adev_alloc(struct device *parent,
							 const char *name,
							 struct ep93xx_map_info *info)
{
	struct ep93xx_regmap_adev *rdev __free(kfree) = NULL;
	struct auxiliary_device *adev;
	int ret;

	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (!rdev)
		return ERR_PTR(-ENOMEM);

	rdev->map = info->map;
	rdev->base = info->base;
	rdev->lock = &info->lock;
	rdev->write = ep93xx_regmap_write;
	rdev->update_bits = ep93xx_regmap_update_bits;

	adev = &rdev->adev;
	adev->name = name;
	adev->dev.parent = parent;
	adev->dev.release = ep93xx_adev_release;

	ret = auxiliary_device_init(adev);
	if (ret)
		return ERR_PTR(ret);

	return &no_free_ptr(rdev)->adev;
}

static int __init ep93xx_controller_register(struct device *parent, const char *name,
					     struct ep93xx_map_info *info)
{
	struct auxiliary_device *adev;
	int ret;

	adev = ep93xx_adev_alloc(parent, name, info);
	if (IS_ERR(adev))
		return PTR_ERR(adev);

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(parent, ep93xx_unregister_adev, adev);
}

static unsigned int __init ep93xx_soc_revision(struct regmap *map)
{
	unsigned int val;

	regmap_read(map, EP93XX_SYSCON_SYSCFG, &val);
	val &= EP93XX_SYSCON_SYSCFG_REV_MASK;
	val >>= EP93XX_SYSCON_SYSCFG_REV_SHIFT;
	return val;
}

static const char __init *ep93xx_get_soc_rev(unsigned int rev)
{
	switch (rev) {
	case EP93XX_CHIP_REV_D0:
		return "D0";
	case EP93XX_CHIP_REV_D1:
		return "D1";
	case EP93XX_CHIP_REV_E0:
		return "E0";
	case EP93XX_CHIP_REV_E1:
		return "E1";
	case EP93XX_CHIP_REV_E2:
		return "E2";
	default:
		return "unknown";
	}
}

static const char *pinctrl_names[] __initconst = {
	"pinctrl-ep9301",	/* EP93XX_9301_SOC */
	"pinctrl-ep9307",	/* EP93XX_9307_SOC */
	"pinctrl-ep9312",	/* EP93XX_9312_SOC */
};

static int __init ep93xx_syscon_probe(struct platform_device *pdev)
{
	enum ep93xx_soc_model model;
	struct ep93xx_map_info *map_info;
	struct soc_device_attribute *attrs;
	struct soc_device *soc_dev;
	struct device *dev = &pdev->dev;
	struct regmap *map;
	void __iomem *base;
	unsigned int rev;
	int ret;

	model = (enum ep93xx_soc_model)(uintptr_t)device_get_match_data(dev);

	map = device_node_to_regmap(dev->of_node);
	if (IS_ERR(map))
		return PTR_ERR(map);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	attrs = devm_kzalloc(dev, sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	rev = ep93xx_soc_revision(map);

	attrs->machine = of_flat_dt_get_machine_name();
	attrs->family = "Cirrus Logic EP93xx";
	attrs->revision = ep93xx_get_soc_rev(rev);

	soc_dev = soc_device_register(attrs);
	if (IS_ERR(soc_dev))
		return PTR_ERR(soc_dev);

	map_info = devm_kzalloc(dev, sizeof(*map_info), GFP_KERNEL);
	if (!map_info)
		return -ENOMEM;

	spin_lock_init(&map_info->lock);
	map_info->map = map;
	map_info->base = base;

	ret = ep93xx_controller_register(dev, pinctrl_names[model], map_info);
	if (ret)
		dev_err(dev, "registering pinctrl controller failed\n");

	/*
	 * EP93xx SSP clock rate was doubled in version E2. For more information
	 * see section 6 "2x SSP (Synchronous Serial Port) Clock – Revision E2 only":
	 *     http://www.cirrus.com/en/pubs/appNote/AN273REV4.pdf
	 */
	if (rev == EP93XX_CHIP_REV_E2)
		ret = ep93xx_controller_register(dev, "clk-ep93xx.e2", map_info);
	else
		ret = ep93xx_controller_register(dev, "clk-ep93xx", map_info);
	if (ret)
		dev_err(dev, "registering clock controller failed\n");

	ret = ep93xx_controller_register(dev, "reset-ep93xx", map_info);
	if (ret)
		dev_err(dev, "registering reset controller failed\n");

	return 0;
}

static const struct of_device_id ep9301_syscon_of_device_ids[] = {
	{ .compatible	= "cirrus,ep9301-syscon", .data = (void *)EP93XX_9301_SOC },
	{ .compatible	= "cirrus,ep9302-syscon", .data = (void *)EP93XX_9301_SOC },
	{ .compatible	= "cirrus,ep9307-syscon", .data = (void *)EP93XX_9307_SOC },
	{ .compatible	= "cirrus,ep9312-syscon", .data = (void *)EP93XX_9312_SOC },
	{ .compatible	= "cirrus,ep9315-syscon", .data = (void *)EP93XX_9312_SOC },
	{ /* sentinel */ }
};

static struct platform_driver ep9301_syscon_driver = {
	.driver = {
		.name = "ep9301-syscon",
		.of_match_table = ep9301_syscon_of_device_ids,
	},
};
builtin_platform_driver_probe(ep9301_syscon_driver, ep93xx_syscon_probe);
