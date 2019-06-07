/* SPDX-License-Identifier: GPL-2.0+ */

#define JPEG_HEADER_SIZE	601

struct rockchip_vpu_jpeg_ctx {
	int width;
	int height;
	int quality;
	unsigned char *buffer;
};

unsigned char *
rockchip_vpu_jpeg_get_qtable(struct rockchip_vpu_jpeg_ctx *ctx, int index);
void rockchip_vpu_jpeg_header_assemble(struct rockchip_vpu_jpeg_ctx *ctx);
