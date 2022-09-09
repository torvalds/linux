// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2018 Intel Corporation. All rights reserved. */
#include <linux/libnvdimm.h>
#include <linux/ndctl.h>
#include <linux/acpi.h>
#include <asm/smp.h>
#include "intel.h"
#include "nfit.h"

static ssize_t firmware_activate_noidle_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc = to_nd_desc(nvdimm_bus);
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);

	return sprintf(buf, "%s\n", acpi_desc->fwa_noidle ? "Y" : "N");
}

static ssize_t firmware_activate_noidle_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc = to_nd_desc(nvdimm_bus);
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);
	ssize_t rc;
	bool val;

	rc = kstrtobool(buf, &val);
	if (rc)
		return rc;
	if (val != acpi_desc->fwa_noidle)
		acpi_desc->fwa_cap = NVDIMM_FWA_CAP_INVALID;
	acpi_desc->fwa_noidle = val;
	return size;
}
DEVICE_ATTR_RW(firmware_activate_noidle);

bool intel_fwa_supported(struct nvdimm_bus *nvdimm_bus)
{
	struct nvdimm_bus_descriptor *nd_desc = to_nd_desc(nvdimm_bus);
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);
	unsigned long *mask;

	if (!test_bit(NVDIMM_BUS_FAMILY_INTEL, &nd_desc->bus_family_mask))
		return false;

	mask = &acpi_desc->family_dsm_mask[NVDIMM_BUS_FAMILY_INTEL];
	return *mask == NVDIMM_BUS_INTEL_FW_ACTIVATE_CMDMASK;
}

static unsigned long intel_security_flags(struct nvdimm *nvdimm,
		enum nvdimm_passphrase_type ptype)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	unsigned long security_flags = 0;
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
		return 0;

	/*
	 * Short circuit the state retrieval while we are doing overwrite.
	 * The DSM spec states that the security state is indeterminate
	 * until the overwrite DSM completes.
	 */
	if (nvdimm_in_overwrite(nvdimm) && ptype == NVDIMM_USER)
		return BIT(NVDIMM_SECURITY_OVERWRITE);

	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);
	if (rc < 0 || nd_cmd.cmd.status) {
		pr_err("%s: security state retrieval failed (%d:%#x)\n",
				nvdimm_name(nvdimm), rc, nd_cmd.cmd.status);
		return 0;
	}

	/* check and see if security is enabled and locked */
	if (ptype == NVDIMM_MASTER) {
		if (nd_cmd.cmd.extended_state & ND_INTEL_SEC_ESTATE_ENABLED)
			set_bit(NVDIMM_SECURITY_UNLOCKED, &security_flags);
		else
			set_bit(NVDIMM_SECURITY_DISABLED, &security_flags);
		if (nd_cmd.cmd.extended_state & ND_INTEL_SEC_ESTATE_PLIMIT)
			set_bit(NVDIMM_SECURITY_FROZEN, &security_flags);
		return security_flags;
	}

	if (nd_cmd.cmd.state & ND_INTEL_SEC_STATE_UNSUPPORTED)
		return 0;

	if (nd_cmd.cmd.state & ND_INTEL_SEC_STATE_ENABLED) {
		if (nd_cmd.cmd.state & ND_INTEL_SEC_STATE_FROZEN ||
		    nd_cmd.cmd.state & ND_INTEL_SEC_STATE_PLIMIT)
			set_bit(NVDIMM_SECURITY_FROZEN, &security_flags);

		if (nd_cmd.cmd.state & ND_INTEL_SEC_STATE_LOCKED)
			set_bit(NVDIMM_SECURITY_LOCKED, &security_flags);
		else
			set_bit(NVDIMM_SECURITY_UNLOCKED, &security_flags);
	} else
		set_bit(NVDIMM_SECURITY_DISABLED, &security_flags);

	return security_flags;
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

static int intel_security_change_key(struct nvdimm *nvdimm,
		const struct nvdimm_key_data *old_data,
		const struct nvdimm_key_data *new_data,
		enum nvdimm_passphrase_type ptype)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	unsigned int cmd = ptype == NVDIMM_MASTER ?
		NVDIMM_INTEL_SET_MASTER_PASSPHRASE :
		NVDIMM_INTEL_SET_PASSPHRASE;
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_set_passphrase cmd;
	} nd_cmd = {
		.pkg = {
			.nd_family = NVDIMM_FAMILY_INTEL,
			.nd_size_in = ND_INTEL_PASSPHRASE_SIZE * 2,
			.nd_size_out = ND_INTEL_STATUS_SIZE,
			.nd_fw_size = ND_INTEL_STATUS_SIZE,
			.nd_command = cmd,
		},
	};
	int rc;

	if (!test_bit(cmd, &nfit_mem->dsm_mask))
		return -ENOTTY;

	memcpy(nd_cmd.cmd.old_pass, old_data->data,
			sizeof(nd_cmd.cmd.old_pass));
	memcpy(nd_cmd.cmd.new_pass, new_data->data,
			sizeof(nd_cmd.cmd.new_pass));
	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);
	if (rc < 0)
		return rc;

	switch (nd_cmd.cmd.status) {
	case 0:
		return 0;
	case ND_INTEL_STATUS_INVALID_PASS:
		return -EINVAL;
	case ND_INTEL_STATUS_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case ND_INTEL_STATUS_INVALID_STATE:
	default:
		return -EIO;
	}
}

static void nvdimm_invalidate_cache(void);

static int __maybe_unused intel_security_unlock(struct nvdimm *nvdimm,
		const struct nvdimm_key_data *key_data)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_unlock_unit cmd;
	} nd_cmd = {
		.pkg = {
			.nd_command = NVDIMM_INTEL_UNLOCK_UNIT,
			.nd_family = NVDIMM_FAMILY_INTEL,
			.nd_size_in = ND_INTEL_PASSPHRASE_SIZE,
			.nd_size_out = ND_INTEL_STATUS_SIZE,
			.nd_fw_size = ND_INTEL_STATUS_SIZE,
		},
	};
	int rc;

	if (!test_bit(NVDIMM_INTEL_UNLOCK_UNIT, &nfit_mem->dsm_mask))
		return -ENOTTY;

	memcpy(nd_cmd.cmd.passphrase, key_data->data,
			sizeof(nd_cmd.cmd.passphrase));
	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);
	if (rc < 0)
		return rc;
	switch (nd_cmd.cmd.status) {
	case 0:
		break;
	case ND_INTEL_STATUS_INVALID_PASS:
		return -EINVAL;
	default:
		return -EIO;
	}

	/* DIMM unlocked, invalidate all CPU caches before we read it */
	nvdimm_invalidate_cache();

	return 0;
}

static int intel_security_disable(struct nvdimm *nvdimm,
		const struct nvdimm_key_data *key_data)
{
	int rc;
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_disable_passphrase cmd;
	} nd_cmd = {
		.pkg = {
			.nd_command = NVDIMM_INTEL_DISABLE_PASSPHRASE,
			.nd_family = NVDIMM_FAMILY_INTEL,
			.nd_size_in = ND_INTEL_PASSPHRASE_SIZE,
			.nd_size_out = ND_INTEL_STATUS_SIZE,
			.nd_fw_size = ND_INTEL_STATUS_SIZE,
		},
	};

	if (!test_bit(NVDIMM_INTEL_DISABLE_PASSPHRASE, &nfit_mem->dsm_mask))
		return -ENOTTY;

	memcpy(nd_cmd.cmd.passphrase, key_data->data,
			sizeof(nd_cmd.cmd.passphrase));
	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);
	if (rc < 0)
		return rc;

	switch (nd_cmd.cmd.status) {
	case 0:
		break;
	case ND_INTEL_STATUS_INVALID_PASS:
		return -EINVAL;
	case ND_INTEL_STATUS_INVALID_STATE:
	default:
		return -ENXIO;
	}

	return 0;
}

static int __maybe_unused intel_security_erase(struct nvdimm *nvdimm,
		const struct nvdimm_key_data *key,
		enum nvdimm_passphrase_type ptype)
{
	int rc;
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	unsigned int cmd = ptype == NVDIMM_MASTER ?
		NVDIMM_INTEL_MASTER_SECURE_ERASE : NVDIMM_INTEL_SECURE_ERASE;
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_secure_erase cmd;
	} nd_cmd = {
		.pkg = {
			.nd_family = NVDIMM_FAMILY_INTEL,
			.nd_size_in = ND_INTEL_PASSPHRASE_SIZE,
			.nd_size_out = ND_INTEL_STATUS_SIZE,
			.nd_fw_size = ND_INTEL_STATUS_SIZE,
			.nd_command = cmd,
		},
	};

	if (!test_bit(cmd, &nfit_mem->dsm_mask))
		return -ENOTTY;

	/* flush all cache before we erase DIMM */
	nvdimm_invalidate_cache();
	memcpy(nd_cmd.cmd.passphrase, key->data,
			sizeof(nd_cmd.cmd.passphrase));
	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);
	if (rc < 0)
		return rc;

	switch (nd_cmd.cmd.status) {
	case 0:
		break;
	case ND_INTEL_STATUS_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case ND_INTEL_STATUS_INVALID_PASS:
		return -EINVAL;
	case ND_INTEL_STATUS_INVALID_STATE:
	default:
		return -ENXIO;
	}

	/* DIMM erased, invalidate all CPU caches before we read it */
	nvdimm_invalidate_cache();
	return 0;
}

static int __maybe_unused intel_security_query_overwrite(struct nvdimm *nvdimm)
{
	int rc;
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_query_overwrite cmd;
	} nd_cmd = {
		.pkg = {
			.nd_command = NVDIMM_INTEL_QUERY_OVERWRITE,
			.nd_family = NVDIMM_FAMILY_INTEL,
			.nd_size_out = ND_INTEL_STATUS_SIZE,
			.nd_fw_size = ND_INTEL_STATUS_SIZE,
		},
	};

	if (!test_bit(NVDIMM_INTEL_QUERY_OVERWRITE, &nfit_mem->dsm_mask))
		return -ENOTTY;

	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);
	if (rc < 0)
		return rc;

	switch (nd_cmd.cmd.status) {
	case 0:
		break;
	case ND_INTEL_STATUS_OQUERY_INPROGRESS:
		return -EBUSY;
	default:
		return -ENXIO;
	}

	/* flush all cache before we make the nvdimms available */
	nvdimm_invalidate_cache();
	return 0;
}

static int __maybe_unused intel_security_overwrite(struct nvdimm *nvdimm,
		const struct nvdimm_key_data *nkey)
{
	int rc;
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_overwrite cmd;
	} nd_cmd = {
		.pkg = {
			.nd_command = NVDIMM_INTEL_OVERWRITE,
			.nd_family = NVDIMM_FAMILY_INTEL,
			.nd_size_in = ND_INTEL_PASSPHRASE_SIZE,
			.nd_size_out = ND_INTEL_STATUS_SIZE,
			.nd_fw_size = ND_INTEL_STATUS_SIZE,
		},
	};

	if (!test_bit(NVDIMM_INTEL_OVERWRITE, &nfit_mem->dsm_mask))
		return -ENOTTY;

	/* flush all cache before we erase DIMM */
	nvdimm_invalidate_cache();
	memcpy(nd_cmd.cmd.passphrase, nkey->data,
			sizeof(nd_cmd.cmd.passphrase));
	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);
	if (rc < 0)
		return rc;

	switch (nd_cmd.cmd.status) {
	case 0:
		return 0;
	case ND_INTEL_STATUS_OVERWRITE_UNSUPPORTED:
		return -ENOTSUPP;
	case ND_INTEL_STATUS_INVALID_PASS:
		return -EINVAL;
	case ND_INTEL_STATUS_INVALID_STATE:
	default:
		return -ENXIO;
	}
}

/*
 * TODO: define a cross arch wbinvd equivalent when/if
 * NVDIMM_FAMILY_INTEL command support arrives on another arch.
 */
#ifdef CONFIG_X86
static void nvdimm_invalidate_cache(void)
{
	wbinvd_on_all_cpus();
}
#else
static void nvdimm_invalidate_cache(void)
{
	WARN_ON_ONCE("cache invalidation required after unlock\n");
}
#endif

static const struct nvdimm_security_ops __intel_security_ops = {
	.get_flags = intel_security_flags,
	.freeze = intel_security_freeze,
	.change_key = intel_security_change_key,
	.disable = intel_security_disable,
#ifdef CONFIG_X86
	.unlock = intel_security_unlock,
	.erase = intel_security_erase,
	.overwrite = intel_security_overwrite,
	.query_overwrite = intel_security_query_overwrite,
#endif
};

const struct nvdimm_security_ops *intel_security_ops = &__intel_security_ops;

static int intel_bus_fwa_businfo(struct nvdimm_bus_descriptor *nd_desc,
		struct nd_intel_bus_fw_activate_businfo *info)
{
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_bus_fw_activate_businfo cmd;
	} nd_cmd = {
		.pkg = {
			.nd_command = NVDIMM_BUS_INTEL_FW_ACTIVATE_BUSINFO,
			.nd_family = NVDIMM_BUS_FAMILY_INTEL,
			.nd_size_out =
				sizeof(struct nd_intel_bus_fw_activate_businfo),
			.nd_fw_size =
				sizeof(struct nd_intel_bus_fw_activate_businfo),
		},
	};
	int rc;

	rc = nd_desc->ndctl(nd_desc, NULL, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd),
			NULL);
	*info = nd_cmd.cmd;
	return rc;
}

/* The fw_ops expect to be called with the nvdimm_bus_lock() held */
static enum nvdimm_fwa_state intel_bus_fwa_state(
		struct nvdimm_bus_descriptor *nd_desc)
{
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);
	struct nd_intel_bus_fw_activate_businfo info;
	struct device *dev = acpi_desc->dev;
	enum nvdimm_fwa_state state;
	int rc;

	/*
	 * It should not be possible for platform firmware to return
	 * busy because activate is a synchronous operation. Treat it
	 * similar to invalid, i.e. always refresh / poll the status.
	 */
	switch (acpi_desc->fwa_state) {
	case NVDIMM_FWA_INVALID:
	case NVDIMM_FWA_BUSY:
		break;
	default:
		/* check if capability needs to be refreshed */
		if (acpi_desc->fwa_cap == NVDIMM_FWA_CAP_INVALID)
			break;
		return acpi_desc->fwa_state;
	}

	/* Refresh with platform firmware */
	rc = intel_bus_fwa_businfo(nd_desc, &info);
	if (rc)
		return NVDIMM_FWA_INVALID;

	switch (info.state) {
	case ND_INTEL_FWA_IDLE:
		state = NVDIMM_FWA_IDLE;
		break;
	case ND_INTEL_FWA_BUSY:
		state = NVDIMM_FWA_BUSY;
		break;
	case ND_INTEL_FWA_ARMED:
		if (info.activate_tmo > info.max_quiesce_tmo)
			state = NVDIMM_FWA_ARM_OVERFLOW;
		else
			state = NVDIMM_FWA_ARMED;
		break;
	default:
		dev_err_once(dev, "invalid firmware activate state %d\n",
				info.state);
		return NVDIMM_FWA_INVALID;
	}

	/*
	 * Capability data is available in the same payload as state. It
	 * is expected to be static.
	 */
	if (acpi_desc->fwa_cap == NVDIMM_FWA_CAP_INVALID) {
		if (info.capability & ND_INTEL_BUS_FWA_CAP_FWQUIESCE)
			acpi_desc->fwa_cap = NVDIMM_FWA_CAP_QUIESCE;
		else if (info.capability & ND_INTEL_BUS_FWA_CAP_OSQUIESCE) {
			/*
			 * Skip hibernate cycle by default if platform
			 * indicates that it does not need devices to be
			 * quiesced.
			 */
			acpi_desc->fwa_cap = NVDIMM_FWA_CAP_LIVE;
		} else
			acpi_desc->fwa_cap = NVDIMM_FWA_CAP_NONE;
	}

	acpi_desc->fwa_state = state;

	return state;
}

static enum nvdimm_fwa_capability intel_bus_fwa_capability(
		struct nvdimm_bus_descriptor *nd_desc)
{
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);

	if (acpi_desc->fwa_cap > NVDIMM_FWA_CAP_INVALID)
		return acpi_desc->fwa_cap;

	if (intel_bus_fwa_state(nd_desc) > NVDIMM_FWA_INVALID)
		return acpi_desc->fwa_cap;

	return NVDIMM_FWA_CAP_INVALID;
}

static int intel_bus_fwa_activate(struct nvdimm_bus_descriptor *nd_desc)
{
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_bus_fw_activate cmd;
	} nd_cmd = {
		.pkg = {
			.nd_command = NVDIMM_BUS_INTEL_FW_ACTIVATE,
			.nd_family = NVDIMM_BUS_FAMILY_INTEL,
			.nd_size_in = sizeof(nd_cmd.cmd.iodev_state),
			.nd_size_out =
				sizeof(struct nd_intel_bus_fw_activate),
			.nd_fw_size =
				sizeof(struct nd_intel_bus_fw_activate),
		},
		/*
		 * Even though activate is run from a suspended context,
		 * for safety, still ask platform firmware to force
		 * quiesce devices by default. Let a module
		 * parameter override that policy.
		 */
		.cmd = {
			.iodev_state = acpi_desc->fwa_noidle
				? ND_INTEL_BUS_FWA_IODEV_OS_IDLE
				: ND_INTEL_BUS_FWA_IODEV_FORCE_IDLE,
		},
	};
	int rc;

	switch (intel_bus_fwa_state(nd_desc)) {
	case NVDIMM_FWA_ARMED:
	case NVDIMM_FWA_ARM_OVERFLOW:
		break;
	default:
		return -ENXIO;
	}

	rc = nd_desc->ndctl(nd_desc, NULL, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd),
			NULL);

	/*
	 * Whether the command succeeded, or failed, the agent checking
	 * for the result needs to query the DIMMs individually.
	 * Increment the activation count to invalidate all the DIMM
	 * states at once (it's otherwise not possible to take
	 * acpi_desc->init_mutex in this context)
	 */
	acpi_desc->fwa_state = NVDIMM_FWA_INVALID;
	acpi_desc->fwa_count++;

	dev_dbg(acpi_desc->dev, "result: %d\n", rc);

	return rc;
}

static const struct nvdimm_bus_fw_ops __intel_bus_fw_ops = {
	.activate_state = intel_bus_fwa_state,
	.capability = intel_bus_fwa_capability,
	.activate = intel_bus_fwa_activate,
};

const struct nvdimm_bus_fw_ops *intel_bus_fw_ops = &__intel_bus_fw_ops;

static int intel_fwa_dimminfo(struct nvdimm *nvdimm,
		struct nd_intel_fw_activate_dimminfo *info)
{
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_fw_activate_dimminfo cmd;
	} nd_cmd = {
		.pkg = {
			.nd_command = NVDIMM_INTEL_FW_ACTIVATE_DIMMINFO,
			.nd_family = NVDIMM_FAMILY_INTEL,
			.nd_size_out =
				sizeof(struct nd_intel_fw_activate_dimminfo),
			.nd_fw_size =
				sizeof(struct nd_intel_fw_activate_dimminfo),
		},
	};
	int rc;

	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);
	*info = nd_cmd.cmd;
	return rc;
}

static enum nvdimm_fwa_state intel_fwa_state(struct nvdimm *nvdimm)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	struct acpi_nfit_desc *acpi_desc = nfit_mem->acpi_desc;
	struct nd_intel_fw_activate_dimminfo info;
	int rc;

	/*
	 * Similar to the bus state, since activate is synchronous the
	 * busy state should resolve within the context of 'activate'.
	 */
	switch (nfit_mem->fwa_state) {
	case NVDIMM_FWA_INVALID:
	case NVDIMM_FWA_BUSY:
		break;
	default:
		/* If no activations occurred the old state is still valid */
		if (nfit_mem->fwa_count == acpi_desc->fwa_count)
			return nfit_mem->fwa_state;
	}

	rc = intel_fwa_dimminfo(nvdimm, &info);
	if (rc)
		return NVDIMM_FWA_INVALID;

	switch (info.state) {
	case ND_INTEL_FWA_IDLE:
		nfit_mem->fwa_state = NVDIMM_FWA_IDLE;
		break;
	case ND_INTEL_FWA_BUSY:
		nfit_mem->fwa_state = NVDIMM_FWA_BUSY;
		break;
	case ND_INTEL_FWA_ARMED:
		nfit_mem->fwa_state = NVDIMM_FWA_ARMED;
		break;
	default:
		nfit_mem->fwa_state = NVDIMM_FWA_INVALID;
		break;
	}

	switch (info.result) {
	case ND_INTEL_DIMM_FWA_NONE:
		nfit_mem->fwa_result = NVDIMM_FWA_RESULT_NONE;
		break;
	case ND_INTEL_DIMM_FWA_SUCCESS:
		nfit_mem->fwa_result = NVDIMM_FWA_RESULT_SUCCESS;
		break;
	case ND_INTEL_DIMM_FWA_NOTSTAGED:
		nfit_mem->fwa_result = NVDIMM_FWA_RESULT_NOTSTAGED;
		break;
	case ND_INTEL_DIMM_FWA_NEEDRESET:
		nfit_mem->fwa_result = NVDIMM_FWA_RESULT_NEEDRESET;
		break;
	case ND_INTEL_DIMM_FWA_MEDIAFAILED:
	case ND_INTEL_DIMM_FWA_ABORT:
	case ND_INTEL_DIMM_FWA_NOTSUPP:
	case ND_INTEL_DIMM_FWA_ERROR:
	default:
		nfit_mem->fwa_result = NVDIMM_FWA_RESULT_FAIL;
		break;
	}

	nfit_mem->fwa_count = acpi_desc->fwa_count;

	return nfit_mem->fwa_state;
}

static enum nvdimm_fwa_result intel_fwa_result(struct nvdimm *nvdimm)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	struct acpi_nfit_desc *acpi_desc = nfit_mem->acpi_desc;

	if (nfit_mem->fwa_count == acpi_desc->fwa_count
			&& nfit_mem->fwa_result > NVDIMM_FWA_RESULT_INVALID)
		return nfit_mem->fwa_result;

	if (intel_fwa_state(nvdimm) > NVDIMM_FWA_INVALID)
		return nfit_mem->fwa_result;

	return NVDIMM_FWA_RESULT_INVALID;
}

static int intel_fwa_arm(struct nvdimm *nvdimm, enum nvdimm_fwa_trigger arm)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	struct acpi_nfit_desc *acpi_desc = nfit_mem->acpi_desc;
	struct {
		struct nd_cmd_pkg pkg;
		struct nd_intel_fw_activate_arm cmd;
	} nd_cmd = {
		.pkg = {
			.nd_command = NVDIMM_INTEL_FW_ACTIVATE_ARM,
			.nd_family = NVDIMM_FAMILY_INTEL,
			.nd_size_in = sizeof(nd_cmd.cmd.activate_arm),
			.nd_size_out =
				sizeof(struct nd_intel_fw_activate_arm),
			.nd_fw_size =
				sizeof(struct nd_intel_fw_activate_arm),
		},
		.cmd = {
			.activate_arm = arm == NVDIMM_FWA_ARM
				? ND_INTEL_DIMM_FWA_ARM
				: ND_INTEL_DIMM_FWA_DISARM,
		},
	};
	int rc;

	switch (intel_fwa_state(nvdimm)) {
	case NVDIMM_FWA_INVALID:
		return -ENXIO;
	case NVDIMM_FWA_BUSY:
		return -EBUSY;
	case NVDIMM_FWA_IDLE:
		if (arm == NVDIMM_FWA_DISARM)
			return 0;
		break;
	case NVDIMM_FWA_ARMED:
		if (arm == NVDIMM_FWA_ARM)
			return 0;
		break;
	default:
		return -ENXIO;
	}

	/*
	 * Invalidate the bus-level state, now that we're committed to
	 * changing the 'arm' state.
	 */
	acpi_desc->fwa_state = NVDIMM_FWA_INVALID;
	nfit_mem->fwa_state = NVDIMM_FWA_INVALID;

	rc = nvdimm_ctl(nvdimm, ND_CMD_CALL, &nd_cmd, sizeof(nd_cmd), NULL);

	dev_dbg(acpi_desc->dev, "%s result: %d\n", arm == NVDIMM_FWA_ARM
			? "arm" : "disarm", rc);
	return rc;
}

static const struct nvdimm_fw_ops __intel_fw_ops = {
	.activate_state = intel_fwa_state,
	.activate_result = intel_fwa_result,
	.arm = intel_fwa_arm,
};

const struct nvdimm_fw_ops *intel_fw_ops = &__intel_fw_ops;
