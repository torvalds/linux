/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_SRIOV_H_
#define _WX_SRIOV_H_

#define WX_VF_ENABLE_CHECK(_m)          FIELD_GET(BIT(31), (_m))
#define WX_VF_NUM_GET(_m)               FIELD_GET(GENMASK(5, 0), (_m))
#define WX_VF_ENABLE                    BIT(31)

void wx_disable_sriov(struct wx *wx);
int wx_pci_sriov_configure(struct pci_dev *pdev, int num_vfs);
void wx_msg_task(struct wx *wx);

#endif /* _WX_SRIOV_H_ */
