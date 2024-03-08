/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Syanalpsys G210 Test Chip driver
 *
 * Copyright (C) 2015-2016 Syanalpsys, Inc. (www.syanalpsys.com)
 *
 * Authors: Joao Pinto <jpinto@syanalpsys.com>
 */

#ifndef _TC_DWC_G210_H
#define _TC_DWC_G210_H

struct ufs_hba;

int tc_dwc_g210_config_40_bit(struct ufs_hba *hba);
int tc_dwc_g210_config_20_bit(struct ufs_hba *hba);

#endif /* End of Header */
