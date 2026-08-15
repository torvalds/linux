/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef _ISP4_FW_CMD_RESP_H_
#define _ISP4_FW_CMD_RESP_H_

/*
 *        Two types of command/response channel.
 *          Type Global Command has one command/response channel.
 *          Type Stream Command has one command/response channel.
 *-----------                                        ------------
 *|         |       ---------------------------      |          |
 *|         |  ---->|  Global Command         |----> |          |
 *|         |       ---------------------------      |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |       ---------------------------      |          |
 *|         |  ---->|   Stream Command        |----> |          |
 *|         |       ---------------------------      |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|  HOST   |                                        | Firmware |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |       --------------------------       |          |
 *|         |  <----|  Global Response       |<----  |          |
 *|         |       --------------------------       |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *|         |       --------------------------       |          |
 *|         |  <----|  Stream Response       |<----  |          |
 *|         |       --------------------------       |          |
 *|         |                                        |          |
 *|         |                                        |          |
 *-----------                                        ------------
 */

/*
 *        cmd_id is in the format of following type:
 *        type: indicate command type, global/stream commands.
 *        group: indicate the command group.
 *        id: A unique command identification in one type and group.
 *        |<-Bit31 ~ Bit24->|<-Bit23 ~ Bit16->|<-Bit15 ~ Bit0->|
 *        |      type       |      group      |       id       |
 */

#define ISP4FW_CMD_TYPE_SHIFT            24
#define ISP4FW_CMD_GROUP_SHIFT           16
#define ISP4FW_CMD_TYPE_STREAM_CTRL      (0x2U << ISP4FW_CMD_TYPE_SHIFT)

#define ISP4FW_CMD_GROUP_STREAM_CTRL     (0x1U << ISP4FW_CMD_GROUP_SHIFT)
#define ISP4FW_CMD_GROUP_STREAM_BUFFER   (0x4U << ISP4FW_CMD_GROUP_SHIFT)

/* Stream  Command */
#define ISP4FW_CMD_ID_SET_STREAM_CONFIG  (ISP4FW_CMD_TYPE_STREAM_CTRL\
					 | ISP4FW_CMD_GROUP_STREAM_CTRL | 0x1)
#define ISP4FW_CMD_ID_SET_OUT_CHAN_PROP  (ISP4FW_CMD_TYPE_STREAM_CTRL\
					 | ISP4FW_CMD_GROUP_STREAM_CTRL | 0x3)
#define ISP4FW_CMD_ID_ENABLE_OUT_CHAN    (ISP4FW_CMD_TYPE_STREAM_CTRL\
					 | ISP4FW_CMD_GROUP_STREAM_CTRL | 0x5)
#define ISP4FW_CMD_ID_START_STREAM       (ISP4FW_CMD_TYPE_STREAM_CTRL\
					 | ISP4FW_CMD_GROUP_STREAM_CTRL | 0x7)
#define ISP4FW_CMD_ID_STOP_STREAM        (ISP4FW_CMD_TYPE_STREAM_CTRL\
					 | ISP4FW_CMD_GROUP_STREAM_CTRL | 0x8)

/* Stream Buffer Command */
#define ISP4FW_CMD_ID_SEND_BUFFER        (ISP4FW_CMD_TYPE_STREAM_CTRL\
					 | ISP4FW_CMD_GROUP_STREAM_BUFFER | 0x1)

/*
 *        resp_id is in the format of following type:
 *        type: indicate command type, global/stream commands.
 *        group: indicate the command group.
 *        id: A unique command identification in one type and group.
 *        |<-Bit31 ~ Bit24->|<-Bit23 ~ Bit16->|<-Bit15 ~ Bit0->|
 *        |      type       |      group      |       id       |
 */

#define ISP4FW_RESP_GROUP_SHIFT          16

#define ISP4FW_RESP_GROUP_GENERAL        (0x1 << ISP4FW_RESP_GROUP_SHIFT)
#define ISP4FW_RESP_GROUP_NOTIFICATION   (0x3 << ISP4FW_RESP_GROUP_SHIFT)

/* General Response */
#define ISP4FW_RESP_ID_CMD_DONE          (ISP4FW_RESP_GROUP_GENERAL | 0x1)

/* Notification */
#define ISP4FW_RESP_ID_NOTI_FRAME_DONE   (ISP4FW_RESP_GROUP_NOTIFICATION | 0x1)

#define ISP4FW_CMD_STATUS_SUCCESS        0
#define ISP4FW_CMD_STATUS_FAIL           1
#define ISP4FW_CMD_STATUS_SKIPPED        2

#define ISP4FW_ADDR_SPACE_TYPE_GPU_VA    4

#define ISP4FW_MEMORY_POOL_SIZE          (100 * 1024 * 1024)

/*
 * standard ISP pipeline: mipicsi=>isp
 */
#define ISP4FW_MIPI0_ISP_PIPELINE_ID     0x5f91

enum isp4fw_sensor_id {
	/* Sensor id for ISP input from MIPI port 0 */
	ISP4FW_SENSOR_ID_ON_MIPI0  = 0,
};

enum isp4fw_stream_id {
	ISP4FW_STREAM_ID_INVALID = -1,
	ISP4FW_STREAM_ID_1 = 0,
	ISP4FW_STREAM_ID_2 = 1,
	ISP4FW_STREAM_ID_3 = 2,
	ISP4FW_STREAM_ID_MAXIMUM
};

enum isp4fw_image_format {
	/* 4:2:0,semi-planar, 8-bit */
	ISP4FW_IMAGE_FORMAT_NV12 = 1,
	/* interleave, 4:2:2, 8-bit */
	ISP4FW_IMAGE_FORMAT_YUV422INTERLEAVED = 7,
};

enum isp4fw_pipe_out_ch {
	ISP4FW_ISP_PIPE_OUT_CH_PREVIEW = 0,
};

enum isp4fw_yuv_range {
	ISP4FW_ISP_YUV_RANGE_FULL = 0,     /* YUV value range in 0~255 */
	ISP4FW_ISP_YUV_RANGE_NARROW = 1,   /* YUV value range in 16~235 */
	ISP4FW_ISP_YUV_RANGE_MAX
};

enum isp4fw_buffer_type {
	ISP4FW_BUFFER_TYPE_PREVIEW = 8,
	ISP4FW_BUFFER_TYPE_META_INFO = 10,
	ISP4FW_BUFFER_TYPE_MEM_POOL = 15,
};

enum isp4fw_buffer_status {
	/* The buffer is INVALID */
	ISP4FW_BUFFER_STATUS_INVALID,
	/* The buffer is not filled with image data */
	ISP4FW_BUFFER_STATUS_SKIPPED,
	/* The buffer is available and awaiting to be filled */
	ISP4FW_BUFFER_STATUS_EXIST,
	/* The buffer is filled with image data */
	ISP4FW_BUFFER_STATUS_DONE,
	/* The buffer is unavailable */
	ISP4FW_BUFFER_STATUS_LACK,
	/* The buffer is dirty, probably caused by LMI leakage */
	ISP4FW_BUFFER_STATUS_DIRTY,
	ISP4FW_BUFFER_STATUS_MAX
};

enum isp4fw_buffer_source {
	/* The buffer is from the stream buffer queue */
	ISP4FW_BUFFER_SOURCE_STREAM,
};

struct isp4fw_error_code {
	u32 code1;
	u32 code2;
	u32 code3;
	u32 code4;
	u32 code5;
};

/* Command Structure for FW */

struct isp4fw_cmd {
	u32 cmd_seq_num;
	u32 cmd_id;
	u32 cmd_param[12];
	u16 cmd_stream_id;
	u8 cmd_silent_resp;
	u8 reserved;
	u32 cmd_check_sum;
};

struct isp4fw_resp_cmd_done {
	/*
	 * The host2fw command seqNum.
	 * To indicate which command this response refers to.
	 */
	u32 cmd_seq_num;
	/* The host2fw command id for host double check. */
	u32 cmd_id;
	/*
	 * Indicate the command process status.
	 * 0 means success. 1 means fail. 2 means skipped
	 */
	u16 cmd_status;
	/*
	 * If cmd_status is 1, the command failed. The host can check
	 * isp4fw_error_code for details.
	 */
	u16 isp4fw_error_code;
	/* The response payload type varies by cmd. */
	u8 payload[36];
};

struct isp4fw_resp_param_package {
	u32 package_addr_lo;	/* The low 32 bit of the pkg address. */
	u32 package_addr_hi;	/* The high 32 bit of the pkg address. */
	u32 package_size;	/* The total pkg size in bytes. */
	u32 package_check_sum;	/* The byte sum of the pkg. */
};

struct isp4fw_resp {
	u32 resp_seq_num;
	u32 resp_id;
	union {
		struct isp4fw_resp_cmd_done cmd_done;
		struct isp4fw_resp_param_package frame_done;
		u32 resp_param[12];
	} param;
	u8  reserved[4];
	u32 resp_check_sum;
};

struct isp4fw_mipi_pipe_path_cfg {
	u32 b_enable;
	enum isp4fw_sensor_id isp4fw_sensor_id;
};

struct isp4fw_isp_pipe_path_cfg {
	u32  isp_pipe_id;	/* pipe ids for pipeline construction */
};

struct isp4fw_isp_stream_cfg {
	/* Isp mipi path */
	struct isp4fw_mipi_pipe_path_cfg mipi_pipe_path_cfg;
	/* Isp pipe path */
	struct isp4fw_isp_pipe_path_cfg  isp_pipe_path_cfg;
	/* enable TNR */
	u32 b_enable_tnr;
	/*
	 * Number of frames for RTA processing.
	 * Set to 0 to use the firmware's default value.
	 */
	u32 rta_frames_per_proc;
};

struct isp4fw_image_prop {
	enum isp4fw_image_format image_format;
	u32 width;
	u32 height;
	u32 luma_pitch;
	u32 chroma_pitch;
	enum isp4fw_yuv_range yuv_range;
};

struct isp4fw_buffer {
	/*
	 * A check num for debug usage, host can set the buf_tags
	 * to different numbers
	 */
	u32 buf_tags;
	union {
		u32 value;
		struct {
			u32 space : 16;
			u32 vmid  : 16;
		} bit;
	} vmid_space;
	u32 buf_base_a_lo;		/* Low address of buffer A */
	u32 buf_base_a_hi;		/* High address of buffer A */
	u32 buf_size_a;			/* Buffer size of buffer A */

	u32 buf_base_b_lo;		/* Low address of buffer B */
	u32 buf_base_b_hi;		/* High address of buffer B */
	u32 buf_size_b;			/* Buffer size of buffer B */

	u32 buf_base_c_lo;		/* Low address of buffer C */
	u32 buf_base_c_hi;		/* High address of buffer C */
	u32 buf_size_c;			/* Buffer size of buffer C */
};

struct isp4fw_buffer_meta_info {
	u32 enabled;					/* enabled flag */
	enum isp4fw_buffer_status status;		/* BufferStatus */
	struct isp4fw_error_code err;			/* err code */
	enum isp4fw_buffer_source source;		/* BufferSource */
	struct isp4fw_image_prop image_prop;		/* image_prop */
	struct isp4fw_buffer buffer;			/* buffer info */
};

struct isp4fw_meta_info {
	u32 poc;				/* frame id */
	u32 fc_id;				/* frame ctl id */
	u32 time_stamp_lo;			/* timestamp low 32 bits */
	u32 time_stamp_hi;			/* timestamp_high 32 bits */
	struct isp4fw_buffer_meta_info preview;	/* preview BufferMetaInfo */
};

struct isp4fw_cmd_send_buffer {
	enum isp4fw_buffer_type buffer_type;
	struct isp4fw_buffer buffer;		/* buffer info */
};

struct isp4fw_cmd_set_out_ch_prop {
	enum isp4fw_pipe_out_ch ch;	/* ISP output channel */
	struct isp4fw_image_prop image_prop;	/* image property */
};

struct isp4fw_cmd_enable_out_ch {
	enum isp4fw_pipe_out_ch ch;	/* ISP output channel */
	u32 is_enable;			/* If channel is enabled or not */
};

struct isp4fw_cmd_set_stream_cfg {
	struct isp4fw_isp_stream_cfg stream_cfg; /* stream path config */
};

#endif /* _ISP4_FW_CMD_RESP_H_ */
