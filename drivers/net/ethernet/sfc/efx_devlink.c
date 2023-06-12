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
#include "ef100_nic.h"
#include "efx_devlink.h"
#include <linux/rtc.h>
#include "mcdi.h"
#include "mcdi_functions.h"
#include "mcdi_pcol.h"
#ifdef CONFIG_SFC_SRIOV
#include "mae.h"
#include "ef100_rep.h"
#endif

struct efx_devlink {
	struct efx_nic *efx;
};

#ifdef CONFIG_SFC_SRIOV
static void efx_devlink_del_port(struct devlink_port *dl_port)
{
	if (!dl_port)
		return;
	devl_port_unregister(dl_port);
}

static int efx_devlink_add_port(struct efx_nic *efx,
				struct mae_mport_desc *mport)
{
	bool external = false;

	if (!ef100_mport_on_local_intf(efx, mport))
		external = true;

	switch (mport->mport_type) {
	case MAE_MPORT_DESC_MPORT_TYPE_VNIC:
		if (mport->vf_idx != MAE_MPORT_DESC_VF_IDX_NULL)
			devlink_port_attrs_pci_vf_set(&mport->dl_port, 0, mport->pf_idx,
						      mport->vf_idx,
						      external);
		else
			devlink_port_attrs_pci_pf_set(&mport->dl_port, 0, mport->pf_idx,
						      external);
		break;
	default:
		/* MAE_MPORT_DESC_MPORT_ALIAS and UNDEFINED */
		return 0;
	}

	mport->dl_port.index = mport->mport_id;

	return devl_port_register(efx->devlink, &mport->dl_port, mport->mport_id);
}

static int efx_devlink_port_addr_get(struct devlink_port *port, u8 *hw_addr,
				     int *hw_addr_len,
				     struct netlink_ext_ack *extack)
{
	struct efx_devlink *devlink = devlink_priv(port->devlink);
	struct mae_mport_desc *mport_desc;
	efx_qword_t pciefn;
	u32 client_id;
	int rc = 0;

	mport_desc = container_of(port, struct mae_mport_desc, dl_port);

	if (!ef100_mport_on_local_intf(devlink->efx, mport_desc)) {
		rc = -EINVAL;
		NL_SET_ERR_MSG_FMT(extack,
				   "Port not on local interface (mport: %u)",
				   mport_desc->mport_id);
		goto out;
	}

	if (ef100_mport_is_vf(mport_desc))
		EFX_POPULATE_QWORD_3(pciefn,
				     PCIE_FUNCTION_PF, PCIE_FUNCTION_PF_NULL,
				     PCIE_FUNCTION_VF, mport_desc->vf_idx,
				     PCIE_FUNCTION_INTF, PCIE_INTERFACE_CALLER);
	else
		EFX_POPULATE_QWORD_3(pciefn,
				     PCIE_FUNCTION_PF, mport_desc->pf_idx,
				     PCIE_FUNCTION_VF, PCIE_FUNCTION_VF_NULL,
				     PCIE_FUNCTION_INTF, PCIE_INTERFACE_CALLER);

	rc = efx_ef100_lookup_client_id(devlink->efx, pciefn, &client_id);
	if (rc) {
		NL_SET_ERR_MSG_FMT(extack,
				   "No internal client_ID for port (mport: %u)",
				   mport_desc->mport_id);
		goto out;
	}

	rc = ef100_get_mac_address(devlink->efx, hw_addr, client_id, true);
	if (rc != 0)
		NL_SET_ERR_MSG_FMT(extack,
				   "No available MAC for port (mport: %u)",
				   mport_desc->mport_id);
out:
	*hw_addr_len = ETH_ALEN;
	return rc;
}

static int efx_devlink_port_addr_set(struct devlink_port *port,
				     const u8 *hw_addr, int hw_addr_len,
				     struct netlink_ext_ack *extack)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_SET_CLIENT_MAC_ADDRESSES_IN_LEN(1));
	struct efx_devlink *devlink = devlink_priv(port->devlink);
	struct mae_mport_desc *mport_desc;
	efx_qword_t pciefn;
	u32 client_id;
	int rc;

	mport_desc = container_of(port, struct mae_mport_desc, dl_port);

	if (!ef100_mport_is_vf(mport_desc)) {
		NL_SET_ERR_MSG_FMT(extack,
				   "port mac change not allowed (mport: %u)",
				   mport_desc->mport_id);
		return -EPERM;
	}

	EFX_POPULATE_QWORD_3(pciefn,
			     PCIE_FUNCTION_PF, PCIE_FUNCTION_PF_NULL,
			     PCIE_FUNCTION_VF, mport_desc->vf_idx,
			     PCIE_FUNCTION_INTF, PCIE_INTERFACE_CALLER);

	rc = efx_ef100_lookup_client_id(devlink->efx, pciefn, &client_id);
	if (rc) {
		NL_SET_ERR_MSG_FMT(extack,
				   "No internal client_ID for port (mport: %u)",
				   mport_desc->mport_id);
		return rc;
	}

	MCDI_SET_DWORD(inbuf, SET_CLIENT_MAC_ADDRESSES_IN_CLIENT_HANDLE,
		       client_id);

	ether_addr_copy(MCDI_PTR(inbuf, SET_CLIENT_MAC_ADDRESSES_IN_MAC_ADDRS),
			hw_addr);

	rc = efx_mcdi_rpc(devlink->efx, MC_CMD_SET_CLIENT_MAC_ADDRESSES, inbuf,
			  sizeof(inbuf), NULL, 0, NULL);
	if (rc)
		NL_SET_ERR_MSG_FMT(extack,
				   "sfc MC_CMD_SET_CLIENT_MAC_ADDRESSES mcdi error (mport: %u)",
				   mport_desc->mport_id);

	return rc;
}

#endif

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

	/* If the partition does not exist, that is not an error. */
	if (rc == -ENOENT)
		return 0;

	if (rc) {
		netif_err(efx, drv, efx->net_dev, "mcdi nvram %s: failed (rc=%d)\n",
			  version_name, rc);
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
	int err;

	/* We do not care here about the specific error but just if an error
	 * happened. The specific error will be reported inside the call
	 * through system messages, and if any error happened in any call
	 * below, we report it through extack.
	 */
	err = efx_devlink_info_nvram_partition(efx, req,
					       NVRAM_PARTITION_TYPE_BUNDLE,
					       DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID);

	err |= efx_devlink_info_nvram_partition(efx, req,
						NVRAM_PARTITION_TYPE_MC_FIRMWARE,
						DEVLINK_INFO_VERSION_GENERIC_FW_MGMT);

	err |= efx_devlink_info_nvram_partition(efx, req,
						NVRAM_PARTITION_TYPE_SUC_FIRMWARE,
						EFX_DEVLINK_INFO_VERSION_FW_MGMT_SUC);

	err |= efx_devlink_info_nvram_partition(efx, req,
						NVRAM_PARTITION_TYPE_EXPANSION_ROM,
						EFX_DEVLINK_INFO_VERSION_FW_EXPROM);

	err |= efx_devlink_info_nvram_partition(efx, req,
						NVRAM_PARTITION_TYPE_EXPANSION_UEFI,
						EFX_DEVLINK_INFO_VERSION_FW_UEFI);
	return err;
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
		memset(&build_date, 0, sizeof(build_date));
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
	int err;

	/* Several different MCDI commands are used. We report if errors
	 * happened through extack. Specific error information via system
	 * messages inside the calls.
	 */
	err = efx_devlink_info_board_cfg(efx, req);

	err |= efx_devlink_info_stored_versions(efx, req);

	err |= efx_devlink_info_running_versions(efx, req);

	if (err)
		NL_SET_ERR_MSG_MOD(extack, "Errors when getting device info. Check system messages");

	return 0;
}

static const struct devlink_ops sfc_devlink_ops = {
	.info_get			= efx_devlink_info_get,
#ifdef CONFIG_SFC_SRIOV
	.port_function_hw_addr_get	= efx_devlink_port_addr_get,
	.port_function_hw_addr_set	= efx_devlink_port_addr_set,
#endif
};

#ifdef CONFIG_SFC_SRIOV
static struct devlink_port *ef100_set_devlink_port(struct efx_nic *efx, u32 idx)
{
	struct mae_mport_desc *mport;
	u32 id;
	int rc;

	if (efx_mae_lookup_mport(efx, idx, &id)) {
		/* This should not happen. */
		if (idx == MAE_MPORT_DESC_VF_IDX_NULL)
			pci_warn_once(efx->pci_dev, "No mport ID found for PF.\n");
		else
			pci_warn_once(efx->pci_dev, "No mport ID found for VF %u.\n",
				      idx);
		return NULL;
	}

	mport = efx_mae_get_mport(efx, id);
	if (!mport) {
		/* This should not happen. */
		if (idx == MAE_MPORT_DESC_VF_IDX_NULL)
			pci_warn_once(efx->pci_dev, "No mport found for PF.\n");
		else
			pci_warn_once(efx->pci_dev, "No mport found for VF %u.\n",
				      idx);
		return NULL;
	}

	rc = efx_devlink_add_port(efx, mport);
	if (rc) {
		if (idx == MAE_MPORT_DESC_VF_IDX_NULL)
			pci_warn(efx->pci_dev,
				 "devlink port creation for PF failed.\n");
		else
			pci_warn(efx->pci_dev,
				 "devlink_port creation for VF %u failed.\n",
				 idx);
		return NULL;
	}

	return &mport->dl_port;
}

void ef100_rep_set_devlink_port(struct efx_rep *efv)
{
	efv->dl_port = ef100_set_devlink_port(efv->parent, efv->idx);
}

void ef100_pf_set_devlink_port(struct efx_nic *efx)
{
	efx->dl_port = ef100_set_devlink_port(efx, MAE_MPORT_DESC_VF_IDX_NULL);
}

void ef100_rep_unset_devlink_port(struct efx_rep *efv)
{
	efx_devlink_del_port(efv->dl_port);
}

void ef100_pf_unset_devlink_port(struct efx_nic *efx)
{
	efx_devlink_del_port(efx->dl_port);
}
#endif

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
