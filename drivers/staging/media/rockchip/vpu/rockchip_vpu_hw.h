/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip VPU codec driver
 *
 * Copyright 2018 Google LLC.
 *	Tomasz Figa <tfiga@chromium.org>
 */

#ifndef ROCKCHIP_VPU_HW_H_
#define ROCKCHIP_VPU_HW_H_

#include <linux/interrupt.h>
#include <linux/v4l2-controls.h>
#include <media/mpeg2-ctrls.h>
#include <media/videobuf2-core.h>

struct rockchip_vpu_dev;
struct rockchip_vpu_ctx;
struct rockchip_vpu_buf;
struct rockchip_vpu_variant;

/**
 * struct rockchip_vpu_aux_buf - auxiliary DMA buffer for hardware data
 * @cpu:	CPU pointer to the buffer.
 * @dma:	DMA address of the buffer.
 * @size:	Size of the buffer.
 */
struct rockchip_vpu_aux_buf {
	void *cpu;
	dma_addr_t dma;
	size_t size;
};

/**
 * struct rockchip_vpu_jpeg_enc_hw_ctx
 * @bounce_buffer:	Bounce buffer
 */
struct rockchip_vpu_jpeg_enc_hw_ctx {
	struct rockchip_vpu_aux_buf bounce_buffer;
};

/**
 * struct rockchip_vpu_mpeg2_dec_hw_ctx
 * @qtable:		Quantization table
 */
struct rockchip_vpu_mpeg2_dec_hw_ctx {
	struct rockchip_vpu_aux_buf qtable;
};

/**
 * struct rockchip_vpu_codec_ops - codec mode specific operations
 *
 * @init:	If needed, can be used for initialization.
 *		Optional and called from process context.
 * @exit:	If needed, can be used to undo the .init phase.
 *		Optional and called from process context.
 * @run:	Start single {en,de)coding job. Called from atomic context
 *		to indicate that a pair of buffers is ready and the hardware
 *		should be programmed and started.
 * @done:	Read back processing results and additional data from hardware.
 * @reset:	Reset the hardware in case of a timeout.
 */
struct rockchip_vpu_codec_ops {
	int (*init)(struct rockchip_vpu_ctx *ctx);
	void (*exit)(struct rockchip_vpu_ctx *ctx);
	void (*run)(struct rockchip_vpu_ctx *ctx);
	void (*done)(struct rockchip_vpu_ctx *ctx, enum vb2_buffer_state);
	void (*reset)(struct rockchip_vpu_ctx *ctx);
};

/**
 * enum rockchip_vpu_enc_fmt - source format ID for hardware registers.
 */
enum rockchip_vpu_enc_fmt {
	RK3288_VPU_ENC_FMT_YUV420P = 0,
	RK3288_VPU_ENC_FMT_YUV420SP = 1,
	RK3288_VPU_ENC_FMT_YUYV422 = 2,
	RK3288_VPU_ENC_FMT_UYVY422 = 3,
};

extern const struct rockchip_vpu_variant rk3399_vpu_variant;
extern const struct rockchip_vpu_variant rk3288_vpu_variant;

void rockchip_vpu_watchdog(struct work_struct *work);
void rockchip_vpu_run(struct rockchip_vpu_ctx *ctx);
void rockchip_vpu_irq_done(struct rockchip_vpu_dev *vpu,
			   unsigned int bytesused,
			   enum vb2_buffer_state result);

void rk3288_vpu_jpeg_enc_run(struct rockchip_vpu_ctx *ctx);
void rk3399_vpu_jpeg_enc_run(struct rockchip_vpu_ctx *ctx);
int rockchip_vpu_jpeg_enc_init(struct rockchip_vpu_ctx *ctx);
void rockchip_vpu_jpeg_enc_exit(struct rockchip_vpu_ctx *ctx);

void rk3288_vpu_mpeg2_dec_run(struct rockchip_vpu_ctx *ctx);
void rk3399_vpu_mpeg2_dec_run(struct rockchip_vpu_ctx *ctx);
void rockchip_vpu_mpeg2_dec_copy_qtable(u8 *qtable,
	const struct v4l2_ctrl_mpeg2_quantization *ctrl);
int rockchip_vpu_mpeg2_dec_init(struct rockchip_vpu_ctx *ctx);
void rockchip_vpu_mpeg2_dec_exit(struct rockchip_vpu_ctx *ctx);

#endif /* ROCKCHIP_VPU_HW_H_ */
