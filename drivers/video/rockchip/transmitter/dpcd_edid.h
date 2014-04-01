#ifndef __DPCD_EDID_H
#define __DPCD_EDID_H
#include "../../edid.h"

#define DPCD_REV					0x00
#define DPCD_MAX_LINK_RATE				0x01
#define DPCD_MAX_LANE_CNT				0x02	

#define DPCD_MAX_DOWNSPREAD				0x03
#define DPCD_NORP					0x04
#define DPCD_DOWNSTREAMPORT_PRESENT			0x05

#define DPCD_RECEIVE_PORT0_CAP_0			0x08
#define DPCD_RECEIVE_PORT0_CAP_1			0x09
#define DPCD_RECEIVE_PORT0_CAP_2			0x0a
#define DPCD_RECEIVE_PORT0_CAP_3			0x0b

#define DPCD_LINK_BW_SET				0x100
#define DPCD_LANE_CNT_SET				0x101
#define DPCD_TRAINING_PATTERN_SET			0x102
#define DPCD_TRAINING_LANE0_SET				0x103
#define DPCD_TRAINING_LANE1_SET				0x104
#define DPCD_TRAINING_LANE2_SET				0x105
#define DPCD_TRAINING_LANE3_SET				0x106
#define DPCD_DOWNSPREAD_CTRL				0x107

#define DPCD_SINK_COUNT					0x200
#define DPCD_DEVICE_SERVICE_IRQ_VECTOR			0x201
#define DPCD_LANE0_1_STATUS				0x202
#define DPCD_LANE2_3_STATUS				0x203
#define DPCD_LANE_ALIGN_STATUS_UPDATED            	0x204
#define DPCD_SINK_STATUS                                0x205
#define DPCD_ADJUST_REQUEST_LANE0_1                     0x206
#define DPCD_ADJUST_REQUEST_LANE2_3                     0x207
#define DPCD_TRAINING_SCORE_LANE0                       0x208
#define DPCD_TRAINING_SCORE_LANE1                       0x209
#define DPCD_TRAINING_SCORE_LANE2                       0x20a
#define DPCD_TRAINING_SCORE_LANE3                       0x20b
#define DPCD_SYMBOL_ERR_CONUT_LANE0			0x210
#define DPCD_SINK_POWER_STATE				0x0600

/* DPCD_ADDR_MAX_LANE_COUNT */
#define DPCD_ENHANCED_FRAME_CAP(x)		(((x) >> 7) & 0x1)
#define DPCD_MAX_LANE_COUNT(x)			((x) & 0x1f)

/* DPCD_ADDR_LANE_COUNT_SET */
#define DPCD_ENHANCED_FRAME_EN			(0x1 << 7)
#define DPCD_LANE_COUNT_SET(x)			((x) & 0x1f)

/* DPCD_ADDR_TRAINING_PATTERN_SET */
#define DPCD_SCRAMBLING_DISABLED		(0x1 << 5)
#define DPCD_SCRAMBLING_ENABLED			(0x0 << 5)
#define DPCD_TRAINING_PATTERN_2			(0x2 << 0)
#define DPCD_TRAINING_PATTERN_1			(0x1 << 0)
#define DPCD_TRAINING_PATTERN_DISABLED		(0x0 << 0)

/* DPCD_ADDR_TRAINING_LANE0_SET */
#define DPCD_MAX_PRE_EMPHASIS_REACHED		(0x1 << 5)
#define DPCD_PRE_EMPHASIS_SET(x)		(((x) & 0x3) << 3)
#define DPCD_PRE_EMPHASIS_GET(x)		(((x) >> 3) & 0x3)
#define DPCD_PRE_EMPHASIS_PATTERN2_LEVEL0	(0x0 << 3)
#define DPCD_MAX_SWING_REACHED			(0x1 << 2)
#define DPCD_VOLTAGE_SWING_SET(x)		(((x) & 0x3) << 0)
#define DPCD_VOLTAGE_SWING_GET(x)		(((x) >> 0) & 0x3)
#define DPCD_VOLTAGE_SWING_PATTERN1_LEVEL0	(0x0 << 0)

/* DPCD_ADDR_LANE0_1_STATUS */
#define DPCD_LANE_SYMBOL_LOCKED			(0x1 << 2)
#define DPCD_LANE_CHANNEL_EQ_DONE		(0x1 << 1)
#define DPCD_LANE_CR_DONE			(0x1 << 0)
#define DPCD_CHANNEL_EQ_BITS			(DPCD_LANE_CR_DONE|	\
						 DPCD_LANE_CHANNEL_EQ_DONE|\
						 DPCD_LANE_SYMBOL_LOCKED)

#define DPCD_TEST_REQUEST                               0x218
#define DPCD_TEST_LINK_RATE                             0x219

#define DPCD_TEST_LANE_COUNT                            0x220
#define DPCD_TEST_RESPONSE                            	0x260
#define DPCD_TEST_EDID_CHECKSUM				0x261
#define TEST_ACK                                        0x01
#define DPCD_TEST_EDID_Checksum_Write                   0x04//bit position

#define DPCD_TEST_EDID_Checksum                         0x261


#define DPCD_SPECIFIC_INTERRUPT                         0x10
#define DPCD_USER_COMM1                                 0x22//define for downstream HDMI Rx sense detection

#define AUX_ADDR_7_0(x)					(((x) >> 0) & 0xff)
#define AUX_ADDR_15_8(x)				(((x) >> 8) & 0xff)
#define AUX_ADDR_19_16(x)				(((x) >> 16) & 0x0f)

#define AUX_RX_COMM_I2C_DEFER				(0x2 << 2)
#define AUX_RX_COMM_AUX_DEFER				(0x2 << 0)

/* DPCD_ADDR_LANE_ALIGN__STATUS_UPDATED */
#define DPCD_LINK_STATUS_UPDATED			(0x1 << 7)
#define DPCD_DOWNSTREAM_PORT_STATUS_CHANGED		(0x1 << 6)
#define DPCD_INTERLANE_ALIGN_DONE			(0x1 << 0)

/* DPCD_ADDR_TEST_REQUEST */
#define DPCD_TEST_EDID_READ				(0x1 << 2)

/* DPCD_ADDR_TEST_RESPONSE */
#define DPCD_TEST_EDID_CHECKSUM_WRITE			(0x1 << 2)

/* DPCD_ADDR_SINK_POWER_STATE */
#define DPCD_SET_POWER_STATE_D0				(0x1 << 0)
#define DPCD_SET_POWER_STATE_D4				(0x2 << 0)


/*
 * EDID device address is 0x50.
 * However, if necessary, you must have set upper address
 * into E-EDID in I2C device, 0x30.
 */

#define EDID_ADDR 					0x50
#define E_EDID_ADDR					0x30
#define EDID_EXTENSION_FLAG				0x7e
#define EDID_CHECKSUM					0x7f	

static unsigned char inline edp_calc_edid_check_sum(unsigned char *edid_data)
{
	int i;
	unsigned char sum = 0;

	for (i = 0; i < EDID_LENGTH; i++)
		sum = sum + edid_data[i];

	return sum;
}

#endif
