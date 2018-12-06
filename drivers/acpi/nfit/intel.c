// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2018 Intel Corporation. All rights reserved. */
#include <linux/libnvdimm.h>
#include <linux/ndctl.h>
#include <linux/acpi.h>
#include "intel.h"
#include "nfit.h"

static enum nvdimm_security_state intel_security_state(struct nvdimm *nvdimm)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_get_security_state cmd;
	} nd_cmd = {
		.pkg = {
			.nd_command = NVDIMM_INTEL_GET_SECURITY_STATE,
			.nd_family = NVDIMM_FAMILY_INTEL,
			.nd_size_out =
				sizeof(struct nd_intel_get_security_state),
			.nd_fw_size =
				sizeof(struct nd_intel_get_security_state),
		},
	};
	int rc;

	if (!test_bit(NVDIMM_INTEL_GET_SECURITY_STATE, &nfit_mem->dsm_mask))
		return -ENXIO;

	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);
	if (rc < 0)
		return rc;
	if (nd_cmd.cmd.status)
		return -EIO;

	/* check and see if security is enabled and locked */
	if (nd_cmd.cmd.state & ND_INTEL_SEC_STATE_UNSUPPORTED)
		return -ENXIO;
	else if (nd_cmd.cmd.state & ND_INTEL_SEC_STATE_ENABLED) {
		if (nd_cmd.cmd.state & ND_INTEL_SEC_STATE_LOCKED)
			return NVDIMM_SECURITY_LOCKED;
		else if (nd_cmd.cmd.state & ND_INTEL_SEC_STATE_FROZEN ||
				nd_cmd.cmd.state & ND_INTEL_SEC_STATE_PLIMIT)
			return NVDIMM_SECURITY_FROZEN;
		else
			return NVDIMM_SECURITY_UNLOCKED;
	}
	return NVDIMM_SECURITY_DISABLED;
}

static int intel_security_freeze(struct nvdimm *nvdimm)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_freeze_lock cmd;
	} nd_cmd = {
		.pkg = {
			.nd_command = NVDIMM_INTEL_FREEZE_LOCK,
			.nd_family = NVDIMM_FAMILY_INTEL,
			.nd_size_out = ND_INTEL_STATUS_SIZE,
			.nd_fw_size = ND_INTEL_STATUS_SIZE,
		},
	};
	int rc;

	if (!test_bit(NVDIMM_INTEL_FREEZE_LOCK, &nfit_mem->dsm_mask))
		return -ENOTTY;

	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);
	if (rc < 0)
		return rc;
	if (nd_cmd.cmd.status)
		return -EIO;
	return 0;
}

static const struct nvdimm_security_ops __intel_security_ops = {
	.state = intel_security_state,
	.freeze = intel_security_freeze,
};
const struct nvdimm_security_ops *intel_security_ops = &__intel_security_ops;
