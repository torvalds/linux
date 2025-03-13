/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef __HBG_DEBUGFS_H
#define __HBG_DEBUGFS_H

void hbg_debugfs_register(void);
void hbg_debugfs_unregister(void);

void hbg_debugfs_init(struct hbg_priv *priv);

#endif
