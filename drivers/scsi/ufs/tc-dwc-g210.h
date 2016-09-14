/*
 * Synopsys G210 Test Chip driver
 *
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors: Joao Pinto <jpinto@synopsys.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _TC_DWC_G210_H
#define _TC_DWC_G210_H

int tc_dwc_g210_config_40_bit(struct ufs_hba *hba);
int tc_dwc_g210_config_20_bit(struct ufs_hba *hba);

#endif /* End of Header */
