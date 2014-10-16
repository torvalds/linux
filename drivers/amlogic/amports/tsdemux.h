/*
 * AMLOGIC Audio/Video streaming port driver.
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
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef TSDEMUX_H
#define TSDEMUX_H
#include "amports_config.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
// TODO: move to register headers
#define NEW_PDTS_READY             4
#define AUDIO_PTS_READY            2
#define VIDEO_PTS_READY            0
#define DIS_CONTINUITY_PACKET      6
#define SUB_PES_READY              7

#define PARSER_INTSTAT_FETCH_CMD    (1<<7)

#define FETCH_ENDIAN                27
#define FETCH_ENDIAN_MASK           (0x7<<27)

#define RESET_DEMUXSTB      (1<<1)
#define RESET_PARSER        (1<<8)

#define VIDEO_PACKET               0
#define AUDIO_PACKET               1
#define SUB_PACKET                 2

#define OTHER_ENDIAN               6
#define BYPASS_ENDIAN              3
#define SECTION_ENDIAN             0

#define USE_HI_BSF_INTERFACE       7
#define DES_OUT_DLY                8
#define TRANSPORT_SCRAMBLING_CONTROL_ODD 6
#define TS_HIU_ENABLE               5
#define FEC_FILE_CLK_DIV            0
#define STB_DEMUX_ENABLE            4
#define KEEP_DUPLICATE_PACKAGE      6

#define ES_VID_MAN_RD_PTR            (1<<0)
#define ES_AUD_MAN_RD_PTR            (1<<4)

#define PS_CFG_PFIFO_EMPTY_CNT_BIT      16
#define PS_CFG_MAX_ES_WR_CYCLE_BIT      12
#define PS_CFG_MAX_FETCH_CYCLE_BIT      0

#define ES_SUB_WR_ENDIAN_BIT         9
#define ES_SUB_MAN_RD_PTR           (1<<8)
#define PARSER_INTSTAT_FETCH_CMD    (1<<7)

#define PARSER_INT_HOST_EN_BIT      8
#endif

struct stream_buf_s;

extern s32 tsdemux_init(u32 vid, u32 aid, u32 sid, u32 pcrid, bool is_hevc);

extern void tsdemux_release(void);

extern ssize_t tsdemux_write(struct file *file,
                             struct stream_buf_s *vbuf,
                             struct stream_buf_s *abuf,
                             const char __user *buf, size_t count);

extern u32 tsdemux_pcrscr_get(void);
extern u8 tsdemux_pcrscr_valid(void);
extern u32 tsdemux_first_pcrscr_get(void);

int     tsdemux_class_register(void);
void  tsdemux_class_unregister(void);
void tsdemux_change_avid(unsigned int vid, unsigned int aid);
void tsdemux_change_sid(unsigned int sid);
void tsdemux_audio_reset(void);
void tsdemux_sub_reset(void);
void tsdemux_set_skipbyte(int skipbyte);
void tsdemux_set_demux(int dev);
#endif /* TSDEMUX_H */

