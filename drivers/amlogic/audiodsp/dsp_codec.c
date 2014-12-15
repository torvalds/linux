#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/cache.h>
#include <asm/cacheflush.h>
//#include <asm/arch/am_regs.h>
#include <mach/am_regs.h>
//#include <asm/bsp.h>

#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/timestamp.h>
//#include <asm/dsp/dsp_register.h>

#include "dsp_microcode.h"
#include "audiodsp_module.h"
#include "dsp_control.h"

#include "dsp_mailbox.h"

#include "dsp_codec.h"

int dsp_codec_start(struct audiodsp_priv *priv)
{
    return dsp_mailbox_send(priv, 1, M2B_IRQ2_DECODE_START, 0, 0, 0);

}
int dsp_codec_stop(struct audiodsp_priv *priv)
{

    return dsp_mailbox_send(priv, 1, M2B_IRQ3_DECODE_STOP, 0, 0, 0);
}


int dsp_codec_get_bufer_data_len(struct audiodsp_priv *priv)
{
#define REVERSD_BYTES   32
#define CACHE_ALIGNED(x)    (x&(~0x1f))
    unsigned long rp, wp, len, flags;
    local_irq_save(flags);
    rp = dsp_codec_get_rd_addr(priv);
    wp = dsp_codec_get_wd_addr(priv);
    if (rp > wp) {
        len = priv->stream_buffer_size - (rp - wp);
    } else {
        len = (wp - rp);
    }
    len = (len > REVERSD_BYTES) ? (len - REVERSD_BYTES) : 0;
    len = CACHE_ALIGNED(len);
    local_irq_restore(flags);
    return len;
}

int dsp_codec_get_bufer_data_len1(struct audiodsp_priv *priv, unsigned long wd_ptr)
{

#define REVERSD_BYTES   32
#define CACHE_ALIGNED(x)    (x&(~0x1f))
    unsigned long rp, wp, len, flags;
    //if(wd_ptr != DSP_RD(DSP_DECODE_OUT_WD_ADDR))
    //  printk("w1 == %x , w2 == %x, r == %x\n", DSP_RD(DSP_DECODE_OUT_WD_ADDR), wd_ptr, DSP_RD(DSP_DECODE_OUT_RD_ADDR));
    local_irq_save(flags);
    rp = dsp_codec_get_rd_addr(priv);
    wp = ARC_2_ARM_ADDR_SWAP(wd_ptr);
    if (rp > wp) {
        len = priv->stream_buffer_size - (rp - wp);
    } else {
        len = (wp - rp);
    }
    len = (len > REVERSD_BYTES) ? (len - REVERSD_BYTES) : 0;
    len = CACHE_ALIGNED(len);
    local_irq_restore(flags);
    return len;
}

unsigned long dsp_codec_inc_rd_addr(struct audiodsp_priv *priv, int size)
{
    unsigned long rd, flags;
    local_irq_save(flags);
    rd = dsp_codec_get_rd_addr(priv);
    rd = rd + size;
    if (rd >= priv->stream_buffer_end) {
        rd = rd - priv->stream_buffer_size;
    }
    DSP_WD(DSP_DECODE_OUT_RD_ADDR, ARM_2_ARC_ADDR_SWAP((void*)rd));
    local_irq_restore(flags);
    return rd;
}


u32 dsp_codec_get_current_pts(struct audiodsp_priv *priv)
{
#ifdef CONFIG_AM_PTSSERVER
    u32  pts;
    u32 delay_pts;
    int len;
    u64 frame_nums;
    int res;
    u32 offset, buffered_len, wp;

    mutex_lock(&priv->stream_buffer_mutex);

    if (priv->frame_format.channel_num == 0 || priv->frame_format.sample_rate == 0 || priv->frame_format.data_width == 0) {
        printk("unvalid audio format!\n");
	 mutex_unlock(&priv->stream_buffer_mutex);
        return -1;
    }
#if 0
    if (priv->stream_fmt == MCODEC_FMT_COOK) {
        pts = priv->cur_frame_info.offset;
        mutex_unlock(&priv->stream_buffer_mutex);
    } else
#endif
    {


        buffered_len = DSP_RD(DSP_BUFFERED_LEN);
        wp = DSP_RD(DSP_DECODE_OUT_WD_PTR);
        offset = DSP_RD(DSP_AFIFO_RD_OFFSET1);
        // before audio start, the pts always be at the first index
        if(!timestamp_apts_started()){
          offset = 0;
        }
        
        if (priv->stream_fmt == MCODEC_FMT_COOK || priv->stream_fmt == MCODEC_FMT_RAAC) {
            pts = DSP_RD(DSP_AFIFO_RD_OFFSET1);
            res = 0;
        } else {
            res = pts_lookup_offset(PTS_TYPE_AUDIO, offset, &pts, 300);
            //printk("pts_lookup_offset = %d, buffer_len = %d, res = %d\n", offset, buffered_len, res);

            if (!priv->first_lookup_over) {
                priv->first_lookup_over = 1;
                if (first_lookup_pts_failed(PTS_TYPE_AUDIO)) {

                    priv->out_len_after_last_valid_pts = 0;
                    priv->last_valid_pts = pts;

                    mutex_unlock(&priv->stream_buffer_mutex);
                    return pts;
                }
            }
        }

        if (res == 0) {
            //printk("check out pts == %x\n", pts);
            priv->out_len_after_last_valid_pts = 0;
            len = buffered_len + dsp_codec_get_bufer_data_len1(priv, wp);
            frame_nums = (len * 8 / (priv->frame_format.data_width * priv->frame_format.channel_num));
	     delay_pts = div64_u64(frame_nums*90*20, priv->frame_format.sample_rate/50);
            //printk("cal delay pts == %x\n", delay_pts);
            if (pts > delay_pts) {
                pts -= delay_pts;
            } else {
                pts = 0;
            }
            priv->last_valid_pts = pts;

            //printk("len = %d, data_width = %d, channel_num = %d, frame_nums = %lld, sample_rate = %d, pts = %d\n",
            //   len, priv->frame_format.data_width,priv->frame_format.channel_num, frame_nums, priv->frame_format.sample_rate, pts);
        }

        else if (priv->last_valid_pts >= 0) {
            pts = priv->last_valid_pts;
            len = priv->out_len_after_last_valid_pts;
            frame_nums = (len * 8 / (priv->frame_format.data_width * priv->frame_format.channel_num));
	     pts += div64_u64(frame_nums*90*20, priv->frame_format.sample_rate/50);

            //printk("last_pts = %d, len = %d, data_width = %d, channel_num = %d, frame_nums = %lld, sample_rate = %d, pts = %d\n",
            //    priv->last_valid_pts, len, priv->frame_format.data_width,priv->frame_format.channel_num, frame_nums, priv->frame_format.sample_rate, pts);
        }

        else {
            printk("checkout audio pts failed!\n");
            pts = -1;
        }

        mutex_unlock(&priv->stream_buffer_mutex);
    }
    return pts;
#endif
    return -1;
}


