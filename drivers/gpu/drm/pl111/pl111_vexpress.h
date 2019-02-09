// SPDX-License-Identifier: GPL-2.0

struct device;
struct pl111_drm_dev_private;
struct regmap;

#ifdef CONFIG_ARCH_VEXPRESS

int pl111_vexpress_clcd_init(struct device *dev,
			     struct pl111_drm_dev_private *priv,
			     struct regmap *map);

int vexpress_muxfpga_init(void);

#else

static inline int pl111_vexpress_clcd_init(struct device *dev,
					   struct pl111_drm_dev_private *priv,
					   struct regmap *map)
{
	return -ENODEV;
}

static inline int vexpress_muxfpga_init(void)
{
	return 0;
}

#endif
