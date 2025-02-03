/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _TA_SECUREDISPLAY_IF_H
#define _TA_SECUREDISPLAY_IF_H

/** Secure Display related enumerations */
/**********************************************************/

/** @enum ta_securedisplay_command
 *    Secure Display Command ID
 */
enum ta_securedisplay_command {
	/* Query whether TA is responding. It is used only for validation purpose */
	TA_SECUREDISPLAY_COMMAND__QUERY_TA              = 1,
	/* Send region of Interest and CRC value to I2C */
	TA_SECUREDISPLAY_COMMAND__SEND_ROI_CRC          = 2,
	/* V2 to send multiple regions of Interest and CRC value to I2C */
	TA_SECUREDISPLAY_COMMAND__SEND_ROI_CRC_V2       = 3,
	/* Maximum Command ID */
	TA_SECUREDISPLAY_COMMAND__MAX_ID                = 0x7FFFFFFF,
};

/** @enum ta_securedisplay_status
 *    Secure Display status returns in shared buffer status
 */
enum ta_securedisplay_status {
	TA_SECUREDISPLAY_STATUS__SUCCESS                 = 0x00,         /* Success */
	TA_SECUREDISPLAY_STATUS__GENERIC_FAILURE         = 0x01,         /* Generic Failure */
	TA_SECUREDISPLAY_STATUS__INVALID_PARAMETER       = 0x02,         /* Invalid Parameter */
	TA_SECUREDISPLAY_STATUS__NULL_POINTER            = 0x03,         /* Null Pointer*/
	TA_SECUREDISPLAY_STATUS__I2C_WRITE_ERROR         = 0x04,         /* Fail to Write to I2C */
	TA_SECUREDISPLAY_STATUS__READ_DIO_SCRATCH_ERROR  = 0x05, /*Fail Read DIO Scratch Register*/
	TA_SECUREDISPLAY_STATUS__READ_CRC_ERROR          = 0x06,         /* Fail to Read CRC*/
	TA_SECUREDISPLAY_STATUS__I2C_INIT_ERROR          = 0x07,     /* Failed to initialize I2C */

	TA_SECUREDISPLAY_STATUS__MAX                     = 0x7FFFFFFF,/* Maximum Value for status*/
};

/** @enum ta_securedisplay_phy_ID
 *    Physical ID number to use for reading corresponding DIO Scratch register for ROI
 */
enum  ta_securedisplay_phy_ID {
	TA_SECUREDISPLAY_PHY0                           = 0,
	TA_SECUREDISPLAY_PHY1                           = 1,
	TA_SECUREDISPLAY_PHY2                           = 2,
	TA_SECUREDISPLAY_PHY3                           = 3,
	TA_SECUREDISPLAY_MAX_PHY                        = 4,
};

/** @enum ta_securedisplay_ta_query_cmd_ret
 *    A predefined specific reteurn value which is 0xAB only used to validate
 *    communication to Secure Display TA is functional.
 *    This value is used to validate whether TA is responding successfully
 */
enum ta_securedisplay_ta_query_cmd_ret {
	/* This is a value to validate if TA is loaded successfully */
	TA_SECUREDISPLAY_QUERY_CMD_RET                 = 0xAB,
};

/** @enum ta_securedisplay_buffer_size
 *    I2C Buffer size which contains 8 bytes of ROI  (X start, X end, Y start, Y end)
 *    and 6 bytes of CRC( R,G,B) and 1  byte for physical ID
 */
enum ta_securedisplay_buffer_size {
	/* 15 bytes = 8 byte (ROI) + 6 byte(CRC) + 1 byte(phy_id) */
	TA_SECUREDISPLAY_I2C_BUFFER_SIZE                = 15,
	/* 16 bytes = 8 byte (ROI) + 6 byte(CRC) + 1 byte(phy_id) + 1 byte(roi_idx) */
	TA_SECUREDISPLAY_V2_I2C_BUFFER_SIZE             = 16,
};

/** Input/output structures for Secure Display commands */
/**********************************************************/
/**
 * Input structures
 */

/** @struct ta_securedisplay_send_roi_crc_input
 *    Physical ID to determine which DIO scratch register should be used to get ROI
 */
struct ta_securedisplay_send_roi_crc_input {
	/* Physical ID */
	uint32_t  phy_id;
};

struct ta_securedisplay_send_roi_crc_v2_input {
	/* Physical ID */
	uint32_t phy_id;
	/* Region of interest index */
	uint8_t  roi_idx;
};

/** @union ta_securedisplay_cmd_input
 *    Input buffer
 */
union ta_securedisplay_cmd_input {
	/* send ROI and CRC input buffer format */
	struct ta_securedisplay_send_roi_crc_input        send_roi_crc;
	/* send ROI and CRC input buffer format, v2 adds a ROI index */
	struct ta_securedisplay_send_roi_crc_v2_input     send_roi_crc_v2;
	uint32_t                                          reserved[4];
};

/**
 * Output structures
 */

/** @struct ta_securedisplay_query_ta_output
 *  Output buffer format for query TA whether TA is responding used only for validation purpose
 */
struct ta_securedisplay_query_ta_output {
	/* return value from TA when it is queried for validation purpose only */
	uint32_t  query_cmd_ret;
};

/** @struct ta_securedisplay_send_roi_crc_output
 *  Output buffer format for send ROI CRC command which will pass I2c buffer created inside TA
 *  and used to write to I2C used only for validation purpose
 */
struct ta_securedisplay_send_roi_crc_output {
	uint8_t  i2c_buf[TA_SECUREDISPLAY_I2C_BUFFER_SIZE];  /* I2C buffer */
	uint8_t  reserved;
};

struct ta_securedisplay_send_roi_crc_v2_output {
	uint8_t  i2c_buf[TA_SECUREDISPLAY_V2_I2C_BUFFER_SIZE];  /* I2C buffer */
};

/** @union ta_securedisplay_cmd_output
 *    Output buffer
 */
union ta_securedisplay_cmd_output {
	/* Query TA output buffer format used only for validation purpose*/
	struct ta_securedisplay_query_ta_output            query_ta;
	/* Send ROI CRC output buffer format used only for validation purpose */
	struct ta_securedisplay_send_roi_crc_output        send_roi_crc;
	/* Send ROI CRC output buffer format used only for validation purpose */
	struct ta_securedisplay_send_roi_crc_v2_output     send_roi_crc_v2;
	uint32_t                                           reserved[4];
};

/** @struct ta_securedisplay_cmd
*    Secure display command which is shared buffer memory
*/
struct ta_securedisplay_cmd {
    uint32_t                                           cmd_id;                         /**< +0  Bytes Command ID */
    enum ta_securedisplay_status                       status;                         /**< +4  Bytes Status code returned by the secure display TA */
    uint32_t                                           reserved[2];                    /**< +8  Bytes Reserved */
    union ta_securedisplay_cmd_input                   securedisplay_in_message;       /**< +16 Bytes Command input buffer */
    union ta_securedisplay_cmd_output                  securedisplay_out_message;      /**< +32 Bytes Command output buffer */
    /**@note Total 48 Bytes */
};

#endif   //_TA_SECUREDISPLAY_IF_H

