// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Quad GMSL Deserializer debugfs helper functions
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include <linux/export.h>
#include <linux/debugfs.h>

#include "maxim4c_api.h"

int maxim4c_dbgfs_init(maxim4c_t *maxim4c)
{
	struct dentry *entry;

	entry = debugfs_create_dir("maxim4c", NULL);

	debugfs_create_u8("timing_override_en", 0600, entry,
			  &maxim4c->mipi_txphy.timing_override_en);

	debugfs_create_u8("t_hs_przero", 0600, entry,
			  &maxim4c->mipi_txphy.timing.t_hs_przero);
	debugfs_create_u8("t_hs_prep", 0600, entry,
			  &maxim4c->mipi_txphy.timing.t_hs_prep);
	debugfs_create_u8("t_clk_trail", 0600, entry,
			  &maxim4c->mipi_txphy.timing.t_clk_trail);
	debugfs_create_u8("t_clk_przero", 0600, entry,
			  &maxim4c->mipi_txphy.timing.t_clk_przero);
	debugfs_create_u8("t_lpx", 0600, entry, &maxim4c->mipi_txphy.timing.t_lpx);
	debugfs_create_u8("t_hs_trail", 0600, entry,
			  &maxim4c->mipi_txphy.timing.t_hs_trail);

	debugfs_create_u8("t_clk_prep", 0600, entry,
			  &maxim4c->mipi_txphy.timing.t_clk_prep);
	debugfs_create_u8("t_lpxesc", 0600, entry,
			  &maxim4c->mipi_txphy.timing.t_lpxesc);

	debugfs_create_u8("csi2_t_pre", 0600, entry,
			  &maxim4c->mipi_txphy.timing.csi2_t_pre);
	debugfs_create_u8("csi2_t_post", 0600, entry,
			  &maxim4c->mipi_txphy.timing.csi2_t_post);
	debugfs_create_u8("csi2_tx_gap", 0600, entry,
			  &maxim4c->mipi_txphy.timing.csi2_tx_gap);
	debugfs_create_u32("csi2_twakeup", 0600, entry,
			  &maxim4c->mipi_txphy.timing.csi2_twakeup);

	maxim4c->dbgfs_root = entry;

	return 0;
}
EXPORT_SYMBOL(maxim4c_dbgfs_init);

void maxim4c_dbgfs_deinit(maxim4c_t *maxim4c)
{
	debugfs_remove_recursive(maxim4c->dbgfs_root);
}
EXPORT_SYMBOL(maxim4c_dbgfs_deinit);
