/* SPDX-License-Identifier: GPL-2.0+ */

#define JPEG_HEADER_SIZE	601

struct hantro_jpeg_ctx {
	int width;
	int height;
	int quality;
	unsigned char *buffer;
};

unsigned char *hantro_jpeg_get_qtable(struct hantro_jpeg_ctx *ctx, int index);
void hantro_jpeg_header_assemble(struct hantro_jpeg_ctx *ctx);
