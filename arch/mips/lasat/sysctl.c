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
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include <asm/time.h>

#include "sysctl.h"
#include "ds1603.h"

static DEFINE_MUTEX(lasat_info_mutex);

/* Strategy function to write EEPROM after changing string entry */
int sysctl_lasatstring(ctl_table *table, int *name, int nlen,
		void *oldval, size_t *oldlenp,
		void *newval, size_t newlen)
{
	int r;

	mutex_lock(&lasat_info_mutex);
	r = sysctl_string(table, name,
			  nlen, oldval, oldlenp, newval, newlen);
	if (r < 0) {
		mutex_unlock(&lasat_info_mutex);
		return r;
	}
	if (newval && newlen)
		lasat_write_eeprom_info();
	mutex_unlock(&lasat_info_mutex);

	return 1;
}


/* And the same for proc */
int proc_dolasatstring(ctl_table *table, int write, struct file *filp,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	int r;

	mutex_lock(&lasat_info_mutex);
	r = proc_dostring(table, write, filp, buffer, lenp, ppos);
	if ((!write) || r) {
		mutex_unlock(&lasat_info_mutex);
		return r;
	}
	lasat_write_eeprom_info();
	mutex_unlock(&lasat_info_mutex);

	return 0;
}

/* proc function to write EEPROM after changing int entry */
int proc_dolasatint(ctl_table *table, int write, struct file *filp,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	int r;

	mutex_lock(&lasat_info_mutex);
	r = proc_dointvec(table, write, filp, buffer, lenp, ppos);
	if ((!write) || r) {
		mutex_unlock(&lasat_info_mutex);
		return r;
	}
	lasat_write_eeprom_info();
	mutex_unlock(&lasat_info_mutex);

	return 0;
}

static int rtctmp;

#ifdef CONFIG_DS1603
/* proc function to read/write RealTime Clock */
int proc_dolasatrtc(ctl_table *table, int write, struct file *filp,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	int r;

	mutex_lock(&lasat_info_mutex);
	if (!write) {
		rtctmp = read_persistent_clock();
		/* check for time < 0 and set to 0 */
		if (rtctmp < 0)
			rtctmp = 0;
	}
	r = proc_dointvec(table, write, filp, buffer, lenp, ppos);
	if ((!write) || r) {
		mutex_unlock(&lasat_info_mutex);
		return r;
	}
	rtc_mips_set_mmss(rtctmp);
	mutex_unlock(&lasat_info_mutex);

	return 0;
}
#endif

/* Sysctl for setting the IP addresses */
int sysctl_lasat_intvec(ctl_table *table, int *name, int nlen,
		    void *oldval, size_t *oldlenp,
		    void *newval, size_t newlen)
{
	int r;

	mutex_lock(&lasat_info_mutex);
	r = sysctl_intvec(table, name, nlen, oldval, oldlenp, newval, newlen);
	if (r < 0) {
		mutex_unlock(&lasat_info_mutex);
		return r;
	}
	if (newval && newlen)
		lasat_write_eeprom_info();
	mutex_unlock(&lasat_info_mutex);

	return 1;
}

#ifdef CONFIG_DS1603
/* Same for RTC */
int sysctl_lasat_rtc(ctl_table *table, int *name, int nlen,
		    void *oldval, size_t *oldlenp,
		    void *newval, size_t newlen)
{
	int r;

	mutex_lock(&lasat_info_mutex);
	rtctmp = read_persistent_clock();
	if (rtctmp < 0)
		rtctmp = 0;
	r = sysctl_intvec(table, name, nlen, oldval, oldlenp, newval, newlen);
	if (r < 0) {
		mutex_unlock(&lasat_info_mutex);
		return r;
	}
	if (newval && newlen)
		rtc_mips_set_mmss(rtctmp);
	mutex_unlock(&lasat_info_mutex);

	return 1;
}
#endif

#ifdef CONFIG_INET
static char lasat_bcastaddr[16];

void update_bcastaddr(void)
{
	unsigned int ip;

	ip = (lasat_board_info.li_eeprom_info.ipaddr &
		lasat_board_info.li_eeprom_info.netmask) |
		~lasat_board_info.li_eeprom_info.netmask;

	sprintf(lasat_bcastaddr, "%d.%d.%d.%d",
			(ip)       & 0xff,
			(ip >>  8) & 0xff,
			(ip >> 16) & 0xff,
			(ip >> 24) & 0xff);
}

static char proc_lasat_ipbuf[32];

/* Parsing of IP address */
int proc_lasat_ip(ctl_table *table, int write, struct file *filp,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	unsigned int ip;
	char *p, c;
	int len;

	if (!table->data || !table->maxlen || !*lenp ||
	    (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	mutex_lock(&lasat_info_mutex);
	if (write) {
		len = 0;
		p = buffer;
		while (len < *lenp) {
			if (get_user(c, p++)) {
				mutex_unlock(&lasat_info_mutex);
				return -EFAULT;
			}
			if (c == 0 || c == '\n')
				break;
			len++;
		}
		if (len >= sizeof(proc_lasat_ipbuf)-1)
			len = sizeof(proc_lasat_ipbuf) - 1;
		if (copy_from_user(proc_lasat_ipbuf, buffer, len)) {
			mutex_unlock(&lasat_info_mutex);
			return -EFAULT;
		}
		proc_lasat_ipbuf[len] = 0;
		*ppos += *lenp;
		/* Now see if we can convert it to a valid IP */
		ip = in_aton(proc_lasat_ipbuf);
		*(unsigned int *)(table->data) = ip;
		lasat_write_eeprom_info();
	} else {
		ip = *(unsigned int *)(table->data);
		sprintf(proc_lasat_ipbuf, "%d.%d.%d.%d",
			(ip)       & 0xff,
			(ip >>  8) & 0xff,
			(ip >> 16) & 0xff,
			(ip >> 24) & 0xff);
		len = strlen(proc_lasat_ipbuf);
		if (len > *lenp)
			len = *lenp;
		if (len)
			if (copy_to_user(buffer, proc_lasat_ipbuf, len)) {
				mutex_unlock(&lasat_info_mutex);
				return -EFAULT;
			}
		if (len < *lenp) {
			if (put_user('\n', ((char *) buffer) + len)) {
				mutex_unlock(&lasat_info_mutex);
				return -EFAULT;
			}
			len++;
		}
		*lenp = len;
		*ppos += len;
	}
	update_bcastaddr();
	mutex_unlock(&lasat_info_mutex);

	return 0;
}
#endif /* defined(CONFIG_INET) */

static int sysctl_lasat_eeprom_value(ctl_table *table, int *name, int nlen,
				     void *oldval, size_t *oldlenp,
				     void *newval, size_t newlen)
{
	int r;

	mutex_lock(&lasat_info_mutex);
	r = sysctl_intvec(table, name, nlen, oldval, oldlenp, newval, newlen);
	if (r < 0) {
		mutex_unlock(&lasat_info_mutex);
		return r;
	}

	if (newval && newlen) {
		if (name && *name == LASAT_PRID)
			lasat_board_info.li_eeprom_info.prid = *(int *)newval;

		lasat_write_eeprom_info();
		lasat_init_board_info();
	}
	mutex_unlock(&lasat_info_mutex);

	return 0;
}

int proc_lasat_eeprom_value(ctl_table *table, int write, struct file *filp,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	int r;

	mutex_lock(&lasat_info_mutex);
	r = proc_dointvec(table, write, filp, buffer, lenp, ppos);
	if ((!write) || r) {
		mutex_unlock(&lasat_info_mutex);
		return r;
	}
	if (filp && filp->f_path.dentry) {
		if (!strcmp(filp->f_path.dentry->d_name.name, "prid"))
			lasat_board_info.li_eeprom_info.prid =
				lasat_board_info.li_prid;
		if (!strcmp(filp->f_path.dentry->d_name.name, "debugaccess"))
			lasat_board_info.li_eeprom_info.debugaccess =
				lasat_board_info.li_debugaccess;
	}
	lasat_write_eeprom_info();
	mutex_unlock(&lasat_info_mutex);

	return 0;
}

extern int lasat_boot_to_service;

#ifdef CONFIG_SYSCTL

static ctl_table lasat_table[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "cpu-hz",
		.data		= &lasat_board_info.li_cpu_hz,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "bus-hz",
		.data		= &lasat_board_info.li_bus_hz,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "bmid",
		.data		= &lasat_board_info.li_bmid,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "prid",
		.data		= &lasat_board_info.li_prid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_lasat_eeprom_value,
		.strategy	= &sysctl_lasat_eeprom_value
	},
#ifdef CONFIG_INET
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "ipaddr",
		.data		= &lasat_board_info.li_eeprom_info.ipaddr,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_lasat_ip,
		.strategy	= &sysctl_lasat_intvec
	},
	{
		.ctl_name	= LASAT_NETMASK,
		.procname	= "netmask",
		.data		= &lasat_board_info.li_eeprom_info.netmask,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_lasat_ip,
		.strategy	= &sysctl_lasat_intvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "bcastaddr",
		.data		= &lasat_bcastaddr,
		.maxlen		= sizeof(lasat_bcastaddr),
		.mode		= 0600,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string
	},
#endif
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "passwd_hash",
		.data		= &lasat_board_info.li_eeprom_info.passwd_hash,
		.maxlen		=
			sizeof(lasat_board_info.li_eeprom_info.passwd_hash),
		.mode		= 0600,
		.proc_handler	= &proc_dolasatstring,
		.strategy	= &sysctl_lasatstring
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "boot-service",
		.data		= &lasat_boot_to_service,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
#ifdef CONFIG_DS1603
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "rtc",
		.data		= &rtctmp,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dolasatrtc,
		.strategy	= &sysctl_lasat_rtc
	},
#endif
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "namestr",
		.data		= &lasat_board_info.li_namestr,
		.maxlen		= sizeof(lasat_board_info.li_namestr),
		.mode		= 0444,
		.proc_handler	=  &proc_dostring,
		.strategy	= &sysctl_string
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "typestr",
		.data		= &lasat_board_info.li_typestr,
		.maxlen		= sizeof(lasat_board_info.li_typestr),
		.mode		= 0444,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string
	},
	{}
};

static ctl_table lasat_root_table[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
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

	return 0;
}

__initcall(lasat_register_sysctl);
#endif /* CONFIG_SYSCTL */
