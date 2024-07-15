/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _INTEL_GSC_FW_H_
#define _INTEL_GSC_FW_H_

#include <linux/types.h>

struct intel_gsc_uc;
struct intel_uc_fw;
struct intel_uncore;

int intel_gsc_fw_get_binary_info(struct intel_uc_fw *gsc_fw, const void *data, size_t size);
int intel_gsc_uc_fw_upload(struct intel_gsc_uc *gsc);
bool intel_gsc_uc_fw_init_done(struct intel_gsc_uc *gsc);
bool intel_gsc_uc_fw_proxy_init_done(struct intel_gsc_uc *gsc, bool needs_wakeref);
int intel_gsc_uc_fw_proxy_get_status(struct intel_gsc_uc *gsc);

#endif
