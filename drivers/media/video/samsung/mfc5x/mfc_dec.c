/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_dec.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Decoder interface for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/cacheflush.h>

#include <linux/kernel.h>
#include <linux/slab.h>


#ifdef CONFIG_BUSFREQ
#include <mach/cpufreq.h>
#endif
#include <mach/regs-mfc.h>

#include "mfc_dec.h"
#include "mfc_cmd.h"
#include "mfc_log.h"

#include "mfc_shm.h"
#include "mfc_reg.h"
#include "mfc_mem.h"
#include "mfc_buf.h"

#undef DUMP_STREAM

#ifdef DUMP_STREAM
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/file.h>

static void mfc_fw_debug(void);
static void dump_stream(unsigned long address, unsigned int size);
#endif
static LIST_HEAD(mfc_decoders);

#if 0
#define MPEG4_START_CODE_PREFIX_SIZE	3
#define MPEG4_START_CODE_PREFIX		0x000001
#define MPEG4_START_CODE_MASK		0x000000FF
static int find_mpeg4_startcode(unsigned long addr, unsigned int size)
{
	unsigned char *data;
	unsigned int i = 0;

	/* FIXME: optimize cache operation size */
	mfc_mem_cache_inv((void *)addr, size);

	/* FIXME: optimize matching algorithm */
	data = (unsigned char *)addr;

	for (i = 0; i < (size - MPEG4_START_CODE_PREFIX_SIZE); i++) {
		if ((data[i] == 0x00) && (data[i + 1] == 0x00) && (data[i + 2] == 0x01))
			return i;
	}

	return -1;
}

static int check_vcl(unsigned long addr, unsigned int size)
{
	return -1;
}
#endif

#ifdef DUMP_STREAM
static void mfc_fw_debug(void)
{
	mfc_err("============= MFC FW Debug (Ver: 0x%08x)  ================\n",
			read_reg(0x58));
	mfc_err("== (0x64: 0x%08x) (0x68: 0x%08x) (0xE4: 0x%08x) \
			 (0xE8: 0x%08x)\n", read_reg(0x64), read_reg(0x68),
			read_reg(0xe4), read_reg(0xe8));
	mfc_err("== (0xF0: 0x%08x) (0xF4: 0x%08x) (0xF8: 0x%08x) \
			 (0xFC: 0x%08x)\n", read_reg(0xf0), read_reg(0xf4),
			read_reg(0xf8), read_reg(0xfc));
}

static void dump_stream(unsigned long address, unsigned int size)
{
	int i, j;
	struct file *file;
	loff_t pos = 0;
	int fd;
	unsigned long addr  = (unsigned long) phys_to_virt(address);
	mm_segment_t old_fs;
	char filename[] = "/data/mfc_decinit_instream.raw";

	printk(KERN_INFO "---- start stream dump ----\n");
	printk(KERN_INFO "size: 0x%04x\n", size);
	for (i = 0; i < size; i += 16) {
		mfc_dbg("0x%04x: ", i);

		if ((size - i) >= 16) {
			for (j = 0; j < 16; j++)
				mfc_dbg("0x%02x ",
					 (u8)(*(u8 *)(addr + i + j)));
		} else {
			for (j = 0; j < (size - i); j++)
				mfc_dbg("0x%02x ",
					 (u8)(*(u8 *)(addr + i + j)));
		}
		mfc_dbg("\n");
	}
	printk(KERN_INFO "---- end stream dump ----\n");

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fd = sys_open(filename, O_WRONLY|O_CREAT, 0644);
	if (fd >= 0) {
		sys_write(fd, (u8 *)addr, size);
		file = fget(fd);
		if (file) {
			vfs_write(file, (u8 *)addr, size, &pos);
			fput(file);
		}
		sys_close(fd);
	} else {
		mfc_err("........Open fail : %d\n", fd);
	}

	set_fs(old_fs);
}
#endif

/*
 * [1] alloc_ctx_buf() implementations
 */
 static int alloc_ctx_buf(struct mfc_inst_ctx *ctx)
{
	struct mfc_alloc_buffer *alloc;

	alloc = _mfc_alloc_buf(ctx, MFC_CTX_SIZE, ALIGN_2KB, MBT_CTX | PORT_A);
	if (alloc == NULL) {
		mfc_err("failed alloc context buffer\n");

		return -1;
	}

	ctx->ctxbufofs = mfc_mem_base_ofs(alloc->real) >> 11;
	ctx->ctxbufsize = alloc->size;

	memset((void *)alloc->addr, 0, alloc->size);

	mfc_mem_cache_clean((void *)alloc->addr, alloc->size);

	return 0;
}

static int h264_alloc_ctx_buf(struct mfc_inst_ctx *ctx)
{
	struct mfc_alloc_buffer *alloc;

	alloc = _mfc_alloc_buf(ctx, MFC_CTX_SIZE_L, ALIGN_2KB, MBT_CTX | PORT_A);
	if (alloc == NULL) {
		mfc_err("failed alloc context buffer\n");

		return -1;
	}

	ctx->ctxbufofs = mfc_mem_base_ofs(alloc->real) >> 11;
	ctx->ctxbufsize = alloc->size;

	memset((void *)alloc->addr, 0, alloc->size);

	mfc_mem_cache_clean((void *)alloc->addr, alloc->size);

	return 0;
}

/*
 * [2] alloc_desc_buf() implementations
 */
static int alloc_desc_buf(struct mfc_inst_ctx *ctx)
{
	struct mfc_alloc_buffer *alloc;

	/* FIXME: size fixed? */
	alloc = _mfc_alloc_buf(ctx, MFC_DESC_SIZE, ALIGN_2KB, MBT_DESC | PORT_A);
	if (alloc == NULL) {
		mfc_err("failed alloc descriptor buffer\n");

		return -1;
	}

	ctx->descbufofs = mfc_mem_base_ofs(alloc->real) >> 11;
	/* FIXME: size fixed? */
	ctx->descbufsize = MFC_DESC_SIZE;

	return 0;
}

/*
 * [3] pre_seq_start() implementations
 */
static int pre_seq_start(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	unsigned reg;

	/* slice interface */
	reg = read_reg(MFC_SI_CH1_DPB_CONF_CTRL);
	if (dec_ctx->slice)
		reg |= (1 << 31);
	else
		reg &= ~(1 << 31);
	write_reg(reg, MFC_SI_CH1_DPB_CONF_CTRL);

	return 0;
}

static int h264_pre_seq_start(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_h264 *h264 = (struct mfc_dec_h264 *)dec_ctx->d_priv;
	unsigned int reg;

	pre_seq_start(ctx);

	/* display delay */
	reg = read_reg(MFC_SI_CH1_DPB_CONF_CTRL);
	if (h264->dispdelay_en > 0) {
		/* enable */
		reg |= (1 << 30);
		/* value */
		reg &= ~(0x3FFF << 16);
		reg |= ((h264->dispdelay_val & 0x3FFF) << 16);
	} else {
		/* disable & value clear */
		reg &= ~(0x7FFF << 16);
	}
	write_reg(reg, MFC_SI_CH1_DPB_CONF_CTRL);

	write_shm(ctx, h264->sei_parse, SEI_ENABLE);

	return 0;
}

static int mpeg4_pre_seq_start(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_mpeg4 *mpeg4 = (struct mfc_dec_mpeg4 *)dec_ctx->d_priv;
	unsigned int reg;

	pre_seq_start(ctx);

	/* loop filter, this register can be used by both decoders & encoders */
	reg = read_reg(MFC_ENC_LF_CTRL);
	if (mpeg4->postfilter)
		reg |= (1 << 0);
	else
		reg &= ~(1 << 0);
	write_reg(reg, MFC_ENC_LF_CTRL);

	return 0;
}

static int fimv1_pre_seq_start(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_fimv1 *fimv1 = (struct mfc_dec_fimv1 *)dec_ctx->d_priv;

	pre_seq_start(ctx);

	/* set width, height for FIMV1 */
	write_reg(fimv1->width, MFC_SI_FIMV1_HRESOL);
	write_reg(fimv1->height, MFC_SI_FIMV1_VRESOL);

	return 0;
}

/*
 * [4] post_seq_start() implementations
 */
static int post_seq_start(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	unsigned int shm;

	/* CHKME: case of FIMV1 */
	ctx->width = read_reg(MFC_SI_HRESOL);
	ctx->height = read_reg(MFC_SI_VRESOL);

	dec_ctx->nummindpb = read_reg(MFC_SI_BUF_NUMBER);
	dec_ctx->numtotaldpb = dec_ctx->nummindpb + dec_ctx->numextradpb;

	shm = read_shm(ctx, DISP_PIC_PROFILE);
	dec_ctx->level = (shm >> 8) & 0xFF;
	dec_ctx->profile = shm & 0x1F;

	return 0;
}

#define MIN_H264_DPB 6
static int h264_post_seq_start(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_h264 *h264 = (struct mfc_dec_h264 *)dec_ctx->d_priv;
	unsigned int shm;

	/*
	post_seq_start(ctx);
	*/
	ctx->width = read_reg(MFC_SI_HRESOL);
	ctx->height = read_reg(MFC_SI_VRESOL);

	dec_ctx->nummindpb = read_reg(MFC_SI_BUF_NUMBER);
	dec_ctx->numtotaldpb = dec_ctx->nummindpb + dec_ctx->numextradpb;

	if (dec_ctx->numtotaldpb < MIN_H264_DPB)
		dec_ctx->numtotaldpb = MIN_H264_DPB;

	mfc_dbg("nummindpb: %d, numextradpb: %d\n", dec_ctx->nummindpb,
			dec_ctx->numextradpb);

	shm = read_shm(ctx, DISP_PIC_PROFILE);
	dec_ctx->level = (shm >> 8) & 0xFF;
	dec_ctx->profile = shm & 0x1F;

	/* FIXME: consider it */
	/*
	h264->dispdelay_en > 0

	if (dec_ctx->numtotaldpb < h264->dispdelay_val)
		dec_ctx->numtotaldpb = h264->dispdelay_val;
	*/

	h264->crop_r_ofs = (read_shm(ctx, CROP_INFO1) >> 16) & 0xFFFF;
	h264->crop_l_ofs = read_shm(ctx, CROP_INFO1) & 0xFFFF;
	h264->crop_b_ofs = (read_shm(ctx, CROP_INFO2) >> 16) & 0xFFFF;
	h264->crop_t_ofs = read_shm(ctx, CROP_INFO2) & 0xFFFF;

	return 0;
}

static int mpeg4_post_seq_start(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_mpeg4 *mpeg4 = (struct mfc_dec_mpeg4 *)dec_ctx->d_priv;
	unsigned int shm;

	/*
	post_seq_start(ctx);
	*/
	ctx->width = read_reg(MFC_SI_HRESOL);
	ctx->height = read_reg(MFC_SI_VRESOL);

	dec_ctx->nummindpb = read_reg(MFC_SI_BUF_NUMBER);
	dec_ctx->numtotaldpb = dec_ctx->nummindpb + dec_ctx->numextradpb;

	shm = read_shm(ctx, DISP_PIC_PROFILE);
	dec_ctx->level = (shm >> 8) & 0xFF;
	dec_ctx->profile = shm & 0x1F;

	mpeg4->aspect_ratio = read_shm(ctx, ASPECT_RATIO_INFO) & 0xF;
	if (mpeg4->aspect_ratio == 0xF) {
		shm = read_shm(ctx, EXTENDED_PAR);
		mpeg4->ext_par_width = (shm >> 16) & 0xFFFF;
		mpeg4->ext_par_height = shm & 0xFFFF;
	} else {
		mpeg4->ext_par_width = 0;
		mpeg4->ext_par_height = 0;
	}

	return 0;
}

static int vc1_post_seq_start(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	unsigned int shm;

	/*
	post_seq_start(ctx);
	*/
	ctx->width = read_reg(MFC_SI_HRESOL);
	ctx->height = read_reg(MFC_SI_VRESOL);

	dec_ctx->nummindpb = read_reg(MFC_SI_BUF_NUMBER);
	dec_ctx->numtotaldpb = dec_ctx->nummindpb + dec_ctx->numextradpb;

	shm = read_shm(ctx, DISP_PIC_PROFILE);
	dec_ctx->level = (shm >> 8) & 0xFF;
	dec_ctx->profile = shm & 0x1F;

	return 0;
}

static int fimv1_post_seq_start(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_fimv1 *fimv1 = (struct mfc_dec_fimv1 *)dec_ctx->d_priv;
	unsigned int shm;

	/*
	post_seq_start(ctx);
	*/
	ctx->width = read_reg(MFC_SI_HRESOL);
	ctx->height = read_reg(MFC_SI_VRESOL);

	dec_ctx->nummindpb = read_reg(MFC_SI_BUF_NUMBER);
	dec_ctx->numtotaldpb = dec_ctx->nummindpb + dec_ctx->numextradpb;

	shm = read_shm(ctx, DISP_PIC_PROFILE);
	dec_ctx->level = (shm >> 8) & 0xFF;
	dec_ctx->profile = shm & 0x1F;

	fimv1->aspect_ratio = read_shm(ctx, ASPECT_RATIO_INFO) & 0xF;
	if (fimv1->aspect_ratio == 0xF) {
		shm = read_shm(ctx, EXTENDED_PAR);
		fimv1->ext_par_width = (shm >> 16) & 0xFFFF;
		fimv1->ext_par_height = shm & 0xFFFF;
	} else {
		fimv1->ext_par_width = 0;
		fimv1->ext_par_height = 0;
	}

	return 0;
}

/*
 * [5] set_init_arg() implementations
 */
static int set_init_arg(struct mfc_inst_ctx *ctx, void *arg)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_init_arg *dec_init_arg = (struct mfc_dec_init_arg *)arg;

	dec_init_arg->out_frm_width = ctx->width;
	dec_init_arg->out_frm_height = ctx->height;
	dec_init_arg->out_buf_width = ALIGN(ctx->width, ALIGN_W);
	dec_init_arg->out_buf_height = ALIGN(ctx->height, ALIGN_H);

	dec_init_arg->out_dpb_cnt = dec_ctx->numtotaldpb;

	return 0;
}

static int h264_set_init_arg(struct mfc_inst_ctx *ctx, void *arg)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_h264 *h264 = (struct mfc_dec_h264 *)dec_ctx->d_priv;
	struct mfc_dec_init_arg *dec_init_arg = (struct mfc_dec_init_arg *)arg;

	set_init_arg(ctx, arg);

	dec_init_arg->out_crop_right_offset = h264->crop_r_ofs;
	dec_init_arg->out_crop_left_offset = h264->crop_l_ofs;
	dec_init_arg->out_crop_bottom_offset = h264->crop_b_ofs;
	dec_init_arg->out_crop_top_offset = h264->crop_t_ofs;

	return 0;
}

static int mpeg4_set_init_arg(struct mfc_inst_ctx *ctx, void *arg)
{
	/*
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_mpeg4 *mpeg4 = (struct mfc_dec_mpeg4 *)dec_ctx->d_priv;
	struct mfc_dec_init_arg *dec_init_arg = (struct mfc_dec_init_arg *)arg;
	*/

	set_init_arg(ctx, arg);

	/*
	dec_init_arg->out_aspect_ratio = mpeg4->aspect_ratio;
	dec_init_arg->out_ext_par_width = mpeg4->ext_par_width;
	dec_init_arg->out_ext_par_height = mpeg4->ext_par_height;
	*/

	return 0;
}

/*
 * [6] set_codec_bufs() implementations
 */
static int set_codec_bufs(struct mfc_inst_ctx *ctx)
{
	return 0;
}

static int h264_set_codec_bufs(struct mfc_inst_ctx *ctx)
{
	struct mfc_alloc_buffer *alloc;

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_NBMV_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_VERT_NB_MV_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_NBIP_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_VERT_NB_IP_ADR);

	return 0;
}

static int vc1_set_codec_bufs(struct mfc_inst_ctx *ctx)
{
	struct mfc_alloc_buffer *alloc;

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_NBDCAC_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_NB_DCAC_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_UPNBMV_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_UP_NB_MV_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_SAMV_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_SA_MV_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_OTLINE_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_OT_LINE_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_BITPLANE_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_BITPLANE3_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_BITPLANE_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_BITPLANE2_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_BITPLANE_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_BITPLANE1_ADR);

	return 0;
}

static int mpeg4_set_codec_bufs(struct mfc_inst_ctx *ctx)
{
	struct mfc_alloc_buffer *alloc;

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_NBDCAC_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_NB_DCAC_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_UPNBMV_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_UP_NB_MV_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_SAMV_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_SA_MV_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_OTLINE_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_OT_LINE_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_SYNPAR_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_SP_ADR);

	return 0;
}

static int h263_set_codec_bufs(struct mfc_inst_ctx *ctx)
{
	struct mfc_alloc_buffer *alloc;

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_NBDCAC_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_NB_DCAC_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_UPNBMV_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_UP_NB_MV_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_SAMV_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_SA_MV_ADR);

	alloc = _mfc_alloc_buf(ctx, MFC_DEC_OTLINE_SIZE, ALIGN_2KB, MBT_CODEC | PORT_A);
	if (alloc == NULL) {
		mfc_free_buf_type(ctx->id, MBT_CODEC);
		mfc_err("failed alloc codec buffer\n");

		return -1;
	}
	write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_OT_LINE_ADR);

	return 0;
}

/*
 * [7] set_dpbs() implementations
 */
static int set_dpbs(struct mfc_inst_ctx *ctx)
{
	struct mfc_alloc_buffer *alloc;
	int i;
	unsigned int reg;
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;

	/* width: 128B align, height: 32B align, size: 8KB align */
	/* add some guard buffers to luma & chroma */
	dec_ctx->lumasize = ALIGN(ctx->width + 24, ALIGN_W) * ALIGN(ctx->height + 16, ALIGN_H);
	dec_ctx->lumasize = ALIGN(dec_ctx->lumasize, ALIGN_8KB);
	dec_ctx->chromasize = ALIGN(ctx->width + 16, ALIGN_W) * ALIGN((ctx->height >> 1) + 4, ALIGN_H);
	dec_ctx->chromasize = ALIGN(dec_ctx->chromasize, ALIGN_8KB);

	for (i = 0; i < dec_ctx->numtotaldpb; i++) {
		/*
		 * allocate chroma buffer
		 */
		alloc = _mfc_alloc_buf(ctx, dec_ctx->chromasize, ALIGN_2KB, MBT_DPB | PORT_A);
		if (alloc == NULL) {
			mfc_free_buf_type(ctx->id, MBT_DPB);
			mfc_err("failed alloc chroma buffer\n");

			return -1;
		}

		/* clear first DPB chroma buffer, referrence buffer for
		   vectors starting with p-frame */
		if (i == 0) {
			memset((void *)alloc->addr, 0x80, alloc->size);
			mfc_mem_cache_clean((void *)alloc->addr, alloc->size);
		}

		/*
		 * set chroma buffer address
		 */
		write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_CHROMA_ADR + (4 * i));

		/*
		 * allocate luma buffer
		 */
		alloc = _mfc_alloc_buf(ctx, dec_ctx->lumasize, ALIGN_2KB, MBT_DPB | PORT_B);
		if (alloc == NULL) {
			mfc_free_buf_type(ctx->id, MBT_DPB);
			mfc_err("failed alloc luma buffer\n");

			return -1;
		}

		/* clear first DPB luma buffer, referrence buffer for
		   vectors starting with p-frame */
		if (i == 0) {
			memset((void *)alloc->addr, 0x0, alloc->size);
			mfc_mem_cache_clean((void *)alloc->addr, alloc->size);
		}

		/*
		 * set luma buffer address
		 */
		write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_LUMA_ADR + (4 * i));
	}

	write_shm(ctx, dec_ctx->lumasize, ALLOCATED_LUMA_DPB_SIZE);
	write_shm(ctx, dec_ctx->chromasize, ALLOCATED_CHROMA_DPB_SIZE);
	write_shm(ctx, 0, ALLOCATED_MV_SIZE);

	/* set DPB number */
	reg = read_reg(MFC_SI_CH1_DPB_CONF_CTRL);
	reg &= ~(0x3FFF);
	reg |= dec_ctx->numtotaldpb;
	write_reg(reg, MFC_SI_CH1_DPB_CONF_CTRL);

	return 0;
}

static int h264_set_dpbs(struct mfc_inst_ctx *ctx)
{
	struct mfc_alloc_buffer *alloc;
	int i;
	unsigned int reg;
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_h264 *h264 = (struct mfc_dec_h264 *)dec_ctx->d_priv;

	/* width: 128B align, height: 32B align, size: 8KB align */
	dec_ctx->lumasize = ALIGN(ctx->width, ALIGN_W) * ALIGN(ctx->height, ALIGN_H);
	dec_ctx->lumasize = ALIGN(dec_ctx->lumasize, ALIGN_8KB);
	dec_ctx->chromasize = ALIGN(ctx->width, ALIGN_W) * ALIGN(ctx->height >> 1, ALIGN_H);
	dec_ctx->chromasize = ALIGN(dec_ctx->chromasize, ALIGN_8KB);

	h264->mvsize = ALIGN(ctx->width, ALIGN_W) * ALIGN(ctx->height >> 2, ALIGN_H);
	h264->mvsize = ALIGN(h264->mvsize, ALIGN_8KB);

	for (i = 0; i < dec_ctx->numtotaldpb; i++) {
		/*
		 * allocate chroma buffer
		 */
		alloc = _mfc_alloc_buf(ctx, dec_ctx->chromasize, ALIGN_2KB, MBT_DPB | PORT_A);
		if (alloc == NULL) {
			mfc_free_buf_type(ctx->id, MBT_DPB);
			mfc_err("failed alloc chroma buffer\n");

			return -1;
		}

		/* clear last DPB chroma buffer, referrence buffer for
		   vectors starting with p-frame */
#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
		if ((i == (dec_ctx->numtotaldpb - 1)) && (!ctx->dev->drm_playback)) {
#else
		if (i == (dec_ctx->numtotaldpb - 1)) {
#endif
			memset((void *)alloc->addr, 0x80, alloc->size);
			mfc_mem_cache_clean((void *)alloc->addr, alloc->size);
		}

		/*
		 * set chroma buffer address
		 */
		write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_CHROMA_ADR + (4 * i));

		/*
		 * allocate luma buffer
		 */
		alloc = _mfc_alloc_buf(ctx, dec_ctx->lumasize, ALIGN_2KB, MBT_DPB | PORT_B);
		if (alloc == NULL) {
			mfc_free_buf_type(ctx->id, MBT_DPB);
			mfc_err("failed alloc luma buffer\n");

			return -1;
		}

		/* clear last DPB luma buffer, referrence buffer for
		   vectors starting with p-frame */
#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
		if ((i == (dec_ctx->numtotaldpb - 1)) && (!ctx->dev->drm_playback)) {
#else
		if (i == (dec_ctx->numtotaldpb - 1)) {
#endif
			memset((void *)alloc->addr, 0x0, alloc->size);
			mfc_mem_cache_clean((void *)alloc->addr, alloc->size);
		}

		/*
		 * set luma buffer address
		 */
		write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_LUMA_ADR + (4 * i));

		/*
		 * allocate mv buffer
		 */
		alloc = _mfc_alloc_buf(ctx, h264->mvsize, ALIGN_2KB, MBT_DPB | PORT_B);
		if (alloc == NULL) {
			mfc_free_buf_type(ctx->id, MBT_DPB);
			mfc_err("failed alloc mv buffer\n");

			return -1;
		}
		/*
		 * set mv buffer address
		 */
		write_reg(mfc_mem_base_ofs(alloc->real) >> 11, MFC_MV_ADR + (4 * i));
	}

	write_shm(ctx, dec_ctx->lumasize, ALLOCATED_LUMA_DPB_SIZE);
	write_shm(ctx, dec_ctx->chromasize, ALLOCATED_CHROMA_DPB_SIZE);

	write_shm(ctx, h264->mvsize, ALLOCATED_MV_SIZE);

	/* set DPB number */
	reg = read_reg(MFC_SI_CH1_DPB_CONF_CTRL);
	reg &= ~(0x3FFF);
	reg |= dec_ctx->numtotaldpb;
	write_reg(reg, MFC_SI_CH1_DPB_CONF_CTRL);

	return 0;
}

/*
 * [8] pre_frame_start() implementations
 */
static int pre_frame_start(struct mfc_inst_ctx *ctx)
{
	return 0;
}

/*
 * [9] post_frame_start() implementations
 */
static int post_frame_start(struct mfc_inst_ctx *ctx)
{
	return 0;
}

static int h264_post_frame_start(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_h264 *h264 = (struct mfc_dec_h264 *)dec_ctx->d_priv;
	unsigned int shm;

	/* h264->sei_parse */
	h264->fp.available = read_shm(ctx, FRAME_PACK_SEI_AVAIL) & 0x1;

	if (h264->fp.available) {
		h264->fp.arrangement_id = read_shm(ctx, FRAME_PACK_ARRGMENT_ID);

		shm = read_shm(ctx, FRAME_PACK_DEC_INFO);
		h264->fp.arrangement_cancel_flag =	(shm >>  0) & 0x1;
		h264->fp.arrangement_type =		(shm >>  1) & 0x7F;
		h264->fp.quincunx_sampling_flag =	(shm >>  8) & 0x1;
		h264->fp.content_interpretation_type =	(shm >>  9) & 0x3F;
		h264->fp.spatial_flipping_flag =	(shm >> 15) & 0x1;
		h264->fp.frame0_flipped_flag =		(shm >> 16) & 0x1;
		h264->fp.field_views_flag =		(shm >> 17) & 0x1;
		h264->fp.current_frame_is_frame0_flag =	(shm >> 18) & 0x1;

		shm = read_shm(ctx, FRAME_PACK_GRID_POS);
		h264->fp.frame0_grid_pos_x = (shm >>  0) & 0xF;
		h264->fp.frame0_grid_pos_y = (shm >>  4) & 0xF;
		h264->fp.frame1_grid_pos_x = (shm >>  8) & 0xF;
		h264->fp.frame1_grid_pos_y = (shm >> 12) & 0xF;
	} else {
		memset((void *)&h264->fp, 0x00, sizeof(struct mfc_frame_packing));
	}

	return 0;
}

/*
 * [10] multi_frame_start() implementations
 */
static int multi_data_frame(struct mfc_inst_ctx *ctx)
{
	return 0;
}

static int mpeg4_multi_data_frame(struct mfc_inst_ctx *ctx)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_mpeg4 *mpeg4 = (struct mfc_dec_mpeg4 *)dec_ctx->d_priv;

	if (!mpeg4->packedpb)
		return 0;

	/* FIXME: I_FRAME is valid? */
	if ((dec_ctx->decframetype == DEC_FRM_I) || (dec_ctx->decframetype == DEC_FRM_P)) {

	}

	return 0;
}

/*
 * [11] set_exe_arg() implementations
 */
static int set_exe_arg(struct mfc_inst_ctx *ctx, void *arg)
{
	/*
	struct mfc_dec_exe_arg *dec_exe_arg = (struct mfc_dec_exe_arg *)arg;
	*/

	return 0;
}

/*
 * [12] get_codec_cfg() implementations
 */
static int get_codec_cfg(struct mfc_inst_ctx *ctx, int type, void *arg)
{
	//struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	union _mfc_config_arg *usercfg = (union _mfc_config_arg *)arg;

	int ret = 0;

	mfc_dbg("type: 0x%08x", type);

	/*
	MFC_DEC_GETCONF_CRC_DATA	= DEC_GET,
	MFC_DEC_GETCONF_BUF_WIDTH_HEIGHT
	MFC_DEC_GETCONF_FRAME_TAG,
	MFC_DEC_GETCONF_PIC_TIME,

	MFC_DEC_GETCONF_ASPECT_RATIO:
	MFC_DEC_GETCONF_EXTEND_PAR:
	*/

	switch (type) {
	case MFC_DEC_GETCONF_CRC_DATA:
		usercfg->basic.values[0] = 0x12;
		usercfg->basic.values[1] = 0x34;
		usercfg->basic.values[2] = 0x56;
		usercfg->basic.values[3] = 0x78;

		break;

	default:
		mfc_dbg("not common cfg, try to codec specific: 0x%08x\n", type);
		ret = 1;

		break;
	}

	return ret;
}

static int h264_get_codec_cfg(struct mfc_inst_ctx *ctx, int type, void *arg)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_h264 *h264 = (struct mfc_dec_h264 *)dec_ctx->d_priv;
	union _mfc_config_arg *usercfg = (union _mfc_config_arg *)arg;
	int ret = 0;

	mfc_dbg("type: 0x%08x", type);
	ret = get_codec_cfg(ctx, type, arg);
	if (ret <= 0)
		return ret;

	switch (type) {
	case MFC_DEC_GETCONF_FRAME_PACKING:
		if (ctx->state < INST_STATE_EXE) {
			mfc_dbg("invalid instance state: 0x%08x\n", type);
			return MFC_STATE_INVALID;
		}

		memcpy(&usercfg->frame_packing, &h264->fp, sizeof(struct mfc_frame_packing));

		break;

	default:
		mfc_err("invalid get config type: 0x%08x\n", type);
		ret = -2;

		break;
	}

	return ret;
}
/*
 * [13] set_codec_cfg() implementations
 */
static int set_codec_cfg(struct mfc_inst_ctx *ctx, int type, void *arg)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	union _mfc_config_arg *usercfg = (union _mfc_config_arg *)arg;
	int ret = 0;

	mfc_dbg("type: 0x%08x", type);

	/*
	MFC_DEC_SETCONF_FRAME_TAG,
	...
	*/

	switch (type) {
	/*
	case MFC_DEC_SETCONF_EXTRA_BUFFER_NUM:
		if (ctx->state >= INST_STATE_INIT)
			return MFC_STATE_INVALID;

		if ((usercfg->basic.values[0] >= 0) && (usercfg->basic.values[0] <= MFC_MAX_EXTRA_DPB)) {
			dec_ctx->numextradpb = usercfg->basic.values[0];
		} else {
			dec_ctx->numextradpb = MFC_MAX_EXTRA_DPB;
			mfc_warn("invalid extra dpb buffer number: %d", usercfg->basic.values[0]);
			mfc_warn("set %d by default", MFC_MAX_EXTRA_DPB);
		}

		break;
	*/
	case MFC_DEC_SETCONF_IS_LAST_FRAME:
		mfc_dbg("ctx->state: 0x%08x", ctx->state);

		if (ctx->state < INST_STATE_EXE) {
			mfc_dbg("invalid instance state: 0x%08x\n", type);
			return MFC_STATE_INVALID;
		}

		if (usercfg->basic.values[0] > 0)
			dec_ctx->lastframe = 1;
		else
			dec_ctx->lastframe = 0;

		break;
	/*
	case MFC_DEC_SETCONF_SLICE_ENABLE:
		if (ctx->state >= INST_STATE_INIT)
			return MFC_STATE_INVALID;

		if (usercfg->basic.values[0] > 0)
			dec_ctx->slice = 1;
		else
			dec_ctx->slice = 0;

		break;
	*/
	/*
	case MFC_DEC_SETCONF_CRC_ENABLE:
		if (ctx->state >= INST_STATE_INIT)
			return MFC_STATE_INVALID;

		if (usercfg->basic.values[0] > 0)
			dec_ctx->crc = 1;
		else
			dec_ctx->crc = 0;

		break;
	*/
	case MFC_DEC_SETCONF_DPB_FLUSH:
		if (ctx->state < INST_STATE_EXE) {
			mfc_dbg("invalid instance state: 0x%08x\n", type);
			return MFC_STATE_INVALID;
		}

		if (usercfg->basic.values[0] > 0){
			dec_ctx->dpbflush = 1;
		}
		break;

	default:
		mfc_dbg("not common cfg, try to codec specific: 0x%08x\n", type);
		ret = 1;

		break;
	}

	return ret;
}

static int h264_set_codec_cfg(struct mfc_inst_ctx *ctx, int type, void *arg)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_h264 *h264 = (struct mfc_dec_h264 *)dec_ctx->d_priv;
	union _mfc_config_arg *usercfg = (union _mfc_config_arg *)arg;
	int ret;

	mfc_dbg("type: 0x%08x", type);

	ret = set_codec_cfg(ctx, type, arg);
	if (ret <= 0)
		return ret;

	ret = 0;

	switch (type) {
	case MFC_DEC_SETCONF_DISPLAY_DELAY:
		if (ctx->state >= INST_STATE_INIT) {
			mfc_dbg("invalid instance state: 0x%08x\n", type);
			return MFC_STATE_INVALID;
		}

		h264->dispdelay_en = 1;
		if ((usercfg->basic.values[0] >= 0) && (usercfg->basic.values[0] <= MFC_MAX_DISP_DELAY)) {
			h264->dispdelay_val = usercfg->basic.values[0];
		} else {
			h264->dispdelay_val = MFC_MAX_DISP_DELAY;
			mfc_warn("invalid diplay delay count: %d", usercfg->basic.values[0]);
			mfc_warn("set %d by default", MFC_MAX_DISP_DELAY);
		}

		break;

	case MFC_DEC_SETCONF_SEI_PARSE:
		mfc_dbg("ctx->state: 0x%08x", ctx->state);

		if (ctx->state >= INST_STATE_INIT) {
			mfc_dbg("invalid instance state: 0x%08x\n", type);
			return MFC_STATE_INVALID;
		}

		if (usercfg->basic.values[0] > 0)
			h264->sei_parse = 1;
		else
			h264->sei_parse = 0;

		break;

	default:
		mfc_err("invalid set cfg type: 0x%08x\n", type);
		ret = -2;

		break;
	}

	return ret;
}

static int mpeg4_set_codec_cfg(struct mfc_inst_ctx *ctx, int type, void *arg)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_mpeg4 *mpeg4 = (struct mfc_dec_mpeg4 *)dec_ctx->d_priv;
	union _mfc_config_arg *usercfg = (union _mfc_config_arg *)arg;
	int ret;

	mfc_dbg("type: 0x%08x", type);

	ret = set_codec_cfg(ctx, type, arg);
	if (ret <= 0)
		return ret;

	ret = 0;

	switch (type) {
	case MFC_DEC_SETCONF_POST_ENABLE:
		if (ctx->state >= INST_STATE_INIT)
			return MFC_STATE_INVALID;

		if (usercfg->basic.values[0] > 0)
			mpeg4->postfilter = 1;
		else
			mpeg4->postfilter = 0;

		break;
/* JYSHIN
	case MFC_DEC_SETCONF_PACKEDPB:
		if (ctx->state < INST_STATE_OPEN)
			return -1;

		if (usercfg->basic.values[0] > 0)
			mpeg4->packedpb = 1;
		else
			mpeg4->packedpb = 1;

		break;
*/
	default:
		mfc_err("invalid set cfg type: 0x%08x\n", type);
		ret = -2;

		break;
	}

	return ret;
}

static int fimv1_set_codec_cfg(struct mfc_inst_ctx *ctx, int type, void *arg)
{
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	struct mfc_dec_fimv1 *fimv1 = (struct mfc_dec_fimv1 *)dec_ctx->d_priv;
	union _mfc_config_arg *usercfg = (union _mfc_config_arg *)arg;
	int ret;

	mfc_dbg("type: 0x%08x", type);

	ret = set_codec_cfg(ctx, type, arg);
	if (ret <= 0)
		return ret;

	ret = 0;

	switch (type) {
	case MFC_DEC_SETCONF_FIMV1_WIDTH_HEIGHT:
		if (ctx->state >= INST_STATE_INIT)
			return MFC_STATE_INVALID;

		fimv1->width = usercfg->basic.values[0];
		fimv1->height = usercfg->basic.values[1];

		break;
/* JYSHIN
	case MFC_DEC_SETCONF_PACKEDPB:
		if (ctx->state < INST_STATE_OPEN)
			return -1;

		if (usercfg->basic.[0] > 0)
			fimv1->packedpb = 1;
		else
			fimv1->packedpb = 1;

		break;
*/
	default:
		mfc_err("invalid set cfg type: 0x%08x\n", type);
		ret = -2;

		break;
	}

	return ret;
}

static struct mfc_dec_info unknown_dec = {
	.name		= "UNKNOWN",
	.codectype	= UNKNOWN_TYPE,
	.codecid	= -1,
	.d_priv_size	= 0,
	/*
	 * The unknown codec operations will be not call,
	 * unused default operations raise build warning.
	 */
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= pre_seq_start,
		.post_seq_start		= post_seq_start,
		.set_init_arg		= set_init_arg,
		.set_codec_bufs		= set_codec_bufs,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= pre_frame_start,
		.post_frame_start	= post_frame_start,
		.multi_data_frame	= multi_data_frame,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= set_codec_cfg,
	},
};

static struct mfc_dec_info h264_dec = {
	.name		= "H264",
	.codectype	= H264_DEC,
	.codecid	= 0,
	.d_priv_size	= sizeof(struct mfc_dec_h264),
	.c_ops		= {
		.alloc_ctx_buf		= h264_alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= h264_pre_seq_start,
		.post_seq_start		= h264_post_seq_start,
		.set_init_arg		= h264_set_init_arg,
		.set_codec_bufs		= h264_set_codec_bufs,
		.set_dpbs		= h264_set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= h264_post_frame_start,
		.multi_data_frame	= NULL,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= h264_get_codec_cfg,
		.set_codec_cfg		= h264_set_codec_cfg,
	},
};

static struct mfc_dec_info vc1_dec = {
	.name		= "VC1",
	.codectype	= VC1_DEC,
	.codecid	= 1,
	.d_priv_size	= 0,
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= pre_seq_start,
		.post_seq_start		= vc1_post_seq_start,
		.set_init_arg		= set_init_arg,
		.set_codec_bufs		= vc1_set_codec_bufs,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= NULL,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= set_codec_cfg,
	},
};

static struct mfc_dec_info mpeg4_dec = {
	.name		= "MPEG4",
	.codectype	= MPEG4_DEC,
	.codecid	= 2,
	.d_priv_size	= sizeof(struct mfc_dec_mpeg4),
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= mpeg4_pre_seq_start,
		.post_seq_start		= mpeg4_post_seq_start,
		.set_init_arg		= mpeg4_set_init_arg,
		.set_codec_bufs		= mpeg4_set_codec_bufs,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= NULL,	/* FIXME: mpeg4_multi_data_frame */
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= mpeg4_set_codec_cfg,
	},
};

static struct mfc_dec_info xvid_dec = {
	.name           = "XVID",
	.codectype      = XVID_DEC,
	.codecid        = 2,
	.d_priv_size	= sizeof(struct mfc_dec_mpeg4),
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= mpeg4_pre_seq_start,
		.post_seq_start		= mpeg4_post_seq_start,
		.set_init_arg		= mpeg4_set_init_arg,
		.set_codec_bufs		= mpeg4_set_codec_bufs,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= NULL,	/* FIXME: mpeg4_multi_data_frame */
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= mpeg4_set_codec_cfg,
	},
};

static struct mfc_dec_info mpeg1_dec = {
	.name		= "MPEG1",
	.codectype	= MPEG1_DEC,
	.codecid	= 3,
	.d_priv_size	= 0,
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= pre_seq_start,
		.post_seq_start		= post_seq_start,
		.set_init_arg		= set_init_arg,
		.set_codec_bufs		= NULL,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= NULL,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= set_codec_cfg,
	},
};

static struct mfc_dec_info mpeg2_dec = {
	.name		= "MPEG2",
	.codectype	= MPEG2_DEC,
	.codecid	= 3,
	.d_priv_size	= 0,
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= pre_seq_start,
		.post_seq_start		= post_seq_start,
		.set_init_arg		= set_init_arg,
		.set_codec_bufs		= NULL,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= NULL,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= set_codec_cfg,
	},
};

static struct mfc_dec_info h263_dec = {
	.name		= "H263",
	.codectype	= H263_DEC,
	.codecid	= 4,
	.d_priv_size	= 0,
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= pre_seq_start,
		.post_seq_start		= post_seq_start,
		.set_init_arg		= set_init_arg,
		.set_codec_bufs		= h263_set_codec_bufs,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= NULL,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= set_codec_cfg,
	},
};

static struct mfc_dec_info vc1rcv_dec = {
	.name		= "VC1RCV",
	.codectype	= VC1RCV_DEC,
	.codecid	= 5,
	.d_priv_size	= 0,
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= pre_seq_start,
		.post_seq_start		= vc1_post_seq_start,
		.set_init_arg		= set_init_arg,
		.set_codec_bufs		= vc1_set_codec_bufs,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= NULL,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= set_codec_cfg,
	},
};

static struct mfc_dec_info fimv1_dec = {
	.name		= "FIMV1",
	.codectype	= FIMV1_DEC,
	.codecid	= 6,
	.d_priv_size	= sizeof(struct mfc_dec_fimv1),
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= fimv1_pre_seq_start,
		.post_seq_start		= fimv1_post_seq_start,
		.set_init_arg		= set_init_arg,		/* FIMXE */
		.set_codec_bufs		= mpeg4_set_codec_bufs, /* FIXME */
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= mpeg4_multi_data_frame,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= fimv1_set_codec_cfg,
	},
};

static struct mfc_dec_info fimv2_dec = {
	.name		= "FIMV2",
	.codectype	= FIMV2_DEC,
	.codecid	= 7,
	.d_priv_size	= sizeof(struct mfc_dec_mpeg4),
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= mpeg4_pre_seq_start,
		.post_seq_start		= mpeg4_post_seq_start,
		.set_init_arg		= mpeg4_set_init_arg,
		.set_codec_bufs		= mpeg4_set_codec_bufs,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= mpeg4_multi_data_frame,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= mpeg4_set_codec_cfg,
	},
};

static struct mfc_dec_info fimv3_dec = {
	.name		= "FIMV3",
	.codectype	= FIMV3_DEC,
	.codecid	= 8,
	.d_priv_size	= sizeof(struct mfc_dec_mpeg4),
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= mpeg4_pre_seq_start,
		.post_seq_start		= mpeg4_post_seq_start,
		.set_init_arg		= mpeg4_set_init_arg,
		.set_codec_bufs		= mpeg4_set_codec_bufs,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= mpeg4_multi_data_frame,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= mpeg4_set_codec_cfg,
	},
};

static struct mfc_dec_info fimv4_dec = {
	.name		= "FIMV4",
	.codectype	= FIMV4_DEC,
	.codecid	= 9,
	.d_priv_size	= sizeof(struct mfc_dec_mpeg4),
	.c_ops		= {
		.alloc_ctx_buf		= alloc_ctx_buf,
		.alloc_desc_buf		= alloc_desc_buf,
		.pre_seq_start		= mpeg4_pre_seq_start,
		.post_seq_start		= mpeg4_post_seq_start,
		.set_init_arg		= mpeg4_set_init_arg,
		.set_codec_bufs		= mpeg4_set_codec_bufs,
		.set_dpbs		= set_dpbs,
		.pre_frame_start	= NULL,
		.post_frame_start	= NULL,
		.multi_data_frame	= mpeg4_multi_data_frame,
		.set_exe_arg		= set_exe_arg,
		.get_codec_cfg		= get_codec_cfg,
		.set_codec_cfg		= mpeg4_set_codec_cfg,
	},
};

static int CheckMPEG4StartCode(unsigned char *src_mem, unsigned int remainSize)
{
    unsigned int index = 0;

	for (index = 0; index < remainSize-3; index++) {
		if ((src_mem[index] == 0x00) && (src_mem[index+1] == 0x00) &&
				(src_mem[index+2] == 0x01))
			return index;
	}

	return -1;
}

static int CheckDecStartCode(unsigned char *src_mem,
				unsigned int nstreamSize,
				SSBSIP_MFC_CODEC_TYPE nCodecType)
{
	unsigned int	index = 0;
	/* Check Start Code within "isearchSize" bytes */
	unsigned int	isearchSize = 20;
	unsigned int	nShift = 0;
	unsigned char	nFlag = 0xFF;

	if (nCodecType == H263_DEC) {
		nFlag = 0x08;
		nShift = 4;
	} else if (nCodecType == MPEG4_DEC) {
		nFlag = 0x01;
		nShift = 0;
	} else if (nCodecType == H264_DEC) {
		nFlag = 0x01;
		nShift = 0;
	} else
		nFlag = 0xFF;

	/* Last frame detection from user */
	if (nstreamSize == 0)
		nFlag = 0xFF;

	if (nFlag == 0xFF)
		return 0;

	if (nstreamSize > 3) {
		if (nstreamSize > isearchSize) {
			for (index = 0; index < isearchSize-3; index++) {
				if ((src_mem[index] == 0x00) &&
					(src_mem[index+1] == 0x00) &&
					((src_mem[index+2] >> nShift) == nFlag))
					return index;
			}
		} else {
			for (index = 0; index < nstreamSize - 3; index++) {
				if ((src_mem[index] == 0x00) &&
					(src_mem[index+1] == 0x00) &&
					((src_mem[index+2] >> nShift) == nFlag))
					return index;
			}
		}
	}

	return -1;
}

void mfc_init_decoders(void)
{
	list_add_tail(&unknown_dec.list, &mfc_decoders);

	list_add_tail(&h264_dec.list, &mfc_decoders);
	list_add_tail(&vc1_dec.list, &mfc_decoders);
	list_add_tail(&mpeg4_dec.list, &mfc_decoders);
	list_add_tail(&xvid_dec.list, &mfc_decoders);
	list_add_tail(&mpeg1_dec.list, &mfc_decoders);
	list_add_tail(&mpeg2_dec.list, &mfc_decoders);
	list_add_tail(&h263_dec.list, &mfc_decoders);
	list_add_tail(&vc1rcv_dec.list, &mfc_decoders);
	list_add_tail(&fimv1_dec.list, &mfc_decoders);
	list_add_tail(&fimv2_dec.list, &mfc_decoders);
	list_add_tail(&fimv3_dec.list, &mfc_decoders);
	list_add_tail(&fimv4_dec.list, &mfc_decoders);

	/* FIXME: 19, 20 */
}

static int mfc_set_decoder(struct mfc_inst_ctx *ctx, SSBSIP_MFC_CODEC_TYPE codectype)
{
	struct list_head *pos;
	struct mfc_dec_info *decoder;
	struct mfc_dec_ctx *dec_ctx;

	ctx->codecid = -1;

	/* find and set codec private */
	list_for_each(pos, &mfc_decoders) {
		decoder = list_entry(pos, struct mfc_dec_info, list);

		if (decoder->codectype == codectype) {
			if (decoder->codecid < 0)
				break;

			/* Allocate Decoder context memory */
			dec_ctx = kzalloc(sizeof(struct mfc_dec_ctx), GFP_KERNEL);
			if (!dec_ctx) {
				mfc_err("failed to allocate codec private\n");
				return -ENOMEM;
			}
			ctx->c_priv = dec_ctx;

			/* Allocate Decoder context private memory */
			dec_ctx->d_priv = kzalloc(decoder->d_priv_size, GFP_KERNEL);
			if (!dec_ctx->d_priv) {
				mfc_err("failed to allocate decoder private\n");
				kfree(dec_ctx);
				ctx->c_priv = NULL;
				return -ENOMEM;
			}

			ctx->codecid = decoder->codecid;
			ctx->type = DECODER;
			ctx->c_ops = (struct codec_operations *)&decoder->c_ops;

			break;
		}
	}

	if (ctx->codecid < 0)
		mfc_err("couldn't find proper decoder codec type: %d\n", codectype);

	return ctx->codecid;
}

static void mfc_set_stream_info(
	struct mfc_inst_ctx *ctx,
	unsigned int addr,
	unsigned int size,
	unsigned int ofs)
{

	if (ctx->buf_cache_type == CACHE) {
		flush_all_cpu_caches();
		outer_flush_all();
	}

	write_reg(addr, MFC_SI_CH1_ES_ADR);
	write_reg(size, MFC_SI_CH1_ES_SIZE);

	/* FIXME: IOCTL_MFC_GET_IN_BUF size */
	write_reg(MFC_CPB_SIZE, MFC_SI_CH1_CPB_SIZE);

	write_reg(ctx->descbufofs, MFC_SI_CH1_DESC_ADR);
	write_reg(ctx->descbufsize, MFC_SI_CH1_DESC_SIZE);

	/* FIXME: right position */
	write_shm(ctx, ofs, START_BYTE_NUM);
}

int mfc_init_decoding(struct mfc_inst_ctx *ctx, union mfc_args *args)
{
	struct mfc_dec_init_arg *init_arg = (struct mfc_dec_init_arg *)args;
	struct mfc_dec_ctx *dec_ctx = NULL;
	struct mfc_pre_cfg *precfg;
	struct list_head *pos, *nxt;
	int ret;

	ret = mfc_set_decoder(ctx, init_arg->in_codec_type);
	if (ret < 0) {
		mfc_err("failed to setup decoder codec\n");
		ret = MFC_DEC_INIT_FAIL;
		goto err_codec_setup;
	}

	dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;

	dec_ctx->streamaddr = init_arg->in_strm_buf;
	dec_ctx->streamsize = init_arg->in_strm_size;

	mfc_dbg("stream size: %d", init_arg->in_strm_size);

	dec_ctx->crc = init_arg->in_crc;
	dec_ctx->pixelcache = init_arg->in_pixelcache;
	dec_ctx->slice = 0;
	mfc_warn("Slice Mode disabled forcefully\n");
	dec_ctx->numextradpb = init_arg->in_numextradpb;
	dec_ctx->dpbflush = 0;
	dec_ctx->ispackedpb = init_arg->in_packed_PB;

	/*
	 * assign pre configuration values to instance context
	 */
	list_for_each_safe(pos, nxt, &ctx->presetcfgs) {
		precfg = list_entry(pos, struct mfc_pre_cfg, list);

		if (ctx->c_ops->set_codec_cfg) {
			ret = ctx->c_ops->set_codec_cfg(ctx, precfg->type, precfg->values);
			if (ret < 0)
				mfc_warn("cannot set preset config type: 0x%08x: %d",
					precfg->type, ret);
		}
	}

	mfc_set_inst_state(ctx, INST_STATE_SETUP);

	/*
	 * allocate context buffer
	 */
#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	if ((!ctx->dev->drm_playback) && (ctx->c_ops->alloc_ctx_buf)) {
#else
	if (ctx->c_ops->alloc_ctx_buf) {
#endif
		if (ctx->c_ops->alloc_ctx_buf(ctx) < 0) {
			ret = MFC_DEC_INIT_FAIL;
			goto err_ctx_buf;
		}
	}

	/* [crc, pixelcache] */
	ret = mfc_cmd_inst_open(ctx);
	if (ret < 0)
		goto err_inst_open;

	mfc_set_inst_state(ctx, INST_STATE_OPEN);

	if (init_shm(ctx) < 0) {
		ret = MFC_DEC_INIT_FAIL;
		goto err_shm_init;
	}

	/*
	 * allocate descriptor buffer
	 */
	if (ctx->c_ops->alloc_desc_buf) {
		if (ctx->c_ops->alloc_desc_buf(ctx) < 0) {
			ret = MFC_DEC_INIT_FAIL;
			goto err_desc_buf;
		}
	}

	/*
	 * execute pre sequence start operation
	 * [slice]
	 */
	if (ctx->c_ops->pre_seq_start) {
		if (ctx->c_ops->pre_seq_start(ctx) < 0) {
			ret = MFC_DEC_INIT_FAIL;
			goto err_pre_seq;
		}
	}

	/* FIXME: postion */
	mfc_set_stream_info(ctx, mfc_mem_base_ofs(dec_ctx->streamaddr) >> 11,
		dec_ctx->streamsize, 0);

	ret = mfc_cmd_seq_start(ctx);
	if (ret < 0)
		goto err_seq_start;

	/* [numextradpb] */
	if (ctx->c_ops->post_seq_start) {
		if (ctx->c_ops->post_seq_start(ctx) < 0) {
			ret = MFC_DEC_INIT_FAIL;
			goto err_post_seq;
		}
	}

	if (ctx->height > MAX_VER_SIZE) {
		if (ctx->height > MAX_HOR_SIZE) {
			mfc_err("Not support resolution: %dx%d\n",
				ctx->width, ctx->height);
			goto err_chk_res;
		}

		if (ctx->width > MAX_VER_SIZE) {
			mfc_err("Not support resolution: %dx%d\n",
				ctx->width, ctx->height);
			goto err_chk_res;
		}
	} else {
		if (ctx->width > MAX_HOR_SIZE) {
			mfc_err("Not support resolution: %dx%d\n",
				ctx->width, ctx->height);
			goto err_chk_res;
		}
	}

	if (ctx->c_ops->set_init_arg) {
		if (ctx->c_ops->set_init_arg(ctx, (void *)init_arg) < 0) {
			ret = MFC_DEC_INIT_FAIL;
			goto err_set_arg;
		}
	}

	mfc_dbg("H: %d, W: %d, DPB_Count: %d", ctx->width, ctx->height,
		dec_ctx->numtotaldpb);

#ifdef CONFIG_BUSFREQ
	/* Lock MFC & Bus FREQ for high resolution */
	if (ctx->width >= MAX_HOR_RES || ctx->height >= MAX_VER_RES) {
		if (atomic_read(&ctx->dev->busfreq_lock_cnt) == 0) {
			if (ctx->codecid == H264_DEC) {
				exynos4_busfreq_lock(DVFS_LOCK_ID_MFC, BUS_L0);
				mfc_dbg("Bus FREQ locked to L0\n");
			} else {
				exynos4_busfreq_lock(DVFS_LOCK_ID_MFC, BUS_L1);
				mfc_dbg("Bus FREQ locked to L1\n");
			}
		}

		atomic_inc(&ctx->dev->busfreq_lock_cnt);
		ctx->busfreq_flag = true;
	}
#endif

	/*
	 * allocate & set codec buffers
	 */
	if (ctx->c_ops->set_codec_bufs) {
		if (ctx->c_ops->set_codec_bufs(ctx) < 0) {
			ret = MFC_DEC_INIT_FAIL;
			goto err_codec_bufs;
		}
	}

	/*
	 * allocate & set DPBs
	 */
	if (ctx->c_ops->set_dpbs) {
		if (ctx->c_ops->set_dpbs(ctx) < 0) {
			ret = MFC_DEC_INIT_FAIL;
			goto err_dpbs_set;
		}
	}

	ret = mfc_cmd_init_buffers(ctx);
	if (ret < 0)
		goto err_buf_init;

	mfc_set_inst_state(ctx, INST_STATE_INIT);

	while (!list_empty(&ctx->presetcfgs)) {
		precfg = list_entry((&ctx->presetcfgs)->next,
				struct mfc_pre_cfg, list);

		mfc_dbg("remove used preset config [0x%08x]\n",
				precfg->type);

		list_del(&precfg->list);
		kfree(precfg);
	}
	INIT_LIST_HEAD(&ctx->presetcfgs);

	mfc_print_buf();

	return MFC_OK;

err_buf_init:
	mfc_free_buf_type(ctx->id, MBT_DPB);

err_dpbs_set:
	mfc_free_buf_type(ctx->id, MBT_CODEC);

err_codec_bufs:
#ifdef CONFIG_BUSFREQ
	/* Release MFC & Bus Frequency lock for High resolution */
	if (ctx->busfreq_flag == true) {
		atomic_dec(&ctx->dev->busfreq_lock_cnt);
		ctx->busfreq_flag = false;

		if (atomic_read(&ctx->dev->busfreq_lock_cnt) == 0) {
			exynos4_busfreq_lock_free(DVFS_LOCK_ID_MFC);
			mfc_dbg("Bus FREQ released\n");
		}
	}
#endif

err_set_arg:
err_chk_res:
err_post_seq:
err_seq_start:
#ifdef DUMP_STREAM
	mfc_fw_debug();
	dump_stream(dec_ctx->streamaddr, dec_ctx->streamsize);
#endif

err_pre_seq:
	mfc_free_buf_type(ctx->id, MBT_DESC);

err_desc_buf:
	mfc_free_buf_type(ctx->id, MBT_SHM);

	ctx->shm = NULL;
	ctx->shmofs = 0;

err_shm_init:
	mfc_cmd_inst_close(ctx);

	ctx->state = INST_STATE_SETUP;

err_inst_open:
	mfc_free_buf_type(ctx->id, MBT_CTX);

err_ctx_buf:
	if (dec_ctx->d_priv)
		kfree(dec_ctx->d_priv);

	kfree(dec_ctx);
	ctx->c_priv = NULL;

	ctx->codecid = -1;
	ctx->type = 0;
	ctx->c_ops = NULL;

	ctx->state = INST_STATE_CREATE;

err_codec_setup:
	return ret;
}

int mfc_change_resolution(struct mfc_inst_ctx *ctx, struct mfc_dec_exe_arg *exe_arg)
{
	int ret;

	mfc_free_buf_type(ctx->id, MBT_DPB);

	ret = mfc_cmd_seq_start(ctx);
	if (ret < 0)
		return ret;

	/* [numextradpb] */
	if (ctx->c_ops->post_seq_start) {
		if (ctx->c_ops->post_seq_start(ctx) < 0)
			return MFC_DEC_INIT_FAIL;
	}

	if (ctx->height > MAX_VER_SIZE) {
		if (ctx->height > MAX_HOR_SIZE) {
			mfc_err("Not support resolution: %dx%d\n",
				ctx->width, ctx->height);
			return MFC_DEC_INIT_FAIL;
		}

		if (ctx->width > MAX_VER_SIZE) {
			mfc_err("Not support resolution: %dx%d\n",
				ctx->width, ctx->height);
			return MFC_DEC_INIT_FAIL;
		}
	} else {
		if (ctx->width > MAX_HOR_SIZE) {
			mfc_err("Not support resolution: %dx%d\n",
				ctx->width, ctx->height);
			return MFC_DEC_INIT_FAIL;
		}
	}

	exe_arg->out_img_width = ctx->width;
	exe_arg->out_img_height = ctx->height;
	exe_arg->out_buf_width = ALIGN(ctx->width, ALIGN_W);
	exe_arg->out_buf_height = ALIGN(ctx->height, ALIGN_H);

	/*
	 * allocate & set DPBs
	 */
	if (ctx->c_ops->set_dpbs) {
		if (ctx->c_ops->set_dpbs(ctx) < 0)
			return MFC_DEC_INIT_FAIL;
	}

	ret = mfc_cmd_init_buffers(ctx);
	if (ret < 0)
		return ret;

	return MFC_OK;
}

int mfc_check_resolution_change(struct mfc_inst_ctx *ctx, struct mfc_dec_exe_arg *exe_arg)
{
	int resol_status;

	if (exe_arg->out_display_status != DISP_S_DECODING)
		return 0;

	resol_status = (read_reg(MFC_SI_DISPLAY_STATUS) >> DISP_RC_SHIFT) & DISP_RC_MASK;

	if (resol_status == DISP_RC_INC || resol_status == DISP_RC_DEC) {
		ctx->resolution_status = RES_SET_CHANGE;
		mfc_dbg("Change Resolution status: %d\n", resol_status);
	}

	return 0;
}

static int mfc_decoding_frame(struct mfc_inst_ctx *ctx, struct mfc_dec_exe_arg *exe_arg, int *consumed)
{
	int start_ofs = *consumed;
	int display_luma_addr;
	int display_chroma_addr;
	int display_frame_type;
	int display_frame_tag;
	unsigned char *stream_vir;
	int ret;
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
#ifdef CONFIG_VIDEO_MFC_VCM_UMP
	void *ump_handle;
#endif

#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	if (!ctx->dev->drm_playback) {
#endif
		/* Check Frame Start code */
		stream_vir = phys_to_virt(exe_arg->in_strm_buf + start_ofs);
		ret = CheckDecStartCode(stream_vir, exe_arg->in_strm_size,
				exe_arg->in_codec_type);
		if (ret < 0) {
			mfc_err("Frame Check start Code Failed\n");
			/* FIXME: Need to define proper error */
			return MFC_FRM_BUF_SIZE_FAIL;
		}
#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	}
#endif

	/* Set Frame Tag */
	write_shm(ctx, dec_ctx->frametag, SET_FRAME_TAG);

	/* FIXME: */
	write_reg(0xFFFFFFFF, MFC_SI_CH1_RELEASE_BUF);
	if (dec_ctx->dpbflush){
		unsigned int reg;
		reg = read_reg(MFC_SI_CH1_DPB_CONF_CTRL);
		reg &= ~(1<<14);
		reg |= (1<<14);
		write_reg(reg, MFC_SI_CH1_DPB_CONF_CTRL); /* for DPB Flush*/
		/*clear dbp flush in context*/
		dec_ctx->dpbflush = 0;
	}

	/* FIXME: postion */
	mfc_set_stream_info(ctx, mfc_mem_base_ofs(exe_arg->in_strm_buf) >> 11,
		exe_arg->in_strm_size, start_ofs);

	/* lastframe: mfc_dec_cfg */
	ret = mfc_cmd_frame_start(ctx);
	if (ret < 0)
		return ret;

	if (ctx->c_ops->post_frame_start) {
		if (ctx->c_ops->post_frame_start(ctx) < 0)
			return MFC_DEC_EXE_ERR;
	}

	/* update display status information */
	dec_ctx->dispstatus = read_reg(MFC_SI_DISPLAY_STATUS) & DISP_S_MASK;

	/* get decode status, frame type */
	dec_ctx->decstatus = read_reg(MFC_SI_DECODE_STATUS) & DEC_S_MASK;
	dec_ctx->decframetype = read_reg(MFC_SI_FRAME_TYPE) & DEC_FRM_MASK;

	if (dec_ctx->dispstatus == DISP_S_DECODING) {
		display_luma_addr = 0;
		display_chroma_addr = 0;

		display_frame_type = DISP_FRM_X;
		display_frame_tag = read_shm(ctx, GET_FRAME_TAG_TOP);
	} else {
		display_luma_addr = read_reg(MFC_SI_DISPLAY_Y_ADR);
		display_chroma_addr = read_reg(MFC_SI_DISPLAY_C_ADR);

		display_frame_type = get_disp_frame_type();
		display_frame_tag = read_shm(ctx, GET_FRAME_TAG_TOP);

		if (dec_ctx->ispackedpb) {
			if ((dec_ctx->decframetype == DEC_FRM_P) || (dec_ctx->decframetype == DEC_FRM_I)) {
				if (display_frame_type == DISP_FRM_N)
					display_frame_type = dec_ctx->predispframetype;
			} else {
				display_luma_addr = dec_ctx->predisplumaaddr;
				display_chroma_addr = dec_ctx->predispchromaaddr;
				display_frame_type = dec_ctx->predispframetype;
				/* over write frame tag */
				display_frame_tag = dec_ctx->predispframetag;
			}

			/* save the display addr */
			dec_ctx->predisplumaaddr = read_reg(MFC_SI_DISPLAY_Y_ADR);
			dec_ctx->predispchromaaddr = read_reg(MFC_SI_DISPLAY_C_ADR);

			/* save the display frame type */
			if (get_disp_frame_type() != DISP_FRM_N) {
				dec_ctx->predispframetype = get_disp_frame_type();
				/* Set Frame Tag */
				dec_ctx->predispframetag =
					read_shm(ctx, GET_FRAME_TAG_TOP);
			}

			mfc_dbg("pre_luma_addr: 0x%08x, pre_chroma_addr:"
					"0x%08x, pre_disp_frame_type: %d\n",
					(dec_ctx->predisplumaaddr << 11),
					(dec_ctx->predispchromaaddr << 11),
					dec_ctx->predispframetype);
		}
	}

	/* handle ImmeidatelyDisplay for Seek, I frame only */
	if (dec_ctx->immediatelydisplay) {
		mfc_dbg("Immediately display\n");
		dec_ctx->dispstatus = dec_ctx->decstatus;
		/* update frame tag information with current ID */
		exe_arg->out_frametag_top = dec_ctx->frametag;
		/* FIXME : need to check this */
		exe_arg->out_frametag_bottom = 0;

		if (dec_ctx->decstatus == DEC_S_DD) {
			mfc_dbg("Immediately display status: DEC_S_DD\n");
			display_luma_addr = read_reg(MFC_SI_DECODE_Y_ADR);
			display_chroma_addr = read_reg(MFC_SI_DECODE_C_ADR);
		}

		display_frame_type = dec_ctx->decframetype;

		/* clear Immediately Display in decode context */
		dec_ctx->immediatelydisplay = 0;
	} else {
		/* Get Frame Tag top and bottom */
		exe_arg->out_frametag_top = display_frame_tag;
		exe_arg->out_frametag_bottom = read_shm(ctx, GET_FRAME_TAG_BOT);
	}

	mfc_dbg("decode y: 0x%08x, c: 0x%08x\n",
		  read_reg(MFC_SI_DECODE_Y_ADR) << 11,
		  read_reg(MFC_SI_DECODE_C_ADR) << 11);

	exe_arg->out_display_status = dec_ctx->dispstatus;

	exe_arg->out_display_Y_addr = (display_luma_addr << 11);
	exe_arg->out_display_C_addr = (display_chroma_addr << 11);

	exe_arg->out_disp_pic_frame_type = display_frame_type;

	exe_arg->out_y_offset = mfc_mem_data_ofs(display_luma_addr << 11, 1);
	exe_arg->out_c_offset = mfc_mem_data_ofs(display_chroma_addr << 11, 1);

#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
	exe_arg->out_y_secure_id = 0;
	exe_arg->out_c_secure_id = 0;

	ump_handle = mfc_get_buf_ump_handle(out_display_Y_addr << 11);
	if (ump_handle != NULL)
		exe_arg->out_y_secure_id = mfc_ump_get_id(ump_handle);

	ump_handle = mfc_get_buf_ump_handle(out_display_C_addr << 11);
	if (ump_handle != NULL)
		exe_arg->out_c_secure_id = mfc_ump_get_id(ump_handle);

	mfc_dbg("secure IDs Y: 0x%08x, C:0x%08x\n", exe_arg->out_y_secure_id,
		exe_arg->out_c_secure_id);
#elif defined(CONFIG_S5P_VMEM)
	exe_arg->out_y_cookie = s5p_getcookie((void *)(out_display_Y_addr << 11));
	exe_arg->out_c_cookie = s5p_getcookie((void *)(out_display_C_addr << 11));

	mfc_dbg("cookie Y: 0x%08x, C:0x%08x\n", exe_arg->out_y_cookie,
		exe_arg->out_c_cookie);
#endif

	exe_arg->out_pic_time_top = read_shm(ctx, PIC_TIME_TOP);
	exe_arg->out_pic_time_bottom = read_shm(ctx, PIC_TIME_BOT);

	exe_arg->out_consumed_byte = read_reg(MFC_SI_FRM_COUNT);

	if (ctx->codecid == H264_DEC) {
		exe_arg->out_crop_right_offset = (read_shm(ctx, CROP_INFO1) >> 16) & 0xFFFF;
		exe_arg->out_crop_left_offset = read_shm(ctx, CROP_INFO1) & 0xFFFF;
		exe_arg->out_crop_bottom_offset = (read_shm(ctx, CROP_INFO2) >> 16) & 0xFFFF;
		exe_arg->out_crop_top_offset = read_shm(ctx, CROP_INFO2)  & 0xFFFF;

		mfc_dbg("crop info t: %d, r: %d, b: %d, l: %d\n",
			exe_arg->out_crop_top_offset,
			exe_arg->out_crop_right_offset,
			exe_arg->out_crop_bottom_offset,
			exe_arg->out_crop_left_offset);
	}
/*
	mfc_dbg("decode frame type: %d\n", dec_ctx->decframetype);
	mfc_dbg("display frame type: %d\n", exe_arg->out_disp_pic_frame_type);
	mfc_dbg("display y: 0x%08x, c: 0x%08x\n",
		exe_arg->out_display_Y_addr, exe_arg->out_display_C_addr);
		*/

	mfc_dbg("decode frame type: %d\n", dec_ctx->decframetype);
	mfc_dbg("display frame type: %d,%d\n",
			exe_arg->out_disp_pic_frame_type,
			exe_arg->out_frametag_top);
	mfc_dbg("display y: 0x%08x, c: 0x%08x\n",
			exe_arg->out_display_Y_addr,
			exe_arg->out_display_C_addr);

	*consumed = read_reg(MFC_SI_FRM_COUNT);
	mfc_dbg("stream size: %d, consumed: %d\n",
		exe_arg->in_strm_size, *consumed);

	return MFC_OK;
}

int mfc_exec_decoding(struct mfc_inst_ctx *ctx, union mfc_args *args)
{
	struct mfc_dec_exe_arg *exe_arg;
	int ret;
	int consumed = 0;
	struct mfc_dec_ctx *dec_ctx = (struct mfc_dec_ctx *)ctx->c_priv;
	int sec_try_tag; /* tag store for second try */

	exe_arg = (struct mfc_dec_exe_arg *)args;

	/* set pre-decoding informations */
	dec_ctx->streamaddr = exe_arg->in_strm_buf;
	dec_ctx->streamsize = exe_arg->in_strm_size;
	dec_ctx->frametag = exe_arg->in_frametag;
	dec_ctx->immediatelydisplay = exe_arg->in_immediately_disp;

	mfc_set_inst_state(ctx, INST_STATE_EXE);

	ret = mfc_decoding_frame(ctx, exe_arg, &consumed);
	sec_try_tag = exe_arg->out_frametag_top;

	mfc_set_inst_state(ctx, INST_STATE_EXE_DONE);

	if (ret == MFC_OK) {
		mfc_check_resolution_change(ctx, exe_arg);
		if (ctx->resolution_status == RES_SET_CHANGE) {
			ret = mfc_decoding_frame(ctx, exe_arg, &consumed);
		} else if ((ctx->resolution_status == RES_WAIT_FRAME_DONE) &&
			(exe_arg->out_display_status == DISP_S_FINISH)) {
			exe_arg->out_display_status = DISP_S_RES_CHANGE;
			ret = mfc_change_resolution(ctx, exe_arg);
			if (ret != MFC_OK)
				return ret;
			ctx->resolution_status = RES_NO_CHANGE;
		}

		if ((dec_ctx->ispackedpb) &&
				(dec_ctx->decframetype == DEC_FRM_P) &&
				(exe_arg->in_strm_size - consumed > 4)) {
			unsigned char *stream_vir;
			int offset = 0;

			mfc_dbg("[%s] strmsize : %d consumed : %d\n", __func__,
					exe_arg->in_strm_size, consumed);

			stream_vir = phys_to_virt(exe_arg->in_strm_buf);

			mfc_mem_cache_inv((void *)stream_vir,
					exe_arg->in_strm_size);

			offset = CheckMPEG4StartCode(stream_vir+consumed,
					dec_ctx->streamsize - consumed);
			if (offset > 4)
				consumed += offset;

			exe_arg->in_strm_size -= consumed;
			dec_ctx->frametag = exe_arg->in_frametag;
			dec_ctx->immediatelydisplay =
				exe_arg->in_immediately_disp;

			mfc_set_inst_state(ctx, INST_STATE_EXE);

			ret = mfc_decoding_frame(ctx, exe_arg, &consumed);
			exe_arg->out_frametag_top = sec_try_tag;

			mfc_set_inst_state(ctx, INST_STATE_EXE_DONE);
		}
	}

	/*
	if (ctx->c_ops->set_dpbs) {
		if (ctx->c_ops->set_dpbs(ctx) < 0)
			return MFC_DEC_INIT_FAIL;
	}
	*/

	return ret;
}

