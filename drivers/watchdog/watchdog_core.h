/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *	watchdog_core.h
 *
 *	(c) Copyright 2008-2011 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
 *
 *	(c) Copyright 2008-2011 Wim Van Sebroeck <wim@iguana.be>.
 *
 *	(c) Copyright 2021 Hewlett Packard Enterprise Development LP.
 *
 *	This source code is part of the generic code that can be used
 *	by all the watchdog timer drivers.
 *
 *	Based on source code of the following authors:
 *	  Matt Domsch <Matt_Domsch@dell.com>,
 *	  Rob Radez <rob@osinvestor.com>,
 *	  Rusty Lynch <rusty@linux.co.intel.com>
 *	  Satyam Sharma <satyam@infradead.org>
 *	  Randy Dunlap <randy.dunlap@oracle.com>
 *
 *	Neither Alan Cox, CymruNet Ltd., Wim Van Sebroeck nor Iguana vzw.
 *	admit liability nor provide warranty for any of this software.
 *	This material is provided "AS-IS" and at no charge.
 */

#include <linux/hrtimer.h>
#include <linux/kthread.h>

#define MAX_DOGS	32	/* Maximum number of watchdog devices */

/*
 * struct watchdog_core_data - watchdog core internal data
 * @dev:	The watchdog's internal device
 * @cdev:	The watchdog's Character device.
 * @wdd:	Pointer to watchdog device.
 * @lock:	Lock for watchdog core.
 * @status:	Watchdog core internal status bits.
 */
struct watchdog_core_data {
	struct device dev;
	struct cdev cdev;
	struct watchdog_device *wdd;
	struct mutex lock;
	ktime_t last_keepalive;
	ktime_t last_hw_keepalive;
	ktime_t open_deadline;
	struct hrtimer timer;
	struct kthread_work work;
#if IS_ENABLED(CONFIG_WATCHDOG_HRTIMER_PRETIMEOUT)
	struct hrtimer pretimeout_timer;
#endif
	unsigned long status;		/* Internal status bits */
#define _WDOG_DEV_OPEN		0	/* Opened ? */
#define _WDOG_ALLOW_RELEASE	1	/* Did we receive the magic char ? */
#define _WDOG_KEEPALIVE		2	/* Did we receive a keepalive ? */
};

/*
 *	Functions/procedures to be called by the core
 */
extern int watchdog_dev_register(struct watchdog_device *);
extern void watchdog_dev_unregister(struct watchdog_device *);
extern int __init watchdog_dev_init(void);
extern void __exit watchdog_dev_exit(void);

static inline bool watchdog_have_pretimeout(struct watchdog_device *wdd)
{
	return wdd->info->options & WDIOF_PRETIMEOUT ||
	       IS_ENABLED(CONFIG_WATCHDOG_HRTIMER_PRETIMEOUT);
}

#if IS_ENABLED(CONFIG_WATCHDOG_HRTIMER_PRETIMEOUT)
void watchdog_hrtimer_pretimeout_init(struct watchdog_device *wdd);
void watchdog_hrtimer_pretimeout_start(struct watchdog_device *wdd);
void watchdog_hrtimer_pretimeout_stop(struct watchdog_device *wdd);
#else
static inline void watchdog_hrtimer_pretimeout_init(struct watchdog_device *wdd) {}
static inline void watchdog_hrtimer_pretimeout_start(struct watchdog_device *wdd) {}
static inline void watchdog_hrtimer_pretimeout_stop(struct watchdog_device *wdd) {}
#endif
