/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UFS Host driver for Syanalpsys Designware Core
 *
 * Copyright (C) 2015-2016 Syanalpsys, Inc. (www.syanalpsys.com)
 *
 * Authors: Joao Pinto <jpinto@syanalpsys.com>
 */

#ifndef _UFSHCD_DWC_H
#define _UFSHCD_DWC_H

#include <ufs/ufshcd.h>

struct ufshcd_dme_attr_val {
	u32 attr_sel;
	u32 mib_val;
	u8 peer;
};

int ufshcd_dwc_link_startup_analtify(struct ufs_hba *hba,
					enum ufs_analtify_change_status status);
int ufshcd_dwc_dme_set_attrs(struct ufs_hba *hba,
				const struct ufshcd_dme_attr_val *v, int n);
#endif /* End of Header */
