/*
 * Thomas Horsten <thh@lasat.com>
 * Copyright (C) 2000 LASAT Networks A/S.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Routines specific to the LASAT boards
 */
#include <linux/types.h>
#include <asm/lasat/lasat.h>

#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/uaccess.h>

#include <asm/time.h>

#ifdef CONFIG_DS1603
#include "ds1603.h"
#endif


/* And the same for proc */
int proc_dolasatstring(struct ctl_table *table, int write,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	int r;

	r = proc_dostring(table, write, buffer, lenp, ppos);
	if ((!write) || r)
		return r;

	lasat_write_eeprom_info();

	return 0;
}

/* proc function to write EEPROM after changing int entry */
int proc_dolasatint(struct ctl_table *table, int write,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	int r;

	r = proc_dointvec(table, write, buffer, lenp, ppos);
	if ((!write) || r)
		return r;

	lasat_write_eeprom_info();

	return 0;
}

#ifdef CONFIG_DS1603
static int rtctmp;

/* proc function to read/write RealTime Clock */
int proc_dolasatrtc(struct ctl_table *table, int write,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	struct timespec64 ts;
	int r;

	if (!write) {
		read_persistent_clock64(&ts);
		rtctmp = ts.tv_sec;
		/* check for time < 0 and set to 0 */
		if (rtctmp < 0)
			rtctmp = 0;
	}
	r = proc_dointvec(table, write, buffer, lenp, ppos);
	if (r)
		return r;

	if (write)
		rtc_mips_set_mmss(rtctmp);

	return 0;
}
#endif

#ifdef CONFIG_INET
int proc_lasat_ip(struct ctl_table *table, int write,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	unsigned int ip;
	char *p, c;
	int len;
	char ipbuf[32];

	if (!table->data || !table->maxlen || !*lenp ||
	    (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		len = 0;
		p = buffer;
		while (len < *lenp) {
			if (get_user(c, p++))
				return -EFAULT;
			if (c == 0 || c == '\n')
				break;
			len++;
		}
		if (len >= sizeof(ipbuf)-1)
			len = sizeof(ipbuf) - 1;
		if (copy_from_user(ipbuf, buffer, len))
			return -EFAULT;
		ipbuf[len] = 0;
		*ppos += *lenp;
		/* Now see if we can convert it to a valid IP */
		ip = in_aton(ipbuf);
		*(unsigned int *)(table->data) = ip;
		lasat_write_eeprom_info();
	} else {
		ip = *(unsigned int *)(table->data);
		sprintf(ipbuf, "%d.%d.%d.%d",
			(ip)	   & 0xff,
			(ip >>	8) & 0xff,
			(ip >> 16) & 0xff,
			(ip >> 24) & 0xff);
		len = strlen(ipbuf);
		if (len > *lenp)
			len = *lenp;
		if (len)
			if (copy_to_user(buffer, ipbuf, len))
				return -EFAULT;
		if (len < *lenp) {
			if (put_user('\n', ((char *) buffer) + len))
				return -EFAULT;
			len++;
		}
		*lenp = len;
		*ppos += len;
	}

	return 0;
}
#endif

int proc_lasat_prid(struct ctl_table *table, int write,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	int r;

	r = proc_dointvec(table, write, buffer, lenp, ppos);
	if (r < 0)
		return r;
	if (write) {
		lasat_board_info.li_eeprom_info.prid =
			lasat_board_info.li_prid;
		lasat_write_eeprom_info();
		lasat_init_board_info();
	}
	return 0;
}

extern int lasat_boot_to_service;

static struct ctl_table lasat_table[] = {
	{
		.procname	= "cpu-hz",
		.data		= &lasat_board_info.li_cpu_hz,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "bus-hz",
		.data		= &lasat_board_info.li_bus_hz,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "bmid",
		.data		= &lasat_board_info.li_bmid,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "prid",
		.data		= &lasat_board_info.li_prid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_lasat_prid,
	},
#ifdef CONFIG_INET
	{
		.procname	= "ipaddr",
		.data		= &lasat_board_info.li_eeprom_info.ipaddr,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_lasat_ip,
	},
	{
		.procname	= "netmask",
		.data		= &lasat_board_info.li_eeprom_info.netmask,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_lasat_ip,
	},
#endif
	{
		.procname	= "passwd_hash",
		.data		= &lasat_board_info.li_eeprom_info.passwd_hash,
		.maxlen		=
			sizeof(lasat_board_info.li_eeprom_info.passwd_hash),
		.mode		= 0600,
		.proc_handler	= proc_dolasatstring,
	},
	{
		.procname	= "boot-service",
		.data		= &lasat_boot_to_service,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_DS1603
	{
		.procname	= "rtc",
		.data		= &rtctmp,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dolasatrtc,
	},
#endif
	{
		.procname	= "namestr",
		.data		= &lasat_board_info.li_namestr,
		.maxlen		= sizeof(lasat_board_info.li_namestr),
		.mode		= 0444,
		.proc_handler	= proc_dostring,
	},
	{
		.procname	= "typestr",
		.data		= &lasat_board_info.li_typestr,
		.maxlen		= sizeof(lasat_board_info.li_typestr),
		.mode		= 0444,
		.proc_handler	= proc_dostring,
	},
	{}
};

static struct ctl_table lasat_root_table[] = {
	{
		.procname	= "lasat",
		.mode		=  0555,
		.child		= lasat_table
	},
	{}
};

static int __init lasat_register_sysctl(void)
{
	struct ctl_table_header *lasat_table_header;

	lasat_table_header =
		register_sysctl_table(lasat_root_table);
	if (!lasat_table_header) {
		printk(KERN_ERR "Unable to register LASAT sysctl\n");
		return -ENOMEM;
	}

	return 0;
}

__initcall(lasat_register_sysctl);
