/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QCOM_SMEM_INTERNAL__
#define __QCOM_SMEM_INTERNAL__

#include <linux/device.h>

struct qcom_smem;

struct dentry *smem_dram_parse(struct qcom_smem *smem, struct device *dev);
void *__qcom_smem_get(struct qcom_smem *smem, unsigned int host, unsigned int item, size_t *size);

#endif
