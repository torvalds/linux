// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#include <linux/module.h>

#include "mcdi.h"
#include "mcdi_functions.h"

int cdx_mcdi_get_num_buses(struct cdx_mcdi *cdx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_CDX_BUS_ENUM_BUSES_OUT_LEN);
	size_t outlen;
	int ret;

	ret = cdx_mcdi_rpc(cdx, MC_CMD_CDX_BUS_ENUM_BUSES, NULL, 0,
			   outbuf, sizeof(outbuf), &outlen);
	if (ret)
		return ret;

	if (outlen != MC_CMD_CDX_BUS_ENUM_BUSES_OUT_LEN)
		return -EIO;

	return MCDI_DWORD(outbuf, CDX_BUS_ENUM_BUSES_OUT_BUS_COUNT);
}

int cdx_mcdi_get_num_devs(struct cdx_mcdi *cdx, int bus_num)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_CDX_BUS_ENUM_DEVICES_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_CDX_BUS_ENUM_DEVICES_IN_LEN);
	size_t outlen;
	int ret;

	MCDI_SET_DWORD(inbuf, CDX_BUS_ENUM_DEVICES_IN_BUS, bus_num);

	ret = cdx_mcdi_rpc(cdx, MC_CMD_CDX_BUS_ENUM_DEVICES, inbuf, sizeof(inbuf),
			   outbuf, sizeof(outbuf), &outlen);
	if (ret)
		return ret;

	if (outlen != MC_CMD_CDX_BUS_ENUM_DEVICES_OUT_LEN)
		return -EIO;

	return MCDI_DWORD(outbuf, CDX_BUS_ENUM_DEVICES_OUT_DEVICE_COUNT);
}

int cdx_mcdi_get_dev_config(struct cdx_mcdi *cdx,
			    u8 bus_num, u8 dev_num,
			    struct cdx_dev_params *dev_params)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_IN_LEN);
	struct resource *res = &dev_params->res[0];
	size_t outlen;
	u32 req_id;
	int ret;

	MCDI_SET_DWORD(inbuf, CDX_BUS_GET_DEVICE_CONFIG_IN_BUS, bus_num);
	MCDI_SET_DWORD(inbuf, CDX_BUS_GET_DEVICE_CONFIG_IN_DEVICE, dev_num);

	ret = cdx_mcdi_rpc(cdx, MC_CMD_CDX_BUS_GET_DEVICE_CONFIG, inbuf, sizeof(inbuf),
			   outbuf, sizeof(outbuf), &outlen);
	if (ret)
		return ret;

	if (outlen != MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_LEN)
		return -EIO;

	dev_params->bus_num = bus_num;
	dev_params->dev_num = dev_num;

	req_id = MCDI_DWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_REQUESTER_ID);
	dev_params->req_id = req_id;

	dev_params->res_count = 0;
	if (MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE) != 0) {
		res[dev_params->res_count].start =
			MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE);
		res[dev_params->res_count].end =
			MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE) +
				   MCDI_QWORD(outbuf,
					      CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE) - 1;
		res[dev_params->res_count].flags = IORESOURCE_MEM;
		dev_params->res_count++;
	}

	if (MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE) != 0) {
		res[dev_params->res_count].start =
			MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE);
		res[dev_params->res_count].end =
			MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE) +
				   MCDI_QWORD(outbuf,
					      CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE) - 1;
		res[dev_params->res_count].flags = IORESOURCE_MEM;
		dev_params->res_count++;
	}

	if (MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE) != 0) {
		res[dev_params->res_count].start =
			MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE);
		res[dev_params->res_count].end =
			MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE) +
				   MCDI_QWORD(outbuf,
					      CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE) - 1;
		res[dev_params->res_count].flags = IORESOURCE_MEM;
		dev_params->res_count++;
	}

	if (MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE) != 0) {
		res[dev_params->res_count].start =
			MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE);
		res[dev_params->res_count].end =
			MCDI_QWORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE) +
				   MCDI_QWORD(outbuf,
					      CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE) - 1;
		res[dev_params->res_count].flags = IORESOURCE_MEM;
		dev_params->res_count++;
	}

	dev_params->vendor = MCDI_WORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_VENDOR_ID);
	dev_params->device = MCDI_WORD(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_DEVICE_ID);
	dev_params->subsys_vendor = MCDI_WORD(outbuf,
					      CDX_BUS_GET_DEVICE_CONFIG_OUT_SUBSYS_VENDOR_ID);
	dev_params->subsys_device = MCDI_WORD(outbuf,
					      CDX_BUS_GET_DEVICE_CONFIG_OUT_SUBSYS_DEVICE_ID);
	dev_params->class = MCDI_DWORD(outbuf,
				       CDX_BUS_GET_DEVICE_CONFIG_OUT_DEVICE_CLASS) & 0xFFFFFF;
	dev_params->revision = MCDI_BYTE(outbuf, CDX_BUS_GET_DEVICE_CONFIG_OUT_DEVICE_REVISION);

	return 0;
}

int cdx_mcdi_bus_enable(struct cdx_mcdi *cdx, u8 bus_num)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_CDX_BUS_UP_IN_LEN);
	int ret;

	MCDI_SET_DWORD(inbuf, CDX_BUS_UP_IN_BUS, bus_num);
	ret = cdx_mcdi_rpc(cdx, MC_CMD_CDX_BUS_UP, inbuf, sizeof(inbuf),
			   NULL, 0, NULL);

	return ret;
}

int cdx_mcdi_bus_disable(struct cdx_mcdi *cdx, u8 bus_num)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_CDX_BUS_DOWN_IN_LEN);
	int ret;

	MCDI_SET_DWORD(inbuf, CDX_BUS_DOWN_IN_BUS, bus_num);
	ret = cdx_mcdi_rpc(cdx, MC_CMD_CDX_BUS_DOWN, inbuf, sizeof(inbuf),
			   NULL, 0, NULL);

	return ret;
}

int cdx_mcdi_reset_device(struct cdx_mcdi *cdx, u8 bus_num, u8 dev_num)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_CDX_DEVICE_RESET_IN_LEN);
	int ret;

	MCDI_SET_DWORD(inbuf, CDX_DEVICE_RESET_IN_BUS, bus_num);
	MCDI_SET_DWORD(inbuf, CDX_DEVICE_RESET_IN_DEVICE, dev_num);

	ret = cdx_mcdi_rpc(cdx, MC_CMD_CDX_DEVICE_RESET, inbuf, sizeof(inbuf),
			   NULL, 0, NULL);

	return ret;
}

static int cdx_mcdi_ctrl_flag_get(struct cdx_mcdi *cdx, u8 bus_num,
				  u8 dev_num, u32 *flags)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_CDX_DEVICE_CONTROL_GET_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_LEN);
	size_t outlen;
	int ret;

	MCDI_SET_DWORD(inbuf, CDX_DEVICE_CONTROL_GET_IN_BUS, bus_num);
	MCDI_SET_DWORD(inbuf, CDX_DEVICE_CONTROL_GET_IN_DEVICE, dev_num);
	ret = cdx_mcdi_rpc(cdx, MC_CMD_CDX_DEVICE_CONTROL_GET, inbuf,
			   sizeof(inbuf), outbuf, sizeof(outbuf), &outlen);
	if (ret)
		return ret;

	if (outlen != MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_LEN)
		return -EIO;

	*flags = MCDI_DWORD(outbuf, CDX_DEVICE_CONTROL_GET_OUT_FLAGS);

	return 0;
}

static int cdx_mcdi_ctrl_flag_set(struct cdx_mcdi *cdx, u8 bus_num,
				  u8 dev_num, bool enable, int bit_pos)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_CDX_DEVICE_CONTROL_SET_IN_LEN);
	u32 flags;
	int ret;

	/*
	 * Get flags and then set/reset bit at bit_pos according to
	 * the input params.
	 */
	ret = cdx_mcdi_ctrl_flag_get(cdx, bus_num, dev_num, &flags);
	if (ret)
		return ret;

	flags = flags & (u32)(~(BIT(bit_pos)));
	if (enable)
		flags |= (1 << bit_pos);

	MCDI_SET_DWORD(inbuf, CDX_DEVICE_CONTROL_SET_IN_BUS, bus_num);
	MCDI_SET_DWORD(inbuf, CDX_DEVICE_CONTROL_SET_IN_DEVICE, dev_num);
	MCDI_SET_DWORD(inbuf, CDX_DEVICE_CONTROL_SET_IN_FLAGS, flags);
	ret = cdx_mcdi_rpc(cdx, MC_CMD_CDX_DEVICE_CONTROL_SET, inbuf,
			   sizeof(inbuf), NULL, 0, NULL);

	return ret;
}

int cdx_mcdi_bus_master_enable(struct cdx_mcdi *cdx, u8 bus_num,
			       u8 dev_num, bool enable)
{
	return cdx_mcdi_ctrl_flag_set(cdx, bus_num, dev_num, enable,
			MC_CMD_CDX_DEVICE_CONTROL_SET_IN_BUS_MASTER_ENABLE_LBN);
}
