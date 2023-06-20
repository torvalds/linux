/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 pureLiFi
 */

#define PURELIFI_BYTE_NUM_ALIGNMENT 4
#define ETH_ALEN 6
#define AP_USER_LIMIT 8

#define PLF_VNDR_FPGA_STATE_REQ 0x30
#define PLF_VNDR_FPGA_SET_REQ 0x33
#define PLF_VNDR_FPGA_SET_CMD 0x34
#define PLF_VNDR_FPGA_STATE_CMD 0x35

#define PLF_VNDR_XL_FW_CMD 0x80
#define PLF_VNDR_XL_DATA_CMD 0x81
#define PLF_VNDR_XL_FILE_CMD 0x82
#define PLF_VNDR_XL_EX_CMD 0x83

#define PLF_MAC_VENDOR_REQUEST 0x36
#define PLF_SERIAL_NUMBER_VENDOR_REQUEST 0x37
#define PLF_FIRMWARE_VERSION_VENDOR_REQUEST 0x39
#define PLF_SERIAL_LEN 14
#define PLF_FW_VER_LEN 8

struct rx_status {
	__be16 rssi;
	u8     rate_idx;
	u8     pad;
	__be64 crc_error_count;
} __packed;

enum plf_usb_req_enum {
	USB_REQ_TEST_WR            = 0,
	USB_REQ_MAC_WR             = 1,
	USB_REQ_POWER_WR           = 2,
	USB_REQ_RXTX_WR            = 3,
	USB_REQ_BEACON_WR          = 4,
	USB_REQ_BEACON_INTERVAL_WR = 5,
	USB_REQ_RTS_CTS_RATE_WR    = 6,
	USB_REQ_HASH_WR            = 7,
	USB_REQ_DATA_TX            = 8,
	USB_REQ_RATE_WR            = 9,
	USB_REQ_SET_FREQ           = 15
};

struct plf_usb_req {
	__be32         id; /* should be plf_usb_req_enum */
	__be32	       len;
	u8             buf[512];
};

