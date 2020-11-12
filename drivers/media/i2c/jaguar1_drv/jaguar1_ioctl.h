/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __JAGUAR1_IOCTL_H__
#define __JAGUAR1_IOCTL_H__

/*----------------------- Set All - for MIPI interface  ---------------------*/
#define IOC_VDEC_INIT_ALL                     0xF0

/*----------------------- VIDEO Initialize  ---------------------*/
#define IOC_VDEC_INPUT_INIT			 		  0x10
#define IOC_VDEC_OUTPUT_SEQ_SET		  		  0x11
#define IOC_VDEC_VIDEO_EQ_SET		  	  	  0x13
#define IOC_VDEC_VIDEO_SW_RESET				  0x14
#define IOC_VDEC_SINGLE_DIFFERNTIAL_SET		  0x15
#define IOC_VDEC_VIDEO_EQ_CABLE_SET		  	  0x16
#define IOC_VDEC_VIDEO_EQ_ANALOG_INPUT_SET	  0x17
#define IOC_VDEC_VIDEO_GET_VIDEO_LOSS         0x18

/*----------------------- Coaxial protocol  ---------------------*/
// Coax UP Stream - 8bit
#define IOC_VDEC_COAX_TX_INIT			  0xA0
#define IOC_VDEC_COAX_TX_CMD_SEND	  0xA1

// Coax UP Stream - 16bit only ACP 720P Support
#define IOC_VDEC_COAX_TX_16BIT_INIT		  0xB4
#define IOC_VDEC_COAX_TX_16BIT_CMD_SEND	  0xB5
#define IOC_VDEC_COAX_TX_CVI_NEW_CMD_SEND 0xB6

// Coax Down Stream
#define IOC_VDEC_COAX_RX_INIT      0xA2
#define IOC_VDEC_COAX_RX_DATA_READ 0xA3
#define IOC_VDEC_COAX_RX_BUF_CLEAR 0xA4
#define IOC_VDEC_COAX_RX_DEINIT    0xA5

// Coax Test
#define IOC_VDEC_COAX_TEST_TX_INIT_DATA_READ  0xA6
#define IOC_VDEC_COAX_TEST_DATA_SET           0xA7
#define IOC_VDEC_COAX_TEST_DATA_READ          0xA8


// Coax FW Update
#define IOC_VDEC_COAX_FW_ACP_HEADER_GET     0xA9
#define IOC_VDEC_COAX_FW_READY_CMD_SET  0xAA
#define IOC_VDEC_COAX_FW_READY_ACK_GET  0xAB
#define IOC_VDEC_COAX_FW_START_CMD_SET  0xAC
#define IOC_VDEC_COAX_FW_START_ACK_GET  0xAD
#define IOC_VDEC_COAX_FW_SEND_DATA_SET  0xAE
#define IOC_VDEC_COAX_FW_SEND_ACK_GET   0xAF
#define IOC_VDEC_COAX_FW_END_CMD_SET    0xB0
#define IOC_VDEC_COAX_FW_END_ACK_GET    0xB1

// Bank Dump Test
#define IOC_VDEC_COAX_BANK_DUMP_GET    0xB2

// ACP Option
#define IOC_VDEC_COAX_RT_NRT_MODE_CHANGE_SET 0xB3
#define IOC_VDEC_COAX_RX_DETECTION_READ      0x12
#define IOC_VDEC_ACP_WRITE                   0xB7


/*----------------------- MOTION -----------------*/
#define IOC_VDEC_MOTION_SET			0x70
#define IOC_VDEC_MOTION_PIXEL_SET     0x71
#define IOC_VDEC_MOTION_PIXEL_GET     0x72
#define IOC_VDEC_MOTION_TSEN_SET      0x73
#define IOC_VDEC_MOTION_PSEN_SET      0x74
#define IOC_VDEC_MOTION_ALL_PIXEL_SET 0x75
#define IOC_VDEC_MOTION_DETECTION_GET 0x76

/*----------------------  GET CHIP ID FUNCTION ---------------------*/
#define IOC_VDEC_GET_CHIP_ID		0x90
#define IOC_VDEC_CH_SW_RESET		0x91
#define IOC_VDEC_HAFC_GAIN12_CTRL	0x92
#define IOC_VDEC_AFE_RESET			0x93
#define IOC_VDEC_GET_DRIVERVER      0x94

#define IOC_VDEC_MANUAL_AGC_STABLE_ENABLE	0x82
#define IOC_VDEC_MANUAL_AGC_STABLE_DISABLE	0x83

#endif
