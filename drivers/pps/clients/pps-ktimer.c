/*
 * pps-ktimer.c -- kernel timer test client
 *
 *
 * Copyright (C) 2005-2006   Rodolfo Giometti <giometti@linux.it>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/pps_kernel.h>

/*
 * Global variables
 */

static int source;
static struct timer_list ktimer;

/*
 * The kernel timer
 */

static void pps_ktimer_event(unsigned long ptr)
{
	struct timespec __ts;
	struct pps_ktime ts;

	/* First of all we get the time stamp... */
	getnstimeofday(&__ts);

	pr_info("PPS event at %lu\n", jiffies);

	/* ... and translate it to PPS time data struct */
	ts.sec = __ts.tv_sec;
	ts.nsec = __ts.tv_nsec;

	pps_event(source, &ts, PPS_CAPTUREASSERT, NULL);

	mod_timer(&ktimer, jiffies + HZ);
}

/*
 * The echo function
 */

static void pps_ktimer_echo(int source, int event, void *data)
{
	pr_info("echo %s %s for source %d\n",
		event & PPS_CAPTUREASSERT ? "assert" : "",
		event & PPS_CAPTURECLEAR ? "clear" : "",
		source);
}

/*
 * The PPS info struct
 */

static struct pps_source_info pps_ktimer_info = {
	.name		= "ktimer",
	.path		= "",
	.mode		= PPS_CAPTUREASSERT | PPS_OFFSETASSERT |
			  PPS_ECHOASSERT |
			  PPS_CANWAIT | PPS_TSFMT_TSPEC,
	.echo		= pps_ktimer_echo,
	.owner		= THIS_MODULE,
};

/*
 * Module staff
 */

static void __exit pps_ktimer_exit(void)
{
	del_timer_sync(&ktimer);
	pps_unregister_source(source);

	pr_info("ktimer PPS source unregistered\n");
}

static int __init pps_ktimer_init(void)
{
	int ret;

	ret = pps_register_source(&pps_ktimer_info,
				PPS_CAPTUREASSERT | PPS_OFFSETASSERT);
	if (ret < 0) {
		printk(KERN_ERR "cannot register ktimer source\n");
		return ret;
	}
	source = ret;

	setup_timer(&ktimer, pps_ktimer_event, 0);
	mod_timer(&ktimer, jiffies + HZ);

	pr_info("ktimer PPS source registered at %d\n", source);

	return  0;
}

module_init(pps_ktimer_init);
module_exit(pps_ktimer_exit);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("dummy PPS source by using a kernel timer (just for debug)");
MODULE_LICENSE("GPL");
