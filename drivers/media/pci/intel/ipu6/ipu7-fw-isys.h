/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Intel Corporation */

#ifndef IPU7_FW_ISYS_H
#define IPU7_FW_ISYS_H

#include "ipu7-boot.h"
#include <linux/limits.h>

#define IPU7_INSYS_MAX_OUTPUT_QUEUES		3U
#define IPU7_INSYS_STREAM_ID_MAX		16U
#define IPU7_INSYS_MAX_INPUT_QUEUES		(IPU7_INSYS_STREAM_ID_MAX + 1U)
#define IPU7_INSYS_OUTPUT_MSG_QUEUE		0U
#define IPU7_INSYS_OUTPUT_LOG_QUEUE		1U
#define IPU7_INSYS_OUTPUT_RESERVED_QUEUE	2U
#define IPU7_INSYS_INPUT_DEV_QUEUE		3U

#define IPU7_INSYS_INPUT_FIRST_QUEUE		3U
#define IPU7_INSYS_INPUT_MSG_QUEUE		4U
#define IPU7_INSYS_INPUT_MSG_MAX_QUEUE		16U

#define IPU7_MSG_ERR_MAX_DETAILS		4U
#define IPU7_ISYS_SIZE_RECV_QUEUE		40U
#define IPU7_ISYS_SIZE_LOG_QUEUE		256U
#define IPU7_ISYS_SIZE_SEND_QUEUE		40U
#define IPU7_ISYS_NUM_RECV_QUEUE		1U
#define IPU7_INSYS_SEND_QUEUE_TOKEN_FLAG_NONE	0U

/* Value of mipi_fn below when firmware ABI < 1.2.1 does not report it. */
#define IPU7_INSYS_MIPI_FRAME_NUMBER_DONT_CARE	U16_MAX

#define IPU7_LOGGER_CFG_CHANNEL_ENABLE_SYSCOM	BIT(1)

#define IPU7_ISYS_MAX_STREAMS	16U
#define IPU7_MAX_OPINS		4
#define IPU7_MAX_IPINS		4

#define IPU7_N_INSYS_MIPI_DATA_TYPE	0x40

#define IPU7_MSG_LINK_FOREIGN_KEY_NONE		(65535U)
#define IPU7_MSG_LINK_PBK_ID_DONT_CARE		(255U)
#define IPU7_MSG_LINK_PBK_SLOT_ID_DONT_CARE	(255U)

#define IPU7_INSYS_STREAM_SYNC_MSG_SEND_RESP_SOF		BIT(0)
#define IPU7_INSYS_STREAM_SYNC_MSG_SEND_RESP_EOF		BIT(1)
#define IPU7_INSYS_STREAM_SYNC_MSG_SEND_IRQ_SOF			BIT(2)
#define IPU7_INSYS_STREAM_SYNC_MSG_SEND_IRQ_EOF			BIT(3)
#define IPU7_INSYS_STREAM_SYNC_MSG_SEND_RESP_SOF_DISCARDED	BIT(4)
#define IPU7_INSYS_STREAM_SYNC_MSG_SEND_RESP_EOF_DISCARDED	BIT(5)
#define IPU7_INSYS_STREAM_SYNC_MSG_SEND_IRQ_SOF_DISCARDED	BIT(6)
#define IPU7_INSYS_STREAM_SYNC_MSG_SEND_IRQ_EOF_DISCARDED	BIT(7)

#define IPU7_INSYS_STREAM_MSG_SEND_RESP_STREAM_OPEN_DONE	BIT(0)
#define IPU7_INSYS_STREAM_MSG_SEND_IRQ_STREAM_OPEN_DONE		BIT(1)
#define IPU7_INSYS_STREAM_MSG_SEND_RESP_STREAM_START_ACK	BIT(2)
#define IPU7_INSYS_STREAM_MSG_SEND_IRQ_STREAM_START_ACK		BIT(3)
#define IPU7_INSYS_STREAM_MSG_SEND_RESP_STREAM_CLOSE_ACK	BIT(4)
#define IPU7_INSYS_STREAM_MSG_SEND_IRQ_STREAM_CLOSE_ACK		BIT(5)
#define IPU7_INSYS_STREAM_MSG_SEND_RESP_STREAM_FLUSH_ACK	BIT(6)
#define IPU7_INSYS_STREAM_MSG_SEND_IRQ_STREAM_FLUSH_ACK		BIT(7)
#define IPU7_INSYS_STREAM_MSG_SEND_RESP_STREAM_ABORT_ACK	BIT(8)
#define IPU7_INSYS_STREAM_MSG_SEND_IRQ_STREAM_ABORT_ACK		BIT(9)

#define IPU7_INSYS_STREAM_ENABLE_MSG_SEND_RESP ( \
	IPU7_INSYS_STREAM_MSG_SEND_RESP_STREAM_OPEN_DONE | \
	IPU7_INSYS_STREAM_MSG_SEND_RESP_STREAM_START_ACK | \
	IPU7_INSYS_STREAM_MSG_SEND_RESP_STREAM_CLOSE_ACK | \
	IPU7_INSYS_STREAM_MSG_SEND_RESP_STREAM_FLUSH_ACK | \
	IPU7_INSYS_STREAM_MSG_SEND_RESP_STREAM_ABORT_ACK)
#define IPU7_INSYS_STREAM_ENABLE_MSG_SEND_IRQ ( \
	IPU7_INSYS_STREAM_MSG_SEND_IRQ_STREAM_OPEN_DONE | \
	IPU7_INSYS_STREAM_MSG_SEND_IRQ_STREAM_START_ACK | \
	IPU7_INSYS_STREAM_MSG_SEND_IRQ_STREAM_CLOSE_ACK | \
	IPU7_INSYS_STREAM_MSG_SEND_IRQ_STREAM_FLUSH_ACK | \
	IPU7_INSYS_STREAM_MSG_SEND_IRQ_STREAM_ABORT_ACK)

#define IPU7_INSYS_FRAME_MSG_SEND_RESP_CAPTURE_ACK		BIT(0)
#define IPU7_INSYS_FRAME_MSG_SEND_IRQ_CAPTURE_ACK		BIT(1)
#define IPU7_INSYS_FRAME_MSG_SEND_RESP_CAPTURE_DONE		BIT(2)
#define IPU7_INSYS_FRAME_MSG_SEND_IRQ_CAPTURE_DONE		BIT(3)
#define IPU7_INSYS_FRAME_MSG_SEND_RESP_PIN_DATA_READY		BIT(4)
#define IPU7_INSYS_FRAME_MSG_SEND_IRQ_PIN_DATA_READY		BIT(5)

#define IPU7_INSYS_FRAME_ENABLE_MSG_SEND_RESP ( \
	IPU7_INSYS_FRAME_MSG_SEND_RESP_CAPTURE_ACK | \
	IPU7_INSYS_FRAME_MSG_SEND_RESP_CAPTURE_DONE | \
	IPU7_INSYS_FRAME_MSG_SEND_RESP_PIN_DATA_READY)
#define IPU7_INSYS_FRAME_ENABLE_MSG_SEND_IRQ ( \
	IPU7_INSYS_FRAME_MSG_SEND_IRQ_CAPTURE_ACK | \
	IPU7_INSYS_FRAME_MSG_SEND_IRQ_CAPTURE_DONE | \
	IPU7_INSYS_FRAME_MSG_SEND_IRQ_PIN_DATA_READY)

enum ipu7_insys_send_type {
	IPU7_INSYS_SEND_TYPE_STREAM_OPEN = 0,
	IPU7_INSYS_SEND_TYPE_STREAM_START_AND_CAPTURE = 1,
	IPU7_INSYS_SEND_TYPE_STREAM_CAPTURE = 2,
	IPU7_INSYS_SEND_TYPE_STREAM_ABORT = 3,
	IPU7_INSYS_SEND_TYPE_STREAM_FLUSH = 4,
	IPU7_INSYS_SEND_TYPE_STREAM_CLOSE = 5,
	N_IPU7_INSYS_SEND_TYPE
};

enum ipu7_insys_resp_type {
	IPU7_INSYS_RESP_TYPE_STREAM_OPEN_DONE = 0,
	IPU7_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_ACK = 1,
	IPU7_INSYS_RESP_TYPE_STREAM_CAPTURE_ACK = 2,
	IPU7_INSYS_RESP_TYPE_STREAM_ABORT_ACK = 3,
	IPU7_INSYS_RESP_TYPE_STREAM_FLUSH_ACK = 4,
	IPU7_INSYS_RESP_TYPE_STREAM_CLOSE_ACK = 5,
	IPU7_INSYS_RESP_TYPE_PIN_DATA_READY = 6,
	IPU7_INSYS_RESP_TYPE_FRAME_SOF = 7,
	IPU7_INSYS_RESP_TYPE_FRAME_EOF = 8,
	IPU7_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_DONE = 9,
	IPU7_INSYS_RESP_TYPE_STREAM_CAPTURE_DONE = 10,
	N_IPU7_INSYS_RESP_TYPE
};

enum ipu7_insys_mipi_dt_rename_mode {
	IPU7_INSYS_MIPI_DT_NO_RENAME = 0,
	IPU7_INSYS_MIPI_DT_RENAMED_MODE = 1,
	N_IPU7_INSYS_MIPI_DT_MODE
};

enum insys_msg_err_capture {
	INSYS_MSG_ERR_CAPTURE_OK = 0,
	INSYS_MSG_ERR_CAPTURE_STREAM_ID = 1,
	INSYS_MSG_ERR_CAPTURE_PAYLOAD_PTR = 2,
	INSYS_MSG_ERR_CAPTURE_MEM_SLOT = 3,
	INSYS_MSG_ERR_CAPTURE_STREAMING_MODE = 4,
	INSYS_MSG_ERR_CAPTURE_AVAILABLE_CMD_SLOT = 5,
	INSYS_MSG_ERR_CAPTURE_CONSUMED_CMD_SLOT = 6,
	INSYS_MSG_ERR_CAPTURE_CMD_SLOT_PAYLOAD_PTR = 7,
	INSYS_MSG_ERR_CAPTURE_CMD_PREPARE = 8,
	INSYS_MSG_ERR_CAPTURE_OUTPUT_PIN = 9,
	INSYS_MSG_ERR_CAPTURE_SYNC_FRAME_DROP = 10,
	INSYS_MSG_ERR_CAPTURE_FRAME_MESSAGES_MAP = 11,
	INSYS_MSG_ERR_CAPTURE_TIMEOUT = 12,
	INSYS_MSG_ERR_CAPTURE_INVALID_STREAM_STATE = 13,
	INSYS_MSG_ERR_CAPTURE_HW_ERR_MULTIBIT_PH_ERROR_DETECTED = 14,
	INSYS_MSG_ERR_CAPTURE_HW_ERR_PAYLOAD_CRC_ERROR = 15,
	INSYS_MSG_ERR_CAPTURE_HW_ERR_INPUT_DATA_LOSS_ELASTIC_FIFO_OVFL  = 16,
	INSYS_MSG_ERR_CAPTURE_HW_ERR_PIXEL_BUFFER_OVERFLOW = 17,
	INSYS_MSG_ERR_CAPTURE_HW_ERR_BAD_FRAME_DIM = 18,
	INSYS_MSG_ERR_CAPTURE_HW_ERR_PHY_SYNC_ERR = 19,
	INSYS_MSG_ERR_CAPTURE_HW_ERR_SECURE_TOUCH = 20,
	INSYS_MSG_ERR_CAPTURE_HW_ERR_MASTER_SLAVE_SYNC_ERR = 21,
	INSYS_MSG_ERR_CAPTURE_FRAME_SKIP_ERR = 22,
	INSYS_MSG_ERR_CAPTURE_FE_INPUT_FIFO_OVERFLOW_ERR = 23,
	INSYS_MSG_ERR_CAPTURE_CMD_SUBMIT_TO_HW = 24,
	INSYS_MSG_ERR_CAPTURE_N
};

enum insys_msg_err_groups {
	INSYS_MSG_ERR_GROUP_RESERVED = 0,
	INSYS_MSG_ERR_GROUP_GENERAL = 1,
	INSYS_MSG_ERR_GROUP_STREAM = 2,
	INSYS_MSG_ERR_GROUP_CAPTURE = 3,
	INSYS_MSG_ERR_GROUP_N,
};

struct ipu7_insys_config {
	u32 timeout_val_ms;
	struct ipu7_fw_logger_config logger_config;
	struct ipu7_wdt_abi wdt_config;
};

struct ipu7_insys_capture_output_pin_payload {
	u64 user_token;
	u32 addr;
	u8 pad[4];
};

struct ipu7_fw_isys_msg_err {
	u32 err_group;
	u32 err_code;
	u32 err_detail[IPU7_MSG_ERR_MAX_DETAILS];
};

struct ipu7_insys_resp {
	u64 buf_id;
	struct ipu7_insys_capture_output_pin_payload pin;
	struct ipu7_fw_isys_msg_err error_info;
	u32 timestamp[2];
	u8 type;
	u8 msg_link_streaming_mode;
	u8 stream_id;
	u8 pin_id;
	u8 frame_id;
	u8 skip_frame;
	u16 mipi_fn;
};

struct ipu7_insys_resp_queue_token {
	struct ipu7_insys_resp resp_info;
};

struct ipu7_insys_send_queue_token {
	u64 buf_handle;
	u32 addr;
	u16 stream_id;
	u8 send_type;
	u8 flag;
};

struct ipu7_fw_isys_output_link {
	u32 buffer_lines;
	u16 foreign_key;
	u16 granularity_pointer_update;
	u8 msg_link_streaming_mode;
	u8 pbk_id;
	u8 pbk_slot_id;
	u8 dest;
	u8 use_sw_managed;
	u8 is_snoop;
	u8 pad[2];
} __packed;

struct ipu7_fw_isys_output_cropping {
	u16 line_top;
	u16 line_bottom;
} __packed;

/* IPU8 InSys firmware layout, ABI 1.0.14. */
struct ipu7_fw_isys_output_cropping_ipu8 {
	u16 line_top;
	u16 line_bottom;
	u16 column_left;
	u16 column_right;
} __packed;

struct ipu7_fw_isys_output_dpcm {
	u8 enable;
	u8 type;
	u8 predictor;
	u8 pad;
} __packed;

/* IPU8 (InSys ABI 1.0.14) only. */
enum ipu7_insys_cfa_dim {
	IPU7_INSYS_CFA_DIM_2X2 = 0,
	IPU7_INSYS_CFA_DIM_4X4 = 1,
	N_IPU7_INSYS_CFA_DIM
};

#define IPU7_INSYS_MAX_BINNING_FACTOR	4U

/* IPU8 (InSys ABI 1.0.14) only. */
struct ipu7_fw_isys_upipe_output_pin_ipu8 {
	u32 opaque_pin_cfg;
	u16 plane_offset_1;
	u16 plane_offset_2;
	u8 single_uob_fifo;
	u8 shared_uob_fifo;
	u8 pad[2];
} __packed;

struct ipu7_fw_isys_output_pin {
	struct ipu7_fw_isys_output_link link;
	struct ipu7_fw_isys_output_cropping crop;
	struct ipu7_fw_isys_output_dpcm dpcm;
	u32 stride;
	u16 ft;
	u8 send_irq;
	u8 input_pin_id;
	u8 early_ack_en;
	u8 pad[3];
} __packed;

/* IPU8 InSys firmware layout, ABI 1.0.14. */
struct ipu7_fw_isys_output_pin_ipu8 {
	struct ipu7_fw_isys_output_link link;
	struct ipu7_fw_isys_output_cropping_ipu8 crop;
	struct ipu7_fw_isys_output_dpcm dpcm;
	struct ipu7_fw_isys_upipe_output_pin_ipu8 upipe_pin_cfg;
	u32 stride;
	u16 ft;
	u8 upipe_enable;
	u8 send_irq;
	u8 input_pin_id;
	u8 early_ack_en;
	u8 cfa_dim;
	u8 binning_factor;
} __packed;

struct ipu7_fw_isys_resolution {
	u32 width;
	u32 height;
} __packed;

struct ipu7_fw_isys_input_pin {
	struct ipu7_fw_isys_resolution input_res;
	u16 sync_msg_map;
	u8 dt;
	u8 disable_mipi_unpacking;
	u8 dt_rename_mode;
	u8 mapped_dt;
	u8 pad[2];
} __packed;

struct ipu7_fw_isys_stream_cfg {
	struct ipu7_fw_isys_input_pin input_pins[IPU7_MAX_IPINS];
	struct ipu7_fw_isys_output_pin output_pins[IPU7_MAX_OPINS];
	u16 stream_msg_map;
	u8 port_id;
	u8 vc;
	u8 nof_input_pins;
	u8 nof_output_pins;
	u8 pad[2];
} __packed;

/* IPU8 InSys firmware layout, ABI 1.0.14. */
struct ipu7_fw_isys_stream_cfg_ipu8 {
	struct ipu7_fw_isys_input_pin input_pins[IPU7_MAX_IPINS];
	struct ipu7_fw_isys_output_pin_ipu8 output_pins[IPU7_MAX_OPINS];
	u16 stream_msg_map;
	u8 port_id;
	u8 vc;
	u8 nof_input_pins;
	u8 nof_output_pins;
	u8 pad[2];
} __packed;

struct ipu7_fw_isys_capture_output_pin {
	u64 user_token;
	u32 addr;
	u8 pad[4];
} __packed;

/* IPU8 (InSys ABI 1.0.14) only. */
struct ipu7_fw_isys_capture_output_pin_cfg_ipu8 {
	struct ipu7_fw_isys_capture_output_pin pin_payload;
	u32 upipe_capture_cfg;
} __packed;

struct ipu7_fw_isys_frame_buff_set {
	struct ipu7_fw_isys_capture_output_pin output_pins[IPU7_MAX_OPINS];
	u8 capture_msg_map;
	u8 frame_id;
	u8 skip_frame;
	u8 pad[5];
} __packed;

/* IPU8 InSys firmware layout, ABI 1.0.14. */
struct ipu7_fw_isys_frame_buff_set_ipu8 {
	struct ipu7_fw_isys_capture_output_pin_cfg_ipu8 output_pins[IPU7_MAX_OPINS];
	u8 capture_msg_map;
	u8 frame_id;
	u8 skip_frame;
	u8 pad[5];
} __packed;

struct ipu6_fw_isys_ops *ipu7_fw_isys_get_ops(void);
irqreturn_t ipu7_isys_isr(struct ipu6_bus_device *adev);

#endif
