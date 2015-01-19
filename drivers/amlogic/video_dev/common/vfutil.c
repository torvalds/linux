/* 
 * 
 */
 
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
//#include <linux/fiq_bridge.h>
#include <linux/fs.h>
#include <mach/am_regs.h>

#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/timestamp.h>
#include <linux/amlogic/amports/tsync.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/clk.h>

static struct vframe_provider_s *vfp = NULL;
static DEFINE_SPINLOCK(lock);

void v4l_reg_provider(struct vframe_provider_s *prov)
{
    ulong flags;

    spin_lock_irqsave(&lock, flags);

    if (vfp) {
        vf_unreg_provider(vfp);
    }
    vfp = prov;
    spin_unlock_irqrestore(&lock, flags);
}

void v4l_unreg_provider(void)
{
    ulong flags;
    //int deinterlace_mode = get_deinterlace_mode();

    spin_lock_irqsave(&lock, flags);

    vfp = NULL;

    //if (deinterlace_mode == 2) {
        //disable_pre_deinterlace();
    //}
    //printk("-----%s------\n",__func__);
    spin_unlock_irqrestore(&lock, flags);
}

const vframe_provider_t* v4l_get_vfp(void)
{
    return vfp;
}
