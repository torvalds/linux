/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _INTEL_GSC_FW_H_
#define _INTEL_GSC_FW_H_

#include <linux/types.h>

struct intel_gsc_uc;
struct intel_uncore;

int intel_gsc_uc_fw_upload(struct intel_gsc_uc *gsc);
bool intel_gsc_uc_fw_init_done(struct intel_gsc_uc *gsc);

#endif
