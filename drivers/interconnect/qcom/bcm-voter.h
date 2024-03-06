/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef __DRIVERS_INTERCONNECT_QCOM_BCM_VOTER_H__
#define __DRIVERS_INTERCONNECT_QCOM_BCM_VOTER_H__

#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>
#include <soc/qcom/tcs.h>

#include "icc-rpmh.h"

struct bcm_voter *of_bcm_voter_get(struct device *dev, const char *name);
void qcom_icc_bcm_voter_add(struct bcm_voter *voter, struct qcom_icc_bcm *bcm);
int qcom_icc_bcm_voter_commit(struct bcm_voter *voter);

#endif
