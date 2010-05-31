/* sysctl.c: implementation of /proc/sys files relating to FRV specifically
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>

static const char frv_cache_wback[] = "wback";
static const char frv_cache_wthru[] = "wthru";

static void frv_change_dcache_mode(unsigned long newmode)
{
	unsigned long flags, hsr0;

	local_irq_save(flags);

	hsr0 = __get_HSR(0);
	hsr0 &= ~HSR0_DCE;
	__set_HSR(0, hsr0);

	asm volatile("	dcef	@(gr0,gr0),#1	\n"
		     "	membar			\n"
		     : : : "memory"
		     );

	hsr0 = (hsr0 & ~HSR0_CBM) | newmode;
	__set_HSR(0, hsr0);
	hsr0 |= HSR0_DCE;
	__set_HSR(0, hsr0);

	local_irq_restore(flags);

	//printk("HSR0 now %08lx\n", hsr0);
}

/*****************************************************************************/
/*
 * handle requests to dynamically switch the write caching mode delivered by /proc
 */
static int procctl_frv_cachemode(ctl_table *table, int write,
				 void __user *buffer, size_t *lenp,
				 loff_t *ppos)
{
	unsigned long hsr0;
	char buff[8];
	int len;

	len = *lenp;

	if (write) {
		/* potential state change */
		if (len <= 1 || len > sizeof(buff) - 1)
			return -EINVAL;

		if (copy_from_user(buff, buffer, len) != 0)
			return -EFAULT;

		if (buff[len - 1] == '\n')
			buff[len - 1] = '\0';
		else
			buff[len] = '\0';

		if (strcmp(buff, frv_cache_wback) == 0) {
			/* switch dcache into write-back mode */
			frv_change_dcache_mode(HSR0_CBM_COPY_BACK);
			return 0;
		}

		if (strcmp(buff, frv_cache_wthru) == 0) {
			/* switch dcache into write-through mode */
			frv_change_dcache_mode(HSR0_CBM_WRITE_THRU);
			return 0;
		}

		return -EINVAL;
	}

	/* read the state */
	if (*ppos > 0) {
		*lenp = 0;
		return 0;
	}

	hsr0 = __get_HSR(0);
	switch (hsr0 & HSR0_CBM) {
	case HSR0_CBM_WRITE_THRU:
		memcpy(buff, frv_cache_wthru, sizeof(frv_cache_wthru) - 1);
		buff[sizeof(frv_cache_wthru) - 1] = '\n';
		len = sizeof(frv_cache_wthru);
		break;
	default:
		memcpy(buff, frv_cache_wback, sizeof(frv_cache_wback) - 1);
		buff[sizeof(frv_cache_wback) - 1] = '\n';
		len = sizeof(frv_cache_wback);
		break;
	}

	if (len > *lenp)
		len = *lenp;

	if (copy_to_user(buffer, buff, len) != 0)
		return -EFAULT;

	*lenp = len;
	*ppos = len;
	return 0;

} /* end procctl_frv_cachemode() */

/*****************************************************************************/
/*
 * permit the mm_struct the nominated process is using have its MMU context ID pinned
 */
#ifdef CONFIG_MMU
static int procctl_frv_pin_cxnr(ctl_table *table, int write,
				void __user *buffer, size_t *lenp,
				loff_t *ppos)
{
	pid_t pid;
	char buff[16], *p;
	int len;

	len = *lenp;

	if (write) {
		/* potential state change */
		if (len <= 1 || len > sizeof(buff) - 1)
			return -EINVAL;

		if (copy_from_user(buff, buffer, len) != 0)
			return -EFAULT;

		if (buff[len - 1] == '\n')
			buff[len - 1] = '\0';
		else
			buff[len] = '\0';

		pid = simple_strtoul(buff, &p, 10);
		if (*p)
			return -EINVAL;

		return cxn_pin_by_pid(pid);
	}

	/* read the currently pinned CXN */
	if (*ppos > 0) {
		*lenp = 0;
		return 0;
	}

	len = snprintf(buff, sizeof(buff), "%d\n", cxn_pinned);
	if (len > *lenp)
		len = *lenp;

	if (copy_to_user(buffer, buff, len) != 0)
		return -EFAULT;

	*lenp = len;
	*ppos = len;
	return 0;

} /* end procctl_frv_pin_cxnr() */
#endif

/*
 * FR-V specific sysctls
 */
static struct ctl_table frv_table[] =
{
	{
		.procname 	= "cache-mode",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= procctl_frv_cachemode,
	},
#ifdef CONFIG_MMU
	{
		.procname	= "pin-cxnr",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= procctl_frv_pin_cxnr
	},
#endif
	{}
};

/*
 * Use a temporary sysctl number. Horrid, but will be cleaned up in 2.6
 * when all the PM interfaces exist nicely.
 */
static struct ctl_table frv_dir_table[] =
{
	{
		.procname	= "frv",
		.mode 		= 0555,
		.child		= frv_table
	},
	{}
};

/*
 * Initialize power interface
 */
static int __init frv_sysctl_init(void)
{
	register_sysctl_table(frv_dir_table);
	return 0;
}

__initcall(frv_sysctl_init);
