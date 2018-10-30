/*
   cx231xx_vbi.h - driver for Conexant Cx23100/101/102 USB video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
		Based on cx88 driver

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _CX231XX_VBI_H
#define _CX231XX_VBI_H

extern const struct videobuf_queue_ops cx231xx_vbi_qops;

#define   NTSC_VBI_START_LINE 10	/* line 10 - 21 */
#define   NTSC_VBI_END_LINE   21
#define   NTSC_VBI_LINES	  (NTSC_VBI_END_LINE-NTSC_VBI_START_LINE+1)

#define   PAL_VBI_START_LINE  6
#define   PAL_VBI_END_LINE    23
#define   PAL_VBI_LINES       (PAL_VBI_END_LINE-PAL_VBI_START_LINE+1)

#define   VBI_STRIDE            1440
#define   VBI_SAMPLES_PER_LINE  1440

#define   CX231XX_NUM_VBI_PACKETS       4
#define   CX231XX_NUM_VBI_BUFS          5

/* stream functions */
int cx231xx_init_vbi_isoc(struct cx231xx *dev, int max_packets,
			  int num_bufs, int max_pkt_size,
			  int (*bulk_copy) (struct cx231xx *dev,
					    struct urb *urb));

void cx231xx_uninit_vbi_isoc(struct cx231xx *dev);

/* vbi data copy functions */
u32 cx231xx_get_vbi_line(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q,
			 u8 sav_eav, u8 *p_buffer, u32 buffer_size);

u32 cx231xx_copy_vbi_line(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q,
			  u8 *p_line, u32 length, int field_number);

void cx231xx_reset_vbi_buffer(struct cx231xx *dev,
			      struct cx231xx_dmaqueue *dma_q);

int cx231xx_do_vbi_copy(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q,
			u8 *p_buffer, u32 bytes_to_copy);

u8 cx231xx_is_vbi_buffer_done(struct cx231xx *dev,
			      struct cx231xx_dmaqueue *dma_q);

#endif
