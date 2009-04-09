/*
    comedi/drivers/comedi_rt_timer.c
    virtual driver for using RTL timing sources

    Authors: David A. Schleef, Frank M. Hess

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1999,2001 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

**************************************************************************
*/
/*
Driver: comedi_rt_timer
Description: Command emulator using real-time tasks
Author: ds, fmhess
Devices:
Status: works

This driver requires RTAI or RTLinux to work correctly.  It doesn't
actually drive hardware directly, but calls other drivers and uses
a real-time task to emulate commands for drivers and devices that
are incapable of native commands.  Thus, you can get accurately
timed I/O on any device.

Since the timing is all done in software, sampling jitter is much
higher than with a device that has an on-board timer, and maximum
sample rate is much lower.

Configuration options:
  [0] - minor number of device you wish to emulate commands for
  [1] - subdevice number you wish to emulate commands for
*/
/*
TODO:
	Support for digital io commands could be added, except I can't see why
		anyone would want to use them
	What happens if device we are emulating for is de-configured?
*/

#include "../comedidev.h"
#include "../comedilib.h"

#include "comedi_fc.h"

#ifdef CONFIG_COMEDI_RTL_V1
#include <rtl_sched.h>
#include <asm/rt_irq.h>
#endif
#ifdef CONFIG_COMEDI_RTL
#include <rtl.h>
#include <rtl_sched.h>
#include <rtl_compat.h>
#include <asm/div64.h>

#ifndef RTLINUX_VERSION_CODE
#define RTLINUX_VERSION_CODE 0
#endif
#ifndef RTLINUX_VERSION
#define RTLINUX_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

/* begin hack to workaround broken HRT_TO_8254() function on rtlinux */
#if RTLINUX_VERSION_CODE <= RTLINUX_VERSION(3, 0, 100)
/* this function sole purpose is to divide a long long by 838 */
static inline RTIME nano2count(long long ns)
{
	do_div(ns, 838);
	return ns;
}

#ifdef rt_get_time()
#undef rt_get_time()
#endif
#define rt_get_time() nano2count(gethrtime())

#else

#define nano2count(x) HRT_TO_8254(x)
#endif
/* end hack */

/* rtl-rtai compatibility */
#define rt_task_wait_period() rt_task_wait()
#define rt_pend_linux_srq(irq) rtl_global_pend_irq(irq)
#define rt_free_srq(irq) rtl_free_soft_irq(irq)
#define rt_request_srq(x, y, z) rtl_get_soft_irq(y, "timer")
#define rt_task_init(a, b, c, d, e, f, g) rt_task_init(a, b, c, d, (e)+1)
#define rt_task_resume(x) rt_task_wakeup(x)
#define rt_set_oneshot_mode()
#define start_rt_timer(x)
#define stop_rt_timer()

#define comedi_rt_task_context_t	int

#endif
#ifdef CONFIG_COMEDI_RTAI
#include <rtai.h>
#include <rtai_sched.h>
#include <rtai_version.h>

/* RTAI_VERSION_CODE doesn't work for rtai-3.6-cv and other strange versions.
 * These are characterized by CONFIG_RTAI_REVISION_LEVEL being defined as an
 * empty macro and CONFIG_RTAI_VERSION_MINOR being defined as something like
 * '6-cv' or '7-test1'.  The problem has been noted by the RTAI folks and they
 * promise not to do it again. :-) Try and work around it here. */
#if !(CONFIG_RTAI_REVISION_LEVEL + 0)
#undef CONFIG_RTAI_REVISION_LEVEL
#define CONFIG_RTAI_REVISION_LEVEL	0
#define cv	0
#define test1	0
#define test2	0
#define test3	0
#endif

#if RTAI_VERSION_CODE < RTAI_MANGLE_VERSION(3, 3, 0)
#define comedi_rt_task_context_t	int
#else
#define comedi_rt_task_context_t	long
#endif

/* Finished checking RTAI_VERSION_CODE. */
#undef cv
#undef test1
#undef test2
#undef test3

#endif

/* This defines the fastest speed we will emulate.  Note that
 * without a watchdog (like in RTAI), we could easily overrun our
 * task period because analog input tends to be slow. */
#define SPEED_LIMIT 100000	/* in nanoseconds */

static int timer_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int timer_detach(struct comedi_device *dev);
static int timer_inttrig(struct comedi_device *dev, struct comedi_subdevice *s,
	unsigned int trig_num);
static int timer_start_cmd(struct comedi_device *dev, struct comedi_subdevice *s);

static struct comedi_driver driver_timer = {
      module:THIS_MODULE,
      driver_name:"comedi_rt_timer",
      attach:timer_attach,
      detach:timer_detach,
/* open:           timer_open, */
};

COMEDI_INITCLEANUP(driver_timer);

struct timer_private {
	comedi_t *device;	/*  device we are emulating commands for */
	int subd;		/*  subdevice we are emulating commands for */
	RT_TASK *rt_task;	/*  rt task that starts scans */
	RT_TASK *scan_task;	/*  rt task that controls conversion timing in a scan */
	/* io_function can point to either an input or output function
	 * depending on what kind of subdevice we are emulating for */
	int (*io_function) (struct comedi_device *dev, struct comedi_cmd *cmd,
		unsigned int index);
/*
* RTIME has units of 1 = 838 nanoseconds time at which first scan
* started, used to check scan timing
*/
	RTIME start;
	/*  time between scans */
	RTIME scan_period;
	/*  time between conversions in a scan */
	RTIME convert_period;
	/*  flags */
	volatile int stop;	/*  indicates we should stop */
	volatile int rt_task_active;	/*  indicates rt_task is servicing a struct comedi_cmd */
	volatile int scan_task_active;	/*  indicates scan_task is servicing a struct comedi_cmd */
	unsigned timer_running:1;
};
#define devpriv ((struct timer_private *)dev->private)

static int timer_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	devpriv->stop = 1;

	return 0;
}

/* checks for scan timing error */
inline static int check_scan_timing(struct comedi_device *dev,
	unsigned long long scan)
{
	RTIME now, timing_error;

	now = rt_get_time();
	timing_error = now - (devpriv->start + scan * devpriv->scan_period);
	if (timing_error > devpriv->scan_period) {
		comedi_error(dev, "timing error");
		rt_printk("scan started %i ns late\n", timing_error * 838);
		return -1;
	}

	return 0;
}

/* checks for conversion timing error */
inline static int check_conversion_timing(struct comedi_device *dev,
	RTIME scan_start, unsigned int conversion)
{
	RTIME now, timing_error;

	now = rt_get_time();
	timing_error =
		now - (scan_start + conversion * devpriv->convert_period);
	if (timing_error > devpriv->convert_period) {
		comedi_error(dev, "timing error");
		rt_printk("conversion started %i ns late\n",
			timing_error * 838);
		return -1;
	}

	return 0;
}

/* devpriv->io_function for an input subdevice */
static int timer_data_read(struct comedi_device *dev, struct comedi_cmd *cmd,
	unsigned int index)
{
	struct comedi_subdevice *s = dev->read_subdev;
	int ret;
	unsigned int data;

	ret = comedi_data_read(devpriv->device, devpriv->subd,
		CR_CHAN(cmd->chanlist[index]),
		CR_RANGE(cmd->chanlist[index]),
		CR_AREF(cmd->chanlist[index]), &data);
	if (ret < 0) {
		comedi_error(dev, "read error");
		return -EIO;
	}
	if (s->flags & SDF_LSAMPL) {
		cfc_write_long_to_buffer(s, data);
	} else {
		comedi_buf_put(s->async, data);
	}

	return 0;
}

/* devpriv->io_function for an output subdevice */
static int timer_data_write(struct comedi_device *dev, struct comedi_cmd *cmd,
	unsigned int index)
{
	struct comedi_subdevice *s = dev->write_subdev;
	unsigned int num_bytes;
	short data;
	unsigned int long_data;
	int ret;

	if (s->flags & SDF_LSAMPL) {
		num_bytes =
			cfc_read_array_from_buffer(s, &long_data,
			sizeof(long_data));
	} else {
		num_bytes = cfc_read_array_from_buffer(s, &data, sizeof(data));
		long_data = data;
	}

	if (num_bytes == 0) {
		comedi_error(dev, "buffer underrun");
		return -EAGAIN;
	}
	ret = comedi_data_write(devpriv->device, devpriv->subd,
		CR_CHAN(cmd->chanlist[index]),
		CR_RANGE(cmd->chanlist[index]),
		CR_AREF(cmd->chanlist[index]), long_data);
	if (ret < 0) {
		comedi_error(dev, "write error");
		return -EIO;
	}

	return 0;
}

/* devpriv->io_function for DIO subdevices */
static int timer_dio_read(struct comedi_device *dev, struct comedi_cmd *cmd,
	unsigned int index)
{
	struct comedi_subdevice *s = dev->read_subdev;
	int ret;
	unsigned int data;

	ret = comedi_dio_bitfield(devpriv->device, devpriv->subd, 0, &data);
	if (ret < 0) {
		comedi_error(dev, "read error");
		return -EIO;
	}

	if (s->flags & SDF_LSAMPL)
		cfc_write_long_to_buffer(s, data);
	else
		cfc_write_to_buffer(s, data);

	return 0;
}

/* performs scans */
static void scan_task_func(comedi_rt_task_context_t d)
{
	struct comedi_device *dev = (struct comedi_device *) d;
	struct comedi_subdevice *s = dev->subdevices + 0;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	int i, ret;
	unsigned long long n;
	RTIME scan_start;

	/*  every struct comedi_cmd causes one execution of while loop */
	while (1) {
		devpriv->scan_task_active = 1;
		/*  each for loop completes one scan */
		for (n = 0; n < cmd->stop_arg || cmd->stop_src == TRIG_NONE;
			n++) {
			if (n) {
				/*  suspend task until next scan */
				ret = rt_task_suspend(devpriv->scan_task);
				if (ret < 0) {
					comedi_error(dev,
						"error suspending scan task");
					async->events |= COMEDI_CB_ERROR;
					goto cleanup;
				}
			}
			/*  check if stop flag was set (by timer_cancel()) */
			if (devpriv->stop)
				goto cleanup;
			ret = check_scan_timing(dev, n);
			if (ret < 0) {
				async->events |= COMEDI_CB_ERROR;
				goto cleanup;
			}
			scan_start = rt_get_time();
			for (i = 0; i < cmd->scan_end_arg; i++) {
				/*  conversion timing */
				if (cmd->convert_src == TRIG_TIMER && i) {
					rt_task_wait_period();
					ret = check_conversion_timing(dev,
						scan_start, i);
					if (ret < 0) {
						async->events |=
							COMEDI_CB_ERROR;
						goto cleanup;
					}
				}
				ret = devpriv->io_function(dev, cmd, i);
				if (ret < 0) {
					async->events |= COMEDI_CB_ERROR;
					goto cleanup;
				}
			}
			s->async->events |= COMEDI_CB_BLOCK;
			comedi_event(dev, s);
			s->async->events = 0;
		}

	      cleanup:

		comedi_unlock(devpriv->device, devpriv->subd);
		async->events |= COMEDI_CB_EOA;
		comedi_event(dev, s);
		async->events = 0;
		devpriv->scan_task_active = 0;
		/*  suspend task until next struct comedi_cmd */
		rt_task_suspend(devpriv->scan_task);
	}
}

static void timer_task_func(comedi_rt_task_context_t d)
{
	struct comedi_device *dev = (struct comedi_device *) d;
	struct comedi_subdevice *s = dev->subdevices + 0;
	struct comedi_cmd *cmd = &s->async->cmd;
	int ret;
	unsigned long long n;

	/*  every struct comedi_cmd causes one execution of while loop */
	while (1) {
		devpriv->rt_task_active = 1;
		devpriv->scan_task_active = 1;
		devpriv->start = rt_get_time();

		for (n = 0; n < cmd->stop_arg || cmd->stop_src == TRIG_NONE;
			n++) {
			/*  scan timing */
			if (n)
				rt_task_wait_period();
			if (devpriv->scan_task_active == 0) {
				goto cleanup;
			}
			ret = rt_task_make_periodic(devpriv->scan_task,
				devpriv->start + devpriv->scan_period * n,
				devpriv->convert_period);
			if (ret < 0) {
				comedi_error(dev, "bug!");
			}
		}

	      cleanup:

		devpriv->rt_task_active = 0;
		/*  suspend until next struct comedi_cmd */
		rt_task_suspend(devpriv->rt_task);
	}
}

static int timer_insn(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	struct comedi_insn xinsn = *insn;

	xinsn.data = data;
	xinsn.subdev = devpriv->subd;

	return comedi_do_insn(devpriv->device, &xinsn);
}

static int cmdtest_helper(struct comedi_cmd *cmd,
	unsigned int start_src,
	unsigned int scan_begin_src,
	unsigned int convert_src,
	unsigned int scan_end_src, unsigned int stop_src)
{
	int err = 0;
	int tmp;

	tmp = cmd->start_src;
	cmd->start_src &= start_src;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= scan_begin_src;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= convert_src;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= scan_end_src;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= stop_src;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	return err;
}

static int timer_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int start_src = 0;

	if (s->type == COMEDI_SUBD_AO)
		start_src = TRIG_INT;
	else
		start_src = TRIG_NOW;

	err = cmdtest_helper(cmd, start_src,	/* start_src */
		TRIG_TIMER | TRIG_FOLLOW,	/* scan_begin_src */
		TRIG_NOW | TRIG_TIMER,	/* convert_src */
		TRIG_COUNT,	/* scan_end_src */
		TRIG_COUNT | TRIG_NONE);	/* stop_src */
	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique and mutually
	 * compatible */

	if (cmd->start_src != TRIG_NOW && cmd->start_src != TRIG_INT)
		err++;
	if (cmd->scan_begin_src != TRIG_TIMER &&
		cmd->scan_begin_src != TRIG_FOLLOW)
		err++;
	if (cmd->convert_src != TRIG_TIMER && cmd->convert_src != TRIG_NOW)
		err++;
	if (cmd->stop_src != TRIG_COUNT && cmd->stop_src != TRIG_NONE)
		err++;
	if (cmd->scan_begin_src == TRIG_FOLLOW
		&& cmd->convert_src != TRIG_TIMER)
		err++;
	if (cmd->convert_src == TRIG_NOW && cmd->scan_begin_src != TRIG_TIMER)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */
	/*  limit frequency, this is fairly arbitrary */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->scan_begin_arg < SPEED_LIMIT) {
			cmd->scan_begin_arg = SPEED_LIMIT;
			err++;
		}
	}
	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < SPEED_LIMIT) {
			cmd->convert_arg = SPEED_LIMIT;
			err++;
		}
	}
	/*  make sure conversion and scan frequencies are compatible */
	if (cmd->convert_src == TRIG_TIMER && cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->convert_arg * cmd->scan_end_arg > cmd->scan_begin_arg) {
			cmd->scan_begin_arg =
				cmd->convert_arg * cmd->scan_end_arg;
			err++;
		}
	}
	if (err)
		return 3;

	/* step 4: fix up and arguments */
	if (err)
		return 4;

	return 0;
}

static int timer_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	int ret;
	struct comedi_cmd *cmd = &s->async->cmd;

	/* hack attack: drivers are not supposed to do this: */
	dev->rt = 1;

	/*  make sure tasks have finished cleanup of last struct comedi_cmd */
	if (devpriv->rt_task_active || devpriv->scan_task_active)
		return -EBUSY;

	ret = comedi_lock(devpriv->device, devpriv->subd);
	if (ret < 0) {
		comedi_error(dev, "failed to obtain lock");
		return ret;
	}
	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:
		devpriv->scan_period = nano2count(cmd->scan_begin_arg);
		break;
	case TRIG_FOLLOW:
		devpriv->scan_period =
			nano2count(cmd->convert_arg * cmd->scan_end_arg);
		break;
	default:
		comedi_error(dev, "bug setting scan period!");
		return -1;
		break;
	}
	switch (cmd->convert_src) {
	case TRIG_TIMER:
		devpriv->convert_period = nano2count(cmd->convert_arg);
		break;
	case TRIG_NOW:
		devpriv->convert_period = 1;
		break;
	default:
		comedi_error(dev, "bug setting conversion period!");
		return -1;
		break;
	}

	if (cmd->start_src == TRIG_NOW)
		return timer_start_cmd(dev, s);

	s->async->inttrig = timer_inttrig;

	return 0;
}

static int timer_inttrig(struct comedi_device *dev, struct comedi_subdevice *s,
	unsigned int trig_num)
{
	if (trig_num != 0)
		return -EINVAL;

	s->async->inttrig = NULL;

	return timer_start_cmd(dev, s);
}

static int timer_start_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	RTIME now, delay, period;
	int ret;

	devpriv->stop = 0;
	s->async->events = 0;

	if (cmd->start_src == TRIG_NOW)
		delay = nano2count(cmd->start_arg);
	else
		delay = 0;

	now = rt_get_time();
	/* Using 'period' this way gets around some weird bug in gcc-2.95.2
	 * that generates the compile error 'internal error--unrecognizable insn'
	 * when rt_task_make_period() is called (observed with rtlinux-3.1, linux-2.2.19).
	 *  - fmhess */
	period = devpriv->scan_period;
	ret = rt_task_make_periodic(devpriv->rt_task, now + delay, period);
	if (ret < 0) {
		comedi_error(dev, "error starting rt_task");
		return ret;
	}
	return 0;
}

static int timer_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	int ret;
	struct comedi_subdevice *s, *emul_s;
	struct comedi_device *emul_dev;
	/* These should probably be devconfig options[] */
	const int timer_priority = 4;
	const int scan_priority = timer_priority + 1;
	char path[20];

	printk("comedi%d: timer: ", dev->minor);

	dev->board_name = "timer";

	if ((ret = alloc_subdevices(dev, 1)) < 0)
		return ret;
	if ((ret = alloc_private(dev, sizeof(struct timer_private))) < 0)
		return ret;

	sprintf(path, "/dev/comedi%d", it->options[0]);
	devpriv->device = comedi_open(path);
	devpriv->subd = it->options[1];

	printk("emulating commands for minor %i, subdevice %d\n",
		it->options[0], devpriv->subd);

	emul_dev = devpriv->device;
	emul_s = emul_dev->subdevices + devpriv->subd;

	/*  input or output subdevice */
	s = dev->subdevices + 0;
	s->type = emul_s->type;
	s->subdev_flags = emul_s->subdev_flags;	/* SDF_GROUND (to fool check_driver) */
	s->n_chan = emul_s->n_chan;
	s->len_chanlist = 1024;
	s->do_cmd = timer_cmd;
	s->do_cmdtest = timer_cmdtest;
	s->cancel = timer_cancel;
	s->maxdata = emul_s->maxdata;
	s->range_table = emul_s->range_table;
	s->range_table_list = emul_s->range_table_list;
	switch (emul_s->type) {
	case COMEDI_SUBD_AI:
		s->insn_read = timer_insn;
		dev->read_subdev = s;
		s->subdev_flags |= SDF_CMD_READ;
		devpriv->io_function = timer_data_read;
		break;
	case COMEDI_SUBD_AO:
		s->insn_write = timer_insn;
		s->insn_read = timer_insn;
		dev->write_subdev = s;
		s->subdev_flags |= SDF_CMD_WRITE;
		devpriv->io_function = timer_data_write;
		break;
	case COMEDI_SUBD_DIO:
		s->insn_write = timer_insn;
		s->insn_read = timer_insn;
		s->insn_bits = timer_insn;
		dev->read_subdev = s;
		s->subdev_flags |= SDF_CMD_READ;
		devpriv->io_function = timer_dio_read;
		break;
	default:
		comedi_error(dev, "failed to determine subdevice type!");
		return -EINVAL;
	}

	rt_set_oneshot_mode();
	start_rt_timer(1);
	devpriv->timer_running = 1;

	devpriv->rt_task = kzalloc(sizeof(RT_TASK), GFP_KERNEL);

	/*  initialize real-time tasks */
	ret = rt_task_init(devpriv->rt_task, timer_task_func,
		(comedi_rt_task_context_t) dev, 3000, timer_priority, 0, 0);
	if (ret < 0) {
		comedi_error(dev, "error initalizing rt_task");
		kfree(devpriv->rt_task);
		devpriv->rt_task = 0;
		return ret;
	}

	devpriv->scan_task = kzalloc(sizeof(RT_TASK), GFP_KERNEL);

	ret = rt_task_init(devpriv->scan_task, scan_task_func,
		(comedi_rt_task_context_t) dev, 3000, scan_priority, 0, 0);
	if (ret < 0) {
		comedi_error(dev, "error initalizing scan_task");
		kfree(devpriv->scan_task);
		devpriv->scan_task = 0;
		return ret;
	}

	return 1;
}

/* free allocated resources */
static int timer_detach(struct comedi_device *dev)
{
	printk("comedi%d: timer: remove\n", dev->minor);

	if (devpriv) {
		if (devpriv->rt_task) {
			rt_task_delete(devpriv->rt_task);
			kfree(devpriv->rt_task);
		}
		if (devpriv->scan_task) {
			rt_task_delete(devpriv->scan_task);
			kfree(devpriv->scan_task);
		}
		if (devpriv->timer_running)
			stop_rt_timer();
		if (devpriv->device)
			comedi_close(devpriv->device);
	}
	return 0;
}
