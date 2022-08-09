// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 */

#include <linux/firmware.h>

#include "hfi.h"
#include "efivar.h"
#include "eprom.h"

#define DEFAULT_PLATFORM_CONFIG_NAME "hfi1_platform.dat"

static int validate_scratch_checksum(struct hfi1_devdata *dd)
{
	u64 checksum = 0, temp_scratch = 0;
	int i, j, version;

	temp_scratch = read_csr(dd, ASIC_CFG_SCRATCH);
	version = (temp_scratch & BITMAP_VERSION_SMASK) >> BITMAP_VERSION_SHIFT;

	/* Prevent power on default of all zeroes from passing checksum */
	if (!version) {
		dd_dev_err(dd, "%s: Config bitmap uninitialized\n", __func__);
		dd_dev_err(dd,
			   "%s: Please update your BIOS to support active channels\n",
			   __func__);
		return 0;
	}

	/*
	 * ASIC scratch 0 only contains the checksum and bitmap version as
	 * fields of interest, both of which are handled separately from the
	 * loop below, so skip it
	 */
	checksum += version;
	for (i = 1; i < ASIC_NUM_SCRATCH; i++) {
		temp_scratch = read_csr(dd, ASIC_CFG_SCRATCH + (8 * i));
		for (j = sizeof(u64); j != 0; j -= 2) {
			checksum += (temp_scratch & 0xFFFF);
			temp_scratch >>= 16;
		}
	}

	while (checksum >> 16)
		checksum = (checksum & CHECKSUM_MASK) + (checksum >> 16);

	temp_scratch = read_csr(dd, ASIC_CFG_SCRATCH);
	temp_scratch &= CHECKSUM_SMASK;
	temp_scratch >>= CHECKSUM_SHIFT;

	if (checksum + temp_scratch == 0xFFFF)
		return 1;

	dd_dev_err(dd, "%s: Configuration bitmap corrupted\n", __func__);
	return 0;
}

static void save_platform_config_fields(struct hfi1_devdata *dd)
{
	struct hfi1_pportdata *ppd = dd->pport;
	u64 temp_scratch = 0, temp_dest = 0;

	temp_scratch = read_csr(dd, ASIC_CFG_SCRATCH_1);

	temp_dest = temp_scratch &
		    (dd->hfi1_id ? PORT1_PORT_TYPE_SMASK :
		     PORT0_PORT_TYPE_SMASK);
	ppd->port_type = temp_dest >>
			 (dd->hfi1_id ? PORT1_PORT_TYPE_SHIFT :
			  PORT0_PORT_TYPE_SHIFT);

	temp_dest = temp_scratch &
		    (dd->hfi1_id ? PORT1_LOCAL_ATTEN_SMASK :
		     PORT0_LOCAL_ATTEN_SMASK);
	ppd->local_atten = temp_dest >>
			   (dd->hfi1_id ? PORT1_LOCAL_ATTEN_SHIFT :
			    PORT0_LOCAL_ATTEN_SHIFT);

	temp_dest = temp_scratch &
		    (dd->hfi1_id ? PORT1_REMOTE_ATTEN_SMASK :
		     PORT0_REMOTE_ATTEN_SMASK);
	ppd->remote_atten = temp_dest >>
			    (dd->hfi1_id ? PORT1_REMOTE_ATTEN_SHIFT :
			     PORT0_REMOTE_ATTEN_SHIFT);

	temp_dest = temp_scratch &
		    (dd->hfi1_id ? PORT1_DEFAULT_ATTEN_SMASK :
		     PORT0_DEFAULT_ATTEN_SMASK);
	ppd->default_atten = temp_dest >>
			     (dd->hfi1_id ? PORT1_DEFAULT_ATTEN_SHIFT :
			      PORT0_DEFAULT_ATTEN_SHIFT);

	temp_scratch = read_csr(dd, dd->hfi1_id ? ASIC_CFG_SCRATCH_3 :
				ASIC_CFG_SCRATCH_2);

	ppd->tx_preset_eq = (temp_scratch & TX_EQ_SMASK) >> TX_EQ_SHIFT;
	ppd->tx_preset_noeq = (temp_scratch & TX_NO_EQ_SMASK) >> TX_NO_EQ_SHIFT;
	ppd->rx_preset = (temp_scratch & RX_SMASK) >> RX_SHIFT;

	ppd->max_power_class = (temp_scratch & QSFP_MAX_POWER_SMASK) >>
				QSFP_MAX_POWER_SHIFT;

	ppd->config_from_scratch = true;
}

void get_platform_config(struct hfi1_devdata *dd)
{
	int ret = 0;
	u8 *temp_platform_config = NULL;
	u32 esize;
	const struct firmware *platform_config_file = NULL;

	if (is_integrated(dd)) {
		if (validate_scratch_checksum(dd)) {
			save_platform_config_fields(dd);
			return;
		}
	} else {
		ret = eprom_read_platform_config(dd,
						 (void **)&temp_platform_config,
						 &esize);
		if (!ret) {
			/* success */
			dd->platform_config.data = temp_platform_config;
			dd->platform_config.size = esize;
			return;
		}
	}
	dd_dev_err(dd,
		   "%s: Failed to get platform config, falling back to sub-optimal default file\n",
		   __func__);

	ret = request_firmware(&platform_config_file,
			       DEFAULT_PLATFORM_CONFIG_NAME,
			       &dd->pcidev->dev);
	if (ret) {
		dd_dev_err(dd,
			   "%s: No default platform config file found\n",
			   __func__);
		return;
	}

	/*
	 * Allocate separate memory block to store data and free firmware
	 * structure. This allows free_platform_config to treat EPROM and
	 * fallback configs in the same manner.
	 */
	dd->platform_config.data = kmemdup(platform_config_file->data,
					   platform_config_file->size,
					   GFP_KERNEL);
	dd->platform_config.size = platform_config_file->size;
	release_firmware(platform_config_file);
}

void free_platform_config(struct hfi1_devdata *dd)
{
	/* Release memory allocated for eprom or fallback file read. */
	kfree(dd->platform_config.data);
	dd->platform_config.data = NULL;
}

void get_port_type(struct hfi1_pportdata *ppd)
{
	int ret;
	u32 temp;

	ret = get_platform_config_field(ppd->dd, PLATFORM_CONFIG_PORT_TABLE, 0,
					PORT_TABLE_PORT_TYPE, &temp,
					4);
	if (ret) {
		ppd->port_type = PORT_TYPE_UNKNOWN;
		return;
	}
	ppd->port_type = temp;
}

int set_qsfp_tx(struct hfi1_pportdata *ppd, int on)
{
	u8 tx_ctrl_byte = on ? 0x0 : 0xF;
	int ret = 0;

	ret = qsfp_write(ppd, ppd->dd->hfi1_id, QSFP_TX_CTRL_BYTE_OFFS,
			 &tx_ctrl_byte, 1);
	/* we expected 1, so consider 0 an error */
	if (ret == 0)
		ret = -EIO;
	else if (ret == 1)
		ret = 0;
	return ret;
}

static int qual_power(struct hfi1_pportdata *ppd)
{
	u32 cable_power_class = 0, power_class_max = 0;
	u8 *cache = ppd->qsfp_info.cache;
	int ret = 0;

	ret = get_platform_config_field(
		ppd->dd, PLATFORM_CONFIG_SYSTEM_TABLE, 0,
		SYSTEM_TABLE_QSFP_POWER_CLASS_MAX, &power_class_max, 4);
	if (ret)
		return ret;

	cable_power_class = get_qsfp_power_class(cache[QSFP_MOD_PWR_OFFS]);

	if (cable_power_class > power_class_max)
		ppd->offline_disabled_reason =
			HFI1_ODR_MASK(OPA_LINKDOWN_REASON_POWER_POLICY);

	if (ppd->offline_disabled_reason ==
			HFI1_ODR_MASK(OPA_LINKDOWN_REASON_POWER_POLICY)) {
		dd_dev_err(
			ppd->dd,
			"%s: Port disabled due to system power restrictions\n",
			__func__);
		ret = -EPERM;
	}
	return ret;
}

static int qual_bitrate(struct hfi1_pportdata *ppd)
{
	u16 lss = ppd->link_speed_supported, lse = ppd->link_speed_enabled;
	u8 *cache = ppd->qsfp_info.cache;

	if ((lss & OPA_LINK_SPEED_25G) && (lse & OPA_LINK_SPEED_25G) &&
	    cache[QSFP_NOM_BIT_RATE_250_OFFS] < 0x64)
		ppd->offline_disabled_reason =
			   HFI1_ODR_MASK(OPA_LINKDOWN_REASON_LINKSPEED_POLICY);

	if ((lss & OPA_LINK_SPEED_12_5G) && (lse & OPA_LINK_SPEED_12_5G) &&
	    cache[QSFP_NOM_BIT_RATE_100_OFFS] < 0x7D)
		ppd->offline_disabled_reason =
			   HFI1_ODR_MASK(OPA_LINKDOWN_REASON_LINKSPEED_POLICY);

	if (ppd->offline_disabled_reason ==
			HFI1_ODR_MASK(OPA_LINKDOWN_REASON_LINKSPEED_POLICY)) {
		dd_dev_err(
			ppd->dd,
			"%s: Cable failed bitrate check, disabling port\n",
			__func__);
		return -EPERM;
	}
	return 0;
}

static int set_qsfp_high_power(struct hfi1_pportdata *ppd)
{
	u8 cable_power_class = 0, power_ctrl_byte = 0;
	u8 *cache = ppd->qsfp_info.cache;
	int ret;

	cable_power_class = get_qsfp_power_class(cache[QSFP_MOD_PWR_OFFS]);

	if (cable_power_class > QSFP_POWER_CLASS_1) {
		power_ctrl_byte = cache[QSFP_PWR_CTRL_BYTE_OFFS];

		power_ctrl_byte |= 1;
		power_ctrl_byte &= ~(0x2);

		ret = qsfp_write(ppd, ppd->dd->hfi1_id,
				 QSFP_PWR_CTRL_BYTE_OFFS,
				 &power_ctrl_byte, 1);
		if (ret != 1)
			return -EIO;

		if (cable_power_class > QSFP_POWER_CLASS_4) {
			power_ctrl_byte |= (1 << 2);
			ret = qsfp_write(ppd, ppd->dd->hfi1_id,
					 QSFP_PWR_CTRL_BYTE_OFFS,
					 &power_ctrl_byte, 1);
			if (ret != 1)
				return -EIO;
		}

		/* SFF 8679 rev 1.7 LPMode Deassert time */
		msleep(300);
	}
	return 0;
}

static void apply_rx_cdr(struct hfi1_pportdata *ppd,
			 u32 rx_preset_index,
			 u8 *cdr_ctrl_byte)
{
	u32 rx_preset;
	u8 *cache = ppd->qsfp_info.cache;
	int cable_power_class;

	if (!((cache[QSFP_MOD_PWR_OFFS] & 0x4) &&
	      (cache[QSFP_CDR_INFO_OFFS] & 0x40)))
		return;

	/* RX CDR present, bypass supported */
	cable_power_class = get_qsfp_power_class(cache[QSFP_MOD_PWR_OFFS]);

	if (cable_power_class <= QSFP_POWER_CLASS_3) {
		/* Power class <= 3, ignore config & turn RX CDR on */
		*cdr_ctrl_byte |= 0xF;
		return;
	}

	get_platform_config_field(
		ppd->dd, PLATFORM_CONFIG_RX_PRESET_TABLE,
		rx_preset_index, RX_PRESET_TABLE_QSFP_RX_CDR_APPLY,
		&rx_preset, 4);

	if (!rx_preset) {
		dd_dev_info(
			ppd->dd,
			"%s: RX_CDR_APPLY is set to disabled\n",
			__func__);
		return;
	}
	get_platform_config_field(
		ppd->dd, PLATFORM_CONFIG_RX_PRESET_TABLE,
		rx_preset_index, RX_PRESET_TABLE_QSFP_RX_CDR,
		&rx_preset, 4);

	/* Expand cdr setting to all 4 lanes */
	rx_preset = (rx_preset | (rx_preset << 1) |
			(rx_preset << 2) | (rx_preset << 3));

	if (rx_preset) {
		*cdr_ctrl_byte |= rx_preset;
	} else {
		*cdr_ctrl_byte &= rx_preset;
		/* Preserve current TX CDR status */
		*cdr_ctrl_byte |= (cache[QSFP_CDR_CTRL_BYTE_OFFS] & 0xF0);
	}
}

static void apply_tx_cdr(struct hfi1_pportdata *ppd,
			 u32 tx_preset_index,
			 u8 *cdr_ctrl_byte)
{
	u32 tx_preset;
	u8 *cache = ppd->qsfp_info.cache;
	int cable_power_class;

	if (!((cache[QSFP_MOD_PWR_OFFS] & 0x8) &&
	      (cache[QSFP_CDR_INFO_OFFS] & 0x80)))
		return;

	/* TX CDR present, bypass supported */
	cable_power_class = get_qsfp_power_class(cache[QSFP_MOD_PWR_OFFS]);

	if (cable_power_class <= QSFP_POWER_CLASS_3) {
		/* Power class <= 3, ignore config & turn TX CDR on */
		*cdr_ctrl_byte |= 0xF0;
		return;
	}

	get_platform_config_field(
		ppd->dd,
		PLATFORM_CONFIG_TX_PRESET_TABLE, tx_preset_index,
		TX_PRESET_TABLE_QSFP_TX_CDR_APPLY, &tx_preset, 4);

	if (!tx_preset) {
		dd_dev_info(
			ppd->dd,
			"%s: TX_CDR_APPLY is set to disabled\n",
			__func__);
		return;
	}
	get_platform_config_field(
		ppd->dd,
		PLATFORM_CONFIG_TX_PRESET_TABLE,
		tx_preset_index,
		TX_PRESET_TABLE_QSFP_TX_CDR, &tx_preset, 4);

	/* Expand cdr setting to all 4 lanes */
	tx_preset = (tx_preset | (tx_preset << 1) |
			(tx_preset << 2) | (tx_preset << 3));

	if (tx_preset)
		*cdr_ctrl_byte |= (tx_preset << 4);
	else
		/* Preserve current/determined RX CDR status */
		*cdr_ctrl_byte &= ((tx_preset << 4) | 0xF);
}

static void apply_cdr_settings(
		struct hfi1_pportdata *ppd, u32 rx_preset_index,
		u32 tx_preset_index)
{
	u8 *cache = ppd->qsfp_info.cache;
	u8 cdr_ctrl_byte = cache[QSFP_CDR_CTRL_BYTE_OFFS];

	apply_rx_cdr(ppd, rx_preset_index, &cdr_ctrl_byte);

	apply_tx_cdr(ppd, tx_preset_index, &cdr_ctrl_byte);

	qsfp_write(ppd, ppd->dd->hfi1_id, QSFP_CDR_CTRL_BYTE_OFFS,
		   &cdr_ctrl_byte, 1);
}

static void apply_tx_eq_auto(struct hfi1_pportdata *ppd)
{
	u8 *cache = ppd->qsfp_info.cache;
	u8 tx_eq;

	if (!(cache[QSFP_EQ_INFO_OFFS] & 0x8))
		return;
	/* Disable adaptive TX EQ if present */
	tx_eq = cache[(128 * 3) + 241];
	tx_eq &= 0xF0;
	qsfp_write(ppd, ppd->dd->hfi1_id, (256 * 3) + 241, &tx_eq, 1);
}

static void apply_tx_eq_prog(struct hfi1_pportdata *ppd, u32 tx_preset_index)
{
	u8 *cache = ppd->qsfp_info.cache;
	u32 tx_preset;
	u8 tx_eq;

	if (!(cache[QSFP_EQ_INFO_OFFS] & 0x4))
		return;

	get_platform_config_field(
		ppd->dd, PLATFORM_CONFIG_TX_PRESET_TABLE,
		tx_preset_index, TX_PRESET_TABLE_QSFP_TX_EQ_APPLY,
		&tx_preset, 4);
	if (!tx_preset) {
		dd_dev_info(
			ppd->dd,
			"%s: TX_EQ_APPLY is set to disabled\n",
			__func__);
		return;
	}
	get_platform_config_field(
			ppd->dd, PLATFORM_CONFIG_TX_PRESET_TABLE,
			tx_preset_index, TX_PRESET_TABLE_QSFP_TX_EQ,
			&tx_preset, 4);

	if (((cache[(128 * 3) + 224] & 0xF0) >> 4) < tx_preset) {
		dd_dev_info(
			ppd->dd,
			"%s: TX EQ %x unsupported\n",
			__func__, tx_preset);

		dd_dev_info(
			ppd->dd,
			"%s: Applying EQ %x\n",
			__func__, cache[608] & 0xF0);

		tx_preset = (cache[608] & 0xF0) >> 4;
	}

	tx_eq = tx_preset | (tx_preset << 4);
	qsfp_write(ppd, ppd->dd->hfi1_id, (256 * 3) + 234, &tx_eq, 1);
	qsfp_write(ppd, ppd->dd->hfi1_id, (256 * 3) + 235, &tx_eq, 1);
}

static void apply_rx_eq_emp(struct hfi1_pportdata *ppd, u32 rx_preset_index)
{
	u32 rx_preset;
	u8 rx_eq, *cache = ppd->qsfp_info.cache;

	if (!(cache[QSFP_EQ_INFO_OFFS] & 0x2))
		return;
	get_platform_config_field(
			ppd->dd, PLATFORM_CONFIG_RX_PRESET_TABLE,
			rx_preset_index, RX_PRESET_TABLE_QSFP_RX_EMP_APPLY,
			&rx_preset, 4);

	if (!rx_preset) {
		dd_dev_info(
			ppd->dd,
			"%s: RX_EMP_APPLY is set to disabled\n",
			__func__);
		return;
	}
	get_platform_config_field(
		ppd->dd, PLATFORM_CONFIG_RX_PRESET_TABLE,
		rx_preset_index, RX_PRESET_TABLE_QSFP_RX_EMP,
		&rx_preset, 4);

	if ((cache[(128 * 3) + 224] & 0xF) < rx_preset) {
		dd_dev_info(
			ppd->dd,
			"%s: Requested RX EMP %x\n",
			__func__, rx_preset);

		dd_dev_info(
			ppd->dd,
			"%s: Applying supported EMP %x\n",
			__func__, cache[608] & 0xF);

		rx_preset = cache[608] & 0xF;
	}

	rx_eq = rx_preset | (rx_preset << 4);

	qsfp_write(ppd, ppd->dd->hfi1_id, (256 * 3) + 236, &rx_eq, 1);
	qsfp_write(ppd, ppd->dd->hfi1_id, (256 * 3) + 237, &rx_eq, 1);
}

static void apply_eq_settings(struct hfi1_pportdata *ppd,
			      u32 rx_preset_index, u32 tx_preset_index)
{
	u8 *cache = ppd->qsfp_info.cache;

	/* no point going on w/o a page 3 */
	if (cache[2] & 4) {
		dd_dev_info(ppd->dd,
			    "%s: Upper page 03 not present\n",
			    __func__);
		return;
	}

	apply_tx_eq_auto(ppd);

	apply_tx_eq_prog(ppd, tx_preset_index);

	apply_rx_eq_emp(ppd, rx_preset_index);
}

static void apply_rx_amplitude_settings(
		struct hfi1_pportdata *ppd, u32 rx_preset_index,
		u32 tx_preset_index)
{
	u32 rx_preset;
	u8 rx_amp = 0, i = 0, preferred = 0, *cache = ppd->qsfp_info.cache;

	/* no point going on w/o a page 3 */
	if (cache[2] & 4) {
		dd_dev_info(ppd->dd,
			    "%s: Upper page 03 not present\n",
			    __func__);
		return;
	}
	if (!(cache[QSFP_EQ_INFO_OFFS] & 0x1)) {
		dd_dev_info(ppd->dd,
			    "%s: RX_AMP_APPLY is set to disabled\n",
			    __func__);
		return;
	}

	get_platform_config_field(ppd->dd,
				  PLATFORM_CONFIG_RX_PRESET_TABLE,
				  rx_preset_index,
				  RX_PRESET_TABLE_QSFP_RX_AMP_APPLY,
				  &rx_preset, 4);

	if (!rx_preset) {
		dd_dev_info(ppd->dd,
			    "%s: RX_AMP_APPLY is set to disabled\n",
			    __func__);
		return;
	}
	get_platform_config_field(ppd->dd,
				  PLATFORM_CONFIG_RX_PRESET_TABLE,
				  rx_preset_index,
				  RX_PRESET_TABLE_QSFP_RX_AMP,
				  &rx_preset, 4);

	dd_dev_info(ppd->dd,
		    "%s: Requested RX AMP %x\n",
		    __func__,
		    rx_preset);

	for (i = 0; i < 4; i++) {
		if (cache[(128 * 3) + 225] & (1 << i)) {
			preferred = i;
			if (preferred == rx_preset)
				break;
		}
	}

	/*
	 * Verify that preferred RX amplitude is not just a
	 * fall through of the default
	 */
	if (!preferred && !(cache[(128 * 3) + 225] & 0x1)) {
		dd_dev_info(ppd->dd, "No supported RX AMP, not applying\n");
		return;
	}

	dd_dev_info(ppd->dd,
		    "%s: Applying RX AMP %x\n", __func__, preferred);

	rx_amp = preferred | (preferred << 4);
	qsfp_write(ppd, ppd->dd->hfi1_id, (256 * 3) + 238, &rx_amp, 1);
	qsfp_write(ppd, ppd->dd->hfi1_id, (256 * 3) + 239, &rx_amp, 1);
}

#define OPA_INVALID_INDEX 0xFFF

static void apply_tx_lanes(struct hfi1_pportdata *ppd, u8 field_id,
			   u32 config_data, const char *message)
{
	u8 i;
	int ret;

	for (i = 0; i < 4; i++) {
		ret = load_8051_config(ppd->dd, field_id, i, config_data);
		if (ret != HCMD_SUCCESS) {
			dd_dev_err(
				ppd->dd,
				"%s: %s for lane %u failed\n",
				message, __func__, i);
		}
	}
}

/*
 * Return a special SerDes setting for low power AOC cables.  The power class
 * threshold and setting being used were all found by empirical testing.
 *
 * Summary of the logic:
 *
 * if (QSFP and QSFP_TYPE == AOC and QSFP_POWER_CLASS < 4)
 *     return 0xe
 * return 0; // leave at default
 */
static u8 aoc_low_power_setting(struct hfi1_pportdata *ppd)
{
	u8 *cache = ppd->qsfp_info.cache;
	int power_class;

	/* QSFP only */
	if (ppd->port_type != PORT_TYPE_QSFP)
		return 0; /* leave at default */

	/* active optical cables only */
	switch ((cache[QSFP_MOD_TECH_OFFS] & 0xF0) >> 4) {
	case 0x0 ... 0x9: fallthrough;
	case 0xC: fallthrough;
	case 0xE:
		/* active AOC */
		power_class = get_qsfp_power_class(cache[QSFP_MOD_PWR_OFFS]);
		if (power_class < QSFP_POWER_CLASS_4)
			return 0xe;
	}
	return 0; /* leave at default */
}

static void apply_tunings(
		struct hfi1_pportdata *ppd, u32 tx_preset_index,
		u8 tuning_method, u32 total_atten, u8 limiting_active)
{
	int ret = 0;
	u32 config_data = 0, tx_preset = 0;
	u8 precur = 0, attn = 0, postcur = 0, external_device_config = 0;
	u8 *cache = ppd->qsfp_info.cache;

	/* Pass tuning method to 8051 */
	read_8051_config(ppd->dd, LINK_TUNING_PARAMETERS, GENERAL_CONFIG,
			 &config_data);
	config_data &= ~(0xff << TUNING_METHOD_SHIFT);
	config_data |= ((u32)tuning_method << TUNING_METHOD_SHIFT);
	ret = load_8051_config(ppd->dd, LINK_TUNING_PARAMETERS, GENERAL_CONFIG,
			       config_data);
	if (ret != HCMD_SUCCESS)
		dd_dev_err(ppd->dd, "%s: Failed to set tuning method\n",
			   __func__);

	/* Set same channel loss for both TX and RX */
	config_data = 0 | (total_atten << 16) | (total_atten << 24);
	apply_tx_lanes(ppd, CHANNEL_LOSS_SETTINGS, config_data,
		       "Setting channel loss");

	/* Inform 8051 of cable capabilities */
	if (ppd->qsfp_info.cache_valid) {
		external_device_config =
			((cache[QSFP_MOD_PWR_OFFS] & 0x4) << 3) |
			((cache[QSFP_MOD_PWR_OFFS] & 0x8) << 2) |
			((cache[QSFP_EQ_INFO_OFFS] & 0x2) << 1) |
			(cache[QSFP_EQ_INFO_OFFS] & 0x4);
		ret = read_8051_config(ppd->dd, DC_HOST_COMM_SETTINGS,
				       GENERAL_CONFIG, &config_data);
		/* Clear, then set the external device config field */
		config_data &= ~(u32)0xFF;
		config_data |= external_device_config;
		ret = load_8051_config(ppd->dd, DC_HOST_COMM_SETTINGS,
				       GENERAL_CONFIG, config_data);
		if (ret != HCMD_SUCCESS)
			dd_dev_err(ppd->dd,
				   "%s: Failed set ext device config params\n",
				   __func__);
	}

	if (tx_preset_index == OPA_INVALID_INDEX) {
		if (ppd->port_type == PORT_TYPE_QSFP && limiting_active)
			dd_dev_err(ppd->dd, "%s: Invalid Tx preset index\n",
				   __func__);
		return;
	}

	/* Following for limiting active channels only */
	get_platform_config_field(
		ppd->dd, PLATFORM_CONFIG_TX_PRESET_TABLE, tx_preset_index,
		TX_PRESET_TABLE_PRECUR, &tx_preset, 4);
	precur = tx_preset;

	get_platform_config_field(
		ppd->dd, PLATFORM_CONFIG_TX_PRESET_TABLE,
		tx_preset_index, TX_PRESET_TABLE_ATTN, &tx_preset, 4);
	attn = tx_preset;

	get_platform_config_field(
		ppd->dd, PLATFORM_CONFIG_TX_PRESET_TABLE,
		tx_preset_index, TX_PRESET_TABLE_POSTCUR, &tx_preset, 4);
	postcur = tx_preset;

	/*
	 * NOTES:
	 * o The aoc_low_power_setting is applied to all lanes even
	 *   though only lane 0's value is examined by the firmware.
	 * o A lingering low power setting after a cable swap does
	 *   not occur.  On cable unplug the 8051 is reset and
	 *   restarted on cable insert.  This resets all settings to
	 *   their default, erasing any previous low power setting.
	 */
	config_data = precur | (attn << 8) | (postcur << 16) |
			(aoc_low_power_setting(ppd) << 24);

	apply_tx_lanes(ppd, TX_EQ_SETTINGS, config_data,
		       "Applying TX settings");
}

/* Must be holding the QSFP i2c resource */
static int tune_active_qsfp(struct hfi1_pportdata *ppd, u32 *ptr_tx_preset,
			    u32 *ptr_rx_preset, u32 *ptr_total_atten)
{
	int ret;
	u16 lss = ppd->link_speed_supported, lse = ppd->link_speed_enabled;
	u8 *cache = ppd->qsfp_info.cache;

	ppd->qsfp_info.limiting_active = 1;

	ret = set_qsfp_tx(ppd, 0);
	if (ret)
		return ret;

	ret = qual_power(ppd);
	if (ret)
		return ret;

	ret = qual_bitrate(ppd);
	if (ret)
		return ret;

	/*
	 * We'll change the QSFP memory contents from here on out, thus we set a
	 * flag here to remind ourselves to reset the QSFP module. This prevents
	 * reuse of stale settings established in our previous pass through.
	 */
	if (ppd->qsfp_info.reset_needed) {
		ret = reset_qsfp(ppd);
		if (ret)
			return ret;
		refresh_qsfp_cache(ppd, &ppd->qsfp_info);
	} else {
		ppd->qsfp_info.reset_needed = 1;
	}

	ret = set_qsfp_high_power(ppd);
	if (ret)
		return ret;

	if (cache[QSFP_EQ_INFO_OFFS] & 0x4) {
		ret = get_platform_config_field(
			ppd->dd,
			PLATFORM_CONFIG_PORT_TABLE, 0,
			PORT_TABLE_TX_PRESET_IDX_ACTIVE_EQ,
			ptr_tx_preset, 4);
		if (ret) {
			*ptr_tx_preset = OPA_INVALID_INDEX;
			return ret;
		}
	} else {
		ret = get_platform_config_field(
			ppd->dd,
			PLATFORM_CONFIG_PORT_TABLE, 0,
			PORT_TABLE_TX_PRESET_IDX_ACTIVE_NO_EQ,
			ptr_tx_preset, 4);
		if (ret) {
			*ptr_tx_preset = OPA_INVALID_INDEX;
			return ret;
		}
	}

	ret = get_platform_config_field(
		ppd->dd, PLATFORM_CONFIG_PORT_TABLE, 0,
		PORT_TABLE_RX_PRESET_IDX, ptr_rx_preset, 4);
	if (ret) {
		*ptr_rx_preset = OPA_INVALID_INDEX;
		return ret;
	}

	if ((lss & OPA_LINK_SPEED_25G) && (lse & OPA_LINK_SPEED_25G))
		get_platform_config_field(
			ppd->dd, PLATFORM_CONFIG_PORT_TABLE, 0,
			PORT_TABLE_LOCAL_ATTEN_25G, ptr_total_atten, 4);
	else if ((lss & OPA_LINK_SPEED_12_5G) && (lse & OPA_LINK_SPEED_12_5G))
		get_platform_config_field(
			ppd->dd, PLATFORM_CONFIG_PORT_TABLE, 0,
			PORT_TABLE_LOCAL_ATTEN_12G, ptr_total_atten, 4);

	apply_cdr_settings(ppd, *ptr_rx_preset, *ptr_tx_preset);

	apply_eq_settings(ppd, *ptr_rx_preset, *ptr_tx_preset);

	apply_rx_amplitude_settings(ppd, *ptr_rx_preset, *ptr_tx_preset);

	ret = set_qsfp_tx(ppd, 1);

	return ret;
}

static int tune_qsfp(struct hfi1_pportdata *ppd,
		     u32 *ptr_tx_preset, u32 *ptr_rx_preset,
		     u8 *ptr_tuning_method, u32 *ptr_total_atten)
{
	u32 cable_atten = 0, remote_atten = 0, platform_atten = 0;
	u16 lss = ppd->link_speed_supported, lse = ppd->link_speed_enabled;
	int ret = 0;
	u8 *cache = ppd->qsfp_info.cache;

	switch ((cache[QSFP_MOD_TECH_OFFS] & 0xF0) >> 4) {
	case 0xA ... 0xB:
		ret = get_platform_config_field(
			ppd->dd,
			PLATFORM_CONFIG_PORT_TABLE, 0,
			PORT_TABLE_LOCAL_ATTEN_25G,
			&platform_atten, 4);
		if (ret)
			return ret;

		if ((lss & OPA_LINK_SPEED_25G) && (lse & OPA_LINK_SPEED_25G))
			cable_atten = cache[QSFP_CU_ATTEN_12G_OFFS];
		else if ((lss & OPA_LINK_SPEED_12_5G) &&
			 (lse & OPA_LINK_SPEED_12_5G))
			cable_atten = cache[QSFP_CU_ATTEN_7G_OFFS];

		/* Fallback to configured attenuation if cable memory is bad */
		if (cable_atten == 0 || cable_atten > 36) {
			ret = get_platform_config_field(
				ppd->dd,
				PLATFORM_CONFIG_SYSTEM_TABLE, 0,
				SYSTEM_TABLE_QSFP_ATTENUATION_DEFAULT_25G,
				&cable_atten, 4);
			if (ret)
				return ret;
		}

		ret = get_platform_config_field(
			ppd->dd, PLATFORM_CONFIG_PORT_TABLE, 0,
			PORT_TABLE_REMOTE_ATTEN_25G, &remote_atten, 4);
		if (ret)
			return ret;

		*ptr_total_atten = platform_atten + cable_atten + remote_atten;

		*ptr_tuning_method = OPA_PASSIVE_TUNING;
		break;
	case 0x0 ... 0x9: fallthrough;
	case 0xC: fallthrough;
	case 0xE:
		ret = tune_active_qsfp(ppd, ptr_tx_preset, ptr_rx_preset,
				       ptr_total_atten);
		if (ret)
			return ret;

		*ptr_tuning_method = OPA_ACTIVE_TUNING;
		break;
	case 0xD: fallthrough;
	case 0xF:
	default:
		dd_dev_warn(ppd->dd, "%s: Unknown/unsupported cable\n",
			    __func__);
		break;
	}
	return ret;
}

/*
 * This function communicates its success or failure via ppd->driver_link_ready
 * Thus, it depends on its association with start_link(...) which checks
 * driver_link_ready before proceeding with the link negotiation and
 * initialization process.
 */
void tune_serdes(struct hfi1_pportdata *ppd)
{
	int ret = 0;
	u32 total_atten = 0;
	u32 remote_atten = 0, platform_atten = 0;
	u32 rx_preset_index, tx_preset_index;
	u8 tuning_method = 0, limiting_active = 0;
	struct hfi1_devdata *dd = ppd->dd;

	rx_preset_index = OPA_INVALID_INDEX;
	tx_preset_index = OPA_INVALID_INDEX;

	/* the link defaults to enabled */
	ppd->link_enabled = 1;
	/* the driver link ready state defaults to not ready */
	ppd->driver_link_ready = 0;
	ppd->offline_disabled_reason = HFI1_ODR_MASK(OPA_LINKDOWN_REASON_NONE);

	/* Skip the tuning for testing (loopback != none) and simulations */
	if (loopback != LOOPBACK_NONE ||
	    ppd->dd->icode == ICODE_FUNCTIONAL_SIMULATOR) {
		ppd->driver_link_ready = 1;

		if (qsfp_mod_present(ppd)) {
			ret = acquire_chip_resource(ppd->dd,
						    qsfp_resource(ppd->dd),
						    QSFP_WAIT);
			if (ret) {
				dd_dev_err(ppd->dd, "%s: hfi%d: cannot lock i2c chain\n",
					   __func__, (int)ppd->dd->hfi1_id);
				goto bail;
			}

			refresh_qsfp_cache(ppd, &ppd->qsfp_info);
			release_chip_resource(ppd->dd, qsfp_resource(ppd->dd));
		}

		return;
	}

	switch (ppd->port_type) {
	case PORT_TYPE_DISCONNECTED:
		ppd->offline_disabled_reason =
			HFI1_ODR_MASK(OPA_LINKDOWN_REASON_DISCONNECTED);
		dd_dev_warn(dd, "%s: Port disconnected, disabling port\n",
			    __func__);
		goto bail;
	case PORT_TYPE_FIXED:
		/* platform_atten, remote_atten pre-zeroed to catch error */
		get_platform_config_field(
			ppd->dd, PLATFORM_CONFIG_PORT_TABLE, 0,
			PORT_TABLE_LOCAL_ATTEN_25G, &platform_atten, 4);

		get_platform_config_field(
			ppd->dd, PLATFORM_CONFIG_PORT_TABLE, 0,
			PORT_TABLE_REMOTE_ATTEN_25G, &remote_atten, 4);

		total_atten = platform_atten + remote_atten;

		tuning_method = OPA_PASSIVE_TUNING;
		break;
	case PORT_TYPE_VARIABLE:
		if (qsfp_mod_present(ppd)) {
			/*
			 * platform_atten, remote_atten pre-zeroed to
			 * catch error
			 */
			get_platform_config_field(
				ppd->dd, PLATFORM_CONFIG_PORT_TABLE, 0,
				PORT_TABLE_LOCAL_ATTEN_25G,
				&platform_atten, 4);

			get_platform_config_field(
				ppd->dd, PLATFORM_CONFIG_PORT_TABLE, 0,
				PORT_TABLE_REMOTE_ATTEN_25G,
				&remote_atten, 4);

			total_atten = platform_atten + remote_atten;

			tuning_method = OPA_PASSIVE_TUNING;
		} else {
			ppd->offline_disabled_reason =
			     HFI1_ODR_MASK(OPA_LINKDOWN_REASON_CHASSIS_CONFIG);
			goto bail;
		}
		break;
	case PORT_TYPE_QSFP:
		if (qsfp_mod_present(ppd)) {
			ret = acquire_chip_resource(ppd->dd,
						    qsfp_resource(ppd->dd),
						    QSFP_WAIT);
			if (ret) {
				dd_dev_err(ppd->dd, "%s: hfi%d: cannot lock i2c chain\n",
					   __func__, (int)ppd->dd->hfi1_id);
				goto bail;
			}
			refresh_qsfp_cache(ppd, &ppd->qsfp_info);

			if (ppd->qsfp_info.cache_valid) {
				ret = tune_qsfp(ppd,
						&tx_preset_index,
						&rx_preset_index,
						&tuning_method,
						&total_atten);

				/*
				 * We may have modified the QSFP memory, so
				 * update the cache to reflect the changes
				 */
				refresh_qsfp_cache(ppd, &ppd->qsfp_info);
				limiting_active =
						ppd->qsfp_info.limiting_active;
			} else {
				dd_dev_err(dd,
					   "%s: Reading QSFP memory failed\n",
					   __func__);
				ret = -EINVAL; /* a fail indication */
			}
			release_chip_resource(ppd->dd, qsfp_resource(ppd->dd));
			if (ret)
				goto bail;
		} else {
			ppd->offline_disabled_reason =
			   HFI1_ODR_MASK(
				OPA_LINKDOWN_REASON_LOCAL_MEDIA_NOT_INSTALLED);
			goto bail;
		}
		break;
	default:
		dd_dev_warn(ppd->dd, "%s: Unknown port type\n", __func__);
		ppd->port_type = PORT_TYPE_UNKNOWN;
		tuning_method = OPA_UNKNOWN_TUNING;
		total_atten = 0;
		limiting_active = 0;
		tx_preset_index = OPA_INVALID_INDEX;
		break;
	}

	if (ppd->offline_disabled_reason ==
			HFI1_ODR_MASK(OPA_LINKDOWN_REASON_NONE))
		apply_tunings(ppd, tx_preset_index, tuning_method,
			      total_atten, limiting_active);

	if (!ret)
		ppd->driver_link_ready = 1;

	return;
bail:
	ppd->driver_link_ready = 0;
}
