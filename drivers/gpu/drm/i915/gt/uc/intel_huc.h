/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_HUC_H_
#define _INTEL_HUC_H_

#include "i915_reg_defs.h"
#include "i915_sw_fence.h"
#include "intel_uc_fw.h"
#include "intel_huc_fw.h"

#include <linux/notifier.h>
#include <linux/hrtimer.h>

struct bus_type;
struct i915_vma;

enum intel_huc_delayed_load_status {
	INTEL_HUC_WAITING_ON_GSC = 0,
	INTEL_HUC_WAITING_ON_PXP,
	INTEL_HUC_DELAYED_LOAD_ERROR,
};

enum intel_huc_authentication_type {
	INTEL_HUC_AUTH_BY_GUC = 0,
	INTEL_HUC_AUTH_BY_GSC,
	INTEL_HUC_AUTH_MAX_MODES
};

struct intel_huc {
	/* Generic uC firmware management */
	struct intel_uc_fw fw;

	/* HuC-specific additions */
	struct {
		i915_reg_t reg;
		u32 mask;
		u32 value;
	} status[INTEL_HUC_AUTH_MAX_MODES];

	struct {
		struct i915_sw_fence fence;
		struct hrtimer timer;
		struct notifier_block nb;
		enum intel_huc_delayed_load_status status;
	} delayed_load;

	/* for load via GSCCS */
	struct i915_vma *heci_pkt;

	bool loaded_via_gsc;
};

int intel_huc_sanitize(struct intel_huc *huc);
void intel_huc_init_early(struct intel_huc *huc);
int intel_huc_init(struct intel_huc *huc);
void intel_huc_fini(struct intel_huc *huc);
int intel_huc_auth(struct intel_huc *huc, enum intel_huc_authentication_type type);
int intel_huc_wait_for_auth_complete(struct intel_huc *huc,
				     enum intel_huc_authentication_type type);
bool intel_huc_is_authenticated(struct intel_huc *huc,
				enum intel_huc_authentication_type type);
int intel_huc_check_status(struct intel_huc *huc);
void intel_huc_update_auth_status(struct intel_huc *huc);

void intel_huc_register_gsc_notifier(struct intel_huc *huc, const struct bus_type *bus);
void intel_huc_unregister_gsc_notifier(struct intel_huc *huc, const struct bus_type *bus);

static inline bool intel_huc_is_supported(struct intel_huc *huc)
{
	return intel_uc_fw_is_supported(&huc->fw);
}

static inline bool intel_huc_is_wanted(struct intel_huc *huc)
{
	return intel_uc_fw_is_enabled(&huc->fw);
}

static inline bool intel_huc_is_used(struct intel_huc *huc)
{
	GEM_BUG_ON(__intel_uc_fw_status(&huc->fw) == INTEL_UC_FIRMWARE_SELECTED);
	return intel_uc_fw_is_available(&huc->fw);
}

static inline bool intel_huc_is_loaded_by_gsc(const struct intel_huc *huc)
{
	return huc->loaded_via_gsc;
}

static inline bool intel_huc_wait_required(struct intel_huc *huc)
{
	return intel_huc_is_used(huc) && intel_huc_is_loaded_by_gsc(huc) &&
	       !intel_huc_is_authenticated(huc, INTEL_HUC_AUTH_BY_GSC);
}

void intel_huc_load_status(struct intel_huc *huc, struct drm_printer *p);

#endif
