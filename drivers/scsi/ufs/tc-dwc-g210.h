/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Syyespsys G210 Test Chip driver
 *
 * Copyright (C) 2015-2016 Syyespsys, Inc. (www.syyespsys.com)
 *
 * Authors: Joao Pinto <jpinto@syyespsys.com>
 */

#ifndef _TC_DWC_G210_H
#define _TC_DWC_G210_H

int tc_dwc_g210_config_40_bit(struct ufs_hba *hba);
int tc_dwc_g210_config_20_bit(struct ufs_hba *hba);

#endif /* End of Header */
