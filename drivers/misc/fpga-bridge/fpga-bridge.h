#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>

#ifndef _LINUX_FPGA_BRIDGE_H
#define _LINUX_FPGA_BRIDGE_H

struct fpga_bridge;

/*---------------------------------------------------------------------------*/

/*
 * fpga_bridge_ops are the low level functions implemented by a specific
 * fpga bridge driver.
 */
struct fpga_bridge_ops {
	/* Returns the FPGA bridge's status */
	int (*enable_show)(struct fpga_bridge *bridge);

	/* Enable a FPGA bridge */
	void (*enable_set)(struct fpga_bridge *bridge, bool enable);

	/* Set FPGA into a specific state during driver remove */
	void (*fpga_bridge_remove)(struct fpga_bridge *bridge);
};

struct fpga_bridge {
	struct device_node *np;
	struct device *parent;
	struct device *dev;
	struct cdev cdev;

	int nr;
	char name[48];
	char label[48];
	unsigned long flags;
	struct fpga_bridge_ops *br_ops;

	void *priv;
};

#if defined(CONFIG_FPGA_BRIDGE) || defined(CONFIG_FPGA_BRIDGE_MODULE)

extern int register_fpga_bridge(struct platform_device *pdev,
				 struct fpga_bridge_ops *br_ops,
				 char *name, void *priv);

extern void remove_fpga_bridge(struct platform_device *pdev);

#endif /* CONFIG_FPGA_BRIDGE */
#endif /* _LINUX_FPGA_BRIDGE_H */
