#ifndef __INTEL_SST_FW_IPC_H__
#define __INTEL_SST_FW_IPC_H__
/*
*  intel_sst_fw_ipc.h - Intel SST Driver for audio engine
*
*  Copyright (C) 2008-10 Intel Corporation
*  Author:	Vinod Koul <vinod.koul@intel.com>
*		Harsha Priya <priya.harsha@intel.com>
*		Dharageswari R <dharageswari.r@intel.com>
*		KP Jeeja <jeeja.kp@intel.com>
*  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; version 2 of the License.
*
*  This program is distributed in the hope that it will be useful, but
*  WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*  General Public License for more details.
*
*  You should have received a copy of the GNU General Public License along
*  with this program; if not, write to the Free Software Foundation, Inc.,
*  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
*
* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*
*  This driver exposes the audio engine functionalities to the ALSA
*	and middleware.
*  This file has definitions shared between the firmware and driver
*/

#define MAX_NUM_STREAMS_MRST 3
#define MAX_NUM_STREAMS 6
#define MAX_DBG_RW_BYTES 80
#define MAX_NUM_SCATTER_BUFFERS 8
#define MAX_LOOP_BACK_DWORDS 8
/* IPC base address and mailbox, timestamp offsets */
#define SST_MAILBOX_SIZE 0x0400
#define SST_MAILBOX_SEND 0x0000
#define SST_MAILBOX_RCV 0x0804
#define SST_TIME_STAMP 0x1800
#define SST_RESERVED_OFFSET 0x1A00
#define SST_CHEKPOINT_OFFSET 0x1C00
#define REPLY_MSG 0x80

/* Message ID's for IPC messages */
/* Bits B7: SST or IA/SC ; B6-B4: Msg Category; B3-B0: Msg Type */

/* I2L Firmware/Codec Download msgs */
#define IPC_IA_PREP_LIB_DNLD 0x01
#define IPC_IA_LIB_DNLD_CMPLT 0x02

#define IPC_IA_SET_PMIC_TYPE 0x03
#define IPC_IA_GET_FW_VERSION 0x04
#define IPC_IA_GET_FW_BUILD_INF 0x05
#define IPC_IA_GET_FW_INFO 0x06

/* I2L Codec Config/control msgs */
#define IPC_IA_SET_CODEC_PARAMS 0x10
#define IPC_IA_GET_CODEC_PARAMS 0x11
#define IPC_IA_SET_PPP_PARAMS 0x12
#define IPC_IA_GET_PPP_PARAMS 0x13
#define IPC_IA_PLAY_FRAMES 0x14
#define IPC_IA_CAPT_FRAMES 0x15
#define IPC_IA_PLAY_VOICE 0x16
#define IPC_IA_CAPT_VOICE 0x17
#define IPC_IA_DECODE_FRAMES 0x18

/* I2L Stream config/control msgs */
#define IPC_IA_ALLOC_STREAM 0x20 /* Allocate a stream ID */
#define IPC_IA_FREE_STREAM 0x21 /* Free the stream ID */
#define IPC_IA_SET_STREAM_PARAMS 0x22
#define IPC_IA_GET_STREAM_PARAMS 0x23
#define IPC_IA_PAUSE_STREAM 0x24
#define IPC_IA_RESUME_STREAM 0x25
#define IPC_IA_DROP_STREAM 0x26
#define IPC_IA_DRAIN_STREAM 0x27 /* Short msg with str_id */
#define IPC_IA_TARGET_DEV_SELECT 0x28
#define IPC_IA_CONTROL_ROUTING 0x29

#define IPC_IA_SET_STREAM_VOL 0x2A /*Vol for stream, pre mixer */
#define IPC_IA_GET_STREAM_VOL 0x2B
#define IPC_IA_SET_STREAM_MUTE 0x2C
#define IPC_IA_GET_STREAM_MUTE 0x2D
#define IPC_IA_ENABLE_RX_TIME_SLOT 0x2E /* Enable Rx time slot 0 or 1 */

#define IPC_IA_START_STREAM 0x30 /* Short msg with str_id */

/* Debug msgs */
#define IPC_IA_DBG_MEM_READ 0x40
#define IPC_IA_DBG_MEM_WRITE 0x41
#define IPC_IA_DBG_LOOP_BACK 0x42

/* L2I Firmware/Codec Download msgs */
#define IPC_IA_FW_INIT_CMPLT 0x81
#define IPC_IA_LPE_GETTING_STALLED 0x82
#define IPC_IA_LPE_UNSTALLED 0x83

/* L2I Codec Config/control msgs */
#define IPC_SST_GET_PLAY_FRAMES 0x90 /* Request IA more data */
#define IPC_SST_GET_CAPT_FRAMES 0x91 /* Request IA more data */
#define IPC_SST_BUF_UNDER_RUN 0x92 /* PB Under run and stopped */
#define IPC_SST_BUF_OVER_RUN 0x93 /* CAP Under run and stopped */
#define IPC_SST_DRAIN_END 0x94 /* PB Drain complete and stopped */
#define IPC_SST_CHNGE_SSP_PARAMS 0x95 /* PB SSP parameters changed */
#define IPC_SST_STREAM_PROCESS_FATAL_ERR 0x96/* error in processing a stream */
#define IPC_SST_PERIOD_ELAPSED 0x97 /* period elapsed */
#define IPC_IA_TARGET_DEV_CHNGD 0x98 /* error in processing a stream */

#define IPC_SST_ERROR_EVENT 0x99 /* Buffer over run occured */
/* L2S messages */
#define IPC_SC_DDR_LINK_UP 0xC0
#define IPC_SC_DDR_LINK_DOWN 0xC1
#define IPC_SC_SET_LPECLK_REQ 0xC2
#define IPC_SC_SSP_BIT_BANG 0xC3

/* L2I Error reporting msgs */
#define IPC_IA_MEM_ALLOC_FAIL 0xE0
#define IPC_IA_PROC_ERR 0xE1 /* error in processing a
					stream can be used by playback and
					capture modules */

/* L2I Debug msgs */
#define IPC_IA_PRINT_STRING 0xF0



/* Command Response or Acknowledge message to any IPC message will have
 * same message ID and stream ID information which is sent.
 * There is no specific Ack message ID. The data field is used as response
 * meaning.
 */
enum ackData {
	IPC_ACK_SUCCESS = 0,
	IPC_ACK_FAILURE
};


enum sst_error_codes {
	/* Error code,response to msgId: Description */
	/* Common error codes */
	SST_SUCCESS = 0,	/* Success */
	SST_ERR_INVALID_STREAM_ID, /* Invalid stream ID */
	SST_ERR_INVALID_MSG_ID,	/* Invalid message ID */
	SST_ERR_INVALID_STREAM_OP, /* Invalid stream operation request */
	SST_ERR_INVALID_PARAMS,	/* Invalid params */
	SST_ERR_INVALID_CODEC,	/* Invalid codec type */
	SST_ERR_INVALID_MEDIA_TYPE, /* Invalid media type */
	SST_ERR_STREAM_ERR,  /* ANY: Stream control or config or
					processing error */

	/* IPC specific error codes */
	SST_IPC_ERR_CALL_BACK_NOT_REGD, /* Call back for msg not regd */
	SST_IPC_ERR_STREAM_NOT_ALLOCATED, /* Stream is not allocated  */
	SST_IPC_ERR_STREAM_ALLOC_FAILED, /* ALLOC:Stream alloc failed */
	SST_IPC_ERR_GET_STREAM_FAILED, /* ALLOC:Get stream id failed*/
	SST_ERR_MOD_NOT_AVAIL, /* SET/GET: Mod(AEC/AGC/ALC) not available */
	SST_ERR_MOD_DNLD_RQD, /* SET/GET: Mod(AEC/AGC/ALC) download required */
	SST_ERR_STREAM_STOPPED,		/* ANY: Stream is in stopped state */
	SST_ERR_STREAM_IN_USE, /* ANY: Stream is already in use */

	/* Capture specific error codes */
	SST_CAP_ERR_INCMPLTE_CAPTURE_MSG,/* ANY:Incomplete message */
	SST_CAP_ERR_CAPTURE_FAIL, /* ANY:Capture op failed */
	SST_CAP_ERR_GET_DDR_NEW_SGLIST,
	SST_CAP_ERR_UNDER_RUN,	/* lack of input data */
	SST_CAP_ERR_OVERFLOW,	/* lack of output space */

	/* Playback specific error codes*/
	SST_PB_ERR_INCMPLTE_PLAY_MSG, /* ANY: Incomplete message */
	SST_PB_ERR_PLAY_FAIL, /* ANY: Playback operation failed */
	SST_PB_ERR_GET_DDR_NEW_SGLIST,

	/* Codec manager specific error codes */
	SST_LIB_ERR_LIB_DNLD_REQUIRED, /* ALLOC: Codec download required */
	SST_LIB_ERR_LIB_NOT_SUPPORTED, /* Library is not supported */

	/* Library manager specific error codes */
	SST_SCC_ERR_PREP_DNLD_FAILED, /* Failed to prepare for codec download */
	SST_SCC_ERR_LIB_DNLD_RES_FAILED, /* Lib download resume failed */
	/* Scheduler specific error codes */
	SST_SCH_ERR_FAIL, /* REPORT: */

	/* DMA specific error codes */
	SST_DMA_ERR_NO_CHNL_AVAILABLE, /* DMA Ch not available */
	SST_DMA_ERR_INVALID_INPUT_PARAMS, /* Invalid input params */
	SST_DMA_ERR_CHNL_ALREADY_SUSPENDED, /* Ch is suspended */
	SST_DMA_ERR_CHNL_ALREADY_STARTED, /* Ch already started */
	SST_DMA_ERR_CHNL_NOT_ENABLED, /* Ch not enabled */
	SST_DMA_ERR_TRANSFER_FAILED, /* Transfer failed */
	SST_SSP_ERR_ALREADY_ENABLED, /* REPORT: SSP already enabled */
	SST_SSP_ERR_ALREADY_DISABLED, /* REPORT: SSP already disabled */
	SST_SSP_ERR_NOT_INITIALIZED,

	/* Other error codes */
	SST_ERR_MOD_INIT_FAIL,	/* Firmware Module init failed */

	/* FW init error codes */
	SST_RDR_ERR_IO_DEV_SEL_NOT_ALLOWED,
	SST_RDR_ERR_ROUTE_ALREADY_STARTED,
	SST_RDR_PREP_CODEC_DNLD_FAILED,

	/* Memory debug error codes */
	SST_ERR_DBG_MEM_READ_FAIL,
	SST_ERR_DBG_MEM_WRITE_FAIL,

	/* Decode error codes */
	SST_ERR_DEC_NEED_INPUT_BUF,

};

enum dbg_mem_data_type {
	/* Data type of debug read/write */
	DATA_TYPE_U32,
	DATA_TYPE_U16,
	DATA_TYPE_U8,
};

/* CAUTION NOTE: All IPC message body must be multiple of 32 bits.*/

/* IPC Header */
union ipc_header {
	struct {
		u32  msg_id:8; /* Message ID - Max 256 Message Types */
		u32  str_id:5;
		u32  large:1;	/* Large Message if large = 1 */
		u32  reserved:2;	/* Reserved for future use */
		u32  data:14;	/* Ack/Info for msg, size of msg in Mailbox */
		u32  done:1; /* bit 30 */
		u32  busy:1; /* bit 31 */
	} part;
	u32 full;
} __attribute__ ((packed));

/* Firmware build info */
struct sst_fw_build_info {
	unsigned char  date[16]; /* Firmware build date */
	unsigned char  time[16]; /* Firmware build time */
} __attribute__ ((packed));

struct ipc_header_fw_init {
	struct snd_sst_fw_version fw_version;/* Firmware version details */
	struct sst_fw_build_info build_info;
	u16 result;	/* Fw init result */
	u8 module_id; /* Module ID in case of error */
	u8 debug_info; /* Debug info from Module ID in case of fail */
} __attribute__ ((packed));

/* Address and size info of a frame buffer in DDR */
struct sst_address_info {
	u32 addr; /* Address at IA */
	u32 size; /* Size of the buffer */
} __attribute__ ((packed));

/* Time stamp */
struct snd_sst_tstamp {
	u64 samples_processed;/* capture - data in DDR */
	u64 samples_rendered;/* playback - data rendered */
	u64 bytes_processed;/* bytes decoded or encoded */
	u32 sampling_frequency;/* eg: 48000, 44100 */
	u32 dma_base_address;/* DMA base address */
	u16	dma_channel_no;/* DMA Channel used for the data transfer*/
	u16	reserved;/* 32 bit alignment */
};

/* Frame info to play or capture */
struct sst_frame_info {
	u16  num_entries; /* number of entries to follow */
	u16  rsrvd;
	struct sst_address_info  addr[MAX_NUM_SCATTER_BUFFERS];
} __attribute__ ((packed));

/* Frames info for decode */
struct snd_sst_decode_info {
	unsigned long long input_bytes_consumed;
	unsigned long long output_bytes_produced;
	struct sst_frame_info frames_in;
	struct sst_frame_info frames_out;
} __attribute__ ((packed));

/* SST to IA print debug message*/
struct ipc_sst_ia_print_params {
	u32 string_size;/* Max value is 160 */
	u8 prt_string[160];/* Null terminated Char string */
} __attribute__ ((packed));

/* Voice data message  */
struct snd_sst_voice_data {
	u16 num_bytes;/* Number of valid voice data bytes */
	u8  pcm_wd_size;/* 0=8 bit, 1=16 bit 2=32 bit */
	u8  reserved;/* Reserved */
	u8  voice_data_buf[0];/* Voice data buffer in bytes, little endian */
} __attribute__ ((packed));

/* SST to IA memory read debug message  */
struct ipc_sst_ia_dbg_mem_rw  {
	u16  num_bytes;/* Maximum of MAX_DBG_RW_BYTES */
	u16  data_type;/* enum: dbg_mem_data_type */
	u32  address;	/* Memory address of data memory of data_type */
	u8	rw_bytes[MAX_DBG_RW_BYTES];/* Maximum of 64 bytes can be RW */
} __attribute__ ((packed));

struct ipc_sst_ia_dbg_loop_back {
	u16 num_dwords; /* Maximum of MAX_DBG_RW_BYTES */
	u16 increment_val;/* Increments dwords by this value, 0- no increment */
	u32 lpbk_dwords[MAX_LOOP_BACK_DWORDS];/* Maximum of 8 dwords loopback */
} __attribute__ ((packed));

/* Stream type params struture for Alloc stream */
struct snd_sst_str_type {
	u8 codec_type;		/* Codec type */
	u8 str_type;		/* 1 = voice 2 = music */
	u8 operation;		/* Playback or Capture */
	u8 protected_str;	/* 0=Non DRM, 1=DRM */
	u8 time_slots;
	u8 reserved;		/* Reserved */
	u16 result;		/* Result used for acknowledgment */
} __attribute__ ((packed));

/* Library info structure */
struct module_info {
	u32 lib_version;
	u32 lib_type;/*TBD- KLOCKWORK u8 lib_type;*/
	u32 media_type;
	u8  lib_name[12];
	u32 lib_caps;
	unsigned char  b_date[16]; /* Lib build date */
	unsigned char  b_time[16]; /* Lib build time */
} __attribute__ ((packed));

/* Library slot info */
struct lib_slot_info {
	u8  slot_num; /* 1 or 2 */
	u8  reserved1;
	u16 reserved2;
	u32 iram_size; /* slot size in IRAM */
	u32 dram_size; /* slot size in DRAM */
	u32 iram_offset; /* starting offset of slot in IRAM */
	u32 dram_offset; /* starting offset of slot in DRAM */
} __attribute__ ((packed));

struct snd_sst_lib_download {
	struct module_info lib_info; /* library info type, capabilities etc */
	struct lib_slot_info slot_info; /* slot info to be downloaded */
	u32 mod_entry_pt;
};

struct snd_sst_lib_download_info {
	struct snd_sst_lib_download dload_lib;
	u16 result;	/* Result used for acknowledgment */
	u8 pvt_id; /* Private ID */
	u8 reserved;  /* for alignment */
};

/* Alloc stream params structure */
struct snd_sst_alloc_params {
	struct snd_sst_str_type str_type;
	struct snd_sst_stream_params stream_params;
};

struct snd_sst_fw_get_stream_params {
	struct snd_sst_stream_params codec_params;
	struct snd_sst_pmic_config pcm_params;
};

/* Alloc stream response message */
struct snd_sst_alloc_response {
	struct snd_sst_str_type str_type; /* Stream type for allocation */
	struct snd_sst_lib_download lib_dnld; /* Valid only for codec dnld */
};

/* Drop response */
struct snd_sst_drop_response {
	u32 result;
	u32 bytes;
};

/* CSV Voice call routing structure */
struct snd_sst_control_routing {
	u8 control; /* 0=start, 1=Stop */
	u8 reserved[3];	/* Reserved- for 32 bit alignment */
};


struct ipc_post {
	struct list_head node;
	union ipc_header header; /* driver specific */
	char *mailbox_data;
};

#endif /* __INTEL_SST_FW_IPC_H__ */
