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
 * Author:  Chen Zhang <chen.zhang@amlogic.com>
 *
 */

#ifndef RMPARSER_H
#define RMPARSER_H

extern void rm_set_vasid(u32 vid, u32 aid);

extern ssize_t rmparser_write(struct file *file,
                              struct stream_buf_s *vbuf,
                              struct stream_buf_s *abuf,
                              const char __user *buf, size_t count);

s32 rmparser_init(void);

extern void rmparser_release(void);

extern void rm_audio_reset(void);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
// TODO: move to register headers
#define ES_PACK_SIZE_BIT                8
#define ES_PACK_SIZE_WID                24

#define ES_CTRL_WID                     8
#define ES_CTRL_BIT                     0
#define ES_TYPE_MASK                    (3 << 6)
#define ES_TYPE_VIDEO                   (0 << 6)
#define ES_TYPE_AUDIO                   (1 << 6)
#define ES_TYPE_SUBTITLE                (2 << 6)

#define ES_WRITE                        (1<<5)
#define ES_PASSTHROUGH                  (1<<4)
#define ES_INSERT_BEFORE_ES_WRITE       (1<<3)
#define ES_DISCARD                      (1<<2)
#define ES_SEARCH                       (1<<1)
#define ES_PARSER_START                 (1<<0)
#define ES_PARSER_BUSY                  (1<<0)

#define PARSER_INTSTAT_FETCH_CMD    (1<<7)
#define PARSER_INTSTAT_PARSE        (1<<4)
#define PARSER_INTSTAT_DISCARD      (1<<3)
#define PARSER_INTSTAT_INSZERO      (1<<2)
#define PARSER_INTSTAT_ACT_NOSSC    (1<<1)
#define PARSER_INTSTAT_SC_FOUND     (1<<0)

#define FETCH_CIR_BUF               (1<<31)
#define FETCH_CHK_BUF_STOP          (1<<30)
#define FETCH_PASSTHROUGH           (1<<29)
#define FETCH_ENDIAN                27
#define FETCH_PASSTHROUGH_TYPE_MASK (0x3<<27)
#define FETCH_ENDIAN_MASK           (0x7<<27)
#define FETCH_BUF_SIZE_MASK         (0x7ffffff)
#define FETCH_CMD_PTR_MASK          3
#define FETCH_CMD_RD_PTR_BIT        5
#define FETCH_CMD_WR_PTR_BIT        3
#define FETCH_CMD_NUM_MASK          3
#define FETCH_CMD_NUM_BIT           0

#define ES_COUNT_MASK                0xfff
#define ES_COUNT_BIT                 20
#define ES_REQ_PENDING               (1<<19)
#define ES_PASSTHROUGH_EN            (1<<18)
#define ES_PASSTHROUGH_TYPE_MASK     (3<<16)
#define ES_PASSTHROUGH_TYPE_VIDEO    (0<<16)
#define ES_PASSTHROUGH_TYPE_AUDIO    (1<<16)
#define ES_PASSTHROUGH_TYPE_SUBTITLE (2<<16)
#define ES_WR_ENDIAN_MASK            (0x7)
#define ES_SUB_WR_ENDIAN_BIT         9
#define ES_SUB_MAN_RD_PTR            (1<<8)
#define ES_AUD_WR_ENDIAN_BIT         5
#define ES_AUD_MAN_RD_PTR            (1<<4)
#define ES_VID_WR_ENDIAN_BIT         1
#define ES_VID_MAN_RD_PTR            (1<<0)

#define PS_CFG_FETCH_DMA_URGENT         (1<<31)
#define PS_CFG_STREAM_DMA_URGENT        (1<<30)
#define PS_CFG_FORCE_PFIFO_REN          (1<<29)
#define PS_CFG_PFIFO_PEAK_EN            (1<<28)
#define PS_CFG_SRC_SEL_BIT              24
#define PS_CFG_SRC_SEL_MASK             (3<<PS_CFG_SRC_SEL_BIT)
#define PS_CFG_SRC_SEL_FETCH            (0<<PS_CFG_SRC_SEL_BIT)
#define PS_CFG_SRC_SEL_AUX1             (1<<PS_CFG_SRC_SEL_BIT) // from NDMA
#define PS_CFG_SRC_SEL_AUX2             (2<<PS_CFG_SRC_SEL_BIT)
#define PS_CFG_SRC_SEL_AUX3             (3<<PS_CFG_SRC_SEL_BIT)
#define PS_CFG_PFIFO_EMPTY_CNT_BIT      16
#define PS_CFG_PFIFO_EMPTY_CNT_MASK     0xff
#define PS_CFG_MAX_ES_WR_CYCLE_BIT      12
#define PS_CFG_MAX_ES_WR_CYCLE_MASK     0xf
#define PS_CFG_STARTCODE_WID_MASK       (0x3<<10)
#define PS_CFG_STARTCODE_WID_8          (0x0<<10)
#define PS_CFG_STARTCODE_WID_16         (0x1<<10)
#define PS_CFG_STARTCODE_WID_24         (0x2<<10)
#define PS_CFG_STARTCODE_WID_32         (0x3<<10)
#define PS_CFG_PFIFO_ACCESS_WID_MASK    (0x3<<8)
#define PS_CFG_PFIFO_ACCESS_WID_8       (0x0<<8)
#define PS_CFG_PFIFO_ACCESS_WID_16      (0x1<<8)
#define PS_CFG_PFIFO_ACCESS_WID_24      (0x2<<8)
#define PS_CFG_PFIFO_ACCESS_WID_32      (0x3<<8)
#define PS_CFG_MAX_FETCH_CYCLE_BIT      0
#define PS_CFG_MAX_FETCH_CYCLE_MASK     0xff

#define PARSER_INT_DISABLE_CNT_MASK 0xffff
#define PARSER_INT_DISABLE_CNT_BIT  16
#define PARSER_INT_HOST_EN_MASK     0xff
#define PARSER_INT_HOST_EN_BIT      8
#define PARSER_INT_AMRISC_EN_MASK   0xff
#define PARSER_INT_AMRISC_EN_BIT    0
#define PARSER_INT_ALL              0xff

#define RESET_PARSER        (1<<8)
#define TS_HIU_ENABLE              5
#define USE_HI_BSF_INTERFACE       7
#endif

#endif /* RMPARSER_H */


