/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Houlong Wei <houlong.wei@mediatek.com>
 *         Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MTK_MDP_IPI_H__
#define __MTK_MDP_IPI_H__

#define MTK_MDP_MAX_NUM_PLANE		3

enum mdp_ipi_msgid {
	AP_MDP_INIT		= 0xd000,
	AP_MDP_DEINIT		= 0xd001,
	AP_MDP_PROCESS		= 0xd002,

	VPU_MDP_INIT_ACK	= 0xe000,
	VPU_MDP_DEINIT_ACK	= 0xe001,
	VPU_MDP_PROCESS_ACK	= 0xe002
};

#pragma pack(push, 4)

/**
 * struct mdp_ipi_init - for AP_MDP_INIT
 * @msg_id   : AP_MDP_INIT
 * @ipi_id   : IPI_MDP
 * @ap_inst  : AP mtk_mdp_vpu address
 */
struct mdp_ipi_init {
	uint32_t msg_id;
	uint32_t ipi_id;
	uint64_t ap_inst;
};

/**
 * struct mdp_ipi_comm - for AP_MDP_PROCESS, AP_MDP_DEINIT
 * @msg_id        : AP_MDP_PROCESS, AP_MDP_DEINIT
 * @ipi_id        : IPI_MDP
 * @ap_inst       : AP mtk_mdp_vpu address
 * @vpu_inst_addr : VPU MDP instance address
 */
struct mdp_ipi_comm {
	uint32_t msg_id;
	uint32_t ipi_id;
	uint64_t ap_inst;
	uint32_t vpu_inst_addr;
};

/**
 * struct mdp_ipi_comm_ack - for VPU_MDP_DEINIT_ACK, VPU_MDP_PROCESS_ACK
 * @msg_id        : VPU_MDP_DEINIT_ACK, VPU_MDP_PROCESS_ACK
 * @ipi_id        : IPI_MDP
 * @ap_inst       : AP mtk_mdp_vpu address
 * @vpu_inst_addr : VPU MDP instance address
 * @status        : VPU exeuction result
 */
struct mdp_ipi_comm_ack {
	uint32_t msg_id;
	uint32_t ipi_id;
	uint64_t ap_inst;
	uint32_t vpu_inst_addr;
	int32_t status;
};

/**
 * struct mdp_config - configured for source/destination image
 * @x        : left
 * @y        : top
 * @w        : width
 * @h        : height
 * @w_stride : bytes in horizontal
 * @h_stride : bytes in vertical
 * @crop_x   : cropped left
 * @crop_y   : cropped top
 * @crop_w   : cropped width
 * @crop_h   : cropped height
 * @format   : color format
 */
struct mdp_config {
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
	int32_t w_stride;
	int32_t h_stride;
	int32_t crop_x;
	int32_t crop_y;
	int32_t crop_w;
	int32_t crop_h;
	int32_t format;
};

struct mdp_buffer {
	uint64_t addr_mva[MTK_MDP_MAX_NUM_PLANE];
	int32_t plane_size[MTK_MDP_MAX_NUM_PLANE];
	int32_t plane_num;
};

struct mdp_config_misc {
	int32_t orientation; /* 0, 90, 180, 270 */
	int32_t hflip; /* 1 will enable the flip */
	int32_t vflip; /* 1 will enable the flip */
	int32_t alpha; /* global alpha */
};

struct mdp_process_vsi {
	struct mdp_config src_config;
	struct mdp_buffer src_buffer;
	struct mdp_config dst_config;
	struct mdp_buffer dst_buffer;
	struct mdp_config_misc misc;
};

#pragma pack(pop)

#endif /* __MTK_MDP_IPI_H__ */
