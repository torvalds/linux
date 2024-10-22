// SPDX-License-Identifier: GPL-2.0+
/*
 * open_alliance_helpers.c - OPEN Alliance specific PHY diagnostic helpers
 *
 * This file contains helper functions for implementing advanced diagnostic
 * features as specified by the OPEN Alliance for automotive Ethernet PHYs.
 * These helpers include functionality for Time Delay Reflection (TDR), dynamic
 * channel quality assessment, and other PHY diagnostics.
 *
 * For more information on the specifications, refer to the OPEN Alliance
 * documentation: https://opensig.org/automotive-ethernet-specifications/
 * Currently following specifications are partially or fully implemented:
 * - Advanced diagnostic features for 1000BASE-T1 automotive Ethernet PHYs.
 *   TC12 - advanced PHY features.
 *   https://opensig.org/wp-content/uploads/2024/03/Advanced_PHY_features_for_automotive_Ethernet_v2.0_fin.pdf
 */

#include <linux/bitfield.h>
#include <linux/ethtool_netlink.h>

#include "open_alliance_helpers.h"

/**
 * oa_1000bt1_get_ethtool_cable_result_code - Convert TDR status to ethtool
 *					      result code
 * @reg_value: Value read from the TDR register
 *
 * This function takes a register value from the HDD.TDR register and converts
 * the TDR status to the corresponding ethtool cable test result code.
 *
 * Return: The appropriate ethtool result code based on the TDR status
 */
int oa_1000bt1_get_ethtool_cable_result_code(u16 reg_value)
{
	u8 tdr_status = FIELD_GET(OA_1000BT1_HDD_TDR_STATUS_MASK, reg_value);
	u8 dist_val = FIELD_GET(OA_1000BT1_HDD_TDR_DISTANCE_MASK, reg_value);

	switch (tdr_status) {
	case OA_1000BT1_HDD_TDR_STATUS_CABLE_OK:
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	case OA_1000BT1_HDD_TDR_STATUS_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	case OA_1000BT1_HDD_TDR_STATUS_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	case OA_1000BT1_HDD_TDR_STATUS_NOISE:
		return ETHTOOL_A_CABLE_RESULT_CODE_NOISE;
	default:
		if (dist_val == OA_1000BT1_HDD_TDR_DISTANCE_RESOLUTION_NOT_POSSIBLE)
			return ETHTOOL_A_CABLE_RESULT_CODE_RESOLUTION_NOT_POSSIBLE;
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}
EXPORT_SYMBOL_GPL(oa_1000bt1_get_ethtool_cable_result_code);

/**
 * oa_1000bt1_get_tdr_distance - Get distance to the main fault from TDR
 *				 register value
 * @reg_value: Value read from the TDR register
 *
 * This function takes a register value from the HDD.TDR register and extracts
 * the distance to the main fault detected by the TDR feature. The distance is
 * measured in centimeters and ranges from 0 to 3100 centimeters. If the
 * distance is not available (0x3f), the function returns -ERANGE.
 *
 * Return: The distance to the main fault in centimeters, or -ERANGE if the
 * resolution is not possible.
 */
int oa_1000bt1_get_tdr_distance(u16 reg_value)
{
	u8 dist_val = FIELD_GET(OA_1000BT1_HDD_TDR_DISTANCE_MASK, reg_value);

	if (dist_val == OA_1000BT1_HDD_TDR_DISTANCE_RESOLUTION_NOT_POSSIBLE)
		return -ERANGE;

	return dist_val * 100;
}
EXPORT_SYMBOL_GPL(oa_1000bt1_get_tdr_distance);
