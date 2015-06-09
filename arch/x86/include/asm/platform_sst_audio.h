/*
 * platform_sst_audio.h:  sst audio platform data header file
 *
 * Copyright (C) 2012-14 Intel Corporation
 * Author: Jeeja KP <jeeja.kp@intel.com>
 * 	Omair Mohammed Abdullah <omair.m.abdullah@intel.com>
 *	Vinod Koul ,vinod.koul@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _PLATFORM_SST_AUDIO_H_
#define _PLATFORM_SST_AUDIO_H_

#include <linux/sfi.h>

#define MAX_NUM_STREAMS_MRFLD	25
#define MAX_NUM_STREAMS	MAX_NUM_STREAMS_MRFLD

enum sst_audio_task_id_mrfld {
	SST_TASK_ID_NONE = 0,
	SST_TASK_ID_SBA = 1,
	SST_TASK_ID_MEDIA = 3,
	SST_TASK_ID_MAX = SST_TASK_ID_MEDIA,
};

/* Device IDs for Merrifield are Pipe IDs,
 * ref: DSP spec v0.75 */
enum sst_audio_device_id_mrfld {
	/* Output pipeline IDs */
	PIPE_ID_OUT_START = 0x0,
	PIPE_CODEC_OUT0 = 0x2,
	PIPE_CODEC_OUT1 = 0x3,
	PIPE_SPROT_LOOP_OUT = 0x4,
	PIPE_MEDIA_LOOP1_OUT = 0x5,
	PIPE_MEDIA_LOOP2_OUT = 0x6,
	PIPE_VOIP_OUT = 0xC,
	PIPE_PCM0_OUT = 0xD,
	PIPE_PCM1_OUT = 0xE,
	PIPE_PCM2_OUT = 0xF,
	PIPE_MEDIA0_OUT = 0x12,
	PIPE_MEDIA1_OUT = 0x13,
/* Input Pipeline IDs */
	PIPE_ID_IN_START = 0x80,
	PIPE_CODEC_IN0 = 0x82,
	PIPE_CODEC_IN1 = 0x83,
	PIPE_SPROT_LOOP_IN = 0x84,
	PIPE_MEDIA_LOOP1_IN = 0x85,
	PIPE_MEDIA_LOOP2_IN = 0x86,
	PIPE_VOIP_IN = 0x8C,
	PIPE_PCM0_IN = 0x8D,
	PIPE_PCM1_IN = 0x8E,
	PIPE_MEDIA0_IN = 0x8F,
	PIPE_MEDIA1_IN = 0x90,
	PIPE_MEDIA2_IN = 0x91,
	PIPE_RSVD = 0xFF,
};

/* The stream map for each platform consists of an array of the below
 * stream map structure.
 */
struct sst_dev_stream_map {
	u8 dev_num;		/* device id */
	u8 subdev_num;		/* substream */
	u8 direction;
	u8 device_id;		/* fw id */
	u8 task_id;		/* fw task */
	u8 status;
};

struct sst_platform_data {
	/* Intel software platform id*/
	struct sst_dev_stream_map *pdev_strm_map;
	unsigned int strm_map_size;
};

struct sst_info {
	u32 iram_start;
	u32 iram_end;
	bool iram_use;
	u32 dram_start;
	u32 dram_end;
	bool dram_use;
	u32 imr_start;
	u32 imr_end;
	bool imr_use;
	u32 mailbox_start;
	bool use_elf;
	bool lpe_viewpt_rqd;
	unsigned int max_streams;
	u32 dma_max_len;
	u8 num_probes;
};

struct sst_lib_dnld_info {
	unsigned int mod_base;
	unsigned int mod_end;
	unsigned int mod_table_offset;
	unsigned int mod_table_size;
	bool mod_ddr_dnld;
};

struct sst_res_info {
	unsigned int shim_offset;
	unsigned int shim_size;
	unsigned int shim_phy_addr;
	unsigned int ssp0_offset;
	unsigned int ssp0_size;
	unsigned int dma0_offset;
	unsigned int dma0_size;
	unsigned int dma1_offset;
	unsigned int dma1_size;
	unsigned int iram_offset;
	unsigned int iram_size;
	unsigned int dram_offset;
	unsigned int dram_size;
	unsigned int mbox_offset;
	unsigned int mbox_size;
	unsigned int acpi_lpe_res_index;
	unsigned int acpi_ddr_index;
	unsigned int acpi_ipc_irq_index;
};

struct sst_ipc_info {
	int ipc_offset;
	unsigned int mbox_recv_off;
};

struct sst_platform_info {
	const struct sst_info *probe_data;
	const struct sst_ipc_info *ipc_info;
	const struct sst_res_info *res_info;
	const struct sst_lib_dnld_info *lib_info;
	const char *platform;
};
int add_sst_platform_device(void);
#endif

