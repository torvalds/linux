// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for AMD network controllers and boards
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "efx_devlink.h"
#include <linux/rtc.h>
#include "mcdi.h"
#include "mcdi_functions.h"
#include "mcdi_pcol.h"

struct efx_devlink {
	struct efx_nic *efx;
};

static int efx_devlink_info_nvram_partition(struct efx_nic *efx,
					    struct devlink_info_req *req,
					    unsigned int partition_type,
					    const char *version_name)
{
	char buf[EFX_MAX_VERSION_INFO_LEN];
	u16 version[4];
	int rc;

	rc = efx_mcdi_nvram_metadata(efx, partition_type, NULL, version, NULL,
				     0);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "mcdi nvram %s: failed\n",
			  version_name);
		return rc;
	}

	snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u", version[0],
		 version[1], version[2], version[3]);
	devlink_info_version_stored_put(req, version_name, buf);

	return 0;
}

static int efx_devlink_info_stored_versions(struct efx_nic *efx,
					    struct devlink_info_req *req)
{
	int rc;

	rc = efx_devlink_info_nvram_partition(efx, req,
					      NVRAM_PARTITION_TYPE_BUNDLE,
					      DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID);
	if (rc)
		return rc;

	rc = efx_devlink_info_nvram_partition(efx, req,
					      NVRAM_PARTITION_TYPE_MC_FIRMWARE,
					      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT);
	if (rc)
		return rc;

	rc = efx_devlink_info_nvram_partition(efx, req,
					      NVRAM_PARTITION_TYPE_SUC_FIRMWARE,
					      EFX_DEVLINK_INFO_VERSION_FW_MGMT_SUC);
	if (rc)
		return rc;

	rc = efx_devlink_info_nvram_partition(efx, req,
					      NVRAM_PARTITION_TYPE_EXPANSION_ROM,
					      EFX_DEVLINK_INFO_VERSION_FW_EXPROM);
	if (rc)
		return rc;

	rc = efx_devlink_info_nvram_partition(efx, req,
					      NVRAM_PARTITION_TYPE_EXPANSION_UEFI,
					      EFX_DEVLINK_INFO_VERSION_FW_UEFI);
	return rc;
}

#define EFX_VER_FLAG(_f)	\
	(MC_CMD_GET_VERSION_V5_OUT_ ## _f ## _PRESENT_LBN)

static void efx_devlink_info_running_v2(struct efx_nic *efx,
					struct devlink_info_req *req,
					unsigned int flags, efx_dword_t *outbuf)
{
	char buf[EFX_MAX_VERSION_INFO_LEN];
	union {
		const __le32 *dwords;
		const __le16 *words;
		const char *str;
	} ver;
	struct rtc_time build_date;
	unsigned int build_id;
	size_t offset;
	__maybe_unused u64 tstamp;

	if (flags & BIT(EFX_VER_FLAG(BOARD_EXT_INFO))) {
		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%s",
			 MCDI_PTR(outbuf, GET_VERSION_V2_OUT_BOARD_NAME));
		devlink_info_version_fixed_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_BOARD_ID,
					       buf);

		/* Favour full board version if present (in V5 or later) */
		if (~flags & BIT(EFX_VER_FLAG(BOARD_VERSION))) {
			snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u",
				 MCDI_DWORD(outbuf,
					    GET_VERSION_V2_OUT_BOARD_REVISION));
			devlink_info_version_fixed_put(req,
						       DEVLINK_INFO_VERSION_GENERIC_BOARD_REV,
						       buf);
		}

		ver.str = MCDI_PTR(outbuf, GET_VERSION_V2_OUT_BOARD_SERIAL);
		if (ver.str[0])
			devlink_info_board_serial_number_put(req, ver.str);
	}

	if (flags & BIT(EFX_VER_FLAG(FPGA_EXT_INFO))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V2_OUT_FPGA_VERSION);
		offset = snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u_%c%u",
				  le32_to_cpu(ver.dwords[0]),
				  'A' + le32_to_cpu(ver.dwords[1]),
				  le32_to_cpu(ver.dwords[2]));

		ver.str = MCDI_PTR(outbuf, GET_VERSION_V2_OUT_FPGA_EXTRA);
		if (ver.str[0])
			snprintf(&buf[offset], EFX_MAX_VERSION_INFO_LEN - offset,
				 " (%s)", ver.str);

		devlink_info_version_running_put(req,
						 EFX_DEVLINK_INFO_VERSION_FPGA_REV,
						 buf);
	}

	if (flags & BIT(EFX_VER_FLAG(CMC_EXT_INFO))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V2_OUT_CMCFW_VERSION);
		offset = snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u",
				  le32_to_cpu(ver.dwords[0]),
				  le32_to_cpu(ver.dwords[1]),
				  le32_to_cpu(ver.dwords[2]),
				  le32_to_cpu(ver.dwords[3]));

#ifdef CONFIG_RTC_LIB
		tstamp = MCDI_QWORD(outbuf,
				    GET_VERSION_V2_OUT_CMCFW_BUILD_DATE);
		if (tstamp) {
			rtc_time64_to_tm(tstamp, &build_date);
			snprintf(&buf[offset], EFX_MAX_VERSION_INFO_LEN - offset,
				 " (%ptRd)", &build_date);
		}
#endif

		devlink_info_version_running_put(req,
						 EFX_DEVLINK_INFO_VERSION_FW_MGMT_CMC,
						 buf);
	}

	ver.words = (__le16 *)MCDI_PTR(outbuf, GET_VERSION_V2_OUT_VERSION);
	offset = snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u",
			  le16_to_cpu(ver.words[0]), le16_to_cpu(ver.words[1]),
			  le16_to_cpu(ver.words[2]), le16_to_cpu(ver.words[3]));
	if (flags & BIT(EFX_VER_FLAG(MCFW_EXT_INFO))) {
		build_id = MCDI_DWORD(outbuf, GET_VERSION_V2_OUT_MCFW_BUILD_ID);
		snprintf(&buf[offset], EFX_MAX_VERSION_INFO_LEN - offset,
			 " (%x) %s", build_id,
			 MCDI_PTR(outbuf, GET_VERSION_V2_OUT_MCFW_BUILD_NAME));
	}
	devlink_info_version_running_put(req,
					 DEVLINK_INFO_VERSION_GENERIC_FW_MGMT,
					 buf);

	if (flags & BIT(EFX_VER_FLAG(SUCFW_EXT_INFO))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V2_OUT_SUCFW_VERSION);
#ifdef CONFIG_RTC_LIB
		tstamp = MCDI_QWORD(outbuf,
				    GET_VERSION_V2_OUT_SUCFW_BUILD_DATE);
		rtc_time64_to_tm(tstamp, &build_date);
#else
		memset(&build_date, 0, sizeof(build_date)
#endif
		build_id = MCDI_DWORD(outbuf, GET_VERSION_V2_OUT_SUCFW_CHIP_ID);

		snprintf(buf, EFX_MAX_VERSION_INFO_LEN,
			 "%u.%u.%u.%u type %x (%ptRd)",
			 le32_to_cpu(ver.dwords[0]), le32_to_cpu(ver.dwords[1]),
			 le32_to_cpu(ver.dwords[2]), le32_to_cpu(ver.dwords[3]),
			 build_id, &build_date);

		devlink_info_version_running_put(req,
						 EFX_DEVLINK_INFO_VERSION_FW_MGMT_SUC,
						 buf);
	}
}

static void efx_devlink_info_running_v3(struct efx_nic *efx,
					struct devlink_info_req *req,
					unsigned int flags, efx_dword_t *outbuf)
{
	char buf[EFX_MAX_VERSION_INFO_LEN];
	union {
		const __le32 *dwords;
		const __le16 *words;
		const char *str;
	} ver;

	if (flags & BIT(EFX_VER_FLAG(DATAPATH_HW_VERSION))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V3_OUT_DATAPATH_HW_VERSION);

		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u",
			 le32_to_cpu(ver.dwords[0]), le32_to_cpu(ver.dwords[1]),
			 le32_to_cpu(ver.dwords[2]));

		devlink_info_version_running_put(req,
						 EFX_DEVLINK_INFO_VERSION_DATAPATH_HW,
						 buf);
	}

	if (flags & BIT(EFX_VER_FLAG(DATAPATH_FW_VERSION))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V3_OUT_DATAPATH_FW_VERSION);

		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u",
			 le32_to_cpu(ver.dwords[0]), le32_to_cpu(ver.dwords[1]),
			 le32_to_cpu(ver.dwords[2]));

		devlink_info_version_running_put(req,
						 EFX_DEVLINK_INFO_VERSION_DATAPATH_FW,
						 buf);
	}
}

static void efx_devlink_info_running_v4(struct efx_nic *efx,
					struct devlink_info_req *req,
					unsigned int flags, efx_dword_t *outbuf)
{
	char buf[EFX_MAX_VERSION_INFO_LEN];
	union {
		const __le32 *dwords;
		const __le16 *words;
		const char *str;
	} ver;

	if (flags & BIT(EFX_VER_FLAG(SOC_BOOT_VERSION))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V4_OUT_SOC_BOOT_VERSION);

		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u",
			 le32_to_cpu(ver.dwords[0]), le32_to_cpu(ver.dwords[1]),
			 le32_to_cpu(ver.dwords[2]),
			 le32_to_cpu(ver.dwords[3]));

		devlink_info_version_running_put(req,
						 EFX_DEVLINK_INFO_VERSION_SOC_BOOT,
						 buf);
	}

	if (flags & BIT(EFX_VER_FLAG(SOC_UBOOT_VERSION))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V4_OUT_SOC_UBOOT_VERSION);

		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u",
			 le32_to_cpu(ver.dwords[0]), le32_to_cpu(ver.dwords[1]),
			 le32_to_cpu(ver.dwords[2]),
			 le32_to_cpu(ver.dwords[3]));

		devlink_info_version_running_put(req,
						 EFX_DEVLINK_INFO_VERSION_SOC_UBOOT,
						 buf);
	}

	if (flags & BIT(EFX_VER_FLAG(SOC_MAIN_ROOTFS_VERSION))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
					GET_VERSION_V4_OUT_SOC_MAIN_ROOTFS_VERSION);

		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u",
			 le32_to_cpu(ver.dwords[0]), le32_to_cpu(ver.dwords[1]),
			 le32_to_cpu(ver.dwords[2]),
			 le32_to_cpu(ver.dwords[3]));

		devlink_info_version_running_put(req,
						 EFX_DEVLINK_INFO_VERSION_SOC_MAIN,
						 buf);
	}

	if (flags & BIT(EFX_VER_FLAG(SOC_RECOVERY_BUILDROOT_VERSION))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V4_OUT_SOC_RECOVERY_BUILDROOT_VERSION);

		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u",
			 le32_to_cpu(ver.dwords[0]), le32_to_cpu(ver.dwords[1]),
			 le32_to_cpu(ver.dwords[2]),
			 le32_to_cpu(ver.dwords[3]));

		devlink_info_version_running_put(req,
						 EFX_DEVLINK_INFO_VERSION_SOC_RECOVERY,
						 buf);
	}

	if (flags & BIT(EFX_VER_FLAG(SUCFW_VERSION)) &&
	    ~flags & BIT(EFX_VER_FLAG(SUCFW_EXT_INFO))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V4_OUT_SUCFW_VERSION);

		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u",
			 le32_to_cpu(ver.dwords[0]), le32_to_cpu(ver.dwords[1]),
			 le32_to_cpu(ver.dwords[2]),
			 le32_to_cpu(ver.dwords[3]));

		devlink_info_version_running_put(req,
						 EFX_DEVLINK_INFO_VERSION_FW_MGMT_SUC,
						 buf);
	}
}

static void efx_devlink_info_running_v5(struct efx_nic *efx,
					struct devlink_info_req *req,
					unsigned int flags, efx_dword_t *outbuf)
{
	char buf[EFX_MAX_VERSION_INFO_LEN];
	union {
		const __le32 *dwords;
		const __le16 *words;
		const char *str;
	} ver;

	if (flags & BIT(EFX_VER_FLAG(BOARD_VERSION))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V5_OUT_BOARD_VERSION);

		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u",
			 le32_to_cpu(ver.dwords[0]), le32_to_cpu(ver.dwords[1]),
			 le32_to_cpu(ver.dwords[2]),
			 le32_to_cpu(ver.dwords[3]));

		devlink_info_version_running_put(req,
						 DEVLINK_INFO_VERSION_GENERIC_BOARD_REV,
						 buf);
	}

	if (flags & BIT(EFX_VER_FLAG(BUNDLE_VERSION))) {
		ver.dwords = (__le32 *)MCDI_PTR(outbuf,
						GET_VERSION_V5_OUT_BUNDLE_VERSION);

		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u",
			 le32_to_cpu(ver.dwords[0]), le32_to_cpu(ver.dwords[1]),
			 le32_to_cpu(ver.dwords[2]),
			 le32_to_cpu(ver.dwords[3]));

		devlink_info_version_running_put(req,
						 DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID,
						 buf);
	}
}

static int efx_devlink_info_running_versions(struct efx_nic *efx,
					     struct devlink_info_req *req)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_VERSION_V5_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_GET_VERSION_EXT_IN_LEN);
	char buf[EFX_MAX_VERSION_INFO_LEN];
	union {
		const __le32 *dwords;
		const __le16 *words;
		const char *str;
	} ver;
	size_t outlength;
	unsigned int flags;
	int rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_VERSION, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlength);
	if (rc || outlength < MC_CMD_GET_VERSION_OUT_LEN) {
		netif_err(efx, drv, efx->net_dev,
			  "mcdi MC_CMD_GET_VERSION failed\n");
		return rc;
	}

	/* Handle previous output */
	if (outlength < MC_CMD_GET_VERSION_V2_OUT_LEN) {
		ver.words = (__le16 *)MCDI_PTR(outbuf,
					       GET_VERSION_EXT_OUT_VERSION);
		snprintf(buf, EFX_MAX_VERSION_INFO_LEN, "%u.%u.%u.%u",
			 le16_to_cpu(ver.words[0]),
			 le16_to_cpu(ver.words[1]),
			 le16_to_cpu(ver.words[2]),
			 le16_to_cpu(ver.words[3]));

		devlink_info_version_running_put(req,
						 DEVLINK_INFO_VERSION_GENERIC_FW_MGMT,
						 buf);
		return 0;
	}

	/* Handle V2 additions */
	flags = MCDI_DWORD(outbuf, GET_VERSION_V2_OUT_FLAGS);
	efx_devlink_info_running_v2(efx, req, flags, outbuf);

	if (outlength < MC_CMD_GET_VERSION_V3_OUT_LEN)
		return 0;

	/* Handle V3 additions */
	efx_devlink_info_running_v3(efx, req, flags, outbuf);

	if (outlength < MC_CMD_GET_VERSION_V4_OUT_LEN)
		return 0;

	/* Handle V4 additions */
	efx_devlink_info_running_v4(efx, req, flags, outbuf);

	if (outlength < MC_CMD_GET_VERSION_V5_OUT_LEN)
		return 0;

	/* Handle V5 additions */
	efx_devlink_info_running_v5(efx, req, flags, outbuf);

	return 0;
}

#define EFX_MAX_SERIALNUM_LEN	(ETH_ALEN * 2 + 1)

static int efx_devlink_info_board_cfg(struct efx_nic *efx,
				      struct devlink_info_req *req)
{
	char sn[EFX_MAX_SERIALNUM_LEN];
	u8 mac_address[ETH_ALEN];
	int rc;

	rc = efx_mcdi_get_board_cfg(efx, (u8 *)mac_address, NULL, NULL);
	if (!rc) {
		snprintf(sn, EFX_MAX_SERIALNUM_LEN, "%pm", mac_address);
		devlink_info_serial_number_put(req, sn);
	}
	return rc;
}

static int efx_devlink_info_get(struct devlink *devlink,
				struct devlink_info_req *req,
				struct netlink_ext_ack *extack)
{
	struct efx_devlink *devlink_private = devlink_priv(devlink);
	struct efx_nic *efx = devlink_private->efx;
	int rc;

	/* Several different MCDI commands are used. We report first error
	 * through extack returning at that point. Specific error
	 * information via system messages.
	 */
	rc = efx_devlink_info_board_cfg(efx, req);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Getting board info failed");
		return rc;
	}
	rc = efx_devlink_info_stored_versions(efx, req);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Getting stored versions failed");
		return rc;
	}
	rc = efx_devlink_info_running_versions(efx, req);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Getting running versions failed");
		return rc;
	}

	return 0;
}

static const struct devlink_ops sfc_devlink_ops = {
	.info_get			= efx_devlink_info_get,
};

void efx_fini_devlink_lock(struct efx_nic *efx)
{
	if (efx->devlink)
		devl_lock(efx->devlink);
}

void efx_fini_devlink_and_unlock(struct efx_nic *efx)
{
	if (efx->devlink) {
		devl_unregister(efx->devlink);
		devl_unlock(efx->devlink);
		devlink_free(efx->devlink);
		efx->devlink = NULL;
	}
}

int efx_probe_devlink_and_lock(struct efx_nic *efx)
{
	struct efx_devlink *devlink_private;

	if (efx->type->is_vf)
		return 0;

	efx->devlink = devlink_alloc(&sfc_devlink_ops,
				     sizeof(struct efx_devlink),
				     &efx->pci_dev->dev);
	if (!efx->devlink)
		return -ENOMEM;

	devl_lock(efx->devlink);
	devlink_private = devlink_priv(efx->devlink);
	devlink_private->efx = efx;

	devl_register(efx->devlink);

	return 0;
}

void efx_probe_devlink_unlock(struct efx_nic *efx)
{
	if (!efx->devlink)
		return;

	devl_unlock(efx->devlink);
}
