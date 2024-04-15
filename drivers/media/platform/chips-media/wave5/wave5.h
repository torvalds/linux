/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Wave5 series multi-standard codec IP - wave5 backend definitions
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#ifndef __WAVE5_FUNCTION_H__
#define __WAVE5_FUNCTION_H__

#define WAVE5_SUBSAMPLED_ONE_SIZE(_w, _h)	(ALIGN((_w) / 4, 16) * ALIGN((_h) / 4, 8))
#define WAVE5_SUBSAMPLED_ONE_SIZE_AVC(_w, _h)	(ALIGN((_w) / 4, 32) * ALIGN((_h) / 4, 4))

/*
 * Bitstream buffer option: Explicit End
 * When set to 1 the VPU assumes that the bitstream has at least one frame and
 * will read until the end of the bitstream buffer.
 * When set to 0 the VPU will not read the last few bytes.
 * This option can be set anytime but cannot be cleared during processing.
 * It can be set to force finish decoding even though there is not enough
 * bitstream data for a full frame.
 */
#define BSOPTION_ENABLE_EXPLICIT_END		BIT(0)
#define BSOPTION_HIGHLIGHT_STREAM_END		BIT(1)
/*
 * When RD_PTR_VALID_FLAG is 0 Wave515 ignores RD_PTR value and starts to
 * decode from the access unit end position of the last decoded picture in
 * bitstream buffer.
 */
#define BSOPTION_RD_PTR_VALID_FLAG		BIT(31)

/*
 * Currently the driver only supports hardware with little endian but for source
 * picture format, the bitstream and the report parameter the hardware works
 * with the opposite endianness, thus hard-code big endian for the register
 * writes
 */
#define PIC_SRC_ENDIANNESS_BIG_ENDIAN		0xf
#define BITSTREAM_ENDIANNESS_BIG_ENDIAN		0xf
#define REPORT_PARAM_ENDIANNESS_BIG_ENDIAN	0xf

#define WTL_RIGHT_JUSTIFIED          0
#define WTL_LEFT_JUSTIFIED           1
#define WTL_PIXEL_8BIT               0
#define WTL_PIXEL_16BIT              1
#define WTL_PIXEL_32BIT              2

/* Mirror & rotation modes of the PRP (pre-processing) module */
#define NONE_ROTATE		0x0
#define ROT_CLOCKWISE_90	0x3
#define ROT_CLOCKWISE_180	0x5
#define ROT_CLOCKWISE_270	0x7
#define MIR_HOR_FLIP		0x11
#define MIR_VER_FLIP		0x9
#define MIR_HOR_VER_FLIP	(MIR_HOR_FLIP | MIR_VER_FLIP)

bool wave5_vpu_is_init(struct vpu_device *vpu_dev);

unsigned int wave5_vpu_get_product_id(struct vpu_device *vpu_dev);

int wave5_vpu_get_version(struct vpu_device *vpu_dev, u32 *revision);

int wave5_vpu_init(struct device *dev, u8 *fw, size_t size);

int wave5_vpu_reset(struct device *dev, enum sw_reset_mode reset_mode);

int wave5_vpu_build_up_dec_param(struct vpu_instance *inst, struct dec_open_param *param);

int wave5_vpu_dec_set_bitstream_flag(struct vpu_instance *inst, bool eos);

int wave5_vpu_hw_flush_instance(struct vpu_instance *inst);

int wave5_vpu_dec_register_framebuffer(struct vpu_instance *inst,
				       struct frame_buffer *fb_arr, enum tiled_map_type map_type,
				       unsigned int count);

int wave5_vpu_re_init(struct device *dev, u8 *fw, size_t size);

int wave5_vpu_dec_init_seq(struct vpu_instance *inst);

int wave5_vpu_dec_get_seq_info(struct vpu_instance *inst, struct dec_initial_info *info);

int wave5_vpu_decode(struct vpu_instance *inst, u32 *fail_res);

int wave5_vpu_dec_get_result(struct vpu_instance *inst, struct dec_output_info *result);

int wave5_vpu_dec_finish_seq(struct vpu_instance *inst, u32 *fail_res);

int wave5_dec_clr_disp_flag(struct vpu_instance *inst, unsigned int index);

int wave5_dec_set_disp_flag(struct vpu_instance *inst, unsigned int index);

int wave5_vpu_clear_interrupt(struct vpu_instance *inst, u32 flags);

dma_addr_t wave5_dec_get_rd_ptr(struct vpu_instance *inst);

int wave5_dec_set_rd_ptr(struct vpu_instance *inst, dma_addr_t addr);

/***< WAVE5 encoder >******/

int wave5_vpu_build_up_enc_param(struct device *dev, struct vpu_instance *inst,
				 struct enc_open_param *open_param);

int wave5_vpu_enc_init_seq(struct vpu_instance *inst);

int wave5_vpu_enc_get_seq_info(struct vpu_instance *inst, struct enc_initial_info *info);

int wave5_vpu_enc_register_framebuffer(struct device *dev, struct vpu_instance *inst,
				       struct frame_buffer *fb_arr, enum tiled_map_type map_type,
				       unsigned int count);

int wave5_vpu_encode(struct vpu_instance *inst, struct enc_param *option, u32 *fail_res);

int wave5_vpu_enc_get_result(struct vpu_instance *inst, struct enc_output_info *result);

int wave5_vpu_enc_finish_seq(struct vpu_instance *inst, u32 *fail_res);

int wave5_vpu_enc_check_open_param(struct vpu_instance *inst, struct enc_open_param *open_param);

#endif /* __WAVE5_FUNCTION_H__ */
