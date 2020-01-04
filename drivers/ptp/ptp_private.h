/*
 * PTP 1588 clock support - private declarations for the core module.
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef _PTP_PRIVATE_H_
#define _PTP_PRIVATE_H_

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/posix-clock.h>
#include <linux/ptp_clock.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/time.h>

#define PTP_MAX_TIMESTAMPS 128
#define PTP_BUF_TIMESTAMPS 30

struct timestamp_event_queue {
	struct ptp_extts_event buf[PTP_MAX_TIMESTAMPS];
	int head;
	int tail;
	spinlock_t lock;
};

struct ptp_clock {
	struct posix_clock clock;
	struct device dev;
	struct ptp_clock_info *info;
	dev_t devid;
	int index; /* index into clocks.map */
	struct pps_device *pps_source;
	long dialed_frequency; /* remembers the frequency adjustment */
	struct timestamp_event_queue tsevq; /* simple fifo for time stamps */
	struct mutex tsevq_mux; /* one process at a time reading the fifo */
	struct mutex pincfg_mux; /* protect concurrent info->pin_config access */
	wait_queue_head_t tsev_wq;
	int defunct; /* tells readers to go away when clock is being removed */
	struct device_attribute *pin_dev_attr;
	struct attribute **pin_attr;
	struct attribute_group pin_attr_group;
	/* 1st entry is a pointer to the real group, 2nd is NULL terminator */
	const struct attribute_group *pin_attr_groups[2];
	struct kthread_worker *kworker;
	struct kthread_delayed_work aux_work;
};

/*
 * The function queue_cnt() is safe for readers to call without
 * holding q->lock. Readers use this function to verify that the queue
 * is nonempty before proceeding with a dequeue operation. The fact
 * that a writer might concurrently increment the tail does not
 * matter, since the queue remains nonempty nonetheless.
 */
static inline int queue_cnt(struct timestamp_event_queue *q)
{
	int cnt = q->tail - q->head;
	return cnt < 0 ? PTP_MAX_TIMESTAMPS + cnt : cnt;
}

/*
 * see ptp_chardev.c
 */

/* caller must hold pincfg_mux */
int ptp_set_pinfunc(struct ptp_clock *ptp, unsigned int pin,
		    enum ptp_pin_function func, unsigned int chan);

long ptp_ioctl(struct posix_clock *pc,
	       unsigned int cmd, unsigned long arg);

int ptp_open(struct posix_clock *pc, fmode_t fmode);

ssize_t ptp_read(struct posix_clock *pc,
		 uint flags, char __user *buf, size_t cnt);

__poll_t ptp_poll(struct posix_clock *pc,
	      struct file *fp, poll_table *wait);

/*
 * see ptp_sysfs.c
 */

extern const struct attribute_group *ptp_groups[];

int ptp_populate_pin_groups(struct ptp_clock *ptp);
void ptp_cleanup_pin_groups(struct ptp_clock *ptp);

#endif
