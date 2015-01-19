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

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/amstream.h>

#include <asm/uaccess.h>
#include <mach/am_regs.h>

#include "vdec_reg.h"
#include "streambuf_reg.h"
#include "streambuf.h"
#include "psparser.h"

#define TIMESTAMP_IONLY 1
#define SAVE_SCR 0

#define MPEG_START_CODE_PATTERN (0x00000100L)
#define MPEG_START_CODE_MASK    (0xffffff00L)
#define MAX_MPG_AUDIOPK_SIZE     0x1000

#define SUB_INSERT_START_CODE_HIGH   0x414d4c55
#define SUB_INSERT_START_CODE_LOW    0xaa000000

#define PARSER_WRITE        (ES_WRITE | ES_PARSER_START)
#define PARSER_VIDEO        (ES_TYPE_VIDEO)
#define PARSER_AUDIO        (ES_TYPE_AUDIO)
#define PARSER_SUBPIC       (ES_TYPE_SUBTITLE)
#define PARSER_PASSTHROUGH  (ES_PASSTHROUGH | ES_PARSER_START)
#define PARSER_AUTOSEARCH   (ES_SEARCH | ES_PARSER_START)
#define PARSER_DISCARD      (ES_DISCARD | ES_PARSER_START)
#define PARSER_BUSY         (ES_PARSER_BUSY)

#define PARSER_PARAMETER_LENGTH_BIT     16

#define PARSER_POP      READ_MPEG_REG(PFIFO_DATA)
#define SET_BLOCK(size) \
WRITE_MPEG_REG_BITS(PARSER_CONTROL, size, ES_PACK_SIZE_BIT, ES_PACK_SIZE_WID)
#define SET_DISCARD_SIZE(size) WRITE_MPEG_REG(PARSER_PARAMETER, size)

enum {
    SEARCH_START_CODE = 0,
    SEND_VIDEO_SEARCH,
    SEND_AUDIO_SEARCH,
    SEND_SUBPIC_SEARCH,
    DISCARD_SEARCH,
    DISCARD_ONLY
};

enum {
    AUDIO_FIRST_ACCESS_ARM = 0,
    AUDIO_FIRST_ACCESS_POPING,
    AUDIO_FIRST_ACCESS_DONE
};

const static char psparser_id[] = "psparser-id";

static DECLARE_WAIT_QUEUE_HEAD(wq);

static struct tasklet_struct psparser_tasklet;
static u32 fetch_done;
static u8 audio_id, video_id, sub_id, sub_id_max;
static u32 audio_first_access;
static u32 packet_remaining;
static u32 video_data_parsed;
static u32 audio_data_parsed;
static u32 pts_equ_dts_flag;

static unsigned first_apts, first_vpts;
static unsigned audio_got_first_pts, video_got_first_dts, sub_got_first_pts;
atomic_t sub_block_found = ATOMIC_INIT(0);

#define DEBUG_VOB_SUB
#ifdef DEBUG_VOB_SUB
static u8 sub_found_num;
static struct subtitle_info *sub_info[MAX_SUB_NUM];
#endif

static bool ptsmgr_first_vpts_ready(void)
{
    return (video_got_first_dts != 0);
}

static bool ptsmgr_first_apts_ready(void)
{
    return (audio_got_first_pts != 0);
}

static void ptsmgr_vpts_checkin(u32 pts)
{
    if (video_got_first_dts == 0) {
        video_got_first_dts = 1;
        first_vpts = pts;
    }

    //    vpts_checkin(pts);
    pts_checkin_offset(PTS_TYPE_VIDEO, video_data_parsed, pts);

}

static void ptsmgr_apts_checkin(u32 pts)
{
    if (audio_got_first_pts == 0) {
        audio_got_first_pts = 1;
        first_apts = pts;
    }

    //    apts_checkin(pts);
    pts_checkin_offset(PTS_TYPE_AUDIO, audio_data_parsed, pts);
}

static u32 parser_process(s32 type, s32 packet_len)
{
    s16 temp, header_len, misc_flags, i;
    u32 pts = 0, dts = 0;
    u32 pts_dts_flag = 0;
	u16 invalid_pts = 0;
	
    temp = PARSER_POP;
    packet_len--;

    if ((temp >> 6) == 0x02) {
        /* mpeg-2 system */
        misc_flags = PARSER_POP;
        header_len = PARSER_POP;
        packet_len -= 2;
        packet_len -= header_len;

        if ((misc_flags >> 6) > 1) {
            /* PTS exist */
            pts  = ((PARSER_POP >> 1) & 7) << 30;   /* bit 32-30 */
            pts |= PARSER_POP << 22;                /* bit 29-22 */
            pts |= (PARSER_POP >> 1) << 15;         /* bit 21-15 */
            pts |= (PARSER_POP << 7);               /* bit 14-07 */
            pts |= (PARSER_POP >> 1);               /* bit 06-00 */
            header_len -= 5;
            pts_dts_flag |= 2;
        }

        if ((misc_flags >> 6) > 2) {
            /* DTS exist */
            dts  = ((PARSER_POP >> 1) & 7) << 30;   /* bit 32-30 */
            dts |= PARSER_POP << 22;                /* bit 29-22 */
            dts |= (PARSER_POP >> 1) << 15;         /* bit 21-15 */
            dts |= (PARSER_POP << 7);               /* bit 14-07 */
            dts |= (PARSER_POP >> 1);               /* bit 06-00 */
            header_len -= 5;
            pts_dts_flag |= 1;
        }

        if (misc_flags & 0x20) {
            /* ESCR_flag */
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
            header_len -= 5;
        }

        if (misc_flags & 0x10) {
            /* ES_rate_flag */
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
            header_len -= 3;
        }

        if (misc_flags & 0x08) {
            /* DSM_trick_mode_flag */
            PARSER_POP;
            header_len -= 1;
        }

        if (misc_flags & 0x04) {
            /* additional_copy_info_flag */
            PARSER_POP;
            header_len -= 1;
        }

        if (misc_flags & 0x02) {
            /* PES_CRC_flag */
            PARSER_POP;
            PARSER_POP;
            header_len -= 2;
        }

        if (misc_flags & 0x01) {
            /* PES_extension_flag */
            misc_flags = PARSER_POP;
            header_len--;

            if ((misc_flags & 0x80) && (header_len >= 128)){
                /* PES_private_data_flag */
                for (i = 0; i < 128; i++) {
                    PARSER_POP;
                }

                header_len -= 128;
            }
#if 0
            if (misc_flags & 0x40) {
                /* pack_header_field_flag */
                /* Invalid case */
            }
#endif
            if (misc_flags & 0x20) {
                /* program_packet_sequence_counter_flag */
                PARSER_POP;
                PARSER_POP;
                header_len -= 2;
            }

            if (misc_flags & 0x10) {
                /* PSTD_buffer_flag */
                PARSER_POP;
                PARSER_POP;
                header_len -= 2;
            }

            if (misc_flags & 1) {
                /* PES_extension_flag_2 */
                temp = PARSER_POP & 0x7f;

                while (temp) {
                    PARSER_POP;
                    temp--;
                    header_len--;
                }
            }

            while (header_len) {
                PARSER_POP;
                header_len--;
            }
        }

        while (header_len) {
            PARSER_POP;
            header_len--;
        }

    } else {
        /* mpeg-1 system */
        while (temp == 0xff) {
            temp = PARSER_POP;
            packet_len--;
        }

        if ((temp >> 6) == 1) {
            PARSER_POP;                             /* STD buffer size */
            temp = PARSER_POP;
            packet_len -= 2;
        }

        if (((temp >> 4) == 2) || ((temp >> 4) == 3)) {
            pts  = ((temp >> 1) & 7) << 30;         /* bit 32-30 */
            pts |= PARSER_POP << 22;                /* bit 29-22 */
            pts |= (PARSER_POP >> 1) << 15;         /* bit 21-15 */
            pts |= (PARSER_POP << 7);               /* bit 14-07 */
            pts |= (PARSER_POP >> 1);               /* bit 06-00 */
            packet_len -= 4;
            pts_dts_flag |= 2;
        }

        if ((temp >> 4) == 3) {
            dts  = ((PARSER_POP >> 1) & 7) << 30;   /* bit 32-30 */
            dts |= PARSER_POP << 22;                /* bit 29-22 */
            dts |= (PARSER_POP >> 1) << 15;         /* bit 21-15 */
            dts |= (PARSER_POP << 7);               /* bit 14-07 */
            dts |= (PARSER_POP >> 1);               /* bit 06-00 */
            packet_len -= 5;
            pts_dts_flag |= 1;
        }
    }

	if ((pts==0) && (dts==0xffffffff)){
		invalid_pts = 1;
		printk("invalid pts \n");
	}

	

    if (!packet_len) {
        return SEARCH_START_CODE;

    } else if (type == 0) {
        if ((pts_dts_flag) && (!invalid_pts)) {
#if TIMESTAMP_IONLY
            if (!ptsmgr_first_vpts_ready()) {
                if (pts_dts_flag & 2) {
                    ptsmgr_vpts_checkin(pts);
                } else {
                    ptsmgr_vpts_checkin(dts);
                }
            } else if ((pts_dts_flag & 3) == 3) {
                if (pts_equ_dts_flag) {
                    if (dts == pts) {
                        ptsmgr_vpts_checkin(pts);
                    }
                }
                else {
                    if (dts == pts) {
                        pts_equ_dts_flag = 1;
                    }
                    ptsmgr_vpts_checkin(pts);
                }
            }
#else
            if (!ptsmgr_first_vpts_ready()) {
                if (pts_dts_flag & 2) {
                    ptsmgr_vpts_checkin(pts);
                } else {
                    ptsmgr_vpts_checkin(dts);
                }
            } else if (pts_dts_flag & 2) {
                ptsmgr_vpts_checkin(pts);
            }
#endif
        }

        if (ptsmgr_first_vpts_ready() || invalid_pts) {
            SET_BLOCK(packet_len);
            video_data_parsed += packet_len;
            return SEND_VIDEO_SEARCH;

        } else {
            SET_DISCARD_SIZE(packet_len);
            return DISCARD_SEARCH;
        }

    } else if (type == 1) {
        /* mpeg audio */
        if (pts_dts_flag & 2) {
            ptsmgr_apts_checkin(pts);
        }

        if (ptsmgr_first_apts_ready()) {
            SET_BLOCK(packet_len);
            audio_data_parsed += packet_len;
            return SEND_AUDIO_SEARCH;

        } else {
            SET_DISCARD_SIZE(packet_len);
            return DISCARD_SEARCH;
        }

    } else if (type == 2) {
        /* Private stream */
        temp = PARSER_POP;  /* sub_stream_id */
        packet_len--;

        if (((temp & 0xf8) == 0xa0) && (temp == audio_id)) {
            /* DVD_VIDEO Audio LPCM data */
            PARSER_POP;
            temp = (PARSER_POP << 8) | PARSER_POP;
            if(temp == 0) {
                temp = 4;
            }
            temp--;
            packet_len -= 3;

            if (audio_first_access == AUDIO_FIRST_ACCESS_ARM) {
                if (temp) {
                    packet_remaining = packet_len - temp;
                    SET_DISCARD_SIZE(temp);
                    audio_first_access = AUDIO_FIRST_ACCESS_POPING;
                    return DISCARD_ONLY;
                }

                audio_first_access = AUDIO_FIRST_ACCESS_DONE;

                if (packet_len) {
                    SET_BLOCK(packet_len);
                    audio_data_parsed += packet_len;
                    return SEND_AUDIO_SEARCH;

                } else {
                    return SEARCH_START_CODE;
                }

            } else {
                PARSER_POP;
                PARSER_POP;
                PARSER_POP;
                packet_len -= 3;
            }

            if (pts_dts_flag & 2) {
                ptsmgr_apts_checkin(pts);
            }

            if (ptsmgr_first_apts_ready()) {
                SET_BLOCK(packet_len);
                audio_data_parsed += packet_len;
                return SEND_AUDIO_SEARCH;

            } else {
                SET_DISCARD_SIZE(packet_len);
                return DISCARD_SEARCH;
            }

        } else if (((temp & 0xf8) == 0x80) && (temp == audio_id)) {
            /* Audio AC3 data */
            PARSER_POP;
            temp = (PARSER_POP << 8) | PARSER_POP;
            packet_len -= 3;

            if (audio_first_access == AUDIO_FIRST_ACCESS_ARM) {
                if (pts_dts_flag & 2) {
                    ptsmgr_apts_checkin(pts);
                }

                if ((temp > 2) && (packet_len > (temp - 2))) {
                    temp -= 2;
                    packet_remaining = packet_len - temp;
                    SET_DISCARD_SIZE(temp);
                    audio_first_access = AUDIO_FIRST_ACCESS_POPING;
                    return DISCARD_ONLY;
                }

                audio_first_access = AUDIO_FIRST_ACCESS_DONE;

                if (packet_len) {
                    SET_BLOCK(packet_len);
                    audio_data_parsed += packet_len;
                    return SEND_AUDIO_SEARCH;

                } else {
                    return SEARCH_START_CODE;
                }
            }

            if (pts_dts_flag & 2) {
                ptsmgr_apts_checkin(pts);
            }

            if (ptsmgr_first_apts_ready()) {
                SET_BLOCK(packet_len);
                audio_data_parsed += packet_len;
                return SEND_AUDIO_SEARCH;

            } else {
                SET_DISCARD_SIZE(packet_len);
                return DISCARD_SEARCH;
            }

        } else if (((temp & 0xf8) == 0x88) && (temp == audio_id)) {
            /* Audio DTS data */
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
            packet_len -= 3;

            if (audio_first_access == AUDIO_FIRST_ACCESS_ARM) {
                audio_first_access = AUDIO_FIRST_ACCESS_DONE;
            }

            if (pts_dts_flag & 2) {
                ptsmgr_apts_checkin(pts);
            }

            if (ptsmgr_first_apts_ready()) {
                SET_BLOCK(packet_len);
                audio_data_parsed += packet_len;
                return SEND_AUDIO_SEARCH;

            } else {
                SET_DISCARD_SIZE(packet_len);
                return DISCARD_SEARCH;
            }
        } else if ((temp & 0xe0) == 0x20) {
            if (temp > sub_id_max) {
                sub_id_max = temp;
            }

			#ifdef DEBUG_VOB_SUB				
			for (i = 0; i < sub_found_num; i ++) {
				if(!sub_info[i])
					break;
				if(temp == sub_info[i]->id)
					break;
			}				
			if (i == sub_found_num && i < MAX_SUB_NUM) {				
				if (sub_info[sub_found_num]) {
					sub_info[sub_found_num]->id = temp;
					sub_found_num ++;
					printk("[psparser_process]found new sub_id=0x%x (num %d)\n", temp, sub_found_num);
				} else {
					printk("[psparser_process]sub info NULL!\n");
				}	
			}
			#endif
			
            if (temp == sub_id) {
                /* DVD sub-picture data */
                if (!packet_len) {
                    return SEARCH_START_CODE;

                } else {
#if 0
                    if (pts_dts_flag & 2) {
                        ptsmgr_spts_checkin(pts);
                    }

                    if (ptsmgr_first_spts_ready()) {
                        SET_BLOCK(packet_len);
                        return SEND_SUBPIC_SEARCH;

                    } else {
                        SET_DISCARD_SIZE(packet_len);
                        return DISCARD_SEARCH;
                    }
#else
                    if (pts_dts_flag & 2) {
                        sub_got_first_pts = 1;
                    }

                    if (sub_got_first_pts) {
                        printk("sub pts 0x%x, len %d\n", pts, packet_len);
                        SET_BLOCK(packet_len);
                        WRITE_MPEG_REG(PARSER_PARAMETER, 16 << PARSER_PARAMETER_LENGTH_BIT);
                        WRITE_MPEG_REG(PARSER_INSERT_DATA, SUB_INSERT_START_CODE_HIGH);
                        WRITE_MPEG_REG(PARSER_INSERT_DATA, SUB_INSERT_START_CODE_LOW | get_sub_type());
                        WRITE_MPEG_REG(PARSER_INSERT_DATA, packet_len);
                        WRITE_MPEG_REG(PARSER_INSERT_DATA, pts);
                        atomic_set(&sub_block_found, 1);
                        return SEND_SUBPIC_SEARCH;
                    } else {
                        SET_DISCARD_SIZE(packet_len);
                        return DISCARD_SEARCH;
                    }
#endif
                }
            } else {
                SET_DISCARD_SIZE(packet_len);
                return DISCARD_SEARCH;
            }
        } else {
            SET_DISCARD_SIZE(packet_len);
            return DISCARD_SEARCH;
        }

        if (!packet_len) {
            return SEARCH_START_CODE;

        } else {
            SET_BLOCK(packet_len);
            audio_data_parsed += packet_len;
            return SEND_AUDIO_SEARCH;
        }
    }

    return SEARCH_START_CODE;
}

static void on_start_code_found(int start_code)
{
    unsigned short packet_len;
    unsigned short temp;
    unsigned next_action;
#if SAVE_SCR
    unsigned scr;
#endif

    if (atomic_read(&sub_block_found)) {
        wakeup_sub_poll();
        atomic_set(&sub_block_found, 0);
    }

    if (audio_first_access == AUDIO_FIRST_ACCESS_POPING) {
        /* we are in the procedure of poping data for audio first access, continue with last packet */
        audio_first_access = AUDIO_FIRST_ACCESS_DONE;

        if (packet_remaining) {
            next_action = SEND_AUDIO_SEARCH;
            SET_BLOCK(packet_remaining);

        } else {
            next_action = SEARCH_START_CODE;
        }

    } else if (start_code == 0xba) {    /* PACK_START_CODE */
        temp = PARSER_POP;

        if ((temp >> 6) == 0x01) {
#if SAVE_SCR
            scr  = ((temp >> 3) & 0x3) << 30;   /* bit 31-30 */
            scr |= (temp & 0x3) << 28;          /* bit 29-28 */
            scr |= (PARSER_POP) << 20;          /* bit 27-20 */
            temp = PARSER_POP;
            scr |= (temp >> 4) << 16;           /* bit 19-16 */
            scr |= (temp & 7) << 13;            /* bit 15-13 */
            scr |= (PARSER_POP) << 5;           /* bit 12-05 */
            scr |= (PARSER_POP) >> 3;           /* bit 04-00 */
#else
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
#endif
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
            temp = PARSER_POP & 7;

            while (temp) {                      /* stuff byte */
                PARSER_POP;
                temp--;
            }

        } else {
            /* mpeg-1 Pack Header */
#if SAVE_SCR
            scr  = ((temp >> 1) & 0x3) << 30;   /* bit 31-30 */
            scr |= (PARSER_POP) << 22;          /* bit 29-22 */
            scr |= (PARSER_POP >> 1) << 15;     /* bit 21-15 */
            scr |= (PARSER_POP) << 7;           /* bit 14-07 */
            scr |= (PARSER_POP >> 1);           /* bit 06-00 */
#else
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
            PARSER_POP;
#endif
        }

        next_action = SEARCH_START_CODE;

    } else {
        packet_len = (PARSER_POP << 8) | PARSER_POP;

        if (start_code == video_id) {
            next_action = parser_process(0, packet_len);

        } else if (start_code == audio_id) {
            /* add mpeg audio packet length check */
            if (packet_len > MAX_MPG_AUDIOPK_SIZE) {
                next_action = SEARCH_START_CODE;

            } else {
                next_action = parser_process(1, packet_len);
            }

        } else if (start_code == 0xbb) {
            SET_DISCARD_SIZE(packet_len);
            next_action = DISCARD_SEARCH;
        } else if (start_code == 0xbd) {
            next_action = parser_process(2, packet_len);

        } else if (start_code == 0xbf) {
            SET_DISCARD_SIZE(packet_len);
            next_action = DISCARD_SEARCH;
        } else if ((start_code < 0xc0) || (start_code > 0xc8)) {
            next_action = SEARCH_START_CODE;

        } else if (packet_len) {
            SET_DISCARD_SIZE(packet_len);
            next_action = DISCARD_SEARCH;

        } else {
            next_action = SEARCH_START_CODE;
        }
    }

    switch (next_action) {
    case SEARCH_START_CODE:
        WRITE_MPEG_REG(PARSER_CONTROL, PARSER_AUTOSEARCH);
        break;

    case SEND_VIDEO_SEARCH:
        WRITE_MPEG_REG_BITS(PARSER_CONTROL, PARSER_AUTOSEARCH | PARSER_VIDEO | PARSER_WRITE, ES_CTRL_BIT, ES_CTRL_WID);
        break;

    case SEND_AUDIO_SEARCH:
        WRITE_MPEG_REG_BITS(PARSER_CONTROL, PARSER_AUTOSEARCH | PARSER_AUDIO | PARSER_WRITE, ES_CTRL_BIT, ES_CTRL_WID);
        break;

    case SEND_SUBPIC_SEARCH:
        WRITE_MPEG_REG_BITS(PARSER_CONTROL, PARSER_AUTOSEARCH | PARSER_SUBPIC | PARSER_WRITE | ES_INSERT_BEFORE_ES_WRITE, ES_CTRL_BIT, ES_CTRL_WID);
        break;

    case DISCARD_SEARCH:
        WRITE_MPEG_REG_BITS(PARSER_CONTROL, PARSER_AUTOSEARCH | PARSER_DISCARD, ES_CTRL_BIT, ES_CTRL_WID);
        break;

    case DISCARD_ONLY:
        WRITE_MPEG_REG_BITS(PARSER_CONTROL, PARSER_DISCARD, ES_CTRL_BIT, ES_CTRL_WID);
        break;
    }
}

static void parser_tasklet(ulong data)
{
    s32 sc;
    u32 int_status = READ_MPEG_REG(PARSER_INT_STATUS);

    WRITE_MPEG_REG(PARSER_INT_STATUS, int_status);

    if (int_status & PARSER_INTSTAT_FETCH_CMD) {
        fetch_done = 1;

        wake_up_interruptible(&wq);
    }

    if (int_status & PARSER_INTSTAT_SC_FOUND) {
        sc = PARSER_POP;

        on_start_code_found(sc);

    } else if (int_status & PARSER_INTSTAT_DISCARD) {
        on_start_code_found(0);
    }
}

static irqreturn_t parser_isr(int irq, void *dev_id)
{
    tasklet_schedule(&psparser_tasklet);

    return IRQ_HANDLED;
}

static ssize_t _psparser_write(const char __user *buf, size_t count)
{
    size_t r = count;
    const char __user *p = buf;
    u32 len;
    int ret;

    if (r > 0) {
        len = min(r, (size_t)FETCHBUF_SIZE);

        if (copy_from_user(fetchbuf_remap, p, len)) {
            return -EFAULT;
        }

        fetch_done = 0;

        wmb();

        WRITE_MPEG_REG(PARSER_FETCH_ADDR, virt_to_phys((u8 *)fetchbuf));
        
        WRITE_MPEG_REG(PARSER_FETCH_CMD,
                       (7 << FETCH_ENDIAN) | len);

        ret = wait_event_interruptible_timeout(wq, fetch_done != 0, HZ/10);
        if (ret == 0) {
            WRITE_MPEG_REG(PARSER_FETCH_CMD, 0);
			printk("write timeout, retry\n");
            return -EAGAIN;
        } else if (ret < 0) {
            return -ERESTARTSYS;
        }

        p += len;
        r -= len;
    }

    return count - r;
}

#ifdef CONFIG_AM_DVB
extern int tsdemux_set_reset_flag(void);
#endif

s32 psparser_init(u32 vid, u32 aid, u32 sid)
{
    s32 r;
    u32 parser_sub_start_ptr;
    u32 parser_sub_end_ptr;
    u32 parser_sub_rp;

	#ifdef DEBUG_VOB_SUB
	u8 i;
	for(i = 0; i < MAX_SUB_NUM; i ++) {
		sub_info[i] = kzalloc(sizeof(struct subtitle_info), GFP_KERNEL);
		if (!sub_info[i]) {
			printk("[psparser_init]alloc for subtitle info failed\n");
		} else {
			sub_info[i]->id = -1;
		}
	}
	sub_found_num = 0;
	#endif
    parser_sub_start_ptr = READ_MPEG_REG(PARSER_SUB_START_PTR);
    parser_sub_end_ptr = READ_MPEG_REG(PARSER_SUB_END_PTR);
    parser_sub_rp = READ_MPEG_REG(PARSER_SUB_RP);

    video_id = vid;
    audio_id = aid;
    sub_id = sid;
    audio_got_first_pts = 0;
    video_got_first_dts = 0;
    sub_got_first_pts = 0;
    first_apts = 0;
    first_vpts = 0;
    pts_equ_dts_flag = 0;

    printk("video 0x%x, audio 0x%x, sub 0x%x\n", video_id, audio_id, sub_id);
    if (fetchbuf == 0) {
        printk("%s: no fetchbuf\n", __FUNCTION__);
        return -ENOMEM;
    }

    WRITE_MPEG_REG(RESET1_REGISTER, RESET_PARSER);

    /* TS data path */
#ifndef CONFIG_AM_DVB
    WRITE_MPEG_REG(FEC_INPUT_CONTROL, 0);
#else
    tsdemux_set_reset_flag();
#endif
    CLEAR_MPEG_REG_MASK(TS_HIU_CTL, 1 << USE_HI_BSF_INTERFACE);
    CLEAR_MPEG_REG_MASK(TS_HIU_CTL_2, 1 << USE_HI_BSF_INTERFACE);
    CLEAR_MPEG_REG_MASK(TS_HIU_CTL_3, 1 << USE_HI_BSF_INTERFACE);
    CLEAR_MPEG_REG_MASK(TS_FILE_CONFIG, (1 << TS_HIU_ENABLE));

    /* hook stream buffer with PARSER */
    WRITE_MPEG_REG(PARSER_VIDEO_START_PTR,
                   READ_VREG(VLD_MEM_VIFIFO_START_PTR));
    WRITE_MPEG_REG(PARSER_VIDEO_END_PTR,
                   READ_VREG(VLD_MEM_VIFIFO_END_PTR));
    CLEAR_MPEG_REG_MASK(PARSER_ES_CONTROL, ES_VID_MAN_RD_PTR);

    WRITE_MPEG_REG(PARSER_AUDIO_START_PTR,
                   READ_MPEG_REG(AIU_MEM_AIFIFO_START_PTR));
    WRITE_MPEG_REG(PARSER_AUDIO_END_PTR,
                   READ_MPEG_REG(AIU_MEM_AIFIFO_END_PTR));
    CLEAR_MPEG_REG_MASK(PARSER_ES_CONTROL, ES_AUD_MAN_RD_PTR);

    WRITE_MPEG_REG(PARSER_CONFIG,
                   (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
                   (1  << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
                   (16 << PS_CFG_MAX_FETCH_CYCLE_BIT));

    WRITE_VREG(VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);
    CLEAR_VREG_MASK(VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);

    WRITE_MPEG_REG(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);
    CLEAR_MPEG_REG_MASK(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);

    WRITE_MPEG_REG(PARSER_SUB_START_PTR, parser_sub_start_ptr);
    WRITE_MPEG_REG(PARSER_SUB_END_PTR, parser_sub_end_ptr);
    WRITE_MPEG_REG(PARSER_SUB_RP, parser_sub_start_ptr);
    WRITE_MPEG_REG(PARSER_SUB_WP, parser_sub_start_ptr);
    SET_MPEG_REG_MASK(PARSER_ES_CONTROL, (7 << ES_SUB_WR_ENDIAN_BIT) | ES_SUB_MAN_RD_PTR);

    WRITE_MPEG_REG(PFIFO_RD_PTR, 0);
    WRITE_MPEG_REG(PFIFO_WR_PTR, 0);

    WRITE_MPEG_REG(PARSER_SEARCH_PATTERN, MPEG_START_CODE_PATTERN);
    WRITE_MPEG_REG(PARSER_SEARCH_MASK, MPEG_START_CODE_MASK);

    WRITE_MPEG_REG(PARSER_CONFIG,
                   (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
                   (1  << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
                   PS_CFG_STARTCODE_WID_24   |
                   PS_CFG_PFIFO_ACCESS_WID_8 | /* single byte pop */
                   (16 << PS_CFG_MAX_FETCH_CYCLE_BIT));
    WRITE_MPEG_REG(PARSER_CONTROL, PARSER_AUTOSEARCH);

    tasklet_init(&psparser_tasklet, parser_tasklet, 0);

    if ((r = pts_start(PTS_TYPE_VIDEO)) < 0) {
        goto Err_1;
    }

    if ((r = pts_start(PTS_TYPE_AUDIO)) < 0) {
        goto Err_2;
    }

    video_data_parsed = 0;
    audio_data_parsed = 0;

    r = request_irq(INT_PARSER, parser_isr,
                    IRQF_SHARED, "psparser", (void *)psparser_id);

    if (r) {
        printk("PS Demux irq register failed.\n");

        r = -ENOENT;
        goto Err_3;
    }

    WRITE_MPEG_REG(PARSER_INT_STATUS, 0xffff);
    WRITE_MPEG_REG(PARSER_INT_ENABLE,
                   PARSER_INT_ALL << PARSER_INT_HOST_EN_BIT);

    return 0;

Err_3:
    pts_stop(PTS_TYPE_AUDIO);

Err_2:
    pts_stop(PTS_TYPE_VIDEO);

Err_1:
    return r;
}

void psparser_release(void)
{
	u8 i;
    printk("psparser_release\n");

    WRITE_MPEG_REG(PARSER_INT_ENABLE, 0);

    free_irq(INT_PARSER, (void *)psparser_id);

    pts_stop(PTS_TYPE_VIDEO);
    pts_stop(PTS_TYPE_AUDIO);
#ifdef DEBUG_VOB_SUB
	for(i = 0; i < MAX_SUB_NUM; i ++) {
		if (sub_info[i]) {
			kfree(sub_info[i]);
		}
	}
	printk("psparser release subtitle info\n");
#endif
}

ssize_t psparser_write(struct file *file,
                       struct stream_buf_s *vbuf,
                       struct stream_buf_s *abuf,
                       const char __user *buf, size_t count)
{
    s32 r;
    stream_port_t *port = (stream_port_t *)file->private_data;

    if ((stbuf_space(vbuf) < count) ||
        (stbuf_space(abuf) < count)) {
        if (file->f_flags & O_NONBLOCK) {
            return -EAGAIN;
        }

        if ((port->flag & PORT_FLAG_VID)
            && (stbuf_space(vbuf) < count)) {
            r = stbuf_wait_space(vbuf, count);
            if (r < 0) {
                return r;
            }
        }
        if ((port->flag & PORT_FLAG_AID)
            && (stbuf_space(abuf) < count)) {
            r = stbuf_wait_space(abuf, count);
            if (r < 0) {
                return r;
            }
        }
    }

    return _psparser_write(buf, count);
}

void psparser_change_avid(unsigned int vid, unsigned int aid)
{
    video_id = vid;
    audio_id = aid;

    return;
}

void psparser_change_sid(unsigned int sid)
{
    sub_id = sid;

    return;
}

void psparser_audio_reset(void)
{
    ulong flags;
	DEFINE_SPINLOCK(lock);

    spin_lock_irqsave(&lock, flags);

    WRITE_MPEG_REG(PARSER_AUDIO_WP,
                   READ_MPEG_REG(AIU_MEM_AIFIFO_START_PTR));
    WRITE_MPEG_REG(PARSER_AUDIO_RP,
                   READ_MPEG_REG(AIU_MEM_AIFIFO_START_PTR));

    WRITE_MPEG_REG(PARSER_AUDIO_START_PTR,
                   READ_MPEG_REG(AIU_MEM_AIFIFO_START_PTR));
    WRITE_MPEG_REG(PARSER_AUDIO_END_PTR,
                   READ_MPEG_REG(AIU_MEM_AIFIFO_END_PTR));
    CLEAR_MPEG_REG_MASK(PARSER_ES_CONTROL, ES_AUD_MAN_RD_PTR);

    WRITE_MPEG_REG(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);
    CLEAR_MPEG_REG_MASK(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);

    audio_data_parsed = 0;

    spin_unlock_irqrestore(&lock, flags);

    return;
}

void psparser_sub_reset(void)
{
    ulong flags;
	DEFINE_SPINLOCK(lock);
    u32 parser_sub_start_ptr;
    u32 parser_sub_end_ptr;

    spin_lock_irqsave(&lock, flags);

    parser_sub_start_ptr = READ_MPEG_REG(PARSER_SUB_START_PTR);
    parser_sub_end_ptr = READ_MPEG_REG(PARSER_SUB_END_PTR);

    WRITE_MPEG_REG(PARSER_SUB_START_PTR, parser_sub_start_ptr);
    WRITE_MPEG_REG(PARSER_SUB_END_PTR, parser_sub_end_ptr);
    WRITE_MPEG_REG(PARSER_SUB_RP, parser_sub_start_ptr);
    WRITE_MPEG_REG(PARSER_SUB_WP, parser_sub_start_ptr);
    SET_MPEG_REG_MASK(PARSER_ES_CONTROL, (7 << ES_SUB_WR_ENDIAN_BIT) | ES_SUB_MAN_RD_PTR);

    spin_unlock_irqrestore(&lock, flags);

    return;
}

u8 psparser_get_sub_found_num(void)
{
#ifdef DEBUG_VOB_SUB
	return sub_found_num;
#else
	return 0;
#endif
}

u8 psparser_get_sub_info(struct subtitle_info **sub_infos)
{
#ifdef DEBUG_VOB_SUB
	u8 i = 0;
	int ret = 0;
	u8 size = sizeof(struct subtitle_info);	
	for (i = 0; i < sub_found_num; i ++) {
		if (!sub_info[i]){
			printk("[psparser_get_sub_info:%d]  sub_info[%d] NULL\n", __LINE__, i);
			ret = -1;
			break;
		}
		if (!sub_infos[i]){
			printk("[psparser_get_sub_info:%d]  sub_infos[%d] NULL\n", __LINE__, i);
			ret = -2;
			break;
		}		
		memcpy(sub_infos[i], sub_info[i], size);		
	}	
	return ret;
#else
	return 0;
#endif
}
