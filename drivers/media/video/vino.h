/*
 * Copyright (C) 1999 Ulf Karlsson <ulfc@bun.falkenberg.se>
 * Copyright (C) 2003 Ladislav Michl <ladis@linux-mips.org>
 */

#ifndef VINO_H
#define VINO_H

#define VINO_BASE	0x00080000	/* Vino is in the EISA address space,
					 * but it is not an EISA bus card */

struct sgi_vino_channel {
	u32 _pad_alpha;
	volatile u32 alpha;

#define VINO_CLIP_X(x)		((x) & 0x3ff)		/* bits 0:9 */
#define VINO_CLIP_ODD(x)	(((x) & 0x1ff) << 10)	/* bits 10:18 */
#define VINO_CLIP_EVEN(x)	(((x) & 0x1ff) << 19)	/* bits 19:27 */
	u32 _pad_clip_start;
	volatile u32 clip_start;
	u32 _pad_clip_end;
	volatile u32 clip_end;

#define VINO_FRAMERT_PAL	(1<<0)			/* 0=NTSC 1=PAL */
#define VINO_FRAMERT_RT(x)	(((x) & 0x1fff) << 1)	/* bits 1:12 */
	u32 _pad_frame_rate;
	volatile u32 frame_rate;

	u32 _pad_field_counter;
	volatile u32 field_counter;
	u32 _pad_line_size;
	volatile u32 line_size;
	u32 _pad_line_count;
	volatile u32 line_count;
	u32 _pad_page_index;
	volatile u32 page_index;
	u32 _pad_next_4_desc;
	volatile u32 next_4_desc;
	u32 _pad_start_desc_tbl;
	volatile u32 start_desc_tbl;

#define VINO_DESC_JUMP		(1<<30)
#define VINO_DESC_STOP		(1<<31)
#define VINO_DESC_VALID		(1<<32)
	u32 _pad_desc_0;
	volatile u32 desc_0;
	u32 _pad_desc_1;
	volatile u32 desc_1;
	u32 _pad_desc_2;
	volatile u32 desc_2;
	u32 _pad_Bdesc_3;
	volatile u32 desc_3;

	u32 _pad_fifo_thres;
	volatile u32 fifo_thres;
	u32 _pad_fifo_read;
	volatile u32 fifo_read;
	u32 _pad_fifo_write;
	volatile u32 fifo_write;
};

struct sgi_vino {
#define VINO_CHIP_ID		0xb
#define VINO_REV_NUM(x)		((x) & 0x0f)
#define VINO_ID_VALUE(x)	(((x) & 0xf0) >> 4)
	u32 _pad_rev_id;
	volatile u32 rev_id;

#define VINO_CTRL_LITTLE_ENDIAN		(1<<0)
#define VINO_CTRL_A_FIELD_TRANS_INT	(1<<1)	/* Field transferred int */
#define VINO_CTRL_A_FIFO_OF_INT		(1<<2)	/* FIFO overflow int */
#define VINO_CTRL_A_END_DESC_TBL_INT	(1<<3)	/* End of desc table int */
#define VINO_CTRL_A_INT			(VINO_CTRL_A_FIELD_TRANS_INT | \
					 VINO_CTRL_A_FIFO_OF_INT | \
					 VINO_CTRL_A_END_DESC_TBL_INT)
#define VINO_CTRL_B_FIELD_TRANS_INT	(1<<4)	/* Field transferred int */
#define VINO_CTRL_B_FIFO_OF_INT		(1<<5)	/* FIFO overflow int */
#define VINO_CTRL_B_END_DESC_TBL_INT	(1<<6)	/* End of desc table int */
#define VINO_CTRL_B_INT			(VINO_CTRL_B_FIELD_TRANS_INT | \
					 VINO_CTRL_B_FIFO_OF_INT | \
					 VINO_CTRL_B_END_DESC_TBL_INT)
#define VINO_CTRL_A_DMA_ENBL		(1<<7)
#define VINO_CTRL_A_INTERLEAVE_ENBL	(1<<8)
#define VINO_CTRL_A_SYNC_ENBL		(1<<9)
#define VINO_CTRL_A_SELECT		(1<<10)	/* 1=D1 0=Philips */
#define VINO_CTRL_A_RGB			(1<<11)	/* 1=RGB 0=YUV */
#define VINO_CTRL_A_LUMA_ONLY		(1<<12)
#define VINO_CTRL_A_DEC_ENBL		(1<<13)	/* Decimation */
#define VINO_CTRL_A_DEC_SCALE_MASK	0x1c000	/* bits 14:17 */
#define VINO_CTRL_A_DEC_SCALE_SHIFT	(14)
#define VINO_CTRL_A_DEC_HOR_ONLY	(1<<17)	/* Horizontal only */
#define VINO_CTRL_A_DITHER		(1<<18)	/* 24 -> 8 bit dither */
#define VINO_CTRL_B_DMA_ENBL		(1<<19)
#define VINO_CTRL_B_INTERLEAVE_ENBL	(1<<20)
#define VINO_CTRL_B_SYNC_ENBL		(1<<21)
#define VINO_CTRL_B_SELECT		(1<<22)	/* 1=D1 0=Philips */
#define VINO_CTRL_B_RGB			(1<<23)	/* 1=RGB 0=YUV */
#define VINO_CTRL_B_LUMA_ONLY		(1<<24)
#define VINO_CTRL_B_DEC_ENBL		(1<<25)	/* Decimation */
#define VINO_CTRL_B_DEC_SCALE_MASK	0x1c000000	/* bits 26:28 */
#define VINO_CTRL_B_DEC_SCALE_SHIFT	(26)
#define VINO_CTRL_B_DEC_HOR_ONLY	(1<<29)	/* Decimation horizontal only */
#define VINO_CTRL_B_DITHER		(1<<30)	/* ChanB 24 -> 8 bit dither */
	u32 _pad_control;
	volatile u32 control;

#define VINO_INTSTAT_A_FIELD_TRANS	(1<<0)	/* Field transferred int */
#define VINO_INTSTAT_A_FIFO_OF		(1<<1)	/* FIFO overflow int */
#define VINO_INTSTAT_A_END_DESC_TBL	(1<<2)	/* End of desc table int */
#define VINO_INTSTAT_A			(VINO_INTSTAT_A_FIELD_TRANS | \
					 VINO_INTSTAT_A_FIFO_OF | \
					 VINO_INTSTAT_A_END_DESC_TBL)
#define VINO_INTSTAT_B_FIELD_TRANS	(1<<3)	/* Field transferred int */
#define VINO_INTSTAT_B_FIFO_OF		(1<<4)	/* FIFO overflow int */
#define VINO_INTSTAT_B_END_DESC_TBL	(1<<5)	/* End of desc table int */
#define VINO_INTSTAT_B			(VINO_INTSTAT_B_FIELD_TRANS | \
					 VINO_INTSTAT_B_FIFO_OF | \
					 VINO_INTSTAT_B_END_DESC_TBL)
	u32 _pad_intr_status;
	volatile u32 intr_status;

	u32 _pad_i2c_control;
	volatile u32 i2c_control;
	u32 _pad_i2c_data;
	volatile u32 i2c_data;

	struct sgi_vino_channel a;
	struct sgi_vino_channel b;
};

#endif
