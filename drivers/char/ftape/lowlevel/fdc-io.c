/*
 * Copyright (C) 1993-1996 Bas Laarhoven,
 *           (C) 1996-1997 Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/fdc-io.c,v $
 * $Revision: 1.7.4.2 $
 * $Date: 1997/11/16 14:48:17 $
 *
 *      This file contains the low-level floppy disk interface code
 *      for the QIC-40/80/3010/3020 floppy-tape driver "ftape" for
 *      Linux.
 */

#include <linux/config.h> /* for CONFIG_FT_* */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/fdc-io.h"
#include "../lowlevel/fdc-isr.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-calibr.h"
#include "../lowlevel/fc-10.h"

/*      Global vars.
 */
static int ftape_motor;
volatile int ftape_current_cylinder = -1;
volatile fdc_mode_enum fdc_mode = fdc_idle;
fdc_config_info fdc;
DECLARE_WAIT_QUEUE_HEAD(ftape_wait_intr);

unsigned int ft_fdc_base       = CONFIG_FT_FDC_BASE;
unsigned int ft_fdc_irq        = CONFIG_FT_FDC_IRQ;
unsigned int ft_fdc_dma        = CONFIG_FT_FDC_DMA;
unsigned int ft_fdc_threshold  = CONFIG_FT_FDC_THR;  /* bytes */
unsigned int ft_fdc_rate_limit = CONFIG_FT_FDC_MAX_RATE; /* bits/sec */
int ft_probe_fc10        = CONFIG_FT_PROBE_FC10;
int ft_mach2             = CONFIG_FT_MACH2;

/*      Local vars.
 */
static spinlock_t fdc_io_lock; 
static unsigned int fdc_calibr_count;
static unsigned int fdc_calibr_time;
static int fdc_status;
volatile __u8 fdc_head;		/* FDC head from sector id */
volatile __u8 fdc_cyl;		/* FDC track from sector id */
volatile __u8 fdc_sect;		/* FDC sector from sector id */
static int fdc_data_rate = 500;	/* data rate (Kbps) */
static int fdc_rate_code;	/* data rate code (0 == 500 Kbps) */
static int fdc_seek_rate = 2;	/* step rate (msec) */
static void (*do_ftape) (void);
static int fdc_fifo_state;	/* original fifo setting - fifo enabled */
static int fdc_fifo_thr;	/* original fifo setting - threshold */
static int fdc_lock_state;	/* original lock setting - locked */
static int fdc_fifo_locked;	/* has fifo && lock set ? */
static __u8 fdc_precomp;	/* default precomp. value (nsec) */
static __u8 fdc_prec_code;	/* fdc precomp. select code */

static char ftape_id[] = "ftape";  /* used by request irq and free irq */

static int fdc_set_seek_rate(int seek_rate);

void fdc_catch_stray_interrupts(int count)
{
	unsigned long flags;

	spin_lock_irqsave(&fdc_io_lock, flags);
	if (count == 0) {
		ft_expected_stray_interrupts = 0;
	} else {
		ft_expected_stray_interrupts += count;
	}
	spin_unlock_irqrestore(&fdc_io_lock, flags);
}

/*  Wait during a timeout period for a given FDC status.
 *  If usecs == 0 then just test status, else wait at least for usecs.
 *  Returns -ETIME on timeout. Function must be calibrated first !
 */
static int fdc_wait(unsigned int usecs, __u8 mask, __u8 state)
{
	int count_1 = (fdc_calibr_count * usecs +
                       fdc_calibr_count - 1) / fdc_calibr_time;

	do {
		fdc_status = inb_p(fdc.msr);
		if ((fdc_status & mask) == state) {
			return 0;
		}
	} while (count_1-- >= 0);
	return -ETIME;
}

int fdc_ready_wait(unsigned int usecs)
{
	return fdc_wait(usecs, FDC_DATA_READY | FDC_BUSY, FDC_DATA_READY);
}

/* Why can't we just use udelay()?
 */
static void fdc_usec_wait(unsigned int usecs)
{
	fdc_wait(usecs, 0, 1);	/* will always timeout ! */
}

static int fdc_ready_out_wait(unsigned int usecs)
{
	fdc_usec_wait(FT_RQM_DELAY);	/* wait for valid RQM status */
	return fdc_wait(usecs, FDC_DATA_OUT_READY, FDC_DATA_OUT_READY);
}

void fdc_wait_calibrate(void)
{
	ftape_calibrate("fdc_wait",
			fdc_usec_wait, &fdc_calibr_count, &fdc_calibr_time); 
}

/*  Wait for a (short) while for the FDC to become ready
 *  and transfer the next command byte.
 *  Return -ETIME on timeout on getting ready (depends on hardware!).
 */
static int fdc_write(const __u8 data)
{
	fdc_usec_wait(FT_RQM_DELAY);	/* wait for valid RQM status */
	if (fdc_wait(150, FDC_DATA_READY_MASK, FDC_DATA_IN_READY) < 0) {
		return -ETIME;
	} else {
		outb(data, fdc.fifo);
		return 0;
	}
}

/*  Wait for a (short) while for the FDC to become ready
 *  and transfer the next result byte.
 *  Return -ETIME if timeout on getting ready (depends on hardware!).
 */
static int fdc_read(__u8 * data)
{
	fdc_usec_wait(FT_RQM_DELAY);	/* wait for valid RQM status */
	if (fdc_wait(150, FDC_DATA_READY_MASK, FDC_DATA_OUT_READY) < 0) {
		return -ETIME;
	} else {
		*data = inb(fdc.fifo);
		return 0;
	}
}

/*  Output a cmd_len long command string to the FDC.
 *  The FDC should be ready to receive a new command or
 *  an error (EBUSY or ETIME) will occur.
 */
int fdc_command(const __u8 * cmd_data, int cmd_len)
{
	int result = 0;
	unsigned long flags;
	int count = cmd_len;
	int retry = 0;
#ifdef TESTING
	static unsigned int last_time;
	unsigned int time;
#endif
	TRACE_FUN(ft_t_any);

	fdc_usec_wait(FT_RQM_DELAY);	/* wait for valid RQM status */
	spin_lock_irqsave(&fdc_io_lock, flags);
	if (!in_interrupt())
		/* Yes, I know, too much comments inside this function
		 * ...
		 * 
		 * Yet another bug in the original driver. All that
		 * havoc is caused by the fact that the isr() sends
		 * itself a command to the floppy tape driver (pause,
		 * micro step pause).  Now, the problem is that
		 * commands are transmitted via the fdc_seek
		 * command. But: the fdc performs seeks in the
		 * background i.e. it doesn't signal busy while
		 * sending the step pulses to the drive. Therefore the
		 * non-interrupt level driver has no chance to tell
		 * whether the isr() just has issued a seek. Therefore
		 * we HAVE TO have a look at the ft_hide_interrupt
		 * flag: it signals the non-interrupt level part of
		 * the driver that it has to wait for the fdc until it
		 * has completet seeking.
		 *
		 * THIS WAS PRESUMABLY THE REASON FOR ALL THAT
		 * "fdc_read timeout" errors, I HOPE :-)
		 */
		if (ft_hide_interrupt) {
			restore_flags(flags);
			TRACE(ft_t_info,
			      "Waiting for the isr() completing fdc_seek()");
			if (fdc_interrupt_wait(2 * FT_SECOND) < 0) {
				TRACE(ft_t_warn,
		      "Warning: timeout waiting for isr() seek to complete");
			}
			if (ft_hide_interrupt || !ft_seek_completed) {
				/* There cannot be another
				 * interrupt. The isr() only stops
				 * the tape and the next interrupt
				 * won't come until we have send our
				 * command to the drive.
				 */
				TRACE_ABORT(-EIO, ft_t_bug,
					    "BUG? isr() is still seeking?\n"
					    KERN_INFO "hide: %d\n"
					    KERN_INFO "seek: %d",
					    ft_hide_interrupt,
					    ft_seek_completed);

			}
			fdc_usec_wait(FT_RQM_DELAY);	/* wait for valid RQM status */
			spin_lock_irqsave(&fdc_io_lock, flags);
		}
	fdc_status = inb(fdc.msr);
	if ((fdc_status & FDC_DATA_READY_MASK) != FDC_DATA_IN_READY) {
		spin_unlock_irqrestore(&fdc_io_lock, flags);
		TRACE_ABORT(-EBUSY, ft_t_err, "fdc not ready");
	} 
	fdc_mode = *cmd_data;	/* used by isr */
#ifdef TESTING
	if (fdc_mode == FDC_SEEK) {
		time = ftape_timediff(last_time, ftape_timestamp());
		if (time < 6000) {
	TRACE(ft_t_bug,"Warning: short timeout between seek commands: %d",
	      time);
		}
	}
#endif
	if (!in_interrupt()) {
		/* shouldn't be cleared if called from isr
		 */
		ft_interrupt_seen = 0;
	}
	while (count) {
		result = fdc_write(*cmd_data);
		if (result < 0) {
			TRACE(ft_t_fdc_dma,
			      "fdc_mode = %02x, status = %02x at index %d",
			      (int) fdc_mode, (int) fdc_status,
			      cmd_len - count);
			if (++retry <= 3) {
				TRACE(ft_t_warn, "fdc_write timeout, retry");
			} else {
				TRACE(ft_t_err, "fdc_write timeout, fatal");
				/* recover ??? */
				break;
			}
		} else {
			--count;
			++cmd_data;
		}
        }
#ifdef TESTING
	if (fdc_mode == FDC_SEEK) {
		last_time = ftape_timestamp();
	}
#endif
	spin_unlock_irqrestore(&fdc_io_lock, flags);
	TRACE_EXIT result;
}

/*  Input a res_len long result string from the FDC.
 *  The FDC should be ready to send the result or an error
 *  (EBUSY or ETIME) will occur.
 */
int fdc_result(__u8 * res_data, int res_len)
{
	int result = 0;
	unsigned long flags;
	int count = res_len;
	int retry = 0;
	TRACE_FUN(ft_t_any);

	spin_lock_irqsave(&fdc_io_lock, flags);
	fdc_status = inb(fdc.msr);
	if ((fdc_status & FDC_DATA_READY_MASK) != FDC_DATA_OUT_READY) {
		TRACE(ft_t_err, "fdc not ready");
		result = -EBUSY;
	} else while (count) {
		if (!(fdc_status & FDC_BUSY)) {
			spin_unlock_irqrestore(&fdc_io_lock, flags);
			TRACE_ABORT(-EIO, ft_t_err, "premature end of result phase");
		}
		result = fdc_read(res_data);
		if (result < 0) {
			TRACE(ft_t_fdc_dma,
			      "fdc_mode = %02x, status = %02x at index %d",
			      (int) fdc_mode,
			      (int) fdc_status,
			      res_len - count);
			if (++retry <= 3) {
				TRACE(ft_t_warn, "fdc_read timeout, retry");
			} else {
				TRACE(ft_t_err, "fdc_read timeout, fatal");
				/* recover ??? */
				break;
				++retry;
			}
		} else {
			--count;
			++res_data;
		}
	}
	spin_unlock_irqrestore(&fdc_io_lock, flags);
	fdc_usec_wait(FT_RQM_DELAY);	/* allow FDC to negate BSY */
	TRACE_EXIT result;
}

/*      Handle command and result phases for
 *      commands without data phase.
 */
static int fdc_issue_command(const __u8 * out_data, int out_count,
		      __u8 * in_data, int in_count)
{
	TRACE_FUN(ft_t_any);

	if (out_count > 0) {
		TRACE_CATCH(fdc_command(out_data, out_count),);
	}
	/* will take 24 - 30 usec for fdc_sense_drive_status and
	 * fdc_sense_interrupt_status commands.
	 *    35 fails sometimes (5/9/93 SJL)
	 * On a loaded system it incidentally takes longer than
	 * this for the fdc to get ready ! ?????? WHY ??????
	 * So until we know what's going on use a very long timeout.
	 */
	TRACE_CATCH(fdc_ready_out_wait(500 /* usec */),);
	if (in_count > 0) {
		TRACE_CATCH(fdc_result(in_data, in_count),
			    TRACE(ft_t_err, "result phase aborted"));
	}
	TRACE_EXIT 0;
}

/*      Wait for FDC interrupt with timeout (in milliseconds).
 *      Signals are blocked so the wait will not be aborted.
 *      Note: interrupts must be enabled ! (23/05/93 SJL)
 */
int fdc_interrupt_wait(unsigned int time)
{
	DECLARE_WAITQUEUE(wait,current);
	sigset_t old_sigmask;	
	static int resetting;
	long timeout;

	TRACE_FUN(ft_t_fdc_dma);

 	if (waitqueue_active(&ftape_wait_intr)) {
		TRACE_ABORT(-EIO, ft_t_err, "error: nested call");
	}
	/* timeout time will be up to USPT microseconds too long ! */
	timeout = (1000 * time + FT_USPT - 1) / FT_USPT;

	spin_lock_irq(&current->sighand->siglock);
	old_sigmask = current->blocked;
	sigfillset(&current->blocked);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&ftape_wait_intr, &wait);
	while (!ft_interrupt_seen && timeout)
		timeout = schedule_timeout_interruptible(timeout);

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = old_sigmask;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	remove_wait_queue(&ftape_wait_intr, &wait);
	/*  the following IS necessary. True: as well
	 *  wake_up_interruptible() as the schedule() set TASK_RUNNING
	 *  when they wakeup a task, BUT: it may very well be that
	 *  ft_interrupt_seen is already set to 1 when we enter here
	 *  in which case schedule() gets never called, and
	 *  TASK_RUNNING never set. This has the funny effect that we
	 *  execute all the code until we leave kernel space, but then
	 *  the task is stopped (a task CANNOT be preempted while in
	 *  kernel mode. Sending a pair of SIGSTOP/SIGCONT to the
	 *  tasks wakes it up again. Funny! :-)
	 */
	current->state = TASK_RUNNING; 
	if (ft_interrupt_seen) { /* woken up by interrupt */
		ft_interrupt_seen = 0;
		TRACE_EXIT 0;
	}
	/*  Original comment:
	 *  In first instance, next statement seems unnecessary since
	 *  it will be cleared in fdc_command. However, a small part of
	 *  the software seems to rely on this being cleared here
	 *  (ftape_close might fail) so stick to it until things get fixed !
	 */
	/*  My deeply sought of knowledge:
	 *  Behold NO! It is obvious. fdc_reset() doesn't call fdc_command()
	 *  but nevertheless uses fdc_interrupt_wait(). OF COURSE this needs to
	 *  be reset here.
	 */
	ft_interrupt_seen = 0;	/* clear for next call */
	if (!resetting) {
		resetting = 1;	/* break infinite recursion if reset fails */
		TRACE(ft_t_any, "cleanup reset");
		fdc_reset();
		resetting = 0;
	}
	TRACE_EXIT (signal_pending(current)) ? -EINTR : -ETIME;
}

/*      Start/stop drive motor. Enable DMA mode.
 */
void fdc_motor(int motor)
{
	int unit = ft_drive_sel;
	int data = unit | FDC_RESET_NOT | FDC_DMA_MODE;
	TRACE_FUN(ft_t_any);

	ftape_motor = motor;
	if (ftape_motor) {
		data |= FDC_MOTOR_0 << unit;
		TRACE(ft_t_noise, "turning motor %d on", unit);
	} else {
		TRACE(ft_t_noise, "turning motor %d off", unit);
	}
	if (ft_mach2) {
		outb_p(data, fdc.dor2);
	} else {
		outb_p(data, fdc.dor);
	}
	ftape_sleep(10 * FT_MILLISECOND);
	TRACE_EXIT;
}

static void fdc_update_dsr(void)
{
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_flow, "rate = %d Kbps, precomp = %d ns",
	      fdc_data_rate, fdc_precomp);
	if (fdc.type >= i82077) {
		outb_p((fdc_rate_code & 0x03) | fdc_prec_code, fdc.dsr);
	} else {
		outb_p(fdc_rate_code & 0x03, fdc.ccr);
	}
	TRACE_EXIT;
}

void fdc_set_write_precomp(int precomp)
{
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_noise, "New precomp: %d nsec", precomp);
	fdc_precomp = precomp;
	/*  write precompensation can be set in multiples of 41.67 nsec.
	 *  round the parameter to the nearest multiple and convert it
	 *  into a fdc setting. Note that 0 means default to the fdc,
	 *  7 is used instead of that.
	 */
	fdc_prec_code = ((fdc_precomp + 21) / 42) << 2;
	if (fdc_prec_code == 0 || fdc_prec_code > (6 << 2)) {
		fdc_prec_code = 7 << 2;
	}
	fdc_update_dsr();
	TRACE_EXIT;
}

/*  Reprogram the 82078 registers to use Data Rate Table 1 on all drives.
 */
static void fdc_set_drive_specs(void)
{
	__u8 cmd[] = { FDC_DRIVE_SPEC, 0x00, 0x00, 0x00, 0x00, 0xc0};
	int result;
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_flow, "Setting of drive specs called");
	if (fdc.type >= i82078_1) {
		cmd[1] = (0 << 5) | (2 << 2);
		cmd[2] = (1 << 5) | (2 << 2);
		cmd[3] = (2 << 5) | (2 << 2);
		cmd[4] = (3 << 5) | (2 << 2);
		result = fdc_command(cmd, NR_ITEMS(cmd));
		if (result < 0) {
			TRACE(ft_t_err, "Setting of drive specs failed");
		}
	}
	TRACE_EXIT;
}

/* Select clock for fdc, must correspond with tape drive setting !
 * This also influences the fdc timing so we must adjust some values.
 */
int fdc_set_data_rate(int rate)
{
	int bad_rate = 0;
	TRACE_FUN(ft_t_any);

	/* Select clock for fdc, must correspond with tape drive setting !
	 * This also influences the fdc timing so we must adjust some values.
	 */
	TRACE(ft_t_fdc_dma, "new rate = %d", rate);
	switch (rate) {
	case 250:
		fdc_rate_code = fdc_data_rate_250;
		break;
	case 500:
		fdc_rate_code = fdc_data_rate_500;
		break;
	case 1000:
		if (fdc.type < i82077) {
			bad_rate = 1;
                } else {
			fdc_rate_code = fdc_data_rate_1000;
		}
		break;
	case 2000:
		if (fdc.type < i82078_1) {
			bad_rate = 1;
                } else {
			fdc_rate_code = fdc_data_rate_2000;
		}
		break;
	default:
		bad_rate = 1;
        }
	if (bad_rate) {
		TRACE_ABORT(-EIO,
			    ft_t_fdc_dma, "%d is not a valid data rate", rate);
	}
	fdc_data_rate = rate;
	fdc_update_dsr();
	fdc_set_seek_rate(fdc_seek_rate);  /* clock changed! */
	ftape_udelay(1000);
	TRACE_EXIT 0;
}

/*  keep the unit select if keep_select is != 0,
 */
static void fdc_dor_reset(int keep_select)
{
	__u8 fdc_ctl = ft_drive_sel;

	if (keep_select != 0) {
		fdc_ctl |= FDC_DMA_MODE;
		if (ftape_motor) {
			fdc_ctl |= FDC_MOTOR_0 << ft_drive_sel;
		}
	}
	ftape_udelay(10); /* ??? but seems to be necessary */
	if (ft_mach2) {
		outb_p(fdc_ctl & 0x0f, fdc.dor);
		outb_p(fdc_ctl, fdc.dor2);
	} else {
		outb_p(fdc_ctl, fdc.dor);
	}
	fdc_usec_wait(10); /* delay >= 14 fdc clocks */
	if (keep_select == 0) {
		fdc_ctl = 0;
	}
	fdc_ctl |= FDC_RESET_NOT;
	if (ft_mach2) {
		outb_p(fdc_ctl & 0x0f, fdc.dor);
		outb_p(fdc_ctl, fdc.dor2);
	} else {
		outb_p(fdc_ctl, fdc.dor);
	}
}

/*      Reset the floppy disk controller. Leave the ftape_unit selected.
 */
void fdc_reset(void)
{
	int st0;
	int i;
	int dummy;
	unsigned long flags;
	TRACE_FUN(ft_t_any);

	spin_lock_irqsave(&fdc_io_lock, flags);

	fdc_dor_reset(1); /* keep unit selected */

	fdc_mode = fdc_idle;

	/*  maybe the spin_lock_irq* pair is not necessary, BUT:
	 *  the following line MUST be here. Otherwise fdc_interrupt_wait()
	 *  won't wait. Note that fdc_reset() is called from 
	 *  ftape_dumb_stop() when the fdc is busy transferring data. In this
	 *  case fdc_isr() MOST PROBABLY sets ft_interrupt_seen, and tries
	 *  to get the result bytes from the fdc etc. CLASH.
	 */
	ft_interrupt_seen = 0;
	
	/*  Program data rate
	 */
	fdc_update_dsr();               /* restore data rate and precomp */

	spin_unlock_irqrestore(&fdc_io_lock, flags);

        /*
         *	Wait for first polling cycle to complete
	 */
	if (fdc_interrupt_wait(1 * FT_SECOND) < 0) {
		TRACE(ft_t_err, "no drive polling interrupt!");
	} else {	/* clear all disk-changed statuses */
		for (i = 0; i < 4; ++i) {
			if(fdc_sense_interrupt_status(&st0, &dummy) != 0) {
				TRACE(ft_t_err, "sense failed for %d", i);
			}
			if (i == ft_drive_sel) {
				ftape_current_cylinder = dummy;
			}
		}
		TRACE(ft_t_noise, "drive polling completed");
	}
	/*
         *	SPECIFY COMMAND
	 */
	fdc_set_seek_rate(fdc_seek_rate);
	/*
	 *	DRIVE SPECIFICATION COMMAND (if fdc type known)
	 */
	if (fdc.type >= i82078_1) {
		fdc_set_drive_specs();
	}
	TRACE_EXIT;
}

#if !defined(CLK_48MHZ)
# define CLK_48MHZ 1
#endif

/*  When we're done, put the fdc into reset mode so that the regular
 *  floppy disk driver will figure out that something is wrong and
 *  initialize the controller the way it wants.
 */
void fdc_disable(void)
{
	__u8 cmd1[] = {FDC_CONFIGURE, 0x00, 0x00, 0x00};
	__u8 cmd2[] = {FDC_LOCK};
	__u8 cmd3[] = {FDC_UNLOCK};
	__u8 stat[1];
	TRACE_FUN(ft_t_flow);

	if (!fdc_fifo_locked) {
		fdc_reset();
		TRACE_EXIT;
	}
	if (fdc_issue_command(cmd3, 1, stat, 1) < 0 || stat[0] != 0x00) {
		fdc_dor_reset(0);
		TRACE_ABORT(/**/, ft_t_bug, 
		"couldn't unlock fifo, configuration remains changed");
	}
	fdc_fifo_locked = 0;
	if (CLK_48MHZ && fdc.type >= i82078) {
		cmd1[0] |= FDC_CLK48_BIT;
	}
	cmd1[2] = ((fdc_fifo_state) ? 0 : 0x20) + (fdc_fifo_thr - 1);
	if (fdc_command(cmd1, NR_ITEMS(cmd1)) < 0) {
		fdc_dor_reset(0);
		TRACE_ABORT(/**/, ft_t_bug,
		"couldn't reconfigure fifo to old state");
	}
	if (fdc_lock_state &&
	    fdc_issue_command(cmd2, 1, stat, 1) < 0) {
		fdc_dor_reset(0);
		TRACE_ABORT(/**/, ft_t_bug, "couldn't lock old state again");
	}
	TRACE(ft_t_noise, "fifo restored: %sabled, thr. %d, %slocked",
	      fdc_fifo_state ? "en" : "dis",
	      fdc_fifo_thr, (fdc_lock_state) ? "" : "not ");
	fdc_dor_reset(0);
	TRACE_EXIT;
}

/*      Specify FDC seek-rate (milliseconds)
 */
static int fdc_set_seek_rate(int seek_rate)
{
	/* set step rate, dma mode, and minimal head load and unload times
	 */
	__u8 in[3] = { FDC_SPECIFY, 1, (1 << 1)};
 
	fdc_seek_rate = seek_rate;
	in[1] |= (16 - (fdc_data_rate * fdc_seek_rate) / 500) << 4;

	return fdc_command(in, 3);
}

/*      Sense drive status: get unit's drive status (ST3)
 */
int fdc_sense_drive_status(int *st3)
{
	__u8 out[2];
	__u8 in[1];
	TRACE_FUN(ft_t_any);

	out[0] = FDC_SENSED;
	out[1] = ft_drive_sel;
	TRACE_CATCH(fdc_issue_command(out, 2, in, 1),);
	*st3 = in[0];
	TRACE_EXIT 0;
}

/*      Sense Interrupt Status command:
 *      should be issued at the end of each seek.
 *      get ST0 and current cylinder.
 */
int fdc_sense_interrupt_status(int *st0, int *current_cylinder)
{
	__u8 out[1];
	__u8 in[2];
	TRACE_FUN(ft_t_any);

	out[0] = FDC_SENSEI;
	TRACE_CATCH(fdc_issue_command(out, 1, in, 2),);
	*st0 = in[0];
	*current_cylinder = in[1];
	TRACE_EXIT 0;
}

/*      step to track
 */
int fdc_seek(int track)
{
	__u8 out[3];
	int st0, pcn;
#ifdef TESTING
	unsigned int time;
#endif
	TRACE_FUN(ft_t_any);

	out[0] = FDC_SEEK;
	out[1] = ft_drive_sel;
	out[2] = track;
#ifdef TESTING
	time = ftape_timestamp();
#endif
	/*  We really need this command to work !
	 */
	ft_seek_completed = 0;
	TRACE_CATCH(fdc_command(out, 3),
		    fdc_reset();
		    TRACE(ft_t_noise, "destination was: %d, resetting FDC...",
			  track));
	/*    Handle interrupts until ft_seek_completed or timeout.
	 */
	for (;;) {
		TRACE_CATCH(fdc_interrupt_wait(2 * FT_SECOND),);
		if (ft_seek_completed) {
			TRACE_CATCH(fdc_sense_interrupt_status(&st0, &pcn),);
			if ((st0 & ST0_SEEK_END) == 0) {
				TRACE_ABORT(-EIO, ft_t_err,
				      "no seek-end after seek completion !??");
			}
			break;
		}
	}
#ifdef TESTING
	time = ftape_timediff(time, ftape_timestamp()) / abs(track - ftape_current_cylinder);
	if ((time < 900 || time > 3100) && abs(track - ftape_current_cylinder) > 5) {
		TRACE(ft_t_warn, "Wrong FDC STEP interval: %d usecs (%d)",
                         time, track - ftape_current_cylinder);
	}
#endif
	/*    Verify whether we issued the right tape command.
	 */
	/* Verify that we seek to the proper track. */
	if (pcn != track) {
		TRACE_ABORT(-EIO, ft_t_err, "bad seek..");
	}
	ftape_current_cylinder = track;
	TRACE_EXIT 0;
}

static int perpend_mode; /* set if fdc is in perpendicular mode */

static int perpend_off(void)
{
 	__u8 perpend[] = {FDC_PERPEND, 0x00};
	TRACE_FUN(ft_t_any);
	
	if (perpend_mode) {
		/* Turn off perpendicular mode */
		perpend[1] = 0x80;
		TRACE_CATCH(fdc_command(perpend, 2),
			    TRACE(ft_t_err,"Perpendicular mode exit failed!"));
		perpend_mode = 0;
	}
	TRACE_EXIT 0;
}

static int handle_perpend(int segment_id)
{
 	__u8 perpend[] = {FDC_PERPEND, 0x00};
	TRACE_FUN(ft_t_any);

	/* When writing QIC-3020 tapes, turn on perpendicular mode
	 * if tape is moving in forward direction (even tracks).
	 */
	if (ft_qic_std == QIC_TAPE_QIC3020 &&
	    ((segment_id / ft_segments_per_track) & 1) == 0) {
/*  FIXME: some i82077 seem to support perpendicular mode as
 *  well. 
 */
#if 0
		if (fdc.type < i82077AA) {}
#else
		if (fdc.type < i82077 && ft_data_rate < 1000) {
#endif
			/*  fdc does not support perpendicular mode: complain 
			 */
			TRACE_ABORT(-EIO, ft_t_err,
				    "Your FDC does not support QIC-3020.");
		}
		perpend[1] = 0x03 /* 0x83 + (0x4 << ft_drive_sel) */ ;
		TRACE_CATCH(fdc_command(perpend, 2),
			   TRACE(ft_t_err,"Perpendicular mode entry failed!"));
		TRACE(ft_t_flow, "Perpendicular mode set");
		perpend_mode = 1;
		TRACE_EXIT 0;
	}
	TRACE_EXIT perpend_off();
}

static inline void fdc_setup_dma(char mode,
				 volatile void *addr, unsigned int count)
{
	/* Program the DMA controller.
	 */
	disable_dma(fdc.dma);
	clear_dma_ff(fdc.dma);
	set_dma_mode(fdc.dma, mode);
	set_dma_addr(fdc.dma, virt_to_bus((void*)addr));
	set_dma_count(fdc.dma, count);
	enable_dma(fdc.dma);
}

/*  Setup fdc and dma for formatting the next segment
 */
int fdc_setup_formatting(buffer_struct * buff)
{
	unsigned long flags;
	__u8 out[6] = {
		FDC_FORMAT, 0x00, 3, 4 * FT_SECTORS_PER_SEGMENT, 0x00, 0x6b
	};
	TRACE_FUN(ft_t_any);
	
	TRACE_CATCH(handle_perpend(buff->segment_id),);
	/* Program the DMA controller.
	 */
        TRACE(ft_t_fdc_dma,
	      "phys. addr. = %lx", virt_to_bus((void*) buff->ptr));
	spin_lock_irqsave(&fdc_io_lock, flags);
	fdc_setup_dma(DMA_MODE_WRITE, buff->ptr, FT_SECTORS_PER_SEGMENT * 4);
	/* Issue FDC command to start reading/writing.
	 */
	out[1] = ft_drive_sel;
	out[4] = buff->gap3;
	TRACE_CATCH(fdc_setup_error = fdc_command(out, sizeof(out)),
		    restore_flags(flags); fdc_mode = fdc_idle);
	spin_unlock_irqrestore(&fdc_io_lock, flags);
	TRACE_EXIT 0;
}


/*      Setup Floppy Disk Controller and DMA to read or write the next cluster
 *      of good sectors from or to the current segment.
 */
int fdc_setup_read_write(buffer_struct * buff, __u8 operation)
{
	unsigned long flags;
	__u8 out[9];
	int dma_mode;
	TRACE_FUN(ft_t_any);

	switch(operation) {
	case FDC_VERIFY:
		if (fdc.type < i82077) {
			operation = FDC_READ;
		}
	case FDC_READ:
	case FDC_READ_DELETED:
		dma_mode = DMA_MODE_READ;
		TRACE(ft_t_fdc_dma, "xfer %d sectors to 0x%p",
		      buff->sector_count, buff->ptr);
		TRACE_CATCH(perpend_off(),);
		break;
	case FDC_WRITE_DELETED:
		TRACE(ft_t_noise, "deleting segment %d", buff->segment_id);
	case FDC_WRITE:
		dma_mode = DMA_MODE_WRITE;
		/* When writing QIC-3020 tapes, turn on perpendicular mode
		 * if tape is moving in forward direction (even tracks).
		 */
		TRACE_CATCH(handle_perpend(buff->segment_id),);
		TRACE(ft_t_fdc_dma, "xfer %d sectors from 0x%p",
		      buff->sector_count, buff->ptr);
		break;
	default:
		TRACE_ABORT(-EIO,
			    ft_t_bug, "bug: invalid operation parameter");
	}
	TRACE(ft_t_fdc_dma, "phys. addr. = %lx",virt_to_bus((void*)buff->ptr));
	spin_lock_irqsave(&fdc_io_lock, flags);
	if (operation != FDC_VERIFY) {
		fdc_setup_dma(dma_mode, buff->ptr,
			      FT_SECTOR_SIZE * buff->sector_count);
	}
	/* Issue FDC command to start reading/writing.
	 */
	out[0] = operation;
	out[1] = ft_drive_sel;
	out[2] = buff->cyl;
	out[3] = buff->head;
	out[4] = buff->sect + buff->sector_offset;
	out[5] = 3;		/* Sector size of 1K. */
	out[6] = out[4] + buff->sector_count - 1;	/* last sector */
	out[7] = 109;		/* Gap length. */
	out[8] = 0xff;		/* No limit to transfer size. */
	TRACE(ft_t_fdc_dma, "C: 0x%02x, H: 0x%02x, R: 0x%02x, cnt: 0x%02x",
		out[2], out[3], out[4], out[6] - out[4] + 1);
	spin_unlock_irqrestore(&fdc_io_lock, flags);
	TRACE_CATCH(fdc_setup_error = fdc_command(out, 9),fdc_mode = fdc_idle);
	TRACE_EXIT 0;
}

int fdc_fifo_threshold(__u8 threshold,
		       int *fifo_state, int *lock_state, int *fifo_thr)
{
	const __u8 cmd0[] = {FDC_DUMPREGS};
	__u8 cmd1[] = {FDC_CONFIGURE, 0, (0x0f & (threshold - 1)), 0};
	const __u8 cmd2[] = {FDC_LOCK};
	const __u8 cmd3[] = {FDC_UNLOCK};
	__u8 reg[10];
	__u8 stat;
	int i;
	int result;
	TRACE_FUN(ft_t_any);

	if (CLK_48MHZ && fdc.type >= i82078) {
		cmd1[0] |= FDC_CLK48_BIT;
	}
	/*  Dump fdc internal registers for examination
	 */
	TRACE_CATCH(fdc_command(cmd0, NR_ITEMS(cmd0)),
		    TRACE(ft_t_warn, "dumpreg cmd failed, fifo unchanged"));
	/*  Now read fdc internal registers from fifo
	 */
	for (i = 0; i < (int)NR_ITEMS(reg); ++i) {
		fdc_read(&reg[i]);
		TRACE(ft_t_fdc_dma, "Register %d = 0x%02x", i, reg[i]);
	}
	if (fifo_state && lock_state && fifo_thr) {
		*fifo_state = (reg[8] & 0x20) == 0;
		*lock_state = reg[7] & 0x80;
		*fifo_thr = 1 + (reg[8] & 0x0f);
	}
	TRACE(ft_t_noise,
	      "original fifo state: %sabled, threshold %d, %slocked",
	      ((reg[8] & 0x20) == 0) ? "en" : "dis",
	      1 + (reg[8] & 0x0f), (reg[7] & 0x80) ? "" : "not ");
	/*  If fdc is already locked, unlock it first ! */
	if (reg[7] & 0x80) {
		fdc_ready_wait(100);
		TRACE_CATCH(fdc_issue_command(cmd3, NR_ITEMS(cmd3), &stat, 1),
			    TRACE(ft_t_bug, "FDC unlock command failed, "
				  "configuration unchanged"));
	}
	fdc_fifo_locked = 0;
	/*  Enable fifo and set threshold at xx bytes to allow a
	 *  reasonably large latency and reduce number of dma bursts.
	 */
	fdc_ready_wait(100);
	if ((result = fdc_command(cmd1, NR_ITEMS(cmd1))) < 0) {
		TRACE(ft_t_bug, "configure cmd failed, fifo unchanged");
	}
	/*  Now lock configuration so reset will not change it
	 */
        if(fdc_issue_command(cmd2, NR_ITEMS(cmd2), &stat, 1) < 0 ||
	   stat != 0x10) {
		TRACE_ABORT(-EIO, ft_t_bug,
			    "FDC lock command failed, stat = 0x%02x", stat);
	}
	fdc_fifo_locked = 1;
	TRACE_EXIT result;
}

static int fdc_fifo_enable(void)
{
	TRACE_FUN(ft_t_any);

	if (fdc_fifo_locked) {
		TRACE_ABORT(0, ft_t_warn, "Fifo not enabled because locked");
	}
	TRACE_CATCH(fdc_fifo_threshold(ft_fdc_threshold /* bytes */,
				       &fdc_fifo_state,
				       &fdc_lock_state,
				       &fdc_fifo_thr),);
	TRACE_CATCH(fdc_fifo_threshold(ft_fdc_threshold /* bytes */,
				       NULL, NULL, NULL),);
	TRACE_EXIT 0;
}

/*   Determine fd controller type 
 */
static __u8 fdc_save_state[2];

static int fdc_probe(void)
{
	__u8 cmd[1];
	__u8 stat[16]; /* must be able to hold dumpregs & save results */
	int i;
	TRACE_FUN(ft_t_any);

	/*  Try to find out what kind of fd controller we have to deal with
	 *  Scheme borrowed from floppy driver:
	 *  first try if FDC_DUMPREGS command works
	 *  (this indicates that we have a 82072 or better)
	 *  then try the FDC_VERSION command (82072 doesn't support this)
	 *  then try the FDC_UNLOCK command (some older 82077's don't support this)
	 *  then try the FDC_PARTID command (82078's support this)
	 */
	cmd[0] = FDC_DUMPREGS;
	if (fdc_issue_command(cmd, 1, stat, 1) != 0) {
		TRACE_ABORT(no_fdc, ft_t_bug, "No FDC found");
	}
	if (stat[0] == 0x80) {
		/* invalid command: must be pre 82072 */
		TRACE_ABORT(i8272,
			    ft_t_warn, "Type 8272A/765A compatible FDC found");
	}
	fdc_result(&stat[1], 9);
	fdc_save_state[0] = stat[7];
	fdc_save_state[1] = stat[8];
	cmd[0] = FDC_VERSION;
	if (fdc_issue_command(cmd, 1, stat, 1) < 0 || stat[0] == 0x80) {
		TRACE_ABORT(i8272, ft_t_warn, "Type 82072 FDC found");
	}
	if (*stat != 0x90) {
		TRACE_ABORT(i8272, ft_t_warn, "Unknown FDC found");
	}
	cmd[0] = FDC_UNLOCK;
	if(fdc_issue_command(cmd, 1, stat, 1) < 0 || stat[0] != 0x00) {
		TRACE_ABORT(i8272, ft_t_warn,
			    "Type pre-1991 82077 FDC found, "
			    "treating it like a 82072");
	}
	if (fdc_save_state[0] & 0x80) { /* was locked */
		cmd[0] = FDC_LOCK; /* restore lock */
		(void)fdc_issue_command(cmd, 1, stat, 1);
		TRACE(ft_t_warn, "FDC is already locked");
	}
	/* Test for a i82078 FDC */
	cmd[0] = FDC_PARTID;
	if (fdc_issue_command(cmd, 1, stat, 1) < 0 || stat[0] == 0x80) {
		/* invalid command: not a i82078xx type FDC */
		for (i = 0; i < 4; ++i) {
			outb_p(i, fdc.tdr);
			if ((inb_p(fdc.tdr) & 0x03) != i) {
				TRACE_ABORT(i82077,
					    ft_t_warn, "Type 82077 FDC found");
			}
		}
		TRACE_ABORT(i82077AA, ft_t_warn, "Type 82077AA FDC found");
	}
	/* FDC_PARTID cmd succeeded */
	switch (stat[0] >> 5) {
	case 0x0:
		/* i82078SL or i82078-1.  The SL part cannot run at
		 * 2Mbps (the SL and -1 dies are identical; they are
		 * speed graded after production, according to Intel).
		 * Some SL's can be detected by doing a SAVE cmd and
		 * look at bit 7 of the first byte (the SEL3V# bit).
		 * If it is 0, the part runs off 3Volts, and hence it
		 * is a SL.
		 */
		cmd[0] = FDC_SAVE;
		if(fdc_issue_command(cmd, 1, stat, 16) < 0) {
			TRACE(ft_t_err, "FDC_SAVE failed. Dunno why");
			/* guess we better claim the fdc to be a i82078 */
			TRACE_ABORT(i82078,
				    ft_t_warn,
				    "Type i82078 FDC (i suppose) found");
		}
		if ((stat[0] & FDC_SEL3V_BIT)) {
			/* fdc running off 5Volts; Pray that it's a i82078-1
			 */
			TRACE_ABORT(i82078_1, ft_t_warn,
				  "Type i82078-1 or 5Volt i82078SL FDC found");
		}
		TRACE_ABORT(i82078, ft_t_warn,
			    "Type 3Volt i82078SL FDC (1Mbps) found");
	case 0x1:
	case 0x2: /* S82078B  */
		/* The '78B  isn't '78 compatible.  Detect it as a '77AA */
		TRACE_ABORT(i82077AA, ft_t_warn, "Type i82077AA FDC found");
	case 0x3: /* NSC PC8744 core; used in several super-IO chips */
		TRACE_ABORT(i82077AA,
			    ft_t_warn, "Type 82077AA compatible FDC found");
	default:
		TRACE(ft_t_warn, "A previously undetected FDC found");
		TRACE_ABORT(i82077AA, ft_t_warn,
			  "Treating it as a 82077AA. Please report partid= %d",
			    stat[0]);
	} /* switch(stat[ 0] >> 5) */
	TRACE_EXIT no_fdc;
}

static int fdc_request_regions(void)
{
	TRACE_FUN(ft_t_flow);

	if (ft_mach2 || ft_probe_fc10) {
		if (!request_region(fdc.sra, 8, "fdc (ft)")) {
#ifndef BROKEN_FLOPPY_DRIVER
			TRACE_EXIT -EBUSY;
#else
			TRACE(ft_t_warn,
"address 0x%03x occupied (by floppy driver?), using it anyway", fdc.sra);
#endif
		}
	} else {
		if (!request_region(fdc.sra, 6, "fdc (ft)")) {
#ifndef BROKEN_FLOPPY_DRIVER
			TRACE_EXIT -EBUSY;
#else
			TRACE(ft_t_warn,
"address 0x%03x occupied (by floppy driver?), using it anyway", fdc.sra);
#endif
		}
		if (!request_region(fdc.sra + 7, 1, "fdc (ft)")) {
#ifndef BROKEN_FLOPPY_DRIVER
			release_region(fdc.sra, 6);
			TRACE_EXIT -EBUSY;
#else
			TRACE(ft_t_warn,
"address 0x%03x occupied (by floppy driver?), using it anyway", fdc.sra + 7);
#endif
		}
	}
	TRACE_EXIT 0;
}

void fdc_release_regions(void)
{
	TRACE_FUN(ft_t_flow);

	if (fdc.sra != 0) {
		if (fdc.dor2 != 0) {
			release_region(fdc.sra, 8);
		} else {
			release_region(fdc.sra, 6);
			release_region(fdc.dir, 1);
		}
	}
	TRACE_EXIT;
}

static int fdc_config_regs(unsigned int fdc_base, 
			   unsigned int fdc_irq, 
			   unsigned int fdc_dma)
{
	TRACE_FUN(ft_t_flow);

	fdc.irq = fdc_irq;
	fdc.dma = fdc_dma;
	fdc.sra = fdc_base;
	fdc.srb = fdc_base + 1;
	fdc.dor = fdc_base + 2;
	fdc.tdr = fdc_base + 3;
	fdc.msr = fdc.dsr = fdc_base + 4;
	fdc.fifo = fdc_base + 5;
	fdc.dir = fdc.ccr = fdc_base + 7;
	fdc.dor2 = (ft_mach2 || ft_probe_fc10) ? fdc_base + 6 : 0;
	TRACE_CATCH(fdc_request_regions(), fdc.sra = 0);
	TRACE_EXIT 0;
}

static int fdc_config(void)
{
	static int already_done;
	TRACE_FUN(ft_t_any);

	if (already_done) {
		TRACE_CATCH(fdc_request_regions(),);
		*(fdc.hook) = fdc_isr;	/* hook our handler in */
		TRACE_EXIT 0;
	}
	if (ft_probe_fc10) {
		int fc_type;
		
		TRACE_CATCH(fdc_config_regs(ft_fdc_base,
					    ft_fdc_irq, ft_fdc_dma),);
		fc_type = fc10_enable();
		if (fc_type != 0) {
			TRACE(ft_t_warn, "FC-%c0 controller found", '0' + fc_type);
			fdc.type = fc10;
			fdc.hook = &do_ftape;
			*(fdc.hook) = fdc_isr;	/* hook our handler in */
			already_done = 1;
			TRACE_EXIT 0;
		} else {
			TRACE(ft_t_warn, "FC-10/20 controller not found");
			fdc_release_regions();
			fdc.type = no_fdc;
			ft_probe_fc10 = 0;
			ft_fdc_base   = 0x3f0;
			ft_fdc_irq    = 6;
			ft_fdc_dma    = 2;
		}
	}
	TRACE(ft_t_warn, "fdc base: 0x%x, irq: %d, dma: %d", 
	      ft_fdc_base, ft_fdc_irq, ft_fdc_dma);
	TRACE_CATCH(fdc_config_regs(ft_fdc_base, ft_fdc_irq, ft_fdc_dma),);
	fdc.hook = &do_ftape;
	*(fdc.hook) = fdc_isr;	/* hook our handler in */
	already_done = 1;
	TRACE_EXIT 0;
}

static irqreturn_t ftape_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	void (*handler) (void) = *fdc.hook;
	int handled = 0;
	TRACE_FUN(ft_t_any);

	*fdc.hook = NULL;
	if (handler) {
		handled = 1;
		handler();
	} else {
		TRACE(ft_t_bug, "Unexpected ftape interrupt");
	}
	TRACE_EXIT IRQ_RETVAL(handled);
}

static int fdc_grab_irq_and_dma(void)
{
	TRACE_FUN(ft_t_any);

	if (fdc.hook == &do_ftape) {
		/*  Get fast interrupt handler.
		 */
		if (request_irq(fdc.irq, ftape_interrupt,
				SA_INTERRUPT, "ft", ftape_id)) {
			TRACE_ABORT(-EIO, ft_t_bug,
				    "Unable to grab IRQ%d for ftape driver",
				    fdc.irq);
		}
		if (request_dma(fdc.dma, ftape_id)) {
			free_irq(fdc.irq, ftape_id);
			TRACE_ABORT(-EIO, ft_t_bug,
			      "Unable to grab DMA%d for ftape driver",
			      fdc.dma);
		}
	}
	if (ft_fdc_base != 0x3f0 && (ft_fdc_dma == 2 || ft_fdc_irq == 6)) {
		/* Using same dma channel or irq as standard fdc, need
		 * to disable the dma-gate on the std fdc. This
		 * couldn't be done in the floppy driver as some
		 * laptops are using the dma-gate to enter a low power
		 * or even suspended state :-(
		 */
		outb_p(FDC_RESET_NOT, 0x3f2);
		TRACE(ft_t_noise, "DMA-gate on standard fdc disabled");
	}
	TRACE_EXIT 0;
}

int fdc_release_irq_and_dma(void)
{
	TRACE_FUN(ft_t_any);

	if (fdc.hook == &do_ftape) {
		disable_dma(fdc.dma);	/* just in case... */
		free_dma(fdc.dma);
		free_irq(fdc.irq, ftape_id);
	}
	if (ft_fdc_base != 0x3f0 && (ft_fdc_dma == 2 || ft_fdc_irq == 6)) {
		/* Using same dma channel as standard fdc, need to
		 * disable the dma-gate on the std fdc. This couldn't
		 * be done in the floppy driver as some laptops are
		 * using the dma-gate to enter a low power or even
		 * suspended state :-(
		 */
		outb_p(FDC_RESET_NOT | FDC_DMA_MODE, 0x3f2);
		TRACE(ft_t_noise, "DMA-gate on standard fdc enabled again");
	}
	TRACE_EXIT 0;
}

int fdc_init(void)
{
	TRACE_FUN(ft_t_any);

	/* find a FDC to use */
	TRACE_CATCH(fdc_config(),);
	TRACE_CATCH(fdc_grab_irq_and_dma(), fdc_release_regions());
	ftape_motor = 0;
	fdc_catch_stray_interrupts(0);	/* clear number of awainted
					 * stray interrupte 
					 */
	fdc_catch_stray_interrupts(1);	/* one always comes (?) */
	TRACE(ft_t_flow, "resetting fdc");
	fdc_set_seek_rate(2);		/* use nominal QIC step rate */
	fdc_reset();			/* init fdc & clear track counters */
	if (fdc.type == no_fdc) {	/* no FC-10 or FC-20 found */
		fdc.type = fdc_probe();
		fdc_reset();		/* update with new knowledge */
	}
	if (fdc.type == no_fdc) {
		fdc_release_irq_and_dma();
		fdc_release_regions();
		TRACE_EXIT -ENXIO;
	}
	if (fdc.type >= i82077) {
		if (fdc_fifo_enable() < 0) {
			TRACE(ft_t_warn, "couldn't enable fdc fifo !");
		} else {
			TRACE(ft_t_flow, "fdc fifo enabled and locked");
		}
	}
	TRACE_EXIT 0;
}
