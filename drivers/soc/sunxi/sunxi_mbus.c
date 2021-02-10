// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Maxime Ripard <maxime@cerno.tech> */

#include <linux/device.h>
#include <linux/dma-map-ops.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>

static const char * const sunxi_mbus_devices[] = {
	/*
	 * The display engine virtual devices are not strictly speaking
	 * connected to the MBUS, but since DRM will perform all the
	 * memory allocations and DMA operations through that device, we
	 * need to have the quirk on those devices too.
	 */
	"allwinner,sun4i-a10-display-engine",
	"allwinner,sun5i-a10s-display-engine",
	"allwinner,sun5i-a13-display-engine",
	"allwinner,sun6i-a31-display-engine",
	"allwinner,sun6i-a31s-display-engine",
	"allwinner,sun7i-a20-display-engine",
	"allwinner,sun8i-a23-display-engine",
	"allwinner,sun8i-a33-display-engine",
	"allwinner,sun9i-a80-display-engine",

	/*
	 * And now we have the regular devices connected to the MBUS
	 * (that we know of).
	 */
	"allwinner,sun4i-a10-csi1",
	"allwinner,sun4i-a10-display-backend",
	"allwinner,sun4i-a10-display-frontend",
	"allwinner,sun4i-a10-video-engine",
	"allwinner,sun5i-a13-display-backend",
	"allwinner,sun5i-a13-video-engine",
	"allwinner,sun6i-a31-csi",
	"allwinner,sun6i-a31-display-backend",
	"allwinner,sun7i-a20-csi0",
	"allwinner,sun7i-a20-display-backend",
	"allwinner,sun7i-a20-display-frontend",
	"allwinner,sun7i-a20-video-engine",
	"allwinner,sun8i-a23-display-backend",
	"allwinner,sun8i-a23-display-frontend",
	"allwinner,sun8i-a33-display-backend",
	"allwinner,sun8i-a33-display-frontend",
	"allwinner,sun8i-a33-video-engine",
	"allwinner,sun8i-a83t-csi",
	"allwinner,sun8i-h3-csi",
	"allwinner,sun8i-h3-video-engine",
	"allwinner,sun8i-v3s-csi",
	"allwinner,sun9i-a80-display-backend",
	"allwinner,sun50i-a64-csi",
	"allwinner,sun50i-a64-video-engine",
	"allwinner,sun50i-h5-video-engine",
	NULL,
};

static int sunxi_mbus_notifier(struct notifier_block *nb,
			       unsigned long event, void *__dev)
{
	struct device *dev = __dev;
	int ret;

	if (event != BUS_NOTIFY_ADD_DEVICE)
		return NOTIFY_DONE;

	/*
	 * Only the devices that need a large memory bandwidth do DMA
	 * directly over the memory bus (called MBUS), instead of going
	 * through the regular system bus.
	 */
	if (!of_device_compatible_match(dev->of_node, sunxi_mbus_devices))
		return NOTIFY_DONE;

	/*
	 * Devices with an interconnects property have the MBUS
	 * relationship described in their DT and dealt with by
	 * of_dma_configure, so we can just skip them.
	 *
	 * Older DTs or SoCs who are not clearly understood need to set
	 * that DMA offset though.
	 */
	if (of_find_property(dev->of_node, "interconnects", NULL))
		return NOTIFY_DONE;

	ret = dma_direct_set_offset(dev, PHYS_OFFSET, 0, SZ_4G);
	if (ret)
		dev_err(dev, "Couldn't setup our DMA offset: %d\n", ret);

	return NOTIFY_DONE;
}

static struct notifier_block sunxi_mbus_nb = {
	.notifier_call = sunxi_mbus_notifier,
};

static const char * const sunxi_mbus_platforms[] __initconst = {
	"allwinner,sun4i-a10",
	"allwinner,sun5i-a10s",
	"allwinner,sun5i-a13",
	"allwinner,sun6i-a31",
	"allwinner,sun7i-a20",
	"allwinner,sun8i-a23",
	"allwinner,sun8i-a33",
	"allwinner,sun8i-a83t",
	"allwinner,sun8i-h3",
	"allwinner,sun8i-r40",
	"allwinner,sun8i-v3",
	"allwinner,sun8i-v3s",
	"allwinner,sun9i-a80",
	"allwinner,sun50i-a64",
	"allwinner,sun50i-h5",
	"nextthing,gr8",
	NULL,
};

static int __init sunxi_mbus_init(void)
{
	if (!of_device_compatible_match(of_root, sunxi_mbus_platforms))
		return 0;

	bus_register_notifier(&platform_bus_type, &sunxi_mbus_nb);
	return 0;
}
arch_initcall(sunxi_mbus_init);
