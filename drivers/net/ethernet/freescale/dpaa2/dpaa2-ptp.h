/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018 NXP
 * Copyright 2020 NXP
 */

#ifndef __RTC_H
#define __RTC_H

#include <linux/fsl/ptp_qoriq.h>

#include "dprtc.h"
#include "dprtc-cmd.h"

extern int dpaa2_phc_index;
extern struct ptp_qoriq *dpaa2_ptp;

#endif
