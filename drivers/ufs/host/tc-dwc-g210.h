/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Synopsys G210 Test Chip driver
 *
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors: Joao Pinto <jpinto@synopsys.com>
 */

#ifndef _TC_DWC_G210_H
#define _TC_DWC_G210_H

struct ufs_hba;

int tc_dwc_g210_config_40_bit(struct ufs_hba *hba);
int tc_dwc_g210_config_20_bit(struct ufs_hba *hba);

#endif /* End of Header */
