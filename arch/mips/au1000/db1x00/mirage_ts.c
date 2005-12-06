/*
 * linux/arch/mips/au1000/db1x00/mirage_ts.c
 *
 * BRIEF MODULE DESCRIPTION
 *	Glue between Mirage board-specific touchscreen pieces
 *	and generic Wolfson Codec touchscreen support.
 *
 *	Based on pb1100_ts.c used in Hydrogen II.
 *
 * Copyright (c) 2003 Embedded Edge, LLC
 *		dan@embeddededge.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>

#include <asm/segment.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <asm/au1000.h>

/*
 *  Imported interface to Wolfson Codec driver.
 */
extern void *wm97xx_ts_get_handle(int which);
extern int wm97xx_ts_ready(void* ts_handle);
extern void wm97xx_ts_set_cal(void* ts_handle, int xscale, int xtrans, int yscale, int ytrans);
extern u16 wm97xx_ts_get_ac97(void* ts_handle, u8 reg);
extern void wm97xx_ts_set_ac97(void* ts_handle, u8 reg, u16 val);
extern int wm97xx_ts_read_data(void* ts_handle, long* x, long* y, long* pressure);
extern void wm97xx_ts_send_data(void* ts_handle, long x, long y, long z);

int wm97xx_comodule_present = 1;


#define TS_NAME "mirage_ts"

#define err(format, arg...) printk(KERN_ERR TS_NAME ": " format "\n" , ## arg)
#define info(format, arg...) printk(KERN_INFO TS_NAME ": " format "\n" , ## arg)
#define warn(format, arg...) printk(KERN_WARNING TS_NAME ": " format "\n" , ## arg)
#define DPRINTK(format, arg...) printk("%s: " format "\n", __FUNCTION__ , ## arg)


#define PEN_DOWN_IRQ	AU1000_GPIO_7

static struct task_struct *ts_task = 0;
static DECLARE_COMPLETION(ts_complete);
static DECLARE_WAIT_QUEUE_HEAD(pendown_wait);

#ifdef CONFIG_WM97XX_FIVEWIRETS
static int release_pressure = 1;
#else
static int release_pressure = 50;
#endif

typedef struct {
   long x;
   long y;
} DOWN_EVENT;

#define SAMPLE_RATE	50	/* samples per second */
#define PEN_DEBOUNCE	5	/* samples for settling - fn of SAMPLE_RATE */
#define PEN_UP_TIMEOUT	10	/* in seconds */
#define PEN_UP_SETTLE	5	/* samples per second */

static struct {
	int xscale;
	int xtrans;
	int yscale;
	int ytrans;
} mirage_ts_cal =
{
#if 0
	.xscale   = 84,
	.xtrans = -157,
	.yscale   = 66,
	.ytrans = -150,
#else
	.xscale   = 84,
	.xtrans = -150,
	.yscale   = 66,
	.ytrans = -146,
#endif
};


static void pendown_irq(int irqnr, void *devid, struct pt_regs *regs)
{
//DPRINTK("got one 0x%x", au_readl(SYS_PINSTATERD));
	wake_up(&pendown_wait);
}

static int ts_thread(void *id)
{
	static int pen_was_down = 0;
	static DOWN_EVENT pen_xy;
	long x, y, z;
	void *ts;	/* handle */
	struct task_struct *tsk = current;
	int timeout = HZ / SAMPLE_RATE;

	ts_task = tsk;

	daemonize();
	tsk->tty = NULL;
	tsk->policy = SCHED_FIFO;
	tsk->rt_priority = 1;
	strcpy(tsk->comm, "touchscreen");

	/* only want to receive SIGKILL */
	spin_lock_irq(&tsk->sigmask_lock);
	siginitsetinv(&tsk->blocked, sigmask(SIGKILL));
	recalc_sigpending(tsk);
	spin_unlock_irq(&tsk->sigmask_lock);

	/* get handle for codec */
	ts = wm97xx_ts_get_handle(0);

	/* proceed only after everybody is ready */
	wait_event_timeout(pendown_wait, wm97xx_ts_ready(ts), HZ/4);

	/* board-specific calibration */
	wm97xx_ts_set_cal(ts,
			mirage_ts_cal.xscale,
			mirage_ts_cal.xtrans,
			mirage_ts_cal.yscale,
			mirage_ts_cal.ytrans);

	/* route Wolfson pendown interrupts to our GPIO */
	au_sync();
	wm97xx_ts_set_ac97(ts, 0x4c, wm97xx_ts_get_ac97(ts, 0x4c) & ~0x0008);
	au_sync();
	wm97xx_ts_set_ac97(ts, 0x56, wm97xx_ts_get_ac97(ts, 0x56) & ~0x0008);
	au_sync();
	wm97xx_ts_set_ac97(ts, 0x52, wm97xx_ts_get_ac97(ts, 0x52) | 0x2008);
	au_sync();

	for (;;) {
		interruptible_sleep_on_timeout(&pendown_wait, timeout);
		disable_irq(PEN_DOWN_IRQ);
		if (signal_pending(tsk)) {
			break;
		}

		/* read codec */
		if (!wm97xx_ts_read_data(ts, &x, &y, &z))
			z = 0;	/* treat no-data and pen-up the same */

		if (signal_pending(tsk)) {
			break;
		}

		if (z >= release_pressure) {
			y = ~y;	/* top to bottom */
			if (pen_was_down > 1 /*&& pen_was_down < PEN_DEBOUNCE*/) {//THXXX
				/* bounce ? */
				x = pen_xy.x;
				y = pen_xy.y;
				--pen_was_down;
			} else if (pen_was_down <= 1) {
				pen_xy.x = x;
				pen_xy.y = y;
				if (pen_was_down)
					wm97xx_ts_send_data(ts, x, y, z);
				pen_was_down = PEN_DEBOUNCE;
			}
			//wm97xx_ts_send_data(ts, x, y, z);
			timeout = HZ / SAMPLE_RATE;
		} else {
			if (pen_was_down) {
				if (--pen_was_down)
					z = release_pressure;
				else //THXXX
				wm97xx_ts_send_data(ts, pen_xy.x, pen_xy.y, z);
			}
			/* The pendown signal takes some time to settle after
			 * reading the pen pressure so wait a little
			 * before enabling the pen.
			 */
			if (! pen_was_down) {
//				interruptible_sleep_on_timeout(&pendown_wait, HZ / PEN_UP_SETTLE);
				timeout = HZ * PEN_UP_TIMEOUT;
			}
		}
		enable_irq(PEN_DOWN_IRQ);
	}
	enable_irq(PEN_DOWN_IRQ);
	ts_task = NULL;
	complete(&ts_complete);
	return 0;
}

static int __init ts_mirage_init(void)
{
	int ret;

	/* pen down signal is connected to GPIO 7 */

	ret = request_irq(PEN_DOWN_IRQ, pendown_irq, 0, "ts-pendown", NULL);
	if (ret) {
		err("unable to get pendown irq%d: [%d]", PEN_DOWN_IRQ, ret);
		return ret;
	}

	lock_kernel();
	ret = kernel_thread(ts_thread, NULL, CLONE_FS | CLONE_FILES);
	if (ret < 0) {
		unlock_kernel();
		return ret;
	}
	unlock_kernel();

	info("Mirage touchscreen IRQ initialized.");

	return 0;
}

static void __exit ts_mirage_exit(void)
{
	if (ts_task) {
		send_sig(SIGKILL, ts_task, 1);
		wait_for_completion(&ts_complete);
	}

	free_irq(PEN_DOWN_IRQ, NULL);
}

module_init(ts_mirage_init);
module_exit(ts_mirage_exit);

