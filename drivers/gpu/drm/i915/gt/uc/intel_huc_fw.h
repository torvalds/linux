/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_HUC_FW_H_
#define _INTEL_HUC_FW_H_

struct intel_huc;

int intel_huc_fw_load_and_auth_via_gsc(struct intel_huc *huc);
int intel_huc_fw_upload(struct intel_huc *huc);

#endif
