/*
 * TimeSync API driver.
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/debugfs.h>
#include <linux/hrtimer.h>
#include "greybus.h"
#include "timesync.h"
#include "greybus_trace.h"

/*
 * Minimum inter-strobe value of one millisecond is chosen because it
 * just-about fits the common definition of a jiffy.
 *
 * Maximum value OTOH is constrained by the number of bits the SVC can fit
 * into a 16 bit up-counter. The SVC configures the timer in microseconds
 * so the maximum allowable value is 65535 microseconds. We clip that value
 * to 10000 microseconds for the sake of using nice round base 10 numbers
 * and since right-now there's no imaginable use-case requiring anything
 * other than a one millisecond inter-strobe time, let alone something
 * higher than ten milliseconds.
 */
#define GB_TIMESYNC_STROBE_DELAY_US		1000
#define GB_TIMESYNC_DEFAULT_OFFSET_US		1000

/* Work queue timers long, short and SVC strobe timeout */
#define GB_TIMESYNC_DELAYED_WORK_LONG		msecs_to_jiffies(10)
#define GB_TIMESYNC_DELAYED_WORK_SHORT		msecs_to_jiffies(1)
#define GB_TIMESYNC_MAX_WAIT_SVC		msecs_to_jiffies(5000)
#define GB_TIMESYNC_KTIME_UPDATE		msecs_to_jiffies(1000)
#define GB_TIMESYNC_MAX_KTIME_CONVERSION	15

/* Maximum number of times we'll retry a failed synchronous sync */
#define GB_TIMESYNC_MAX_RETRIES			5

/* Reported nanoseconds/femtoseconds per clock */
static u64 gb_timesync_ns_per_clock;
static u64 gb_timesync_fs_per_clock;

/* Maximum difference we will accept converting FrameTime to ktime */
static u32 gb_timesync_max_ktime_diff;

/* Reported clock rate */
static unsigned long gb_timesync_clock_rate;

/* Workqueue */
static void gb_timesync_worker(struct work_struct *work);

/* List of SVCs with one FrameTime per SVC */
static LIST_HEAD(gb_timesync_svc_list);

/* Synchronize parallel contexts accessing a valid timesync_svc pointer */
static DEFINE_MUTEX(gb_timesync_svc_list_mutex);

/* Structure to convert from FrameTime to timespec/ktime */
struct gb_timesync_frame_time_data {
	u64 frame_time;
	struct timespec ts;
};

struct gb_timesync_svc {
	struct list_head list;
	struct list_head interface_list;
	struct gb_svc *svc;
	struct gb_timesync_host_device *timesync_hd;

	spinlock_t spinlock;	/* Per SVC spinlock to sync with ISR */
	struct mutex mutex;	/* Per SVC mutex for regular synchronization */

	struct dentry *frame_time_dentry;
	struct dentry *frame_ktime_dentry;
	struct workqueue_struct *work_queue;
	wait_queue_head_t wait_queue;
	struct delayed_work delayed_work;
	struct timer_list ktime_timer;

	/* The current local FrameTime */
	u64 frame_time_offset;
	struct gb_timesync_frame_time_data strobe_data[GB_TIMESYNC_MAX_STROBES];
	struct gb_timesync_frame_time_data ktime_data;

	/* The SVC FrameTime and relative AP FrameTime @ last TIMESYNC_PING */
	u64 svc_ping_frame_time;
	u64 ap_ping_frame_time;

	/* Transitory settings */
	u32 strobe_mask;
	bool offset_down;
	bool print_ping;
	bool capture_ping;
	int strobe;

	/* Current state */
	int state;
};

struct gb_timesync_host_device {
	struct list_head list;
	struct gb_host_device *hd;
	u64 ping_frame_time;
};

struct gb_timesync_interface {
	struct list_head list;
	struct gb_interface *interface;
	u64 ping_frame_time;
};

enum gb_timesync_state {
	GB_TIMESYNC_STATE_INVALID		= 0,
	GB_TIMESYNC_STATE_INACTIVE		= 1,
	GB_TIMESYNC_STATE_INIT			= 2,
	GB_TIMESYNC_STATE_WAIT_SVC		= 3,
	GB_TIMESYNC_STATE_AUTHORITATIVE		= 4,
	GB_TIMESYNC_STATE_PING			= 5,
	GB_TIMESYNC_STATE_ACTIVE		= 6,
};

static void gb_timesync_ktime_timer_fn(unsigned long data);

static u64 gb_timesync_adjust_count(struct gb_timesync_svc *timesync_svc,
				    u64 counts)
{
	if (timesync_svc->offset_down)
		return counts - timesync_svc->frame_time_offset;
	else
		return counts + timesync_svc->frame_time_offset;
}

/*
 * This function provides the authoritative FrameTime to a calling function. It
 * is designed to be lockless and should remain that way the caller is assumed
 * to be state-aware.
 */
static u64 __gb_timesync_get_frame_time(struct gb_timesync_svc *timesync_svc)
{
	u64 clocks = gb_timesync_platform_get_counter();

	return gb_timesync_adjust_count(timesync_svc, clocks);
}

static void gb_timesync_schedule_svc_timeout(struct gb_timesync_svc
					     *timesync_svc)
{
	queue_delayed_work(timesync_svc->work_queue,
			   &timesync_svc->delayed_work,
			   GB_TIMESYNC_MAX_WAIT_SVC);
}

static void gb_timesync_set_state(struct gb_timesync_svc *timesync_svc,
				  int state)
{
	switch (state) {
	case GB_TIMESYNC_STATE_INVALID:
		timesync_svc->state = state;
		wake_up(&timesync_svc->wait_queue);
		break;
	case GB_TIMESYNC_STATE_INACTIVE:
		timesync_svc->state = state;
		wake_up(&timesync_svc->wait_queue);
		break;
	case GB_TIMESYNC_STATE_INIT:
		if (timesync_svc->state != GB_TIMESYNC_STATE_INVALID) {
			timesync_svc->strobe = 0;
			timesync_svc->frame_time_offset = 0;
			timesync_svc->state = state;
			cancel_delayed_work(&timesync_svc->delayed_work);
			queue_delayed_work(timesync_svc->work_queue,
					   &timesync_svc->delayed_work,
					   GB_TIMESYNC_DELAYED_WORK_LONG);
		}
		break;
	case GB_TIMESYNC_STATE_WAIT_SVC:
		if (timesync_svc->state == GB_TIMESYNC_STATE_INIT)
			timesync_svc->state = state;
		break;
	case GB_TIMESYNC_STATE_AUTHORITATIVE:
		if (timesync_svc->state == GB_TIMESYNC_STATE_WAIT_SVC) {
			timesync_svc->state = state;
			cancel_delayed_work(&timesync_svc->delayed_work);
			queue_delayed_work(timesync_svc->work_queue,
					   &timesync_svc->delayed_work, 0);
		}
		break;
	case GB_TIMESYNC_STATE_PING:
		if (timesync_svc->state == GB_TIMESYNC_STATE_ACTIVE) {
			timesync_svc->state = state;
			queue_delayed_work(timesync_svc->work_queue,
					   &timesync_svc->delayed_work,
					   GB_TIMESYNC_DELAYED_WORK_SHORT);
		}
		break;
	case GB_TIMESYNC_STATE_ACTIVE:
		if (timesync_svc->state == GB_TIMESYNC_STATE_AUTHORITATIVE ||
		    timesync_svc->state == GB_TIMESYNC_STATE_PING) {
			timesync_svc->state = state;
			wake_up(&timesync_svc->wait_queue);
		}
		break;
	}

	if (WARN_ON(timesync_svc->state != state)) {
		pr_err("Invalid state transition %d=>%d\n",
		       timesync_svc->state, state);
	}
}

static void gb_timesync_set_state_atomic(struct gb_timesync_svc *timesync_svc,
					 int state)
{
	unsigned long flags;

	spin_lock_irqsave(&timesync_svc->spinlock, flags);
	gb_timesync_set_state(timesync_svc, state);
	spin_unlock_irqrestore(&timesync_svc->spinlock, flags);
}

static u64 gb_timesync_diff(u64 x, u64 y)
{
	if (x > y)
		return x - y;
	else
		return y - x;
}

static void gb_timesync_adjust_to_svc(struct gb_timesync_svc *svc,
				      u64 svc_frame_time, u64 ap_frame_time)
{
	if (svc_frame_time > ap_frame_time) {
		svc->frame_time_offset = svc_frame_time - ap_frame_time;
		svc->offset_down = false;
	} else {
		svc->frame_time_offset = ap_frame_time - svc_frame_time;
		svc->offset_down = true;
	}
}

/*
 * Associate a FrameTime with a ktime timestamp represented as struct timespec
 * Requires the calling context to hold timesync_svc->mutex
 */
static void gb_timesync_store_ktime(struct gb_timesync_svc *timesync_svc,
				    struct timespec ts, u64 frame_time)
{
	timesync_svc->ktime_data.ts = ts;
	timesync_svc->ktime_data.frame_time = frame_time;
}

/*
 * Find the two pulses that best-match our expected inter-strobe gap and
 * then calculate the difference between the SVC time at the second pulse
 * to the local time at the second pulse.
 */
static void gb_timesync_collate_frame_time(struct gb_timesync_svc *timesync_svc,
					   u64 *frame_time)
{
	int i = 0;
	u64 delta, ap_frame_time;
	u64 strobe_delay_ns = GB_TIMESYNC_STROBE_DELAY_US * NSEC_PER_USEC;
	u64 least = 0;

	for (i = 1; i < GB_TIMESYNC_MAX_STROBES; i++) {
		delta = timesync_svc->strobe_data[i].frame_time -
			timesync_svc->strobe_data[i - 1].frame_time;
		delta *= gb_timesync_ns_per_clock;
		delta = gb_timesync_diff(delta, strobe_delay_ns);

		if (!least || delta < least) {
			least = delta;
			gb_timesync_adjust_to_svc(timesync_svc, frame_time[i],
						  timesync_svc->strobe_data[i].frame_time);

			ap_frame_time = timesync_svc->strobe_data[i].frame_time;
			ap_frame_time = gb_timesync_adjust_count(timesync_svc,
								 ap_frame_time);
			gb_timesync_store_ktime(timesync_svc,
						timesync_svc->strobe_data[i].ts,
						ap_frame_time);

			pr_debug("adjust %s local %llu svc %llu delta %llu\n",
				 timesync_svc->offset_down ? "down" : "up",
				 timesync_svc->strobe_data[i].frame_time,
				 frame_time[i], delta);
		}
	}
}

static void gb_timesync_teardown(struct gb_timesync_svc *timesync_svc)
{
	struct gb_timesync_interface *timesync_interface;
	struct gb_svc *svc = timesync_svc->svc;
	struct gb_interface *interface;
	struct gb_host_device *hd;
	int ret;

	list_for_each_entry(timesync_interface,
			    &timesync_svc->interface_list, list) {
		interface = timesync_interface->interface;
		ret = gb_interface_timesync_disable(interface);
		if (ret) {
			dev_err(&interface->dev,
				"interface timesync_disable %d\n", ret);
		}
	}

	hd = timesync_svc->timesync_hd->hd;
	ret = hd->driver->timesync_disable(hd);
	if (ret < 0) {
		dev_err(&hd->dev, "host timesync_disable %d\n",
			ret);
	}

	gb_svc_timesync_wake_pins_release(svc);
	gb_svc_timesync_disable(svc);
	gb_timesync_platform_unlock_bus();

	gb_timesync_set_state_atomic(timesync_svc, GB_TIMESYNC_STATE_INACTIVE);
}

static void gb_timesync_platform_lock_bus_fail(struct gb_timesync_svc
						*timesync_svc, int ret)
{
	if (ret == -EAGAIN) {
		gb_timesync_set_state(timesync_svc, timesync_svc->state);
	} else {
		pr_err("Failed to lock timesync bus %d\n", ret);
		gb_timesync_set_state(timesync_svc, GB_TIMESYNC_STATE_INACTIVE);
	}
}

static void gb_timesync_enable(struct gb_timesync_svc *timesync_svc)
{
	struct gb_svc *svc = timesync_svc->svc;
	struct gb_host_device *hd;
	struct gb_timesync_interface *timesync_interface;
	struct gb_interface *interface;
	u64 init_frame_time;
	unsigned long clock_rate = gb_timesync_clock_rate;
	int ret;

	/*
	 * Get access to the wake pins in the AP and SVC
	 * Release these pins either in gb_timesync_teardown() or in
	 * gb_timesync_authoritative()
	 */
	ret = gb_timesync_platform_lock_bus(timesync_svc);
	if (ret < 0) {
		gb_timesync_platform_lock_bus_fail(timesync_svc, ret);
		return;
	}
	ret = gb_svc_timesync_wake_pins_acquire(svc, timesync_svc->strobe_mask);
	if (ret) {
		dev_err(&svc->dev,
			"gb_svc_timesync_wake_pins_acquire %d\n", ret);
		gb_timesync_teardown(timesync_svc);
		return;
	}

	/* Choose an initial time in the future */
	init_frame_time = __gb_timesync_get_frame_time(timesync_svc) + 100000UL;

	/* Send enable command to all relevant participants */
	list_for_each_entry(timesync_interface, &timesync_svc->interface_list,
			    list) {
		interface = timesync_interface->interface;
		ret = gb_interface_timesync_enable(interface,
						   GB_TIMESYNC_MAX_STROBES,
						   init_frame_time,
						   GB_TIMESYNC_STROBE_DELAY_US,
						   clock_rate);
		if (ret) {
			dev_err(&interface->dev,
				"interface timesync_enable %d\n", ret);
		}
	}

	hd = timesync_svc->timesync_hd->hd;
	ret = hd->driver->timesync_enable(hd, GB_TIMESYNC_MAX_STROBES,
					  init_frame_time,
					  GB_TIMESYNC_STROBE_DELAY_US,
					  clock_rate);
	if (ret < 0) {
		dev_err(&hd->dev, "host timesync_enable %d\n",
			ret);
	}

	gb_timesync_set_state_atomic(timesync_svc, GB_TIMESYNC_STATE_WAIT_SVC);
	ret = gb_svc_timesync_enable(svc, GB_TIMESYNC_MAX_STROBES,
				     init_frame_time,
				     GB_TIMESYNC_STROBE_DELAY_US,
				     clock_rate);
	if (ret) {
		dev_err(&svc->dev,
			"gb_svc_timesync_enable %d\n", ret);
		gb_timesync_teardown(timesync_svc);
		return;
	}

	/* Schedule a timeout waiting for SVC to complete strobing */
	gb_timesync_schedule_svc_timeout(timesync_svc);
}

static void gb_timesync_authoritative(struct gb_timesync_svc *timesync_svc)
{
	struct gb_svc *svc = timesync_svc->svc;
	struct gb_host_device *hd;
	struct gb_timesync_interface *timesync_interface;
	struct gb_interface *interface;
	u64 svc_frame_time[GB_TIMESYNC_MAX_STROBES];
	int ret;

	/* Get authoritative time from SVC and adjust local clock */
	ret = gb_svc_timesync_authoritative(svc, svc_frame_time);
	if (ret) {
		dev_err(&svc->dev,
			"gb_svc_timesync_authoritative %d\n", ret);
		gb_timesync_teardown(timesync_svc);
		return;
	}
	gb_timesync_collate_frame_time(timesync_svc, svc_frame_time);

	/* Transmit authoritative time to downstream slaves */
	hd = timesync_svc->timesync_hd->hd;
	ret = hd->driver->timesync_authoritative(hd, svc_frame_time);
	if (ret < 0)
		dev_err(&hd->dev, "host timesync_authoritative %d\n", ret);

	list_for_each_entry(timesync_interface,
			    &timesync_svc->interface_list, list) {
		interface = timesync_interface->interface;
		ret = gb_interface_timesync_authoritative(
						interface,
						svc_frame_time);
		if (ret) {
			dev_err(&interface->dev,
				"interface timesync_authoritative %d\n", ret);
		}
	}

	/* Release wake pins */
	gb_svc_timesync_wake_pins_release(svc);
	gb_timesync_platform_unlock_bus();

	/* Transition to state ACTIVE */
	gb_timesync_set_state_atomic(timesync_svc, GB_TIMESYNC_STATE_ACTIVE);

	/* Schedule a ping to verify the synchronized system time */
	timesync_svc->print_ping = true;
	gb_timesync_set_state_atomic(timesync_svc, GB_TIMESYNC_STATE_PING);
}

static int __gb_timesync_get_status(struct gb_timesync_svc *timesync_svc)
{
	int ret = -EINVAL;

	switch (timesync_svc->state) {
	case GB_TIMESYNC_STATE_INVALID:
	case GB_TIMESYNC_STATE_INACTIVE:
		ret = -ENODEV;
		break;
	case GB_TIMESYNC_STATE_INIT:
	case GB_TIMESYNC_STATE_WAIT_SVC:
	case GB_TIMESYNC_STATE_AUTHORITATIVE:
		ret = -EAGAIN;
		break;
	case GB_TIMESYNC_STATE_PING:
	case GB_TIMESYNC_STATE_ACTIVE:
		ret = 0;
		break;
	}
	return ret;
}

/*
 * This routine takes a FrameTime and derives the difference with-respect
 * to a reference FrameTime/ktime pair. It then returns the calculated
 * ktime based on the difference between the supplied FrameTime and
 * the reference FrameTime.
 *
 * The time difference is calculated to six decimal places. Taking 19.2MHz
 * as an example this means we have 52.083333~ nanoseconds per clock or
 * 52083333~ femtoseconds per clock.
 *
 * Naively taking the count difference and converting to
 * seconds/nanoseconds would quickly see the 0.0833 component produce
 * noticeable errors. For example a time difference of one second would
 * loose 19200000 * 0.08333x nanoseconds or 1.59 seconds.
 *
 * In contrast calculating in femtoseconds the same example of 19200000 *
 * 0.000000083333x nanoseconds per count of error is just 1.59 nanoseconds!
 *
 * Continuing the example of 19.2 MHz we cap the maximum error difference
 * at a worst-case 0.3 microseconds over a potential calculation window of
 * abount 15 seconds, meaning you can convert a FrameTime that is <= 15
 * seconds older/younger than the reference time with a maximum error of
 * 0.2385 useconds. Note 19.2MHz is an example frequency not a requirement.
 */
static int gb_timesync_to_timespec(struct gb_timesync_svc *timesync_svc,
				   u64 frame_time, struct timespec *ts)
{
	unsigned long flags;
	u64 delta_fs, counts, sec, nsec;
	bool add;
	int ret = 0;

	memset(ts, 0x00, sizeof(*ts));
	mutex_lock(&timesync_svc->mutex);
	spin_lock_irqsave(&timesync_svc->spinlock, flags);

	ret = __gb_timesync_get_status(timesync_svc);
	if (ret)
		goto done;

	/* Support calculating ktime upwards or downwards from the reference */
	if (frame_time < timesync_svc->ktime_data.frame_time) {
		add = false;
		counts = timesync_svc->ktime_data.frame_time - frame_time;
	} else {
		add = true;
		counts = frame_time - timesync_svc->ktime_data.frame_time;
	}

	/* Enforce the .23 of a usecond boundary @ 19.2MHz */
	if (counts > gb_timesync_max_ktime_diff) {
		ret = -EINVAL;
		goto done;
	}

	/* Determine the time difference in femtoseconds */
	delta_fs = counts * gb_timesync_fs_per_clock;

	/* Convert to seconds */
	sec = delta_fs;
	do_div(sec, NSEC_PER_SEC);
	do_div(sec, 1000000UL);

	/* Get the nanosecond remainder */
	nsec = do_div(delta_fs, sec);
	do_div(nsec, 1000000UL);

	if (add) {
		/* Add the calculated offset - overflow nanoseconds upwards */
		ts->tv_sec = timesync_svc->ktime_data.ts.tv_sec + sec;
		ts->tv_nsec = timesync_svc->ktime_data.ts.tv_nsec + nsec;
		if (ts->tv_nsec >= NSEC_PER_SEC) {
			ts->tv_sec++;
			ts->tv_nsec -= NSEC_PER_SEC;
		}
	} else {
		/* Subtract the difference over/underflow as necessary */
		if (nsec > timesync_svc->ktime_data.ts.tv_nsec) {
			sec++;
			nsec = nsec + timesync_svc->ktime_data.ts.tv_nsec;
			nsec = do_div(nsec, NSEC_PER_SEC);
		} else {
			nsec = timesync_svc->ktime_data.ts.tv_nsec - nsec;
		}
		/* Cannot return a negative second value */
		if (sec > timesync_svc->ktime_data.ts.tv_sec) {
			ret = -EINVAL;
			goto done;
		}
		ts->tv_sec = timesync_svc->ktime_data.ts.tv_sec - sec;
		ts->tv_nsec = nsec;
	}
done:
	spin_unlock_irqrestore(&timesync_svc->spinlock, flags);
	mutex_unlock(&timesync_svc->mutex);
	return ret;
}

static size_t gb_timesync_log_frame_time(struct gb_timesync_svc *timesync_svc,
					 char *buf, size_t buflen)
{
	struct gb_svc *svc = timesync_svc->svc;
	struct gb_host_device *hd;
	struct gb_timesync_interface *timesync_interface;
	struct gb_interface *interface;
	unsigned int len;
	size_t off;

	/* AP/SVC */
	off = snprintf(buf, buflen, "%s frametime: ap=%llu %s=%llu ",
		       greybus_bus_type.name,
		       timesync_svc->ap_ping_frame_time, dev_name(&svc->dev),
		       timesync_svc->svc_ping_frame_time);
	len = buflen - off;

	/* APB/GPB */
	if (len < buflen) {
		hd = timesync_svc->timesync_hd->hd;
		off += snprintf(&buf[off], len, "%s=%llu ", dev_name(&hd->dev),
				timesync_svc->timesync_hd->ping_frame_time);
		len = buflen - off;
	}

	list_for_each_entry(timesync_interface,
			    &timesync_svc->interface_list, list) {
		if (len < buflen) {
			interface = timesync_interface->interface;
			off += snprintf(&buf[off], len, "%s=%llu ",
					dev_name(&interface->dev),
					timesync_interface->ping_frame_time);
			len = buflen - off;
		}
	}
	if (len < buflen)
		off += snprintf(&buf[off], len, "\n");
	return off;
}

static size_t gb_timesync_log_frame_ktime(struct gb_timesync_svc *timesync_svc,
					  char *buf, size_t buflen)
{
	struct gb_svc *svc = timesync_svc->svc;
	struct gb_host_device *hd;
	struct gb_timesync_interface *timesync_interface;
	struct gb_interface *interface;
	struct timespec ts;
	unsigned int len;
	size_t off;

	/* AP */
	gb_timesync_to_timespec(timesync_svc, timesync_svc->ap_ping_frame_time,
				&ts);
	off = snprintf(buf, buflen, "%s frametime: ap=%lu.%lu ",
		       greybus_bus_type.name, ts.tv_sec, ts.tv_nsec);
	len = buflen - off;
	if (len >= buflen)
		goto done;

	/* SVC */
	gb_timesync_to_timespec(timesync_svc, timesync_svc->svc_ping_frame_time,
				&ts);
	off += snprintf(&buf[off], len, "%s=%lu.%lu ", dev_name(&svc->dev),
			ts.tv_sec, ts.tv_nsec);
	len = buflen - off;
	if (len >= buflen)
		goto done;

	/* APB/GPB */
	hd = timesync_svc->timesync_hd->hd;
	gb_timesync_to_timespec(timesync_svc,
				timesync_svc->timesync_hd->ping_frame_time,
				&ts);
	off += snprintf(&buf[off], len, "%s=%lu.%lu ",
			dev_name(&hd->dev),
			ts.tv_sec, ts.tv_nsec);
	len = buflen - off;
	if (len >= buflen)
		goto done;

	list_for_each_entry(timesync_interface,
			    &timesync_svc->interface_list, list) {
		interface = timesync_interface->interface;
		gb_timesync_to_timespec(timesync_svc,
					timesync_interface->ping_frame_time,
					&ts);
		off += snprintf(&buf[off], len, "%s=%lu.%lu ",
				dev_name(&interface->dev),
				ts.tv_sec, ts.tv_nsec);
		len = buflen - off;
		if (len >= buflen)
			goto done;
	}
	off += snprintf(&buf[off], len, "\n");
done:
	return off;
}

/*
 * Send an SVC initiated wake 'ping' to each TimeSync participant.
 * Get the FrameTime from each participant associated with the wake
 * ping.
 */
static void gb_timesync_ping(struct gb_timesync_svc *timesync_svc)
{
	struct gb_svc *svc = timesync_svc->svc;
	struct gb_host_device *hd;
	struct gb_timesync_interface *timesync_interface;
	struct gb_control *control;
	u64 *ping_frame_time;
	int ret;

	/* Get access to the wake pins in the AP and SVC */
	ret = gb_timesync_platform_lock_bus(timesync_svc);
	if (ret < 0) {
		gb_timesync_platform_lock_bus_fail(timesync_svc, ret);
		return;
	}
	ret = gb_svc_timesync_wake_pins_acquire(svc, timesync_svc->strobe_mask);
	if (ret) {
		dev_err(&svc->dev,
			"gb_svc_timesync_wake_pins_acquire %d\n", ret);
		gb_timesync_teardown(timesync_svc);
		return;
	}

	/* Have SVC generate a timesync ping */
	timesync_svc->capture_ping = true;
	timesync_svc->svc_ping_frame_time = 0;
	ret = gb_svc_timesync_ping(svc, &timesync_svc->svc_ping_frame_time);
	timesync_svc->capture_ping = false;
	if (ret) {
		dev_err(&svc->dev,
			"gb_svc_timesync_ping %d\n", ret);
		gb_timesync_teardown(timesync_svc);
		return;
	}

	/* Get the ping FrameTime from each APB/GPB */
	hd = timesync_svc->timesync_hd->hd;
	timesync_svc->timesync_hd->ping_frame_time = 0;
	ret = hd->driver->timesync_get_last_event(hd,
		&timesync_svc->timesync_hd->ping_frame_time);
	if (ret)
		dev_err(&hd->dev, "host timesync_get_last_event %d\n", ret);

	list_for_each_entry(timesync_interface,
			    &timesync_svc->interface_list, list) {
		control = timesync_interface->interface->control;
		timesync_interface->ping_frame_time = 0;
		ping_frame_time = &timesync_interface->ping_frame_time;
		ret = gb_control_timesync_get_last_event(control,
							 ping_frame_time);
		if (ret) {
			dev_err(&timesync_interface->interface->dev,
				"gb_control_timesync_get_last_event %d\n", ret);
		}
	}

	/* Ping success - move to timesync active */
	gb_svc_timesync_wake_pins_release(svc);
	gb_timesync_platform_unlock_bus();
	gb_timesync_set_state_atomic(timesync_svc, GB_TIMESYNC_STATE_ACTIVE);
}

static void gb_timesync_log_ping_time(struct gb_timesync_svc *timesync_svc)
{
	char *buf;

	if (!timesync_svc->print_ping)
		return;

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf) {
		gb_timesync_log_frame_time(timesync_svc, buf, PAGE_SIZE);
		dev_dbg(&timesync_svc->svc->dev, "%s", buf);
		kfree(buf);
	}
}

/*
 * Perform the actual work of scheduled TimeSync logic.
 */
static void gb_timesync_worker(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct gb_timesync_svc *timesync_svc =
		container_of(delayed_work, struct gb_timesync_svc, delayed_work);

	mutex_lock(&timesync_svc->mutex);

	switch (timesync_svc->state) {
	case GB_TIMESYNC_STATE_INIT:
		gb_timesync_enable(timesync_svc);
		break;

	case GB_TIMESYNC_STATE_WAIT_SVC:
		dev_err(&timesync_svc->svc->dev,
			"timeout SVC strobe completion %d/%d\n",
			timesync_svc->strobe, GB_TIMESYNC_MAX_STROBES);
		gb_timesync_teardown(timesync_svc);
		break;

	case GB_TIMESYNC_STATE_AUTHORITATIVE:
		gb_timesync_authoritative(timesync_svc);
		break;

	case GB_TIMESYNC_STATE_PING:
		gb_timesync_ping(timesync_svc);
		gb_timesync_log_ping_time(timesync_svc);
		break;

	default:
		pr_err("Invalid state %d for delayed work\n",
		       timesync_svc->state);
		break;
	}

	mutex_unlock(&timesync_svc->mutex);
}

/*
 * Schedule a new TimeSync INIT or PING operation serialized w/r to
 * gb_timesync_worker().
 */
static int gb_timesync_schedule(struct gb_timesync_svc *timesync_svc, int state)
{
	int ret = 0;

	if (state != GB_TIMESYNC_STATE_INIT && state != GB_TIMESYNC_STATE_PING)
		return -EINVAL;

	mutex_lock(&timesync_svc->mutex);
	if (timesync_svc->state !=  GB_TIMESYNC_STATE_INVALID) {
		gb_timesync_set_state_atomic(timesync_svc, state);
	} else {
		ret = -ENODEV;
	}
	mutex_unlock(&timesync_svc->mutex);
	return ret;
}

static int __gb_timesync_schedule_synchronous(
	struct gb_timesync_svc *timesync_svc, int state)
{
	unsigned long flags;
	int ret;

	ret = gb_timesync_schedule(timesync_svc, state);
	if (ret)
		return ret;

	ret = wait_event_interruptible(timesync_svc->wait_queue,
			(timesync_svc->state == GB_TIMESYNC_STATE_ACTIVE ||
			 timesync_svc->state == GB_TIMESYNC_STATE_INACTIVE ||
			 timesync_svc->state == GB_TIMESYNC_STATE_INVALID));
	if (ret)
		return ret;

	mutex_lock(&timesync_svc->mutex);
	spin_lock_irqsave(&timesync_svc->spinlock, flags);

	ret = __gb_timesync_get_status(timesync_svc);

	spin_unlock_irqrestore(&timesync_svc->spinlock, flags);
	mutex_unlock(&timesync_svc->mutex);

	return ret;
}

static struct gb_timesync_svc *gb_timesync_find_timesync_svc(
	struct gb_host_device *hd)
{
	struct gb_timesync_svc *timesync_svc;

	list_for_each_entry(timesync_svc, &gb_timesync_svc_list, list) {
		if (timesync_svc->svc == hd->svc)
			return timesync_svc;
	}
	return NULL;
}

static struct gb_timesync_interface *gb_timesync_find_timesync_interface(
	struct gb_timesync_svc *timesync_svc,
	struct gb_interface *interface)
{
	struct gb_timesync_interface *timesync_interface;

	list_for_each_entry(timesync_interface, &timesync_svc->interface_list, list) {
		if (timesync_interface->interface == interface)
			return timesync_interface;
	}
	return NULL;
}

int gb_timesync_schedule_synchronous(struct gb_interface *interface)
{
	int ret;
	struct gb_timesync_svc *timesync_svc;
	int retries;

	if (!(interface->features & GREYBUS_INTERFACE_FEATURE_TIMESYNC))
		return 0;

	mutex_lock(&gb_timesync_svc_list_mutex);
	for (retries = 0; retries < GB_TIMESYNC_MAX_RETRIES; retries++) {
		timesync_svc = gb_timesync_find_timesync_svc(interface->hd);
		if (!timesync_svc) {
			ret = -ENODEV;
			goto done;
		}

		ret = __gb_timesync_schedule_synchronous(timesync_svc,
						 GB_TIMESYNC_STATE_INIT);
		if (!ret)
			break;
	}
	if (ret && retries == GB_TIMESYNC_MAX_RETRIES)
		ret = -ETIMEDOUT;
done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(gb_timesync_schedule_synchronous);

void gb_timesync_schedule_asynchronous(struct gb_interface *interface)
{
	struct gb_timesync_svc *timesync_svc;

	if (!(interface->features & GREYBUS_INTERFACE_FEATURE_TIMESYNC))
		return;

	mutex_lock(&gb_timesync_svc_list_mutex);
	timesync_svc = gb_timesync_find_timesync_svc(interface->hd);
	if (!timesync_svc)
		goto done;

	gb_timesync_schedule(timesync_svc, GB_TIMESYNC_STATE_INIT);
done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
	return;
}
EXPORT_SYMBOL_GPL(gb_timesync_schedule_asynchronous);

static ssize_t gb_timesync_ping_read(struct file *file, char __user *ubuf,
				     size_t len, loff_t *offset, bool ktime)
{
	struct gb_timesync_svc *timesync_svc = file->f_inode->i_private;
	char *buf;
	ssize_t ret = 0;

	mutex_lock(&gb_timesync_svc_list_mutex);
	mutex_lock(&timesync_svc->mutex);
	if (list_empty(&timesync_svc->interface_list))
		ret = -ENODEV;
	timesync_svc->print_ping = false;
	mutex_unlock(&timesync_svc->mutex);
	if (ret)
		goto done;

	ret = __gb_timesync_schedule_synchronous(timesync_svc,
						 GB_TIMESYNC_STATE_PING);
	if (ret)
		goto done;

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto done;
	}

	if (ktime)
		ret = gb_timesync_log_frame_ktime(timesync_svc, buf, PAGE_SIZE);
	else
		ret = gb_timesync_log_frame_time(timesync_svc, buf, PAGE_SIZE);
	if (ret > 0)
		ret = simple_read_from_buffer(ubuf, len, offset, buf, ret);
	kfree(buf);
done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
	return ret;
}

static ssize_t gb_timesync_ping_read_frame_time(struct file *file,
						char __user *buf,
						size_t len, loff_t *offset)
{
	return gb_timesync_ping_read(file, buf, len, offset, false);
}

static ssize_t gb_timesync_ping_read_frame_ktime(struct file *file,
						 char __user *buf,
						 size_t len, loff_t *offset)
{
	return gb_timesync_ping_read(file, buf, len, offset, true);
}

static const struct file_operations gb_timesync_debugfs_frame_time_ops = {
	.read		= gb_timesync_ping_read_frame_time,
};

static const struct file_operations gb_timesync_debugfs_frame_ktime_ops = {
	.read		= gb_timesync_ping_read_frame_ktime,
};

static int gb_timesync_hd_add(struct gb_timesync_svc *timesync_svc,
			      struct gb_host_device *hd)
{
	struct gb_timesync_host_device *timesync_hd;

	timesync_hd = kzalloc(sizeof(*timesync_hd), GFP_KERNEL);
	if (!timesync_hd)
		return -ENOMEM;

	WARN_ON(timesync_svc->timesync_hd);
	timesync_hd->hd = hd;
	timesync_svc->timesync_hd = timesync_hd;

	return 0;
}

static void gb_timesync_hd_remove(struct gb_timesync_svc *timesync_svc,
				  struct gb_host_device *hd)
{
	if (timesync_svc->timesync_hd->hd == hd) {
		kfree(timesync_svc->timesync_hd);
		timesync_svc->timesync_hd = NULL;
		return;
	}
	WARN_ON(1);
}

int gb_timesync_svc_add(struct gb_svc *svc)
{
	struct gb_timesync_svc *timesync_svc;
	int ret;

	timesync_svc = kzalloc(sizeof(*timesync_svc), GFP_KERNEL);
	if (!timesync_svc)
		return -ENOMEM;

	timesync_svc->work_queue =
		create_singlethread_workqueue("gb-timesync-work_queue");

	if (!timesync_svc->work_queue) {
		kfree(timesync_svc);
		return -ENOMEM;
	}

	mutex_lock(&gb_timesync_svc_list_mutex);
	INIT_LIST_HEAD(&timesync_svc->interface_list);
	INIT_DELAYED_WORK(&timesync_svc->delayed_work, gb_timesync_worker);
	mutex_init(&timesync_svc->mutex);
	spin_lock_init(&timesync_svc->spinlock);
	init_waitqueue_head(&timesync_svc->wait_queue);

	timesync_svc->svc = svc;
	timesync_svc->frame_time_offset = 0;
	timesync_svc->capture_ping = false;
	gb_timesync_set_state_atomic(timesync_svc, GB_TIMESYNC_STATE_INACTIVE);

	timesync_svc->frame_time_dentry =
		debugfs_create_file("frame-time", S_IRUGO, svc->debugfs_dentry,
				    timesync_svc,
				    &gb_timesync_debugfs_frame_time_ops);
	timesync_svc->frame_ktime_dentry =
		debugfs_create_file("frame-ktime", S_IRUGO, svc->debugfs_dentry,
				    timesync_svc,
				    &gb_timesync_debugfs_frame_ktime_ops);

	list_add(&timesync_svc->list, &gb_timesync_svc_list);
	ret = gb_timesync_hd_add(timesync_svc, svc->hd);
	if (ret) {
		list_del(&timesync_svc->list);
		debugfs_remove(timesync_svc->frame_ktime_dentry);
		debugfs_remove(timesync_svc->frame_time_dentry);
		destroy_workqueue(timesync_svc->work_queue);
		kfree(timesync_svc);
		goto done;
	}

	init_timer(&timesync_svc->ktime_timer);
	timesync_svc->ktime_timer.function = gb_timesync_ktime_timer_fn;
	timesync_svc->ktime_timer.expires = jiffies + GB_TIMESYNC_KTIME_UPDATE;
	timesync_svc->ktime_timer.data = (unsigned long)timesync_svc;
	add_timer(&timesync_svc->ktime_timer);
done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(gb_timesync_svc_add);

void gb_timesync_svc_remove(struct gb_svc *svc)
{
	struct gb_timesync_svc *timesync_svc;
	struct gb_timesync_interface *timesync_interface;
	struct gb_timesync_interface *next;

	mutex_lock(&gb_timesync_svc_list_mutex);
	timesync_svc = gb_timesync_find_timesync_svc(svc->hd);
	if (!timesync_svc)
		goto done;

	cancel_delayed_work_sync(&timesync_svc->delayed_work);

	mutex_lock(&timesync_svc->mutex);

	gb_timesync_set_state_atomic(timesync_svc, GB_TIMESYNC_STATE_INVALID);
	del_timer_sync(&timesync_svc->ktime_timer);
	gb_timesync_teardown(timesync_svc);

	gb_timesync_hd_remove(timesync_svc, svc->hd);
	list_for_each_entry_safe(timesync_interface, next,
				 &timesync_svc->interface_list, list) {
		list_del(&timesync_interface->list);
		kfree(timesync_interface);
	}
	debugfs_remove(timesync_svc->frame_ktime_dentry);
	debugfs_remove(timesync_svc->frame_time_dentry);
	destroy_workqueue(timesync_svc->work_queue);
	list_del(&timesync_svc->list);

	mutex_unlock(&timesync_svc->mutex);

	kfree(timesync_svc);
done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
}
EXPORT_SYMBOL_GPL(gb_timesync_svc_remove);

/*
 * Add a Greybus Interface to the set of TimeSync Interfaces.
 */
int gb_timesync_interface_add(struct gb_interface *interface)
{
	struct gb_timesync_svc *timesync_svc;
	struct gb_timesync_interface *timesync_interface;
	int ret = 0;

	if (!(interface->features & GREYBUS_INTERFACE_FEATURE_TIMESYNC))
		return 0;

	mutex_lock(&gb_timesync_svc_list_mutex);
	timesync_svc = gb_timesync_find_timesync_svc(interface->hd);
	if (!timesync_svc) {
		ret = -ENODEV;
		goto done;
	}

	timesync_interface = kzalloc(sizeof(*timesync_interface), GFP_KERNEL);
	if (!timesync_interface) {
		ret = -ENOMEM;
		goto done;
	}

	mutex_lock(&timesync_svc->mutex);
	timesync_interface->interface = interface;
	list_add(&timesync_interface->list, &timesync_svc->interface_list);
	timesync_svc->strobe_mask |= 1 << interface->interface_id;
	mutex_unlock(&timesync_svc->mutex);

done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(gb_timesync_interface_add);

/*
 * Remove a Greybus Interface from the set of TimeSync Interfaces.
 */
void gb_timesync_interface_remove(struct gb_interface *interface)
{
	struct gb_timesync_svc *timesync_svc;
	struct gb_timesync_interface *timesync_interface;

	if (!(interface->features & GREYBUS_INTERFACE_FEATURE_TIMESYNC))
		return;

	mutex_lock(&gb_timesync_svc_list_mutex);
	timesync_svc = gb_timesync_find_timesync_svc(interface->hd);
	if (!timesync_svc)
		goto done;

	timesync_interface = gb_timesync_find_timesync_interface(timesync_svc,
								 interface);
	if (!timesync_interface)
		goto done;

	mutex_lock(&timesync_svc->mutex);
	timesync_svc->strobe_mask &= ~(1 << interface->interface_id);
	list_del(&timesync_interface->list);
	kfree(timesync_interface);
	mutex_unlock(&timesync_svc->mutex);
done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
}
EXPORT_SYMBOL_GPL(gb_timesync_interface_remove);

/*
 * Give the authoritative FrameTime to the calling function. Returns zero if we
 * are not in GB_TIMESYNC_STATE_ACTIVE.
 */
static u64 gb_timesync_get_frame_time(struct gb_timesync_svc *timesync_svc)
{
	unsigned long flags;
	u64 ret;

	spin_lock_irqsave(&timesync_svc->spinlock, flags);
	if (timesync_svc->state == GB_TIMESYNC_STATE_ACTIVE)
		ret = __gb_timesync_get_frame_time(timesync_svc);
	else
		ret = 0;
	spin_unlock_irqrestore(&timesync_svc->spinlock, flags);
	return ret;
}

u64 gb_timesync_get_frame_time_by_interface(struct gb_interface *interface)
{
	struct gb_timesync_svc *timesync_svc;
	u64 ret = 0;

	mutex_lock(&gb_timesync_svc_list_mutex);
	timesync_svc = gb_timesync_find_timesync_svc(interface->hd);
	if (!timesync_svc)
		goto done;

	ret = gb_timesync_get_frame_time(timesync_svc);
done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(gb_timesync_get_frame_time_by_interface);

u64 gb_timesync_get_frame_time_by_svc(struct gb_svc *svc)
{
	struct gb_timesync_svc *timesync_svc;
	u64 ret = 0;

	mutex_lock(&gb_timesync_svc_list_mutex);
	timesync_svc = gb_timesync_find_timesync_svc(svc->hd);
	if (!timesync_svc)
		goto done;

	ret = gb_timesync_get_frame_time(timesync_svc);
done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(gb_timesync_get_frame_time_by_svc);

/* Incrementally updates the conversion base from FrameTime to ktime */
static void gb_timesync_ktime_timer_fn(unsigned long data)
{
	struct gb_timesync_svc *timesync_svc =
		(struct gb_timesync_svc *)data;
	unsigned long flags;
	u64 frame_time;
	struct timespec ts;

	spin_lock_irqsave(&timesync_svc->spinlock, flags);

	if (timesync_svc->state != GB_TIMESYNC_STATE_ACTIVE)
		goto done;

	ktime_get_ts(&ts);
	frame_time = __gb_timesync_get_frame_time(timesync_svc);
	gb_timesync_store_ktime(timesync_svc, ts, frame_time);

done:
	spin_unlock_irqrestore(&timesync_svc->spinlock, flags);
	mod_timer(&timesync_svc->ktime_timer,
		  jiffies + GB_TIMESYNC_KTIME_UPDATE);
}

int gb_timesync_to_timespec_by_svc(struct gb_svc *svc, u64 frame_time,
				   struct timespec *ts)
{
	struct gb_timesync_svc *timesync_svc;
	int ret = 0;

	mutex_lock(&gb_timesync_svc_list_mutex);
	timesync_svc = gb_timesync_find_timesync_svc(svc->hd);
	if (!timesync_svc) {
		ret = -ENODEV;
		goto done;
	}
	ret = gb_timesync_to_timespec(timesync_svc, frame_time, ts);
done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(gb_timesync_to_timespec_by_svc);

int gb_timesync_to_timespec_by_interface(struct gb_interface *interface,
					 u64 frame_time, struct timespec *ts)
{
	struct gb_timesync_svc *timesync_svc;
	int ret = 0;

	mutex_lock(&gb_timesync_svc_list_mutex);
	timesync_svc = gb_timesync_find_timesync_svc(interface->hd);
	if (!timesync_svc) {
		ret = -ENODEV;
		goto done;
	}

	ret = gb_timesync_to_timespec(timesync_svc, frame_time, ts);
done:
	mutex_unlock(&gb_timesync_svc_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(gb_timesync_to_timespec_by_interface);

void gb_timesync_irq(struct gb_timesync_svc *timesync_svc)
{
	unsigned long flags;
	u64 strobe_time;
	bool strobe_is_ping = true;
	struct timespec ts;

	ktime_get_ts(&ts);
	strobe_time = __gb_timesync_get_frame_time(timesync_svc);

	spin_lock_irqsave(&timesync_svc->spinlock, flags);

	if (timesync_svc->state == GB_TIMESYNC_STATE_PING) {
		if (!timesync_svc->capture_ping)
			goto done_nolog;
		timesync_svc->ap_ping_frame_time = strobe_time;
		goto done_log;
	} else if (timesync_svc->state != GB_TIMESYNC_STATE_WAIT_SVC) {
		goto done_nolog;
	}

	timesync_svc->strobe_data[timesync_svc->strobe].frame_time = strobe_time;
	timesync_svc->strobe_data[timesync_svc->strobe].ts = ts;

	if (++timesync_svc->strobe == GB_TIMESYNC_MAX_STROBES) {
		gb_timesync_set_state(timesync_svc,
				      GB_TIMESYNC_STATE_AUTHORITATIVE);
	}
	strobe_is_ping = false;
done_log:
	trace_gb_timesync_irq(strobe_is_ping, timesync_svc->strobe,
			      GB_TIMESYNC_MAX_STROBES, strobe_time);
done_nolog:
	spin_unlock_irqrestore(&timesync_svc->spinlock, flags);
}
EXPORT_SYMBOL(gb_timesync_irq);

int __init gb_timesync_init(void)
{
	int ret = 0;

	ret = gb_timesync_platform_init();
	if (ret) {
		pr_err("timesync platform init fail!\n");
		return ret;
	}

	gb_timesync_clock_rate = gb_timesync_platform_get_clock_rate();

	/* Calculate nanoseconds and femtoseconds per clock */
	gb_timesync_fs_per_clock = FSEC_PER_SEC;
	do_div(gb_timesync_fs_per_clock, gb_timesync_clock_rate);
	gb_timesync_ns_per_clock = NSEC_PER_SEC;
	do_div(gb_timesync_ns_per_clock, gb_timesync_clock_rate);

	/* Calculate the maximum number of clocks we will convert to ktime */
	gb_timesync_max_ktime_diff =
		GB_TIMESYNC_MAX_KTIME_CONVERSION * gb_timesync_clock_rate;

	pr_info("Time-Sync @ %lu Hz max ktime conversion +/- %d seconds\n",
		gb_timesync_clock_rate, GB_TIMESYNC_MAX_KTIME_CONVERSION);
	return 0;
}

void gb_timesync_exit(void)
{
	gb_timesync_platform_exit();
}
