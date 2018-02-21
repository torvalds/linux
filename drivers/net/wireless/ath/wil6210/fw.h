/*
 * Copyright (c) 2014,2016 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define WIL_FW_SIGNATURE (0x36323130) /* '0126' */
#define WIL_FW_FMT_VERSION (1) /* format version driver supports */

enum wil_fw_record_type {
	wil_fw_type_comment = 1,
	wil_fw_type_data = 2,
	wil_fw_type_fill = 3,
	wil_fw_type_action = 4,
	wil_fw_type_verify = 5,
	wil_fw_type_file_header = 6,
	wil_fw_type_direct_write = 7,
	wil_fw_type_gateway_data = 8,
	wil_fw_type_gateway_data4 = 9,
};

struct wil_fw_record_head {
	__le16 type; /* enum wil_fw_record_type */
	__le16 flags; /* to be defined */
	__le32 size; /* whole record, bytes after head */
} __packed;

/* data block. write starting from @addr
 * data_size inferred from the @head.size. For this case,
 * data_size = @head.size - offsetof(struct wil_fw_record_data, data)
 */
struct wil_fw_record_data { /* type == wil_fw_type_data */
	__le32 addr;
	__le32 data[0]; /* [data_size], see above */
} __packed;

/* fill with constant @value, @size bytes starting from @addr */
struct wil_fw_record_fill { /* type == wil_fw_type_fill */
	__le32 addr;
	__le32 value;
	__le32 size;
} __packed;

/* free-form comment
 * for informational purpose, data_size is @head.size from record header
 */
struct wil_fw_record_comment { /* type == wil_fw_type_comment */
	u8 data[0]; /* free-form data [data_size], see above */
} __packed;

/* Comment header - common for all comment record types */
struct wil_fw_record_comment_hdr {
	__le32 magic;
};

/* FW capabilities encoded inside a comment record */
#define WIL_FW_CAPABILITIES_MAGIC (0xabcddcba)
struct wil_fw_record_capabilities { /* type == wil_fw_type_comment */
	/* identifies capabilities record */
	struct wil_fw_record_comment_hdr hdr;
	/* capabilities (variable size), see enum wmi_fw_capability */
	u8 capabilities[0];
};

/* brd file info encoded inside a comment record */
#define WIL_BRD_FILE_MAGIC (0xabcddcbb)
struct wil_fw_record_brd_file { /* type == wil_fw_type_comment */
	/* identifies brd file record */
	struct wil_fw_record_comment_hdr hdr;
	__le32 version;
	__le32 base_addr;
	__le32 max_size_bytes;
} __packed;

/* perform action
 * data_size = @head.size - offsetof(struct wil_fw_record_action, data)
 */
struct wil_fw_record_action { /* type == wil_fw_type_action */
	__le32 action; /* action to perform: reset, wait for fw ready etc. */
	__le32 data[0]; /* action specific, [data_size], see above */
} __packed;

/* data block for struct wil_fw_record_direct_write */
struct wil_fw_data_dwrite {
	__le32 addr;
	__le32 value;
	__le32 mask;
} __packed;

/* write @value to the @addr,
 * preserve original bits accordingly to the @mask
 * data_size is @head.size where @head is record header
 */
struct wil_fw_record_direct_write { /* type == wil_fw_type_direct_write */
	struct wil_fw_data_dwrite data[0];
} __packed;

/* verify condition: [@addr] & @mask == @value
 * if condition not met, firmware download fails
 */
struct wil_fw_record_verify { /* type == wil_fw_verify */
	__le32 addr; /* read from this address */
	__le32 value; /* reference value */
	__le32 mask; /* mask for verification */
} __packed;

/* file header
 * First record of every file
 */
/* the FW version prefix in the comment */
#define WIL_FW_VERSION_PREFIX "FW version: "
#define WIL_FW_VERSION_PREFIX_LEN (sizeof(WIL_FW_VERSION_PREFIX) - 1)
struct wil_fw_record_file_header {
	__le32 signature ; /* Wilocity signature */
	__le32 reserved;
	__le32 crc; /* crc32 of the following data  */
	__le32 version; /* format version */
	__le32 data_len; /* total data in file, including this record */
	u8 comment[32]; /* short description */
} __packed;

/* 1-dword gateway */
/* data block for the struct wil_fw_record_gateway_data */
struct wil_fw_data_gw {
	__le32 addr;
	__le32 value;
} __packed;

/* gateway write block.
 * write starting address and values from the data buffer
 * through the gateway
 * data_size inferred from the @head.size. For this case,
 * data_size = @head.size - offsetof(struct wil_fw_record_gateway_data, data)
 */
struct wil_fw_record_gateway_data { /* type == wil_fw_type_gateway_data */
	__le32 gateway_addr_addr;
	__le32 gateway_value_addr;
	__le32 gateway_cmd_addr;
	__le32 gateway_ctrl_address;
#define WIL_FW_GW_CTL_BUSY	BIT(29) /* gateway busy performing operation */
#define WIL_FW_GW_CTL_RUN	BIT(30) /* start gateway operation */
	__le32 command;
	struct wil_fw_data_gw data[0]; /* total size [data_size], see above */
} __packed;

/* 4-dword gateway */
/* data block for the struct wil_fw_record_gateway_data4 */
struct wil_fw_data_gw4 {
	__le32 addr;
	__le32 value[4];
} __packed;

/* gateway write block.
 * write starting address and values from the data buffer
 * through the gateway
 * data_size inferred from the @head.size. For this case,
 * data_size = @head.size - offsetof(struct wil_fw_record_gateway_data4, data)
 */
struct wil_fw_record_gateway_data4 { /* type == wil_fw_type_gateway_data4 */
	__le32 gateway_addr_addr;
	__le32 gateway_value_addr[4];
	__le32 gateway_cmd_addr;
	__le32 gateway_ctrl_address; /* same logic as for 1-dword gw */
	__le32 command;
	struct wil_fw_data_gw4 data[0]; /* total size [data_size], see above */
} __packed;
