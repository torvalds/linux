/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 */

#ifndef _VDEC_IPI_MSG_H_
#define _VDEC_IPI_MSG_H_

/*
 * enum vdec_ipi_msgid - message id between AP and VPU
 * @AP_IPIMSG_XXX	: AP to VPU cmd message id
 * @VPU_IPIMSG_XXX_ACK	: VPU ack AP cmd message id
 */
enum vdec_ipi_msgid {
	AP_IPIMSG_DEC_INIT = 0xA000,
	AP_IPIMSG_DEC_START = 0xA001,
	AP_IPIMSG_DEC_END = 0xA002,
	AP_IPIMSG_DEC_DEINIT = 0xA003,
	AP_IPIMSG_DEC_RESET = 0xA004,

	VPU_IPIMSG_DEC_INIT_ACK = 0xB000,
	VPU_IPIMSG_DEC_START_ACK = 0xB001,
	VPU_IPIMSG_DEC_END_ACK = 0xB002,
	VPU_IPIMSG_DEC_DEINIT_ACK = 0xB003,
	VPU_IPIMSG_DEC_RESET_ACK = 0xB004,
};

/**
 * struct vdec_ap_ipi_cmd - generic AP to VPU ipi command format
 * @msg_id	: vdec_ipi_msgid
 * @vpu_inst_addr : VPU decoder instance address. Used if ABI version < 2.
 * @inst_id     : instance ID. Used if the ABI version >= 2.
 */
struct vdec_ap_ipi_cmd {
	uint32_t msg_id;
	union {
		uint32_t vpu_inst_addr;
		uint32_t inst_id;
	};
};

/**
 * struct vdec_vpu_ipi_ack - generic VPU to AP ipi command format
 * @msg_id	: vdec_ipi_msgid
 * @status	: VPU exeuction result
 * @ap_inst_addr	: AP video decoder instance address
 */
struct vdec_vpu_ipi_ack {
	uint32_t msg_id;
	int32_t status;
	uint64_t ap_inst_addr;
};

/**
 * struct vdec_ap_ipi_init - for AP_IPIMSG_DEC_INIT
 * @msg_id	: AP_IPIMSG_DEC_INIT
 * @reserved	: Reserved field
 * @ap_inst_addr	: AP video decoder instance address
 */
struct vdec_ap_ipi_init {
	uint32_t msg_id;
	uint32_t reserved;
	uint64_t ap_inst_addr;
};

/**
 * struct vdec_ap_ipi_dec_start - for AP_IPIMSG_DEC_START
 * @msg_id	: AP_IPIMSG_DEC_START
 * @vpu_inst_addr : VPU decoder instance address. Used if ABI version < 2.
 * @inst_id     : instance ID. Used if the ABI version >= 2.
 * @data	: Header info
 *	H264 decoder [0]:buf_sz [1]:nal_start
 *	VP8 decoder  [0]:width/height
 *	VP9 decoder  [0]:profile, [1][2] width/height
 * @reserved	: Reserved field
 */
struct vdec_ap_ipi_dec_start {
	uint32_t msg_id;
	union {
		uint32_t vpu_inst_addr;
		uint32_t inst_id;
	};
	uint32_t data[3];
	uint32_t reserved;
};

/**
 * struct vdec_vpu_ipi_init_ack - for VPU_IPIMSG_DEC_INIT_ACK
 * @msg_id	: VPU_IPIMSG_DEC_INIT_ACK
 * @status	: VPU exeuction result
 * @ap_inst_addr	: AP vcodec_vpu_inst instance address
 * @vpu_inst_addr	: VPU decoder instance address
 * @vdec_abi_version:	ABI version of the firmware. Kernel can use it to
 *			ensure that it is compatible with the firmware.
 *			This field is not valid for MT8173 and must not be
 *			accessed for this chip.
 * @inst_id     : instance ID. Valid only if the ABI version >= 2.
 */
struct vdec_vpu_ipi_init_ack {
	uint32_t msg_id;
	int32_t status;
	uint64_t ap_inst_addr;
	uint32_t vpu_inst_addr;
	uint32_t vdec_abi_version;
	uint32_t inst_id;
};

#endif
