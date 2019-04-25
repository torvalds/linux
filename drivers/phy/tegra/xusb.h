/*
 * Copyright (c) 2014-2015, NVIDIA CORPORATION.  All rights reserved.
 * Copyright (c) 2015, Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __PHY_TEGRA_XUSB_H
#define __PHY_TEGRA_XUSB_H

#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include <linux/usb/otg.h>

/* legacy entry points for backwards-compatibility */
int tegra_xusb_padctl_legacy_probe(struct platform_device *pdev);
int tegra_xusb_padctl_legacy_remove(struct platform_device *pdev);

struct phy;
struct phy_provider;
struct platform_device;
struct regulator;

/*
 * lanes
 */
struct tegra_xusb_lane_soc {
	const char *name;

	unsigned int offset;
	unsigned int shift;
	unsigned int mask;

	const char * const *funcs;
	unsigned int num_funcs;
};

struct tegra_xusb_lane {
	const struct tegra_xusb_lane_soc *soc;
	struct tegra_xusb_pad *pad;
	struct device_node *np;
	struct list_head list;
	unsigned int function;
	unsigned int index;
};

int tegra_xusb_lane_parse_dt(struct tegra_xusb_lane *lane,
			     struct device_node *np);

struct tegra_xusb_usb3_lane {
	struct tegra_xusb_lane base;
};

static inline struct tegra_xusb_usb3_lane *
to_usb3_lane(struct tegra_xusb_lane *lane)
{
	return container_of(lane, struct tegra_xusb_usb3_lane, base);
}

struct tegra_xusb_usb2_lane {
	struct tegra_xusb_lane base;

	u32 hs_curr_level_offset;
	bool powered_on;
};

static inline struct tegra_xusb_usb2_lane *
to_usb2_lane(struct tegra_xusb_lane *lane)
{
	return container_of(lane, struct tegra_xusb_usb2_lane, base);
}

struct tegra_xusb_ulpi_lane {
	struct tegra_xusb_lane base;
};

static inline struct tegra_xusb_ulpi_lane *
to_ulpi_lane(struct tegra_xusb_lane *lane)
{
	return container_of(lane, struct tegra_xusb_ulpi_lane, base);
}

struct tegra_xusb_hsic_lane {
	struct tegra_xusb_lane base;

	u32 strobe_trim;
	u32 rx_strobe_trim;
	u32 rx_data_trim;
	u32 tx_rtune_n;
	u32 tx_rtune_p;
	u32 tx_rslew_n;
	u32 tx_rslew_p;
	bool auto_term;
};

static inline struct tegra_xusb_hsic_lane *
to_hsic_lane(struct tegra_xusb_lane *lane)
{
	return container_of(lane, struct tegra_xusb_hsic_lane, base);
}

struct tegra_xusb_pcie_lane {
	struct tegra_xusb_lane base;
};

static inline struct tegra_xusb_pcie_lane *
to_pcie_lane(struct tegra_xusb_lane *lane)
{
	return container_of(lane, struct tegra_xusb_pcie_lane, base);
}

struct tegra_xusb_sata_lane {
	struct tegra_xusb_lane base;
};

static inline struct tegra_xusb_sata_lane *
to_sata_lane(struct tegra_xusb_lane *lane)
{
	return container_of(lane, struct tegra_xusb_sata_lane, base);
}

struct tegra_xusb_lane_ops {
	struct tegra_xusb_lane *(*probe)(struct tegra_xusb_pad *pad,
					 struct device_node *np,
					 unsigned int index);
	void (*remove)(struct tegra_xusb_lane *lane);
};

/*
 * pads
 */
struct tegra_xusb_pad_soc;
struct tegra_xusb_padctl;

struct tegra_xusb_pad_ops {
	struct tegra_xusb_pad *(*probe)(struct tegra_xusb_padctl *padctl,
					const struct tegra_xusb_pad_soc *soc,
					struct device_node *np);
	void (*remove)(struct tegra_xusb_pad *pad);
};

struct tegra_xusb_pad_soc {
	const char *name;

	const struct tegra_xusb_lane_soc *lanes;
	unsigned int num_lanes;

	const struct tegra_xusb_pad_ops *ops;
};

struct tegra_xusb_pad {
	const struct tegra_xusb_pad_soc *soc;
	struct tegra_xusb_padctl *padctl;
	struct phy_provider *provider;
	struct phy **lanes;
	struct device dev;

	const struct tegra_xusb_lane_ops *ops;

	struct list_head list;
};

static inline struct tegra_xusb_pad *to_tegra_xusb_pad(struct device *dev)
{
	return container_of(dev, struct tegra_xusb_pad, dev);
}

int tegra_xusb_pad_init(struct tegra_xusb_pad *pad,
			struct tegra_xusb_padctl *padctl,
			struct device_node *np);
int tegra_xusb_pad_register(struct tegra_xusb_pad *pad,
			    const struct phy_ops *ops);
void tegra_xusb_pad_unregister(struct tegra_xusb_pad *pad);

struct tegra_xusb_usb3_pad {
	struct tegra_xusb_pad base;

	unsigned int enable;
	struct mutex lock;
};

static inline struct tegra_xusb_usb3_pad *
to_usb3_pad(struct tegra_xusb_pad *pad)
{
	return container_of(pad, struct tegra_xusb_usb3_pad, base);
}

struct tegra_xusb_usb2_pad {
	struct tegra_xusb_pad base;

	struct clk *clk;
	unsigned int enable;
	struct mutex lock;
};

static inline struct tegra_xusb_usb2_pad *
to_usb2_pad(struct tegra_xusb_pad *pad)
{
	return container_of(pad, struct tegra_xusb_usb2_pad, base);
}

struct tegra_xusb_ulpi_pad {
	struct tegra_xusb_pad base;
};

static inline struct tegra_xusb_ulpi_pad *
to_ulpi_pad(struct tegra_xusb_pad *pad)
{
	return container_of(pad, struct tegra_xusb_ulpi_pad, base);
}

struct tegra_xusb_hsic_pad {
	struct tegra_xusb_pad base;

	struct regulator *supply;
	struct clk *clk;
};

static inline struct tegra_xusb_hsic_pad *
to_hsic_pad(struct tegra_xusb_pad *pad)
{
	return container_of(pad, struct tegra_xusb_hsic_pad, base);
}

struct tegra_xusb_pcie_pad {
	struct tegra_xusb_pad base;

	struct reset_control *rst;
	struct clk *pll;

	unsigned int enable;
};

static inline struct tegra_xusb_pcie_pad *
to_pcie_pad(struct tegra_xusb_pad *pad)
{
	return container_of(pad, struct tegra_xusb_pcie_pad, base);
}

struct tegra_xusb_sata_pad {
	struct tegra_xusb_pad base;

	struct reset_control *rst;
	struct clk *pll;

	unsigned int enable;
};

static inline struct tegra_xusb_sata_pad *
to_sata_pad(struct tegra_xusb_pad *pad)
{
	return container_of(pad, struct tegra_xusb_sata_pad, base);
}

/*
 * ports
 */
struct tegra_xusb_port_ops;

struct tegra_xusb_port {
	struct tegra_xusb_padctl *padctl;
	struct tegra_xusb_lane *lane;
	unsigned int index;

	struct list_head list;
	struct device dev;

	const struct tegra_xusb_port_ops *ops;
};

struct tegra_xusb_lane_map {
	unsigned int port;
	const char *type;
	unsigned int index;
	const char *func;
};

struct tegra_xusb_lane *
tegra_xusb_port_find_lane(struct tegra_xusb_port *port,
			  const struct tegra_xusb_lane_map *map,
			  const char *function);

struct tegra_xusb_port *
tegra_xusb_find_port(struct tegra_xusb_padctl *padctl, const char *type,
		     unsigned int index);

struct tegra_xusb_usb2_port {
	struct tegra_xusb_port base;

	struct regulator *supply;
	enum usb_dr_mode mode;
	bool internal;
};

static inline struct tegra_xusb_usb2_port *
to_usb2_port(struct tegra_xusb_port *port)
{
	return container_of(port, struct tegra_xusb_usb2_port, base);
}

struct tegra_xusb_usb2_port *
tegra_xusb_find_usb2_port(struct tegra_xusb_padctl *padctl,
			  unsigned int index);

struct tegra_xusb_ulpi_port {
	struct tegra_xusb_port base;

	struct regulator *supply;
	bool internal;
};

static inline struct tegra_xusb_ulpi_port *
to_ulpi_port(struct tegra_xusb_port *port)
{
	return container_of(port, struct tegra_xusb_ulpi_port, base);
}

struct tegra_xusb_hsic_port {
	struct tegra_xusb_port base;
};

static inline struct tegra_xusb_hsic_port *
to_hsic_port(struct tegra_xusb_port *port)
{
	return container_of(port, struct tegra_xusb_hsic_port, base);
}

struct tegra_xusb_usb3_port {
	struct tegra_xusb_port base;
	struct regulator *supply;
	bool context_saved;
	unsigned int port;
	bool internal;

	u32 tap1;
	u32 amp;
	u32 ctle_z;
	u32 ctle_g;
};

static inline struct tegra_xusb_usb3_port *
to_usb3_port(struct tegra_xusb_port *port)
{
	return container_of(port, struct tegra_xusb_usb3_port, base);
}

struct tegra_xusb_usb3_port *
tegra_xusb_find_usb3_port(struct tegra_xusb_padctl *padctl,
			  unsigned int index);

struct tegra_xusb_port_ops {
	int (*enable)(struct tegra_xusb_port *port);
	void (*disable)(struct tegra_xusb_port *port);
	struct tegra_xusb_lane *(*map)(struct tegra_xusb_port *port);
};

/*
 * pad controller
 */
struct tegra_xusb_padctl_soc;

struct tegra_xusb_padctl_ops {
	struct tegra_xusb_padctl *
		(*probe)(struct device *dev,
			 const struct tegra_xusb_padctl_soc *soc);
	void (*remove)(struct tegra_xusb_padctl *padctl);

	int (*usb3_save_context)(struct tegra_xusb_padctl *padctl,
				 unsigned int index);
	int (*hsic_set_idle)(struct tegra_xusb_padctl *padctl,
			     unsigned int index, bool idle);
	int (*usb3_set_lfps_detect)(struct tegra_xusb_padctl *padctl,
				    unsigned int index, bool enable);
};

struct tegra_xusb_padctl_soc {
	const struct tegra_xusb_pad_soc * const *pads;
	unsigned int num_pads;

	struct {
		struct {
			const struct tegra_xusb_port_ops *ops;
			unsigned int count;
		} usb2, ulpi, hsic, usb3;
	} ports;

	const struct tegra_xusb_padctl_ops *ops;

	const char * const *supply_names;
	unsigned int num_supplies;
};

struct tegra_xusb_padctl {
	struct device *dev;
	void __iomem *regs;
	struct mutex lock;
	struct reset_control *rst;

	const struct tegra_xusb_padctl_soc *soc;

	struct tegra_xusb_pad *pcie;
	struct tegra_xusb_pad *sata;
	struct tegra_xusb_pad *ulpi;
	struct tegra_xusb_pad *usb2;
	struct tegra_xusb_pad *hsic;

	struct list_head ports;
	struct list_head lanes;
	struct list_head pads;

	unsigned int enable;

	struct clk *clk;

	struct regulator_bulk_data *supplies;
};

static inline void padctl_writel(struct tegra_xusb_padctl *padctl, u32 value,
				 unsigned long offset)
{
	dev_dbg(padctl->dev, "%08lx < %08x\n", offset, value);
	writel(value, padctl->regs + offset);
}

static inline u32 padctl_readl(struct tegra_xusb_padctl *padctl,
			       unsigned long offset)
{
	u32 value = readl(padctl->regs + offset);
	dev_dbg(padctl->dev, "%08lx > %08x\n", offset, value);
	return value;
}

struct tegra_xusb_lane *tegra_xusb_find_lane(struct tegra_xusb_padctl *padctl,
					     const char *name,
					     unsigned int index);

#if defined(CONFIG_ARCH_TEGRA_124_SOC) || defined(CONFIG_ARCH_TEGRA_132_SOC)
extern const struct tegra_xusb_padctl_soc tegra124_xusb_padctl_soc;
#endif
#if defined(CONFIG_ARCH_TEGRA_210_SOC)
extern const struct tegra_xusb_padctl_soc tegra210_xusb_padctl_soc;
#endif
#if defined(CONFIG_ARCH_TEGRA_186_SOC)
extern const struct tegra_xusb_padctl_soc tegra186_xusb_padctl_soc;
#endif

#endif /* __PHY_TEGRA_XUSB_H */
