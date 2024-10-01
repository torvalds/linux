/*
 * dvb_filter.h
 *
 * Copyright (C) 2003 Convergence GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DVB_FILTER_H_
#define _DVB_FILTER_H_

#include <linux/slab.h>

#include <media/demux.h>

typedef int (dvb_filter_pes2ts_cb_t) (void *, unsigned char *);

struct dvb_filter_pes2ts {
	unsigned char buf[188];
	unsigned char cc;
	dvb_filter_pes2ts_cb_t *cb;
	void *priv;
};

void dvb_filter_pes2ts_init(struct dvb_filter_pes2ts *p2ts, unsigned short pid,
			    dvb_filter_pes2ts_cb_t *cb, void *priv);

int dvb_filter_pes2ts(struct dvb_filter_pes2ts *p2ts, unsigned char *pes,
		      int len, int payload_start);


#define PROG_STREAM_MAP  0xBC
#define PRIVATE_STREAM1  0xBD
#define PADDING_STREAM   0xBE
#define PRIVATE_STREAM2  0xBF
#define AUDIO_STREAM_S   0xC0
#define AUDIO_STREAM_E   0xDF
#define VIDEO_STREAM_S   0xE0
#define VIDEO_STREAM_E   0xEF
#define ECM_STREAM       0xF0
#define EMM_STREAM       0xF1
#define DSM_CC_STREAM    0xF2
#define ISO13522_STREAM  0xF3
#define PROG_STREAM_DIR  0xFF

#define DVB_PICTURE_START    0x00
#define DVB_USER_START       0xb2
#define DVB_SEQUENCE_HEADER  0xb3
#define DVB_SEQUENCE_ERROR   0xb4
#define DVB_EXTENSION_START  0xb5
#define DVB_SEQUENCE_END     0xb7
#define DVB_GOP_START        0xb8
#define DVB_EXCEPT_SLICE     0xb0

#define SEQUENCE_EXTENSION           0x01
#define SEQUENCE_DISPLAY_EXTENSION   0x02
#define PICTURE_CODING_EXTENSION     0x08
#define QUANT_MATRIX_EXTENSION       0x03
#define PICTURE_DISPLAY_EXTENSION    0x07

#define I_FRAME 0x01
#define B_FRAME 0x02
#define P_FRAME 0x03

/* Initialize sequence_data */
#define INIT_HORIZONTAL_SIZE        720
#define INIT_VERTICAL_SIZE          576
#define INIT_ASPECT_RATIO          0x02
#define INIT_FRAME_RATE            0x03
#define INIT_DISP_HORIZONTAL_SIZE   540
#define INIT_DISP_VERTICAL_SIZE     576


//flags2
#define PTS_DTS_FLAGS    0xC0
#define ESCR_FLAG        0x20
#define ES_RATE_FLAG     0x10
#define DSM_TRICK_FLAG   0x08
#define ADD_CPY_FLAG     0x04
#define PES_CRC_FLAG     0x02
#define PES_EXT_FLAG     0x01

//pts_dts flags
#define PTS_ONLY         0x80
#define PTS_DTS          0xC0

#define TS_SIZE        188
#define TRANS_ERROR    0x80
#define PAY_START      0x40
#define TRANS_PRIO     0x20
#define PID_MASK_HI    0x1F
//flags
#define TRANS_SCRMBL1  0x80
#define TRANS_SCRMBL2  0x40
#define ADAPT_FIELD    0x20
#define PAYLOAD        0x10
#define COUNT_MASK     0x0F

// adaptation flags
#define DISCON_IND     0x80
#define RAND_ACC_IND   0x40
#define ES_PRI_IND     0x20
#define PCR_FLAG       0x10
#define OPCR_FLAG      0x08
#define SPLICE_FLAG    0x04
#define TRANS_PRIV     0x02
#define ADAP_EXT_FLAG  0x01

// adaptation extension flags
#define LTW_FLAG       0x80
#define PIECE_RATE     0x40
#define SEAM_SPLICE    0x20


#define MAX_PLENGTH 0xFFFF
#define MMAX_PLENGTH (256*MAX_PLENGTH)

#ifndef IPACKS
#define IPACKS 2048
#endif

struct ipack {
	int size;
	int found;
	u8 *buf;
	u8 cid;
	u32 plength;
	u8 plen[2];
	u8 flag1;
	u8 flag2;
	u8 hlength;
	u8 pts[5];
	u16 *pid;
	int mpeg;
	u8 check;
	int which;
	int done;
	void *data;
	void (*func)(u8 *buf,  int size, void *priv);
	int count;
	int repack_subids;
};

struct dvb_video_info {
	u32 horizontal_size;
	u32 vertical_size;
	u32 aspect_ratio;
	u32 framerate;
	u32 video_format;
	u32 bit_rate;
	u32 comp_bit_rate;
	u32 vbv_buffer_size;
	s16 vbv_delay;
	u32 CSPF;
	u32 off;
};

#define OFF_SIZE 4
#define FIRST_FIELD 0
#define SECOND_FIELD 1
#define VIDEO_FRAME_PICTURE 0x03

struct mpg_picture {
	int       channel;
	struct dvb_video_info vinfo;
	u32      *sequence_gop_header;
	u32      *picture_header;
	s32       time_code;
	int       low_delay;
	int       closed_gop;
	int       broken_link;
	int       sequence_header_flag;
	int       gop_flag;
	int       sequence_end_flag;

	u8        profile_and_level;
	s32       picture_coding_parameter;
	u32       matrix[32];
	s8        matrix_change_flag;

	u8        picture_header_parameter;
  /* bit 0 - 2: bwd f code
     bit 3    : fpb vector
     bit 4 - 6: fwd f code
     bit 7    : fpf vector */

	int       mpeg1_flag;
	int       progressive_sequence;
	int       sequence_display_extension_flag;
	u32       sequence_header_data;
	s16       last_frame_centre_horizontal_offset;
	s16       last_frame_centre_vertical_offset;

	u32       pts[2]; /* [0] 1st field, [1] 2nd field */
	int       top_field_first;
	int       repeat_first_field;
	int       progressive_frame;
	int       bank;
	int       forward_bank;
	int       backward_bank;
	int       compress;
	s16       frame_centre_horizontal_offset[OFF_SIZE];
		  /* [0-2] 1st field, [3] 2nd field */
	s16       frame_centre_vertical_offset[OFF_SIZE];
		  /* [0-2] 1st field, [3] 2nd field */
	s16       temporal_reference[2];
		  /* [0] 1st field, [1] 2nd field */

	s8        picture_coding_type[2];
		  /* [0] 1st field, [1] 2nd field */
	s8        picture_structure[2];
		  /* [0] 1st field, [1] 2nd field */
	s8        picture_display_extension_flag[2];
		  /* [0] 1st field, [1] 2nd field */
		  /* picture_display_extenion() 0:no 1:exit*/
	s8        pts_flag[2];
		  /* [0] 1st field, [1] 2nd field */
};

struct dvb_audio_info {
	int layer;
	u32 bit_rate;
	u32 frequency;
	u32 mode;
	u32 mode_extension ;
	u32 emphasis;
	u32 framesize;
	u32 off;
};

int dvb_filter_get_ac3info(u8 *mbuf, int count, struct dvb_audio_info *ai, int pr);


#endif
