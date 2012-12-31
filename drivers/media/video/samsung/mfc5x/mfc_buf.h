/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_buf.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Buffer manager for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MFC_BUF_H_
#define __MFC_BUF_H_ __FILE__

#include <linux/list.h>

#include "mfc.h"
#include "mfc_inst.h"
#include "mfc_interface.h"

/* FIXME */
#define ALIGN_4B	(1 <<  2)
#define ALIGN_2KB	(1 << 11)
#define ALIGN_4KB	(1 << 12)
#define ALIGN_8KB	(1 << 13)
#define ALIGN_64KB	(1 << 16)
#define ALIGN_128KB	(1 << 17)

#define ALIGN_W		128
#define ALIGN_H		32

/* System */					/* Size, Port, Align */
#define MFC_FW_SYSTEM_SIZE	(0x80000)	/* 512KB, A, N(4KB for VMEM) */

/* Instance */
#define MFC_CTX_SIZE_L		(0x96000)	/* 600KB, N, 2KB, H.264 Decoding only */
#define MFC_CTX_SIZE		(0x2800)	/* 10KB, N, 2KB */
#define MFC_SHM_SIZE		(0x400)		/* 1KB, N, 4B */

/* Decoding */
#define MFC_CPB_SIZE		(0x400000)	/* Max.4MB, A, 2KB */
#define MFC_DESC_SIZE		(0x20000)	/* Max.128KB, A, 2KB */

#define MFC_DEC_NBMV_SIZE	(0x4000)	/* 16KB, A, 2KB */
#define MFC_DEC_NBIP_SIZE	(0x8000)	/* 32KB, A, 2KB */
#define MFC_DEC_NBDCAC_SIZE	(0x4000)	/* 16KB, A, 2KB */
#define MFC_DEC_UPNBMV_SIZE	(0x11000)	/* 68KB, A, 2KB */
#define MFC_DEC_SAMV_SIZE	(0x40000)	/* 256KB, A, 2KB */
#define MFC_DEC_OTLINE_SIZE	(0x8000)	/* 32KB, A, 2KB */
#define MFC_DEC_SYNPAR_SIZE	(0x11000)	/* 68KB, A, 2KB */
#define MFC_DEC_BITPLANE_SIZE	(0x800)		/* 2KB, A, 2KB */

/* Encoding */
#define MFC_STRM_SIZE		(0x300000)	/* 3MB, A, 2KB (multi. 4KB) */

/* FIXME: variable size */
#define MFC_ENC_UPMV_SIZE	(0x10000)	/* Var, A, 2KB */
#define MFC_ENC_COLFLG_SIZE	(0x10000)	/* Var, A, 2KB */
#define MFC_ENC_INTRAMD_SIZE	(0x10000)	/* Var, A, 2KB */
#define MFC_ENC_INTRAPRED_SIZE	(0x4000)	/* 16KB, A, 2KB */
#define MFC_ENC_NBORINFO_SIZE	(0x10000)	/* Var, A, 2KB */
#define MFC_ENC_ACDCCOEF_SIZE	(0x10000)	/* Var, A, 2KB */

#define MFC_LUMA_ALIGN		ALIGN_8KB
#define MFC_CHROMA_ALIGN	ALIGN_8KB
#define MFC_MV_ALIGN		ALIGN_8KB	/* H.264 Decoding only */

#define PORT_A			0
#define PORT_B			1

/* FIXME: MFC Buffer Type add as allocation parameter */
/*
#define MBT_ACCESS_MASK	(0xFF << 24)
#define MBT_SYSMMU	(0x01 << 24)
*/
#define MBT_KERNEL	(0x02 << 24)
#define MBT_USER	(0x04 << 24)
#define MBT_OTHER	(0x08 << 24)
#if 0
#define MBT_TYPE_MASK	(0xFF << 16)
#define MBT_CTX		(MBT_SYSMMU | MBT_KERNEL | (0x01 << 16))/* S, K */
#define MBT_DESC	(MBT_SYSMMU | (0x02 << 16))		/* S */
#define MBT_CODEC	(MBT_SYSMMU | (0x04 << 16))		/* S */
#define MBT_SHM		(MBT_SYSMMU | MBT_KERNEL | (0x08 << 16))/* S, K */
#define MBT_CPB		(MBT_SYSMMU | MBT_USER   | (0x10 << 16))/* D: S, [K], U E: */
#define MBT_DPB		(MBT_SYSMMU | MBT_USER   | (0x20 << 16))/* D: S, [K], U E: */
#endif
#define MBT_CTX		(MBT_KERNEL | (0x01 << 16))	/* S, K */
#define MBT_DESC		      (0x02 << 16)	/* S */
#define MBT_CODEC		      (0x04 << 16)	/* S */
#define MBT_SHM		(MBT_KERNEL | (0x08 << 16))	/* S, K */
#if 0
#define MBT_CPB		(MBT_USER | (0x10 << 16))	/* D: S, [K], U E: */
#define MBT_DPB		(MBT_USER | (0x20 << 16))	/* D: S, [K], U E: */
#endif
#define MBT_CPB		(MBT_KERNEL | MBT_USER | (0x10 << 16))	/* D: S, [K], U E: */
#define MBT_DPB		(MBT_KERNEL | MBT_USER | (0x20 << 16))	/* D: S, [K], U E: */

enum MFC_BUF_ALLOC_SCHEME {
	MBS_BEST_FIT	= 0,
	MBS_FIRST_FIT	= 1,
};

/* Remove before Release */
#if 0
#define CPB_BUF_SIZE	(0x400000)       /* 3MB   : 3x1024x1024 for decoder    */
#define DESC_BUF_SIZE         (0x20000)        /* 128KB : 128x1024                   */
#define SHARED_BUF_SIZE       (0x10000)        /* 64KB  :  64x1024                   */
#define PRED_BUF_SIZE         (0x10000)        /* 64KB  :  64x1024                   */
#define DEC_CODEC_BUF_SIZE    (0x80000)        /* 512KB : 512x1024 size per instance */
#define ENC_CODEC_BUF_SIZE    (0x50000)        /* 320KB : 512x1024 size per instance */

#define STREAM_BUF_SIZE       (0x200000)       /* 2MB   : 2x1024x1024 for encoder    */
#define MV_BUF_SIZE           (0x10000)        /* 64KB  : 64x1024 for encoder        */

#define MFC_CONTEXT_SIZE_L  (640 * 1024)     /* 600KB -> 640KB for alignment       */
#define VC1DEC_CONTEXT_SIZE   (64 * 1024)      /* 10KB  ->  64KB for alignment       */
#define MPEG2DEC_CONTEXT_SIZE (64 * 1024)      /* 10KB  ->  64KB for alignment       */
#define H263DEC_CONTEXT_SIZE  (64 * 1024)      /* 10KB  ->  64KB for alignment       */
#define MPEG4DEC_CONTEXT_SIZE (64 * 1024)      /* 10KB  ->  64KB for alignment       */
#define H264ENC_CONTEXT_SIZE  (64 * 1024)      /* 10KB  ->  64KB for alignment       */
#define MPEG4ENC_CONTEXT_SIZE (64 * 1024)      /* 10KB  ->  64KB for alignment       */
#define H263ENC_CONTEXT_SIZE  (64 * 1024)      /* 10KB  ->  64KB for alignment       */

#define DESC_BUF_SIZE		(0x20000)	/* 128KB : 128x1024 */
#define SHARED_MEM_SIZE		(0x1000)	/* 4KB   : 4x1024 size */

#define CPB_BUF_SIZE		(0x400000)	/* 4MB : 4x1024x1024 for decoder */
#define STREAM_BUF_SIZE		(0x200000)	/* 2MB : 2x1024x1024 for encoder */
#define ENC_UP_INTRA_PRED_SIZE	(0x10000)	/* 64KB : 64x1024 for encoder */
#endif

struct mfc_alloc_buffer {
	struct list_head list;
	unsigned long real;	/* phys. or virt. addr for MFC	*/
	unsigned int size;	/* allocation size		*/
	unsigned char *addr;	/* kernel virtual address space */
	unsigned int type;	/* buffer type			*/
	int owner;		/* instance context id		*/
#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
	struct vcm_mmu_res *vcm_s;
	struct vcm_res *vcm_k;
	unsigned long vcm_addr;
	size_t vcm_size;
	void *ump_handle;
#elif defined(CONFIG_S5P_VMEM)
	unsigned int vmem_cookie;
	unsigned long vmem_addr;
	size_t vmem_size;
#else
	unsigned int ofs;	/*
				 * offset phys. or virt. contiguous memory
				 * phys.[bootmem, memblock] virt.[vmalloc]
				 * when user use mmap,
				 * user can access whole of memory by offset.
				 */
#endif
};

struct mfc_free_buffer {
	struct list_head list;
	unsigned long real;	/* phys. or virt. addr for MFC	*/
	unsigned int size;
};

void mfc_print_buf(void);

int mfc_init_buf(void);
void mfc_final_buf(void);
void mfc_set_buf_alloc_scheme(enum MFC_BUF_ALLOC_SCHEME scheme);
void mfc_merge_buf(void);
struct mfc_alloc_buffer *_mfc_alloc_buf(
	struct mfc_inst_ctx *ctx, unsigned int size, int align, int flag);
int mfc_alloc_buf(
	struct mfc_inst_ctx *ctx, struct mfc_buf_alloc_arg* args, int flag);
int _mfc_free_buf(unsigned long real);
int mfc_free_buf(struct mfc_inst_ctx *ctx, unsigned int key);
void mfc_free_buf_type(int owner, int type);
void mfc_free_buf_inst(int owner);
unsigned long mfc_get_buf_real(int owner, unsigned int key);
/*
unsigned char *mfc_get_buf_addr(int owner, unsigned char *user);
unsigned char *_mfc_get_buf_addr(int owner, unsigned char *user);
*/
#ifdef CONFIG_VIDEO_MFC_VCM_UMP
unsigned int mfc_vcm_bind_from_others(struct mfc_inst_ctx *ctx,
				struct mfc_buf_alloc_arg *args, int flag);
void *mfc_get_buf_ump_handle(unsigned long real);
#endif
#endif /* __MFC_BUF_H_ */
