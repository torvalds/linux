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
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/device.h>
#include <linux/delay.h>

#include <asm/uaccess.h>
#include <mach/am_regs.h>

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#include "vdec_reg.h"
#include "streambuf_reg.h"
#include "streambuf.h"
#include "amports_config.h"

#include "tsdemux.h"

const static char tsdemux_fetch_id[] = "tsdemux-fetch-id";
const static char tsdemux_irq_id[] = "tsdemux-irq-id";

static DECLARE_WAIT_QUEUE_HEAD(wq);
static u32 fetch_done;
static u32 discontinued_counter;
static u32 first_pcr = 0;
static u8 pcrscr_valid=0;

static int demux_skipbyte;

#ifdef ENABLE_DEMUX_DRIVER
static struct tsdemux_ops *demux_ops = NULL;
static irq_handler_t       demux_handler;
static void               *demux_data;
static DEFINE_SPINLOCK(demux_ops_lock);

void tsdemux_set_ops(struct tsdemux_ops *ops)
{
    unsigned long flags;

    spin_lock_irqsave(&demux_ops_lock, flags);
    demux_ops = ops;
    spin_unlock_irqrestore(&demux_ops_lock, flags);
}

EXPORT_SYMBOL(tsdemux_set_ops);

int tsdemux_set_reset_flag_ext(void)
{
    int r;

    if (demux_ops && demux_ops->set_reset_flag) {
        r = demux_ops->set_reset_flag();
    } else {
        WRITE_MPEG_REG(FEC_INPUT_CONTROL, 0);
        r = 0;
    }

    return r;
}

int tsdemux_set_reset_flag(void)
{
    unsigned long flags;
    int r;

    spin_lock_irqsave(&demux_ops_lock, flags);
    r = tsdemux_set_reset_flag_ext();
    spin_unlock_irqrestore(&demux_ops_lock, flags);

    return r;
}

static int tsdemux_reset(void)
{
    unsigned long flags;
    int r;

    spin_lock_irqsave(&demux_ops_lock, flags);
    if (demux_ops && demux_ops->reset) {
        tsdemux_set_reset_flag_ext();
        r = demux_ops->reset();
    } else {
        WRITE_MPEG_REG(RESET1_REGISTER, RESET_DEMUXSTB);
        WRITE_MPEG_REG(STB_TOP_CONFIG, 0);
        WRITE_MPEG_REG(DEMUX_CONTROL, 0);
        r = 0;
    }
    spin_unlock_irqrestore(&demux_ops_lock, flags);

    return r;
}

static irqreturn_t tsdemux_default_isr_handler(int irq, void *dev_id)
{
    u32 int_status = READ_MPEG_REG(STB_INT_STATUS);

    if (demux_handler) {
        demux_handler(irq, (void*)0);
    }

    WRITE_MPEG_REG(STB_INT_STATUS, int_status);
    return IRQ_HANDLED;
}

static int tsdemux_request_irq(irq_handler_t handler, void *data)
{
    unsigned long flags;
    int r;

    spin_lock_irqsave(&demux_ops_lock, flags);
    if (demux_ops && demux_ops->request_irq) {
        r = demux_ops->request_irq(handler, data);
    } else {
        demux_handler = handler;
        demux_data    = data;
        r = request_irq(INT_DEMUX, tsdemux_default_isr_handler,
                        IRQF_SHARED, "tsdemux-irq",
                        (void *)tsdemux_irq_id);
        WRITE_MPEG_REG(STB_INT_MASK,
                       (1 << SUB_PES_READY)
                       | (1 << NEW_PDTS_READY)
                       | (1 << DIS_CONTINUITY_PACKET));
    }
    spin_unlock_irqrestore(&demux_ops_lock, flags);

    return r;
}

static int tsdemux_free_irq(void)
{
    unsigned long flags;
    int r;

    spin_lock_irqsave(&demux_ops_lock, flags);
    if (demux_ops && demux_ops->free_irq) {
        r = demux_ops->free_irq();
    } else {
        WRITE_MPEG_REG(STB_INT_MASK, 0);
        free_irq(INT_DEMUX, (void *)tsdemux_irq_id);
        r = 0;
    }
    spin_unlock_irqrestore(&demux_ops_lock, flags);

    return r;
}

static int tsdemux_set_vid(int vpid)
{
    unsigned long flags;
    int r = 0;

    spin_lock_irqsave(&demux_ops_lock, flags);
    if (demux_ops && demux_ops->set_vid) {
        r = demux_ops->set_vid(vpid);
    } else if ((vpid >= 0) && (vpid < 0x1FFF)) {
        u32 data = READ_MPEG_REG(FM_WR_DATA);
        WRITE_MPEG_REG(FM_WR_DATA,
                       (((vpid & 0x1fff) | (VIDEO_PACKET << 13)) << 16) |
                       (data & 0xffff));
        WRITE_MPEG_REG(FM_WR_ADDR, 0x8000);
        while (READ_MPEG_REG(FM_WR_ADDR) & 0x8000) {
            ;
        }
        WRITE_MPEG_REG(MAX_FM_COMP_ADDR, 1);
        r = 0;
    }
    spin_unlock_irqrestore(&demux_ops_lock, flags);

    return r;
}

static int tsdemux_set_aid(int apid)
{
    unsigned long flags;
    int r = 0;

    spin_lock_irqsave(&demux_ops_lock, flags);
    if (demux_ops && demux_ops->set_aid) {
        r = demux_ops->set_aid(apid);
    } else if ((apid >= 0) && (apid < 0x1FFF)) {
        u32 data = READ_MPEG_REG(FM_WR_DATA);
        WRITE_MPEG_REG(FM_WR_DATA,
                       ((apid & 0x1fff) | (AUDIO_PACKET << 13)) |
                       (data & 0xffff0000));
        WRITE_MPEG_REG(FM_WR_ADDR, 0x8000);
        while (READ_MPEG_REG(FM_WR_ADDR) & 0x8000) {
            ;
        }
        WRITE_MPEG_REG(MAX_FM_COMP_ADDR, 1);
        r = 0;
    }
    spin_unlock_irqrestore(&demux_ops_lock, flags);

    return r;
}

static int tsdemux_set_sid(int spid)
{
    unsigned long flags;
    int r = 0;

    spin_lock_irqsave(&demux_ops_lock, flags);
    if (demux_ops && demux_ops->set_sid) {
        r = demux_ops->set_sid(spid);
    } else if ((spid >= 0) && (spid < 0x1FFF)) {
        WRITE_MPEG_REG(FM_WR_DATA,
                       (((spid & 0x1fff) | (SUB_PACKET << 13)) << 16) | 0xffff);
        WRITE_MPEG_REG(FM_WR_ADDR, 0x8001);
        while (READ_MPEG_REG(FM_WR_ADDR) & 0x8000) {
            ;
        }
        WRITE_MPEG_REG(MAX_FM_COMP_ADDR, 1);
        r = 0;
    }
    spin_unlock_irqrestore(&demux_ops_lock, flags);

    return r;
}

static int tsdemux_set_pcrid(int pcrpid)
{
    unsigned long flags;
    int r = 0;

    spin_lock_irqsave(&demux_ops_lock, flags);
    if (demux_ops && demux_ops->set_pcrid) {
        r = demux_ops->set_pcrid(pcrpid);
    }
    spin_unlock_irqrestore(&demux_ops_lock, flags);

    return r;
}

static int tsdemux_set_skip_byte(int skipbyte)
{
    unsigned long flags;
    int r = 0;

    spin_lock_irqsave(&demux_ops_lock, flags);
    if (demux_ops && demux_ops->set_skipbyte) {
        r = demux_ops->set_skipbyte(skipbyte);
    }else{
        demux_skipbyte = skipbyte;
    }
    spin_unlock_irqrestore(&demux_ops_lock, flags);

    return r;
}

static int tsdemux_config(void)
{
    unsigned long flags;

    spin_lock_irqsave(&demux_ops_lock, flags);

    if (!demux_ops) {
        WRITE_MPEG_REG(STB_INT_MASK, 0);
        WRITE_MPEG_REG(STB_INT_STATUS, 0xffff);

        /* TS data path */
        WRITE_MPEG_REG(FEC_INPUT_CONTROL, 0x7000);
        WRITE_MPEG_REG(DEMUX_MEM_REQ_EN,
                       (1 << VIDEO_PACKET) |
                       (1 << AUDIO_PACKET) |
                       (1 << SUB_PACKET));
        WRITE_MPEG_REG(DEMUX_ENDIAN,
                       (7 << OTHER_ENDIAN)  |
                       (7 << BYPASS_ENDIAN) |
                       (0 << SECTION_ENDIAN));
        WRITE_MPEG_REG(TS_HIU_CTL, 1 << USE_HI_BSF_INTERFACE);
        WRITE_MPEG_REG(TS_FILE_CONFIG,
                       (demux_skipbyte << 16)                  |
                       (6 << DES_OUT_DLY)                      |
                       (3 << TRANSPORT_SCRAMBLING_CONTROL_ODD) |
                       (1 << TS_HIU_ENABLE)                    |
                       (4 << FEC_FILE_CLK_DIV));

        /* enable TS demux */
        WRITE_MPEG_REG(DEMUX_CONTROL, (1 << STB_DEMUX_ENABLE));
    }

    spin_unlock_irqrestore(&demux_ops_lock, flags);
    return 0;
}
#endif /*ENABLE_DEMUX_DRIVER*/

static irqreturn_t tsdemux_isr(int irq, void *dev_id)
{
#ifndef ENABLE_DEMUX_DRIVER
    u32 int_status = READ_MPEG_REG(STB_INT_STATUS);
#else
    int id = (int)dev_id;
    u32 int_status = id ? READ_MPEG_REG(STB_INT_STATUS_2) : READ_MPEG_REG(STB_INT_STATUS);
#endif

    if (int_status & (1 << NEW_PDTS_READY)) {
#ifndef ENABLE_DEMUX_DRIVER
        u32 pdts_status = READ_MPEG_REG(STB_PTS_DTS_STATUS);

        if (pdts_status & (1 << VIDEO_PTS_READY))
            pts_checkin_wrptr(PTS_TYPE_VIDEO,
                              READ_MPEG_REG(VIDEO_PDTS_WR_PTR),
                              READ_MPEG_REG(VIDEO_PTS_DEMUX));

        if (pdts_status & (1 << AUDIO_PTS_READY))
            pts_checkin_wrptr(PTS_TYPE_AUDIO,
                              READ_MPEG_REG(AUDIO_PDTS_WR_PTR),
                              READ_MPEG_REG(AUDIO_PTS_DEMUX));

        WRITE_MPEG_REG(STB_PTS_DTS_STATUS, pdts_status);
#else
#define DMX_READ_REG(i,r)\
	((i)?((i==1)?READ_MPEG_REG(r##_2):READ_MPEG_REG(r##_3)):READ_MPEG_REG(r))
	
        u32 pdts_status = DMX_READ_REG(id, STB_PTS_DTS_STATUS);

        if (pdts_status & (1 << VIDEO_PTS_READY))
            pts_checkin_wrptr(PTS_TYPE_VIDEO,
                              DMX_READ_REG(id, VIDEO_PDTS_WR_PTR),
                              DMX_READ_REG(id, VIDEO_PTS_DEMUX));

        if (pdts_status & (1 << AUDIO_PTS_READY))
            pts_checkin_wrptr(PTS_TYPE_AUDIO,
                              DMX_READ_REG(id, AUDIO_PDTS_WR_PTR),
                              DMX_READ_REG(id, AUDIO_PTS_DEMUX));

        if (id == 1) {
            WRITE_MPEG_REG(STB_PTS_DTS_STATUS_2, pdts_status);
        } else if (id == 2){
            WRITE_MPEG_REG(STB_PTS_DTS_STATUS_3, pdts_status);
        } else {
            WRITE_MPEG_REG(STB_PTS_DTS_STATUS, pdts_status);
        }
#endif
    }
    if (int_status & (1 << DIS_CONTINUITY_PACKET)) {
        discontinued_counter++;
        //printk("discontinued counter=%d\n",discontinued_counter);
    }
    if (int_status & (1 << SUB_PES_READY)) {
        /* TODO: put data to somewhere */
        //printk("subtitle pes ready\n");
        wakeup_sub_poll();
    }
#ifndef ENABLE_DEMUX_DRIVER
    WRITE_MPEG_REG(STB_INT_STATUS, int_status);
#endif
    return IRQ_HANDLED;
}

static irqreturn_t parser_isr(int irq, void *dev_id)
{
    u32 int_status = READ_MPEG_REG(PARSER_INT_STATUS);

    WRITE_MPEG_REG(PARSER_INT_STATUS, int_status);

    if (int_status & PARSER_INTSTAT_FETCH_CMD) {
        fetch_done = 1;

        wake_up_interruptible(&wq);
    }


    return IRQ_HANDLED;
}

static ssize_t _tsdemux_write(const char __user *buf, size_t count)
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
        
        WRITE_MPEG_REG(PARSER_FETCH_CMD, (7 << FETCH_ENDIAN) | len);

        ret = wait_event_interruptible_timeout(wq, fetch_done != 0, HZ/2);
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

s32 tsdemux_init(u32 vid, u32 aid, u32 sid, u32 pcrid, bool is_hevc)
{
    s32 r;
    u32 parser_sub_start_ptr;
    u32 parser_sub_end_ptr;
    u32 parser_sub_rp;
    u32 pcr_num;

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_type(MOD_DEMUX, 1);
#endif

    parser_sub_start_ptr = READ_MPEG_REG(PARSER_SUB_START_PTR);
    parser_sub_end_ptr = READ_MPEG_REG(PARSER_SUB_END_PTR);
    parser_sub_rp = READ_MPEG_REG(PARSER_SUB_RP);

    WRITE_MPEG_REG(RESET1_REGISTER, RESET_PARSER);

#ifdef ENABLE_DEMUX_DRIVER
    tsdemux_reset();
#else
    WRITE_MPEG_REG(RESET1_REGISTER, RESET_PARSER | RESET_DEMUXSTB);

    WRITE_MPEG_REG(STB_TOP_CONFIG, 0);
    WRITE_MPEG_REG(DEMUX_CONTROL, 0);
#endif

    /* set PID filter */
    printk("tsdemux video_pid = 0x%x, audio_pid = 0x%x, sub_pid = 0x%x, pcrid = 0x%x\n",
           vid, aid, sid, pcrid);

#ifndef ENABLE_DEMUX_DRIVER
    WRITE_MPEG_REG(FM_WR_DATA,
                   (((vid & 0x1fff) | (VIDEO_PACKET << 13)) << 16) |
                   ((aid & 0x1fff) | (AUDIO_PACKET << 13)));
    WRITE_MPEG_REG(FM_WR_ADDR, 0x8000);
    while (READ_MPEG_REG(FM_WR_ADDR) & 0x8000) {
        ;
    }

    WRITE_MPEG_REG(FM_WR_DATA,
                   (((sid & 0x1fff) | (SUB_PACKET << 13)) << 16) | 0xffff);
    WRITE_MPEG_REG(FM_WR_ADDR, 0x8001);
    while (READ_MPEG_REG(FM_WR_ADDR) & 0x8000) {
        ;
    }

    WRITE_MPEG_REG(MAX_FM_COMP_ADDR, 1);

    WRITE_MPEG_REG(STB_INT_MASK, 0);
    WRITE_MPEG_REG(STB_INT_STATUS, 0xffff);

    /* TS data path */
    WRITE_MPEG_REG(FEC_INPUT_CONTROL, 0x7000);
    WRITE_MPEG_REG(DEMUX_MEM_REQ_EN,
                   (1 << VIDEO_PACKET) |
                   (1 << AUDIO_PACKET) |
                   (1 << SUB_PACKET));
    WRITE_MPEG_REG(DEMUX_ENDIAN,
                   (7 << OTHER_ENDIAN)  |
                   (7 << BYPASS_ENDIAN) |
                   (0 << SECTION_ENDIAN));
    WRITE_MPEG_REG(TS_HIU_CTL, 1 << USE_HI_BSF_INTERFACE);
    WRITE_MPEG_REG(TS_FILE_CONFIG,
                   (demux_skipbyte << 16)                  |
                   (6 << DES_OUT_DLY)                      |
                   (3 << TRANSPORT_SCRAMBLING_CONTROL_ODD) |
                   (1 << TS_HIU_ENABLE)                    |
                   (4 << FEC_FILE_CLK_DIV));

    /* enable TS demux */
    WRITE_MPEG_REG(DEMUX_CONTROL, (1 << STB_DEMUX_ENABLE) | (1 << KEEP_DUPLICATE_PACKAGE));
#endif

    if (fetchbuf == 0) {
        printk("%s: no fetchbuf\n", __FUNCTION__);
        return -ENOMEM;
    }

    /* hook stream buffer with PARSER */
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (HAS_HEVC_VDEC && is_hevc) {
        WRITE_MPEG_REG(PARSER_VIDEO_START_PTR,
                       READ_VREG(HEVC_STREAM_START_ADDR));
        WRITE_MPEG_REG(PARSER_VIDEO_END_PTR,
                       READ_VREG(HEVC_STREAM_END_ADDR) - 8);

        CLEAR_MPEG_REG_MASK(PARSER_ES_CONTROL, ES_VID_MAN_RD_PTR);

        WRITE_VREG(DOS_GEN_CTRL0, 3<<1);    // set vififo_vbuf_rp_sel=>hevc

        SET_VREG_MASK(HEVC_STREAM_CONTROL, (1<<3)|(0<<4)); // set use_parser_vbuf_wp
        SET_VREG_MASK(HEVC_STREAM_CONTROL, 1); // set stream_fetch_enable
        SET_VREG_MASK(HEVC_STREAM_FIFO_CTL, (1<<29)); // set stream_buffer_hole with 256 bytes
    } else
#endif
    {
        WRITE_MPEG_REG(PARSER_VIDEO_START_PTR,
                       READ_VREG(VLD_MEM_VIFIFO_START_PTR));
        WRITE_MPEG_REG(PARSER_VIDEO_END_PTR,
                       READ_VREG(VLD_MEM_VIFIFO_END_PTR));
        CLEAR_MPEG_REG_MASK(PARSER_ES_CONTROL, ES_VID_MAN_RD_PTR);

        WRITE_VREG(VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);
        CLEAR_VREG_MASK(VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);

        if (HAS_HEVC_VDEC) {
            WRITE_VREG(DOS_GEN_CTRL0, 0);    // set vififo_vbuf_rp_sel=>vdec
        }
    }

    WRITE_MPEG_REG(PARSER_AUDIO_START_PTR,
                   READ_MPEG_REG(AIU_MEM_AIFIFO_START_PTR));
    WRITE_MPEG_REG(PARSER_AUDIO_END_PTR,
                   READ_MPEG_REG(AIU_MEM_AIFIFO_END_PTR));
    CLEAR_MPEG_REG_MASK(PARSER_ES_CONTROL, ES_AUD_MAN_RD_PTR);

    WRITE_MPEG_REG(PARSER_CONFIG,
                   (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
                   (1  << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
                   (16 << PS_CFG_MAX_FETCH_CYCLE_BIT));

    WRITE_MPEG_REG(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);
    CLEAR_MPEG_REG_MASK(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);

    WRITE_MPEG_REG(PARSER_SUB_START_PTR, parser_sub_start_ptr);
    WRITE_MPEG_REG(PARSER_SUB_END_PTR, parser_sub_end_ptr);
    WRITE_MPEG_REG(PARSER_SUB_RP, parser_sub_rp);
    SET_MPEG_REG_MASK(PARSER_ES_CONTROL, (7 << ES_SUB_WR_ENDIAN_BIT) | ES_SUB_MAN_RD_PTR);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (HAS_HEVC_VDEC)
        r = pts_start((is_hevc) ? PTS_TYPE_HEVC : PTS_TYPE_VIDEO);
    else 
#endif
        r = pts_start(PTS_TYPE_VIDEO);

    if (r < 0) {
        printk("Video pts start failed.(%d)\n", r);
        goto err1;
    }

    if ((r = pts_start(PTS_TYPE_AUDIO)) < 0) {
        printk("Audio pts start failed.(%d)\n", r);
        goto err2;
    }

    r = request_irq(INT_PARSER, parser_isr,
                    IRQF_SHARED, "tsdemux-fetch",
                    (void *)tsdemux_fetch_id);
    if (r) {
        goto err3;
    }

    WRITE_MPEG_REG(PARSER_INT_STATUS, 0xffff);
    WRITE_MPEG_REG(PARSER_INT_ENABLE, PARSER_INTSTAT_FETCH_CMD << PARSER_INT_HOST_EN_BIT);

    WRITE_MPEG_REG(PARSER_VIDEO_HOLE, 0x400);
    WRITE_MPEG_REG(PARSER_AUDIO_HOLE, 0x400);

    discontinued_counter = 0;
#ifndef ENABLE_DEMUX_DRIVER
    r = request_irq(INT_DEMUX, tsdemux_isr,
                    IRQF_SHARED, "tsdemux-irq",
                    (void *)tsdemux_irq_id);
    WRITE_MPEG_REG(STB_INT_MASK,
                   (1 << SUB_PES_READY)
                   | (1 << NEW_PDTS_READY)
                   | (1 << DIS_CONTINUITY_PACKET));
    if (r) {
        goto err4;
    }
#else
    tsdemux_config();
    tsdemux_request_irq(tsdemux_isr, (void *)tsdemux_irq_id);
    if (vid < 0x1FFF) {
        tsdemux_set_vid(vid);
    }
    if (aid < 0x1FFF) {
        tsdemux_set_aid(aid);
    }
    if (sid < 0x1FFF) {
        tsdemux_set_sid(sid);
    }
    if ((pcrid < 0x1FFF) && (pcrid != vid) && (pcrid != aid) && (pcrid != sid)) {
    	tsdemux_set_pcrid(pcrid);
	}
#endif

    if (pcrid < 0x1FFF){
    /* set paramater to fetch pcr */  
    pcr_num=0;
    if(pcrid == vid)
    	pcr_num=0;
    else if(pcrid == aid)
    	pcr_num=1;
    else
    	pcr_num=3;

    if(READ_MPEG_REG(TS_HIU_CTL_2) & 0x40){
    	WRITE_MPEG_REG(PCR90K_CTL_2, 12 << 1);    
    	WRITE_MPEG_REG(ASSIGN_PID_NUMBER_2, pcr_num);    
    	printk("[tsdemux_init] To use device 2,pcr_num=%d \n",pcr_num);
    }
    else if(READ_MPEG_REG(TS_HIU_CTL_3) & 0x40){
    	WRITE_MPEG_REG(PCR90K_CTL_3, 12 << 1); 
    	WRITE_MPEG_REG(ASSIGN_PID_NUMBER_3, pcr_num);    
    	printk("[tsdemux_init] To use device 3,pcr_num=%d \n",pcr_num);
    }
    else{
    	WRITE_MPEG_REG(PCR90K_CTL, 12 << 1); 
    	WRITE_MPEG_REG(ASSIGN_PID_NUMBER, pcr_num);    
    	printk("[tsdemux_init] To use device 1,pcr_num=%d \n",pcr_num);
    }
	first_pcr = 0;
    pcrscr_valid=1;
    }

    return 0;

#ifndef ENABLE_DEMUX_DRIVER
err4:
    free_irq(INT_PARSER, (void *)tsdemux_fetch_id);
#endif
err3:
    pts_stop(PTS_TYPE_AUDIO);
err2:
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (HAS_HEVC_VDEC)
        pts_stop((is_hevc) ? PTS_TYPE_HEVC : PTS_TYPE_VIDEO);
    else
#endif
        pts_stop(PTS_TYPE_VIDEO);
err1:
    printk("TS Demux init failed.\n");
    return -ENOENT;
}

void tsdemux_release(void)
{
    pcrscr_valid=0;
    first_pcr=0;

    WRITE_MPEG_REG(PARSER_INT_ENABLE, 0);
    WRITE_MPEG_REG(PARSER_VIDEO_HOLE, 0);
    WRITE_MPEG_REG(PARSER_AUDIO_HOLE, 0);
    free_irq(INT_PARSER, (void *)tsdemux_fetch_id);

#ifndef ENABLE_DEMUX_DRIVER
    WRITE_MPEG_REG(STB_INT_MASK, 0);
    free_irq(INT_DEMUX, (void *)tsdemux_irq_id);
#else

    tsdemux_set_aid(0xffff);
    tsdemux_set_vid(0xffff);
    tsdemux_set_sid(0xffff);
    tsdemux_set_pcrid(0xffff);
    tsdemux_free_irq();

#endif


    pts_stop(PTS_TYPE_VIDEO);
    pts_stop(PTS_TYPE_AUDIO);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_type(MOD_DEMUX, 0);
#endif

}
static int limited_delay_check(struct file *file,
                      struct stream_buf_s *vbuf,
                      struct stream_buf_s *abuf,
                      const char __user *buf, size_t count)
{
	int write_size;
	if(	vbuf->max_buffer_delay_ms>0 && abuf->max_buffer_delay_ms>0 &&
		stbuf_level(vbuf)>1024 && stbuf_level(abuf)>256){
		int vdelay=calculation_stream_delayed_ms(PTS_TYPE_VIDEO,NULL,NULL);
		int adelay=calculation_stream_delayed_ms(PTS_TYPE_AUDIO,NULL,NULL);
		int maxretry=10;/*max wait 100ms,if timeout,try again top level.*/
		/*too big  delay,do wait now.*/
		if(!(file->f_flags & O_NONBLOCK)){/*if noblock mode,don't do wait.*/
			while(vdelay>vbuf->max_buffer_delay_ms && adelay>abuf->max_buffer_delay_ms && maxretry-->0){
				///printk("too big delay,vdelay:%d>%d adelay:%d>%d,do wait now,retry=%d\n",vdelay,vbuf->max_buffer_delay_ms,adelay,abuf->max_buffer_delay_ms,maxretry);
				msleep(10);
				vdelay=calculation_stream_delayed_ms(PTS_TYPE_VIDEO,NULL,NULL);
				adelay=calculation_stream_delayed_ms(PTS_TYPE_AUDIO,NULL,NULL);
			}
		}
		if(vdelay>vbuf->max_buffer_delay_ms && adelay>abuf->max_buffer_delay_ms)
			return 0;
	}
    write_size = min(stbuf_space(vbuf), stbuf_space(abuf));
    write_size = min((int)count, write_size);
	return write_size;
}


ssize_t tsdemux_write(struct file *file,
                      struct stream_buf_s *vbuf,
                      struct stream_buf_s *abuf,
                      const char __user *buf, size_t count)
{
    s32 r;
    stream_port_t *port = (stream_port_t *)file->private_data;
    size_t wait_size, write_size;

    if ((stbuf_space(vbuf) < count) ||
        (stbuf_space(abuf) < count)) {
        if (file->f_flags & O_NONBLOCK) {
	      write_size=min(stbuf_space(vbuf), stbuf_space(abuf));
	      if(write_size<=188)/*have 188 bytes,write now., */
			return -EAGAIN;
        }else{
	        wait_size = min(stbuf_canusesize(vbuf) / 8, stbuf_canusesize(abuf) / 4);
	        if ((port->flag & PORT_FLAG_VID)
	            && (stbuf_space(vbuf) < wait_size)) {
	            r = stbuf_wait_space(vbuf, wait_size);

	            if (r < 0) {
			//printk("write no space--- no space,%d--%d,r-%d\n",stbuf_space(vbuf),stbuf_space(abuf),r);
	                return r;
	            }
	        }

	        if ((port->flag & PORT_FLAG_AID)
	            && (stbuf_space(abuf) < wait_size)) {
	            r = stbuf_wait_space(abuf, wait_size);

	            if (r < 0) {
			//printk("write no stbuf_wait_space--- no space,%d--%d,r-%d\n",stbuf_space(vbuf),stbuf_space(abuf),r);
	                return r;
	            }
	        }
        }
    }
	vbuf->last_write_jiffies64=jiffies_64;
	abuf->last_write_jiffies64=jiffies_64;
	write_size=limited_delay_check(file,vbuf,abuf,buf,count);
    if (write_size > 0) {
        return _tsdemux_write(buf, write_size);
    } else {
        return -EAGAIN;
    }
}

static ssize_t show_discontinue_counter(struct class *class, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", discontinued_counter);
}

static struct class_attribute tsdemux_class_attrs[] = {
    __ATTR(discontinue_counter,  S_IRUGO, show_discontinue_counter, NULL),
    __ATTR_NULL
};

static struct class tsdemux_class = {
        .name = "tsdemux",
        .class_attrs = tsdemux_class_attrs,
    };


int     tsdemux_class_register(void)
{
    int r;
    if ((r = class_register(&tsdemux_class)) < 0) {
        printk("register tsdemux class error!\n");
    }
    discontinued_counter = 0;
    return r;
}
void  tsdemux_class_unregister(void)
{
    class_unregister(&tsdemux_class);
}

void tsdemux_change_avid(unsigned int vid, unsigned int aid)
{
#ifndef ENABLE_DEMUX_DRIVER
    WRITE_MPEG_REG(FM_WR_DATA,
                   (((vid & 0x1fff) | (VIDEO_PACKET << 13)) << 16) |
                   ((aid & 0x1fff) | (AUDIO_PACKET << 13)));
    WRITE_MPEG_REG(FM_WR_ADDR, 0x8000);
    while (READ_MPEG_REG(FM_WR_ADDR) & 0x8000) {
        ;
    }
#else
    tsdemux_set_vid(vid);
    tsdemux_set_aid(aid);
#endif
    return;
}

void tsdemux_change_sid(unsigned int sid)
{
#ifndef ENABLE_DEMUX_DRIVER
    WRITE_MPEG_REG(FM_WR_DATA,
                   (((sid & 0x1fff) | (SUB_PACKET << 13)) << 16) | 0xffff);
    WRITE_MPEG_REG(FM_WR_ADDR, 0x8001);
    while (READ_MPEG_REG(FM_WR_ADDR) & 0x8000) {
        ;
    }
#else
    tsdemux_set_sid(sid);
#endif
    return;
}

void tsdemux_audio_reset(void)
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

    spin_unlock_irqrestore(&lock, flags);

    return;
}

void tsdemux_sub_reset(void)
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

void tsdemux_set_skipbyte(int skipbyte)
{
#ifndef ENABLE_DEMUX_DRIVER
    demux_skipbyte = skipbyte;
#else
    tsdemux_set_skip_byte(skipbyte);
#endif
    return;
}

void tsdemux_set_demux(int dev)
{
#ifdef ENABLE_DEMUX_DRIVER
    unsigned long flags;
    int r = 0;

    spin_lock_irqsave(&demux_ops_lock, flags);
    if (demux_ops && demux_ops->set_demux) {
        r = demux_ops->set_demux(dev);
    }
    spin_unlock_irqrestore(&demux_ops_lock, flags);
#endif
}

u32 tsdemux_pcrscr_get(void)
{
    u32 pcr;
    if(READ_MPEG_REG(TS_HIU_CTL_2) & 0x40){
    	
    	pcr = READ_MPEG_REG(PCR_DEMUX_2);
    }
    else if(READ_MPEG_REG(TS_HIU_CTL_3) & 0x40){
    	pcr = READ_MPEG_REG(PCR_DEMUX_3);
    }
    else{
    	pcr = READ_MPEG_REG(PCR_DEMUX);    
   }
    if(first_pcr == 0)
    	first_pcr = pcr;
    return pcr;
}

 u32 tsdemux_first_pcrscr_get(void)
 {
 	return first_pcr;
 }
u8 tsdemux_pcrscr_valid(void)
{
    return pcrscr_valid;
}
