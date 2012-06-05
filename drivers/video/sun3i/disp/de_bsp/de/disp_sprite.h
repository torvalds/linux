/*
 * drivers/video/sun3i/disp/de_bsp/de/disp_sprite.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Danling <danliang@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef _DISP_SPRITE_H_
#define _DISP_SPRITE_H_


#include "disp_display_i.h"

#define SPRITE_OPENED           0x00000001
#define SPRITE_OPENED_MASK      (~(SPRITE_OPENED))
#define SPRITE_USED	            0x00000002
#define SPRITE_USED_MASK        (~(SPRITE_USED))

#define SPRITE_BLOCK_OPENED     0x00000004
#define SPRITE_BLOCK_OPEN_MASK  (~(SPRITE_OPENED))
#define SPRITE_BLOCK_USED       0x00000008
#define SPRITE_BLOCK_USED_MASK  (~(SPRITE_BLOCK_USED))

typedef struct
{
	__s32   enable;
	__s32	id;//0-31
	__disp_rect_t src_win;
	__disp_rect_t scn_win;
	__u32	address;
	__disp_rectsz_t size;
}sprite_block_data_t;


typedef struct my_list_head
{
	struct my_list_head * next;
	struct my_list_head * prev;
	sprite_block_data_t * data;
}list_head_t;



typedef struct
{
    __u32               status;
    __u32               block_status[MAX_SPRITE_BLOCKS];
    __bool 		        enable;
	__disp_pixel_seq_t  pixel_seq;//0:argb,1:bgra
	__disp_pixel_fmt_t  format;//0:32bpp; 1:8bpp
	__bool 		        global_alpha_enable;
	__u8 		        global_alpha_value;
	__u8		        block_num;
	__s32 	            sprite_hid[MAX_SPRITE_BLOCKS];
	list_head_t *       header;
}sprite_t;


#endif
