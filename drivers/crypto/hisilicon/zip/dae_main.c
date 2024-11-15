// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 HiSilicon Limited. */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/uacce.h>
#include "zip.h"

/* memory */
#define DAE_MEM_START_OFFSET		0x331040
#define DAE_MEM_DONE_OFFSET		0x331044
#define DAE_MEM_START_MASK		0x1
#define DAE_MEM_DONE_MASK		0x1
#define DAE_REG_RD_INTVRL_US		10
#define DAE_REG_RD_TMOUT_US		USEC_PER_SEC

#define DAE_ALG_NAME			"hashagg"

static inline bool dae_is_support(struct hisi_qm *qm)
{
	if (test_bit(QM_SUPPORT_DAE, &qm->caps))
		return true;

	return false;
}

int hisi_dae_set_user_domain(struct hisi_qm *qm)
{
	u32 val;
	int ret;

	if (!dae_is_support(qm))
		return 0;

	val = readl(qm->io_base + DAE_MEM_START_OFFSET);
	val |= DAE_MEM_START_MASK;
	writel(val, qm->io_base + DAE_MEM_START_OFFSET);
	ret = readl_relaxed_poll_timeout(qm->io_base + DAE_MEM_DONE_OFFSET, val,
					 val & DAE_MEM_DONE_MASK,
					 DAE_REG_RD_INTVRL_US, DAE_REG_RD_TMOUT_US);
	if (ret)
		pci_err(qm->pdev, "failed to init dae memory!\n");

	return ret;
}

int hisi_dae_set_alg(struct hisi_qm *qm)
{
	size_t len;

	if (!dae_is_support(qm))
		return 0;

	if (!qm->uacce)
		return 0;

	len = strlen(qm->uacce->algs);
	/* A line break may be required */
	if (len + strlen(DAE_ALG_NAME) + 1 >= QM_DEV_ALG_MAX_LEN) {
		pci_err(qm->pdev, "algorithm name is too long!\n");
		return -EINVAL;
	}

	if (len)
		strcat((char *)qm->uacce->algs, "\n");

	strcat((char *)qm->uacce->algs, DAE_ALG_NAME);

	return 0;
}
