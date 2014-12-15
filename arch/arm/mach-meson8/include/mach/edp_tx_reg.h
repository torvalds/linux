#ifndef EDP_TX_REG_H
#define EDP_TX_REG_H

#define EDP_TX_LINK_BW_SET								0x0000
#define EDP_TX_LINK_COUNT_SET							0x0004
#define EDP_TX_ENHANCED_FRAME_EN						0x0008
#define EDP_TX_TRAINING_PATTERN_SET						0x000c
#define EDP_TX_LINK_QUAL_PATTERN_SET					0x0010
#define EDP_TX_SCRAMBLING_DISABLE						0x0014
#define EDP_TX_DOWNSPREAD_CTRL							0x0018
#define EDP_TX_ALTERNATE_SCRAMBLER_RESET				0x001c
#define EDP_TX_PANEL_SELF_REFRESH						0x0020

#define EDP_TX_TRANSMITTER_OUTPUT_ENABLE				0x0080
#define EDP_TX_MAIN_STREAM_ENABLE						0x0084
#define EDP_TX_SECONDARY_STREAM_ENABLE					0x0088
#define EDP_TX_FORCE_SCRAMBLER_RESET					0x00c0
#define EDP_TX_USER_CONTROL_STATUS						0x00c4

#define EDP_TX_CORE_CAPBILITIES							0x00f8
#define EDP_TX_CORE_ID									0x00fc

#define EDP_TX_AUX_COMMAND								0x0100
#define EDP_TX_AUX_WRITE_FIFO							0x0104
#define EDP_TX_AUX_ADDRESS								0x0108
#define EDP_TX_AUX_CLOCK_DIVIDER						0x010c
#define EDP_TX_AUX_STATE								0x0130
#define EDP_TX_AUX_REPLY_DATA							0x0134
#define EDP_TX_AUX_REPLY_CODE							0x0138
#define EDP_TX_AUX_REPLY_COUNT							0x013c
#define EDP_TX_AUX_INTERRUPT_STATUS						0x0140
#define EDP_TX_AUX_INTERRUPT_MASK						0x0144
#define EDP_TX_AUX_REPLY_DATA_COUNT						0x0148
#define EDP_TX_AUX_STATUS								0x014c
#define EDP_TX_AUX_REPLY_CLOCK_WIDTH					0x0150

#define EDP_TX_MAIN_STREAM_HTOTAL						0x0180
#define EDP_TX_MAIN_STREAM_VTOTAL						0x0184
#define EDP_TX_MAIN_STREAM_POLARITY						0x0188
#define EDP_TX_MAIN_STREAM_HSWIDTH						0x018c
#define EDP_TX_MAIN_STREAM_VSWIDTH						0x0190
#define EDP_TX_MAIN_STREAM_HRES							0x0194
#define EDP_TX_MAIN_STREAM_VRES							0x0198
#define EDP_TX_MAIN_STREAM_HSTART						0x019c
#define EDP_TX_MAIN_STREAM_VSTART						0x01a0
#define EDP_TX_MAIN_STREAM_MISC0						0x01a4
#define EDP_TX_MAIN_STREAM_MISC1						0x01a8
#define EDP_TX_MAIN_STREAM_M_VID						0x01ac
#define EDP_TX_MAIN_STREAM_TRANSFER_UNIT_SIZE			0x01b0
#define EDP_TX_MAIN_STREAM_N_VID						0x01b4
#define EDP_TX_MAIN_STREAM_USER_PIXEL_WIDTH				0x01b8
#define EDP_TX_MAIN_STREAM_DATA_COUNT_PER_LANE			0x01bc
#define EDP_TX_MAIN_STREAM_INTERLACED					0x01c0
#define EDP_TX_MAIN_STREAM_USER_SYNC_POLARITY			0x01c4

#define EDP_TX_PHY_RESET								0x0200
#define EDP_TX_PHY_PRE_EMPHASIS_LANE_0					0x0210
#define EDP_TX_PHY_PRE_EMPHASIS_LANE_1					0x0214
#define EDP_TX_PHY_PRE_EMPHASIS_LANE_2					0x0218
#define EDP_TX_PHY_PRE_EMPHASIS_LANE_3					0x021c
#define EDP_TX_PHY_VOLTAGE_DIFF_LANE_0					0x0220
#define EDP_TX_PHY_VOLTAGE_DIFF_LANE_1					0x0224
#define EDP_TX_PHY_VOLTAGE_DIFF_LANE_2					0x0228
#define EDP_TX_PHY_VOLTAGE_DIFF_LANE_3					0x022c
#define EDP_TX_PHY_TRANSMIT_PRBS7						0x0230
#define EDP_TX_PHY_POWER_DOWN							0x0238
#define EDP_TX_PHY_POST_EMPHASIS_LANE_0					0x0240
#define EDP_TX_PHY_POST_EMPHASIS_LANE_1					0x0244
#define EDP_TX_PHY_POST_EMPHASIS_LANE_2					0x024c
#define EDP_TX_PHY_POST_EMPHASIS_LANE_3					0x024c
#define EDP_TX_PHY_STATUS								0x0280

#define EDP_TX_HDCP_ENABLE								0x0400
#define EDP_TX_HDCP_KM_LOWER							0x0410
#define EDP_TX_HDCP_KM_UPPER							0x0414
#define EDP_TX_HDCP_AN_LOWER							0x0418
#define EDP_TX_HDCP_AN_UPPER							0x041c
#define EDP_TX_HDCP_AUTO_AN_VALUE_LOWER					0x0420
#define EDP_TX_HDCP_AUTO_AN_VALUE_UPPER					0x0424
#define EDP_TX_HDCP_STATUS								0x0428


//***************************************************************************************************//
//******note: below address are not eDP Tx core register's, but eDP sink device DPCD register's******//
//***************************************************************************************************//
//AUX offset address
//DPCD information
//***************************************************************************************************//
#define EDP_DPCD_REVISION							0x0000
#define EDP_DPCD_MAX_LINK_RATE						0x0001
#define EDP_DPCD_MAX_LANE_COUNT						0x0002
#define EDP_DPCD_MAX_DOWNSPREAD						0x0003
#define EDP_DPCD_NUM_RX_PORTS						0x0004
#define EDP_DPCD_DOWNSTREAM_PORTS_PRESENT			0x0005
#define EDP_DPCD_MAIN_LINK_CODING					0x0006
#define EDP_DPCD_NUM_DOWNSTREAM_PORTS				0x0007
#define EDP_DPCD_RX_PORT0_CAPS_0					0x0008
#define EDP_DPCD_RX_PORT0_CAPS_1					0x0009
#define EDP_DPCD_RX_PORT1_CAPS_0					0x000A
#define EDP_DPCD_RX_PORT1_CAPS_1					0x000B
#define EDP_DPCD_I2C_SPEED_CAP						0x000C	//v1.2? 
#define EDP_DPCD_CONFIGURATION_CAP					0x000D	//v1.2? 
#define EDP_DPCD_TRAINING_AUX_RD_INTERVAL			0x000E	//v1.2?
#define EDP_DPCD_MUTI_STREAM_TRANSPORT_CAP			0x0021	//v1.2
#define EDP_DPCD_PSR_SUPPORT						0x0070	//v1.2?
#define EDP_DPCD_PSR_CAPS							0x0071	//v1.2?
#define EDP_DPCD_DOWNSTREAM_PORT_CAPS				0x0080 // Downstream Port 0 - 15 Capabilities

#define EDP_DPCD_LINK_BANDWIDTH_SET					0x0100
#define EDP_DPCD_LANE_COUNT_SET						0x0101
#define EDP_DPCD_TRAINING_PATTERN_SET				0x0102
#define EDP_DPCD_TRAINING_LANE0_SET					0x0103
#define EDP_DPCD_TRAINING_LANE1_SET					0x0104
#define EDP_DPCD_TRAINING_LANE2_SET					0x0105
#define EDP_DPCD_TRAINING_LANE3_SET					0x0106
#define EDP_DPCD_DOWNSPREAD_CONTROL					0x0107
#define EDP_DPCD_MAIN_LINK_CODING_SET				0x0108
#define EDP_DPCD_CONFIGURATION_SET					0x010A

#define EDP_DPCD_SINK_COUNT							0x0200
#define EDP_DPCD_DEVICE_SERVICE_IRQ					0x0201
#define EDP_DPCD_STATUS_LANE_0_1					0x0202
#define EDP_DPCD_STATUS_LANE_2_3					0x0203
#define EDP_DPCD_LANE_ALIGNMENT_STATUS_UPDATED		0x0204
#define EDP_DPCD_SINK_STATUS						0x0205
#define EDP_DPCD_ADJUST_REQUEST_LANE_0_1			0x0206
#define EDP_DPCD_ADJUST_REQUEST_LANE_2_3			0x0207
#define EDP_DPCD_TRAINING_SCORE_LANE_0				0x0208
#define EDP_DPCD_TRAINING_SCORE_LANE_1				0x0209
#define EDP_DPCD_TRAINING_SCORE_LANE_2				0x020A
#define EDP_DPCD_TRAINING_SCORE_LANE_3				0x020B

#define EDP_DPCD_SYMBOL_ERROR_COUNT_LANE_0			0x0210
#define EDP_DPCD_SYMBOL_ERROR_COUNT_LANE_1			0x0212 
#define EDP_DPCD_SYMBOL_ERROR_COUNT_LANE_2			0x0214
#define EDP_DPCD_SYMBOL_ERROR_COUNT_LANE_3			0x0216

#define EDP_DPCD_TEST_REQUEST						0x0218
#define EDP_DPCD_TEST_LINK_RATE						0x0219
#define EDP_DPCD_TEST_LANE_COUNT					0x0220
#define EDP_DPCD_TEST_PATTERN						0x0221
#define EDP_DPCD_TEST_RESPONSE						0x0260
#define EDP_DPCD_TEST_EDID_CHECKSUM					0x0261

// Source Device-Specific Field 0x0303 - 0x003FF : Reserved for vendor-specific usage
#define EDP_DPCD_SOURCE_IEEE_OUI_0					0x0300 // Source IEEE OUI 7:0
#define EDP_DPCD_SOURCE_IEEE_OUI_1					0x0301 // Source IEEE OUI 15:8
#define EDP_DPCD_SOURCE_IEEE_OUI_2					0x0302 // Source IEEE OUI 23:16

// Sink Device-Specific Field    0x0403 - 0x004FF : Reserved for vendor-specific usage
#define EDP_DPCD_SINK_IEEE_OUT_0					0x0400 // Sink IEEE OUI 7:0
#define EDP_DPCD_SINK_IEEE_OUT_1					0x0401 // Sink IEEE OUI 15:8
#define EDP_DPCD_SINK_IEEE_OUT_2					0x0402 // Sink IEEE OUI 23:16 

// Branch Device-Specific Field  0x0503 - 0x005FF : Reserved for vendor-specific usage
#define EDP_DPCD_BRANCH_DEVICE_IEEE_OUI_0			0x0500 // Branch Device IEEE OUI 7:0
#define EDP_DPCD_BRANCH_DEVICE_IEEE_OUI_1			0x0501 // Branch Device IEEE OUI 15:8
#define EDP_DPCD_BRANCH_DEVICE_IEEE_OUI_2			0x0502 // Branch Device IEEE OUI 23:16
#define EDP_DPCD_SET_POWER							0x0600 // Set Power

//HDCP field in DPCD
#define EDP_DPCD_HDCP_BKSV							0x68000
#define EDP_DPCD_HDCP_ROPRIME						0x68005
#define EDP_DPCD_HDCP_AKSV							0x68007
#define EDP_DPCD_HDCP_AN							0x6800c
#define EDP_DPCD_HDCP_VPRIME_H1						0x68014
#define EDP_DPCD_HDCP_VPRIME_H2						0x68018
#define EDP_DPCD_HDCP_VPRIME_H3						0x6801c
#define EDP_DPCD_HDCP_VPRIME_H4						0x68020
#define EDP_DPCD_HDCP_VPRIME_H5						0x68024
#define EDP_DPCD_HDCP_BCAPS							0x68028
#define EDP_DPCD_HDCP_BSTATUS						0x68029
#define EDP_DPCD_HDCP_BINFO							0x6802a
#define EDP_DPCD_HDCP_KSVFIFO						0x6802c
#define EDP_DPCD_HDCP_RESERVED						0x6803b
#define EDP_DPCD_HDCP_DEBUG							0x680c0

#endif