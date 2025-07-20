// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pps_gen_parport.c -- kernel parallel port PPS signal generator
 *
 * Copyright (C) 2009   Alexander Gordeev <lasaine@lvk.cs.msu.su>
 */


/*
 * TODO:
 * fix issues when realtime clock is adjusted in a leap
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/parport.h>

#define SIGNAL		0
#define NO_SIGNAL	PARPORT_CONTROL_STROBE

/* module parameters */

#define SEND_DELAY_MAX		100000

static unsigned int send_delay = 30000;
MODULE_PARM_DESC(delay,
	"Delay between setting and dropping the signal (ns)");
module_param_named(delay, send_delay, uint, 0);


#define SAFETY_INTERVAL	3000	/* set the hrtimer earlier for safety (ns) */

/* internal per port structure */
struct pps_generator_pp {
	struct pardevice *pardev;	/* parport device */
	struct hrtimer timer;
	long port_write_time;		/* calibrated port write time (ns) */
};

static struct pps_generator_pp device = {
	.pardev = NULL,
};

static int attached;

/* calibrated time between a hrtimer event and the reaction */
static long hrtimer_error = SAFETY_INTERVAL;

/* the kernel hrtimer event */
static enum hrtimer_restart hrtimer_event(struct hrtimer *timer)
{
	struct timespec64 expire_time, ts1, ts2, ts3, dts;
	struct pps_generator_pp *dev;
	struct parport *port;
	long lim, delta;
	unsigned long flags;

	/* We have to disable interrupts here. The idea is to prevent
	 * other interrupts on the same processor to introduce random
	 * lags while polling the clock. ktime_get_real_ts64() takes <1us on
	 * most machines while other interrupt handlers can take much
	 * more potentially.
	 *
	 * NB: approx time with blocked interrupts =
	 * send_delay + 3 * SAFETY_INTERVAL
	 */
	local_irq_save(flags);

	/* first of all we get the time stamp... */
	ktime_get_real_ts64(&ts1);
	expire_time = ktime_to_timespec64(hrtimer_get_softexpires(timer));
	dev = container_of(timer, struct pps_generator_pp, timer);
	lim = NSEC_PER_SEC - send_delay - dev->port_write_time;

	/* check if we are late */
	if (expire_time.tv_sec != ts1.tv_sec || ts1.tv_nsec > lim) {
		local_irq_restore(flags);
		pr_err("we are late this time %lld.%09ld\n",
				(s64)ts1.tv_sec, ts1.tv_nsec);
		goto done;
	}

	/* busy loop until the time is right for an assert edge */
	do {
		ktime_get_real_ts64(&ts2);
	} while (expire_time.tv_sec == ts2.tv_sec && ts2.tv_nsec < lim);

	/* set the signal */
	port = dev->pardev->port;
	port->ops->write_control(port, SIGNAL);

	/* busy loop until the time is right for a clear edge */
	lim = NSEC_PER_SEC - dev->port_write_time;
	do {
		ktime_get_real_ts64(&ts2);
	} while (expire_time.tv_sec == ts2.tv_sec && ts2.tv_nsec < lim);

	/* unset the signal */
	port->ops->write_control(port, NO_SIGNAL);

	ktime_get_real_ts64(&ts3);

	local_irq_restore(flags);

	/* update calibrated port write time */
	dts = timespec64_sub(ts3, ts2);
	dev->port_write_time =
		(dev->port_write_time + timespec64_to_ns(&dts)) >> 1;

done:
	/* update calibrated hrtimer error */
	dts = timespec64_sub(ts1, expire_time);
	delta = timespec64_to_ns(&dts);
	/* If the new error value is bigger then the old, use the new
	 * value, if not then slowly move towards the new value. This
	 * way it should be safe in bad conditions and efficient in
	 * good conditions.
	 */
	if (delta >= hrtimer_error)
		hrtimer_error = delta;
	else
		hrtimer_error = (3 * hrtimer_error + delta) >> 2;

	/* update the hrtimer expire time */
	hrtimer_set_expires(timer,
			ktime_set(expire_time.tv_sec + 1,
				NSEC_PER_SEC - (send_delay +
				dev->port_write_time + SAFETY_INTERVAL +
				2 * hrtimer_error)));

	return HRTIMER_RESTART;
}

/* calibrate port write time */
#define PORT_NTESTS_SHIFT	5
static void calibrate_port(struct pps_generator_pp *dev)
{
	struct parport *port = dev->pardev->port;
	int i;
	long acc = 0;

	for (i = 0; i < (1 << PORT_NTESTS_SHIFT); i++) {
		struct timespec64 a, b;
		unsigned long irq_flags;

		local_irq_save(irq_flags);
		ktime_get_real_ts64(&a);
		port->ops->write_control(port, NO_SIGNAL);
		ktime_get_real_ts64(&b);
		local_irq_restore(irq_flags);

		b = timespec64_sub(b, a);
		acc += timespec64_to_ns(&b);
	}

	dev->port_write_time = acc >> PORT_NTESTS_SHIFT;
	pr_info("port write takes %ldns\n", dev->port_write_time);
}

static inline ktime_t next_intr_time(struct pps_generator_pp *dev)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);

	return ktime_set(ts.tv_sec +
			((ts.tv_nsec > 990 * NSEC_PER_MSEC) ? 1 : 0),
			NSEC_PER_SEC - (send_delay +
			dev->port_write_time + 3 * SAFETY_INTERVAL));
}

static void parport_attach(struct parport *port)
{
	struct pardev_cb pps_cb;

	if (send_delay > SEND_DELAY_MAX) {
		pr_err("delay value should be not greater then %d\n", SEND_DELAY_MAX);
		return;
	}

	if (attached) {
		/* we already have a port */
		return;
	}

	memset(&pps_cb, 0, sizeof(pps_cb));
	pps_cb.private = &device;
	pps_cb.flags = PARPORT_FLAG_EXCL;
	device.pardev = parport_register_dev_model(port, KBUILD_MODNAME,
						   &pps_cb, 0);
	if (!device.pardev) {
		pr_err("couldn't register with %s\n", port->name);
		return;
	}

	if (parport_claim_or_block(device.pardev) < 0) {
		pr_err("couldn't claim %s\n", port->name);
		goto err_unregister_dev;
	}

	pr_info("attached to %s\n", port->name);
	attached = 1;

	calibrate_port(&device);

	hrtimer_setup(&device.timer, hrtimer_event, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	hrtimer_start(&device.timer, next_intr_time(&device), HRTIMER_MODE_ABS);

	return;

err_unregister_dev:
	parport_unregister_device(device.pardev);
}

static void parport_detach(struct parport *port)
{
	if (port->cad != device.pardev)
		return;	/* not our port */

	hrtimer_cancel(&device.timer);
	parport_release(device.pardev);
	parport_unregister_device(device.pardev);
}

static struct parport_driver pps_gen_parport_driver = {
	.name = KBUILD_MODNAME,
	.match_port = parport_attach,
	.detach = parport_detach,
};
module_parport_driver(pps_gen_parport_driver);

MODULE_AUTHOR("Alexander Gordeev <lasaine@lvk.cs.msu.su>");
MODULE_DESCRIPTION("parallel port PPS signal generator");
MODULE_LICENSE("GPL");
