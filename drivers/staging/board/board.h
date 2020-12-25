/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BOARD_H__
#define __BOARD_H__

#include <linux/init.h>
#include <linux/of.h>

struct board_staging_clk {
	const char *clk;
	const char *con_id;
	const char *dev_id;
};

struct board_staging_dev {
	/* Platform Device */
	struct platform_device *pdev;
	/* Clocks (optional) */
	const struct board_staging_clk *clocks;
	unsigned int nclocks;
	/* Generic PM Domain (optional) */
	const char *domain;
};

struct resource;

bool board_staging_dt_node_available(const struct resource *resource,
				     unsigned int num_resources);
int board_staging_gic_setup_xlate(const char *gic_match, unsigned int base);
void board_staging_gic_fixup_resources(struct resource *res, unsigned int nres);
int board_staging_register_clock(const struct board_staging_clk *bsc);
int board_staging_register_device(const struct board_staging_dev *dev);
void board_staging_register_devices(const struct board_staging_dev *devs,
				    unsigned int ndevs);

#endif /* __BOARD_H__ */
