/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_enc.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Encoder interface for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MFC_ENC_H
#define __MFC_ENC_H __FILE__

#include <linux/list.h>

#include "mfc.h"
#include "mfc_interface.h"
#include "mfc_inst.h"

enum enc_pc {
	EPC_ENABLE	= 0,
	EPC_DISABLE	= 3,
};

struct mfc_enc_ctx {
	unsigned int lumasize;		/* C */
	unsigned int chromasize;	/* C */

	unsigned long streamaddr;	/* K */
	unsigned int streamsize;	/* K */

	/* FIXME: temp. */
	unsigned char *kstrmaddr;

	/* init */
	enum enc_pc pixelcache;
	unsigned int numdpb;

	/* init | exec */
	unsigned int framemap;
	unsigned int outputmode;

	/* exec */
	unsigned int interlace;
	unsigned int forceframe;
	unsigned int frameskip;
	unsigned int framerate;
	unsigned int bitrate;
	unsigned int iperiodval;
	unsigned int vuiinfoval;
	unsigned int vuiextendsar;

	unsigned int frame_skip_enable;
	unsigned int vui_info_enable;
	unsigned int hier_p_enable;

	unsigned int slicecount;
	unsigned int slicesize;

	/* change flag */
	unsigned int setflag;
	unsigned int FrameTypeCngTag;
	unsigned int FrameRateCngTag;
	unsigned int BitRateCngTag;
	unsigned int FrameSkipCngTag;
	unsigned int VUIInfoCngTag;
	unsigned int IPeriodCngTag;
	unsigned int HierPCngTag;

	void *e_priv;
};

#define CHG_FRAME_PACKING	0x00000001
#define CHG_I_PERIOD		0x00000002
struct mfc_enc_h264 {
	unsigned int change;
	unsigned int vui_enable;
	unsigned int hier_p_enable;

	unsigned int i_period;

	unsigned int sei_gen;		/* H */
	struct mfc_frame_packing fp;	/* H */
};

int mfc_init_encoding(struct mfc_inst_ctx *ctx, union mfc_args *args);
/*
int mfc_init_encoding(struct mfc_inst_ctx *ctx, struct mfc_dec_init_arg *init_arg);
*/
int mfc_exec_encoding(struct mfc_inst_ctx *ctx, union mfc_args *args);
/*
int mfc_exec_encoding(struct mfc_inst_ctx *ctx, struct mfc_dec_exe_arg *exe_arg);
*/

/*---------------------------------------------------------------------------*/

struct mfc_enc_info {
	struct list_head list;
	const char *name;
	SSBSIP_MFC_CODEC_TYPE codectype;
	int codecid;
	unsigned int e_priv_size;

	const struct codec_operations c_ops;
};

void mfc_init_encoders(void);

#endif /* __MFC_ENC_H */
