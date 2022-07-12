/*
 * Expose some of the kernel scheduler routines
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_linux_sched.c 514727 2014-11-12 03:02:48Z $
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/sched.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
#include <uapi/linux/sched/types.h>
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) */
#include <typedefs.h>
#include <linuxver.h>

int setScheduler(struct task_struct *p, int policy, struct sched_param *param)
{
	int rc = 0;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
	rc = sched_setscheduler(p, policy, param);
#else
	switch (policy) {
		case SCHED_FIFO:
			if (param->sched_priority > 1)
				/* If the priority is 2 or higher, it is considered 
				 * as high priority. priority is set basically MAX_RT_PRIO/2
				 */
				sched_set_fifo(p);
			else
				sched_set_fifo_low(p);
			break;
		case SCHED_NORMAL:
			sched_set_normal(p, PRIO_TO_NICE(p->static_prio));
			break;
	}
#endif
	return rc;
}

int get_scheduler_policy(struct task_struct *p)
{
	int rc = SCHED_NORMAL;
	rc = p->policy;
	return rc;
}
