/*
 * Amlogic Apollo
 * frame buffer driver
 *
 * Copyright (C) 2009 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:	Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef TVOUTC_H
#define TVOUTC_H

#include "tvmode.h"

#define  	DEFAULT_VDAC_SEQUENCE   	0x120120

typedef  enum{
	INTERALCE_COMPONENT=0,
	CVBS_SVIDEO,
	PROGRESSIVE,
	SIGNAL_SET_MAX
}video_signal_set_t;
typedef enum {
    VIDEO_SIGNAL_TYPE_INTERLACE_Y = 0, /**< Interlace Y signal */
    VIDEO_SIGNAL_TYPE_CVBS,            /**< CVBS signal */
    VIDEO_SIGNAL_TYPE_SVIDEO_LUMA,     /**< S-Video luma signal */
    VIDEO_SIGNAL_TYPE_SVIDEO_CHROMA,   /**< S-Video chroma signal */
    VIDEO_SIGNAL_TYPE_INTERLACE_PB,    /**< Interlace Pb signal */
    VIDEO_SIGNAL_TYPE_INTERLACE_PR,    /**< Interlace Pr signal */
    VIDEO_SIGNAL_TYPE_INTERLACE_R,     /**< Interlace R signal */
    VIDEO_SIGNAL_TYPE_INTERLACE_G,     /**< Interlace G signal */
    VIDEO_SIGNAL_TYPE_INTERLACE_B,     /**< Interlace B signal */
    VIDEO_SIGNAL_TYPE_PROGRESSIVE_Y,   /**< Progressive Y signal */
    VIDEO_SIGNAL_TYPE_PROGRESSIVE_PB,  /**< Progressive Pb signal */
    VIDEO_SIGNAL_TYPE_PROGRESSIVE_PR,  /**< Progressive Pr signal */
    VIDEO_SIGNAL_TYPE_PROGEESSIVE_R,   /**< Progressive R signal */
    VIDEO_SIGNAL_TYPE_PROGEESSIVE_G,   /**< Progressive G signal */
    VIDEO_SIGNAL_TYPE_PROGEESSIVE_B,   /**< Progressive B signal */
    VIDEO_SIGNAL_TYPE_MAX
} video_signal_type_t;

int tvoutc_setmode2(tvmode_t mode);
int 	 get_current_vdac_setting2(void) ;
void  change_vdac_setting2(unsigned int  vdec_setting,vmode_t mode);
#endif /* TVOUTC_H */
