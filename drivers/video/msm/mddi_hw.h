/* drivers/video/msm_fb/mddi_hw.h
 *
 * MSM MDDI Hardware Registers and Structures
 *
 * Copyright (C) 2007 QUALCOMM Incorporated
 * Copyright (C) 2007 Google Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _MDDI_HW_H_
#define _MDDI_HW_H_

#include <linux/types.h>

#define MDDI_CMD                0x0000
#define MDDI_VERSION            0x0004
#define MDDI_PRI_PTR            0x0008
#define MDDI_SEC_PTR            0x000c
#define MDDI_BPS                0x0010
#define MDDI_SPM                0x0014
#define MDDI_INT                0x0018
#define MDDI_INTEN              0x001c
#define MDDI_REV_PTR            0x0020
#define MDDI_REV_SIZE           0x0024
#define MDDI_STAT               0x0028
#define MDDI_REV_RATE_DIV       0x002c
#define MDDI_REV_CRC_ERR        0x0030
#define MDDI_TA1_LEN            0x0034
#define MDDI_TA2_LEN            0x0038
#define MDDI_TEST_BUS           0x003c
#define MDDI_TEST               0x0040
#define MDDI_REV_PKT_CNT        0x0044
#define MDDI_DRIVE_HI           0x0048
#define MDDI_DRIVE_LO           0x004c
#define MDDI_DISP_WAKE          0x0050
#define MDDI_REV_ENCAP_SZ       0x0054
#define MDDI_RTD_VAL            0x0058
#define MDDI_PAD_CTL            0x0068
#define MDDI_DRIVER_START_CNT   0x006c
#define MDDI_NEXT_PRI_PTR       0x0070
#define MDDI_NEXT_SEC_PTR       0x0074
#define MDDI_MISR_CTL           0x0078
#define MDDI_MISR_DATA          0x007c
#define MDDI_SF_CNT             0x0080
#define MDDI_MF_CNT             0x0084
#define MDDI_CURR_REV_PTR       0x0088
#define MDDI_CORE_VER           0x008c

#define MDDI_INT_PRI_PTR_READ       0x0001
#define MDDI_INT_SEC_PTR_READ       0x0002
#define MDDI_INT_REV_DATA_AVAIL     0x0004
#define MDDI_INT_DISP_REQ           0x0008
#define MDDI_INT_PRI_UNDERFLOW      0x0010
#define MDDI_INT_SEC_UNDERFLOW      0x0020
#define MDDI_INT_REV_OVERFLOW       0x0040
#define MDDI_INT_CRC_ERROR          0x0080
#define MDDI_INT_MDDI_IN            0x0100
#define MDDI_INT_PRI_OVERWRITE      0x0200
#define MDDI_INT_SEC_OVERWRITE      0x0400
#define MDDI_INT_REV_OVERWRITE      0x0800
#define MDDI_INT_DMA_FAILURE        0x1000
#define MDDI_INT_LINK_ACTIVE        0x2000
#define MDDI_INT_IN_HIBERNATION     0x4000
#define MDDI_INT_PRI_LINK_LIST_DONE 0x8000
#define MDDI_INT_SEC_LINK_LIST_DONE 0x10000
#define MDDI_INT_NO_CMD_PKTS_PEND   0x20000
#define MDDI_INT_RTD_FAILURE        0x40000
#define MDDI_INT_REV_PKT_RECEIVED   0x80000
#define MDDI_INT_REV_PKTS_AVAIL     0x100000

#define MDDI_INT_NEED_CLEAR ( \
	MDDI_INT_REV_DATA_AVAIL | \
	MDDI_INT_PRI_UNDERFLOW | \
	MDDI_INT_SEC_UNDERFLOW | \
	MDDI_INT_REV_OVERFLOW | \
	MDDI_INT_CRC_ERROR | \
	MDDI_INT_REV_PKT_RECEIVED)


#define MDDI_STAT_LINK_ACTIVE        0x0001
#define MDDI_STAT_NEW_REV_PTR        0x0002
#define MDDI_STAT_NEW_PRI_PTR        0x0004
#define MDDI_STAT_NEW_SEC_PTR        0x0008
#define MDDI_STAT_IN_HIBERNATION     0x0010
#define MDDI_STAT_PRI_LINK_LIST_DONE 0x0020
#define MDDI_STAT_SEC_LINK_LIST_DONE 0x0040
#define MDDI_STAT_PENDING_TIMING_PKT 0x0080
#define MDDI_STAT_PENDING_REV_ENCAP  0x0100
#define MDDI_STAT_PENDING_POWERDOWN  0x0200
#define MDDI_STAT_RTD_MEAS_FAIL      0x0800
#define MDDI_STAT_CLIENT_WAKEUP_REQ  0x1000


#define MDDI_CMD_POWERDOWN           0x0100
#define MDDI_CMD_POWERUP             0x0200
#define MDDI_CMD_HIBERNATE           0x0300
#define MDDI_CMD_RESET               0x0400
#define MDDI_CMD_DISP_IGNORE         0x0501
#define MDDI_CMD_DISP_LISTEN         0x0500
#define MDDI_CMD_SEND_REV_ENCAP      0x0600
#define MDDI_CMD_GET_CLIENT_CAP      0x0601
#define MDDI_CMD_GET_CLIENT_STATUS   0x0602
#define MDDI_CMD_SEND_RTD            0x0700
#define MDDI_CMD_LINK_ACTIVE         0x0900
#define MDDI_CMD_PERIODIC_REV_ENCAP  0x0A00
#define MDDI_CMD_FORCE_NEW_REV_PTR   0x0C00



#define MDDI_VIDEO_REV_PKT_SIZE              0x40
#define MDDI_CLIENT_CAPABILITY_REV_PKT_SIZE  0x60
#define MDDI_MAX_REV_PKT_SIZE                0x60

/* #define MDDI_REV_BUFFER_SIZE 128 */
#define MDDI_REV_BUFFER_SIZE (MDDI_MAX_REV_PKT_SIZE * 4)

/* MDP sends 256 pixel packets, so lower value hibernates more without
 * significantly increasing latency of waiting for next subframe */
#define MDDI_HOST_BYTES_PER_SUBFRAME  0x3C00
#define MDDI_HOST_TA2_LEN       0x000c
#define MDDI_HOST_REV_RATE_DIV  0x0002


struct __attribute__((packed)) mddi_rev_packet {
	uint16_t length;
	uint16_t type;
	uint16_t client_id;
};

struct __attribute__((packed)) mddi_client_status {
	uint16_t length;
	uint16_t type;
	uint16_t client_id;
	uint16_t reverse_link_request;  /* bytes needed in rev encap message */
	uint8_t  crc_error_count;
	uint8_t  capability_change;
	uint16_t graphics_busy_flags;
	uint16_t crc16;
};

struct __attribute__((packed)) mddi_client_caps {
	uint16_t length; /* length, exclusive of this field */
	uint16_t type; /* 66 */
	uint16_t client_id;

	uint16_t Protocol_Version;
	uint16_t Minimum_Protocol_Version;
	uint16_t Data_Rate_Capability;
	uint8_t  Interface_Type_Capability;
	uint8_t  Number_of_Alt_Displays;
	uint16_t PostCal_Data_Rate;
	uint16_t Bitmap_Width;
	uint16_t Bitmap_Height;
	uint16_t Display_Window_Width;
	uint16_t Display_Window_Height;
	uint32_t Color_Map_Size;
	uint16_t Color_Map_RGB_Width;
	uint16_t RGB_Capability;
	uint8_t  Monochrome_Capability;
	uint8_t  Reserved_1;
	uint16_t Y_Cb_Cr_Capability;
	uint16_t Bayer_Capability;
	uint16_t Alpha_Cursor_Image_Planes;
	uint32_t Client_Feature_Capability_Indicators;
	uint8_t  Maximum_Video_Frame_Rate_Capability;
	uint8_t  Minimum_Video_Frame_Rate_Capability;
	uint16_t Minimum_Sub_frame_Rate;
	uint16_t Audio_Buffer_Depth;
	uint16_t Audio_Channel_Capability;
	uint16_t Audio_Sample_Rate_Capability;
	uint8_t  Audio_Sample_Resolution;
	uint8_t  Mic_Audio_Sample_Resolution;
	uint16_t Mic_Sample_Rate_Capability;
	uint8_t  Keyboard_Data_Format;
	uint8_t  pointing_device_data_format;
	uint16_t content_protection_type;
	uint16_t Mfr_Name;
	uint16_t Product_Code;
	uint16_t Reserved_3;
	uint32_t Serial_Number;
	uint8_t  Week_of_Manufacture;
	uint8_t  Year_of_Manufacture;

	uint16_t crc16;
} mddi_client_capability_type;


struct __attribute__((packed)) mddi_video_stream {
	uint16_t length;
	uint16_t type; /* 16 */
	uint16_t client_id; /* 0 */

	uint16_t video_data_format_descriptor;
/* format of each pixel in the Pixel Data in the present stream in the
 * present packet.
 * If bits [15:13] = 000 monochrome
 * If bits [15:13] = 001 color pixels (palette).
 * If bits [15:13] = 010 color pixels in raw RGB
 * If bits [15:13] = 011 data in 4:2:2 Y Cb Cr format
 * If bits [15:13] = 100 Bayer pixels
 */

	uint16_t pixel_data_attributes;
/* interpreted as follows:
 * Bits [1:0] = 11  pixel data is displayed to both eyes
 * Bits [1:0] = 10  pixel data is routed to the left eye only.
 * Bits [1:0] = 01  pixel data is routed to the right eye only.
 * Bits [1:0] = 00  pixel data is routed to the alternate display.
 * Bit 2 is 0  Pixel Data is in the standard progressive format.
 * Bit 2 is 1  Pixel Data is in interlace format.
 * Bit 3 is 0  Pixel Data is in the standard progressive format.
 * Bit 3 is 1  Pixel Data is in alternate pixel format.
 * Bit 4 is 0  Pixel Data is to or from the display frame buffer.
 * Bit 4 is 1  Pixel Data is to or from the camera.
 * Bit 5 is 0  pixel data contains the next consecutive row of pixels.
 * Bit 5 is 1  X Left Edge, Y Top Edge, X Right Edge, Y Bottom Edge,
 *             X Start, and Y Start parameters are not defined and
 *             shall be ignored by the client.
 * Bits [7:6] = 01  Pixel data is written to the offline image buffer.
 * Bits [7:6] = 00  Pixel data is written to the buffer to refresh display.
 * Bits [7:6] = 11  Pixel data is written to all image buffers.
 * Bits [7:6] = 10  Invalid. Reserved for future use.
 * Bits 8 through 11 alternate display number.
 * Bits 12 through 14 are reserved for future use and shall be set to zero.
 * Bit 15 is 1 the row of pixels is the last row of pixels in a frame.
 */

	uint16_t x_left_edge;
	uint16_t y_top_edge;
	/* X,Y coordinate of the top left edge of the screen window */

	uint16_t x_right_edge;
	uint16_t y_bottom_edge;
	/* X,Y coordinate of the bottom right edge of the window being
	 * updated. */

	uint16_t x_start;
	uint16_t y_start;
	/* (X Start, Y Start) is the first pixel in the Pixel Data field
	 * below. */

	uint16_t pixel_count;
	/* number of pixels in the Pixel Data field below. */

	uint16_t parameter_CRC;
	/* 16-bit CRC of all bytes from the Packet Length to the Pixel Count. */

	uint16_t reserved;
	/* 16-bit variable to make structure align on 4 byte boundary */
};

#define TYPE_VIDEO_STREAM      16
#define TYPE_CLIENT_CAPS       66
#define TYPE_REGISTER_ACCESS   146
#define TYPE_CLIENT_STATUS     70

struct __attribute__((packed)) mddi_register_access {
	uint16_t length;
	uint16_t type; /* 146 */
	uint16_t client_id;

	uint16_t read_write_info;
	/* Bits 13:0  a 14-bit unsigned integer that specifies the number of
	 *            32-bit Register Data List items to be transferred in the
	 *            Register Data List field.
	 * Bits[15:14] = 00  Write to register(s);
	 * Bits[15:14] = 10  Read from register(s);
	 * Bits[15:14] = 11  Response to a Read.
	 * Bits[15:14] = 01  this value is reserved for future use. */
#define MDDI_WRITE     (0 << 14)
#define MDDI_READ      (2 << 14)
#define MDDI_READ_RESP (3 << 14)

	uint32_t register_address;
	/* the register address that is to be written to or read from. */

	uint16_t crc16;

	uint32_t register_data_list;
	/* list of 4-byte register data values for/from client registers */
};

struct __attribute__((packed)) mddi_llentry {
	uint16_t flags;
	uint16_t header_count;
	uint16_t data_count;
	dma_addr_t data; /* 32 bit */
	struct mddi_llentry *next;
	uint16_t reserved;
	union {
		struct mddi_video_stream v;
		struct mddi_register_access r;
		uint32_t _[12];
	} u;
};

#endif
