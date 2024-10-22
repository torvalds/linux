/* SPDX-License-Identifier: GPL-2.0 */

#ifndef OPEN_ALLIANCE_HELPERS_H
#define OPEN_ALLIANCE_HELPERS_H

/*
 * These defines reflect the TDR (Time Delay Reflection) diagnostic feature
 * for 1000BASE-T1 automotive Ethernet PHYs as specified by the OPEN Alliance.
 *
 * The register values are part of the HDD.TDR register, which provides
 * information about the cable status and faults. The exact register offset
 * is device-specific and should be provided by the driver.
 */
#define OA_1000BT1_HDD_TDR_ACTIVATION_MASK		GENMASK(1, 0)
#define OA_1000BT1_HDD_TDR_ACTIVATION_OFF		1
#define OA_1000BT1_HDD_TDR_ACTIVATION_ON		2

#define OA_1000BT1_HDD_TDR_STATUS_MASK			GENMASK(7, 4)
#define OA_1000BT1_HDD_TDR_STATUS_SHORT			3
#define OA_1000BT1_HDD_TDR_STATUS_OPEN			6
#define OA_1000BT1_HDD_TDR_STATUS_NOISE			5
#define OA_1000BT1_HDD_TDR_STATUS_CABLE_OK		7
#define OA_1000BT1_HDD_TDR_STATUS_TEST_IN_PROGRESS	8
#define OA_1000BT1_HDD_TDR_STATUS_TEST_NOT_POSSIBLE	13

/*
 * OA_1000BT1_HDD_TDR_DISTANCE_MASK:
 * This mask is used to extract the distance to the first/main fault
 * detected by the TDR feature. Each bit represents an approximate distance
 * of 1 meter, ranging from 0 to 31 meters. The exact interpretation of the
 * bits may vary, but generally:
 * 000000 = no error
 * 000001 = error about 0-1m away
 * 000010 = error between 1-2m away
 * ...
 * 011111 = error about 30-31m away
 * 111111 = resolution not possible / out of distance
 */
#define OA_1000BT1_HDD_TDR_DISTANCE_MASK			GENMASK(13, 8)
#define OA_1000BT1_HDD_TDR_DISTANCE_NO_ERROR			0
#define OA_1000BT1_HDD_TDR_DISTANCE_RESOLUTION_NOT_POSSIBLE	0x3f

int oa_1000bt1_get_ethtool_cable_result_code(u16 reg_value);
int oa_1000bt1_get_tdr_distance(u16 reg_value);

#endif /* OPEN_ALLIANCE_HELPERS_H */

