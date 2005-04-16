/*
 *  drivers/s390/cio/blacklist.c
 *   S/390 common I/O routines -- blacklisting of specific devices
 *   $Revision: 1.33 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *			      IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Cornelia Huck (cohuck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/device.h>

#include <asm/cio.h>
#include <asm/uaccess.h>

#include "blacklist.h"
#include "cio.h"
#include "cio_debug.h"
#include "css.h"

/*
 * "Blacklisting" of certain devices:
 * Device numbers given in the commandline as cio_ignore=... won't be known
 * to Linux.
 *
 * These can be single devices or ranges of devices
 */

/* 65536 bits to indicate if a devno is blacklisted or not */
#define __BL_DEV_WORDS (__MAX_SUBCHANNELS + (8*sizeof(long) - 1) / \
			 (8*sizeof(long)))
static unsigned long bl_dev[__BL_DEV_WORDS];
typedef enum {add, free} range_action;

/*
 * Function: blacklist_range
 * (Un-)blacklist the devices from-to
 */
static inline void
blacklist_range (range_action action, unsigned int from, unsigned int to)
{
	if (!to)
		to = from;

	if (from > to || to > __MAX_SUBCHANNELS) {
		printk (KERN_WARNING "Invalid blacklist range "
			"0x%04x to 0x%04x, skipping\n", from, to);
		return;
	}
	for (; from <= to; from++) {
		if (action == add)
			set_bit (from, bl_dev);
		else
			clear_bit (from, bl_dev);
	}
}

/*
 * Function: blacklist_busid
 * Get devno/busid from given string.
 * Shamelessly grabbed from dasd_devmap.c.
 */
static inline int
blacklist_busid(char **str, int *id0, int *id1, int *devno)
{
	int val, old_style;
	char *sav;

	sav = *str;

	/* check for leading '0x' */
	old_style = 0;
	if ((*str)[0] == '0' && (*str)[1] == 'x') {
		*str += 2;
		old_style = 1;
	}
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		goto confused;
	val = simple_strtoul(*str, str, 16);
	if (old_style || (*str)[0] != '.') {
		*id0 = *id1 = 0;
		if (val < 0 || val > 0xffff)
			goto confused;
		*devno = val;
		if ((*str)[0] != ',' && (*str)[0] != '-' &&
		    (*str)[0] != '\n' && (*str)[0] != '\0')
			goto confused;
		return 0;
	}
	/* New style x.y.z busid */
	if (val < 0 || val > 0xff)
		goto confused;
	*id0 = val;
	(*str)++;
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		goto confused;
	val = simple_strtoul(*str, str, 16);
	if (val < 0 || val > 0xff || (*str)++[0] != '.')
		goto confused;
	*id1 = val;
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		goto confused;
	val = simple_strtoul(*str, str, 16);
	if (val < 0 || val > 0xffff)
		goto confused;
	*devno = val;
	if ((*str)[0] != ',' && (*str)[0] != '-' &&
	    (*str)[0] != '\n' && (*str)[0] != '\0')
		goto confused;
	return 0;
confused:
	strsep(str, ",\n");
	printk(KERN_WARNING "Invalid cio_ignore parameter '%s'\n", sav);
	return 1;
}

static inline int
blacklist_parse_parameters (char *str, range_action action)
{
	unsigned int from, to, from_id0, to_id0, from_id1, to_id1;

	while (*str != 0 && *str != '\n') {
		range_action ra = action;
		while(*str == ',')
			str++;
		if (*str == '!') {
			ra = !action;
			++str;
		}

		/*
		 * Since we have to parse the proc commands and the
		 * kernel arguments we have to check four cases
		 */
		if (strncmp(str,"all,",4) == 0 || strcmp(str,"all") == 0 ||
		    strncmp(str,"all\n",4) == 0 || strncmp(str,"all ",4) == 0) {
			from = 0;
			to = __MAX_SUBCHANNELS;
			str += 3;
		} else {
			int rc;

			rc = blacklist_busid(&str, &from_id0,
					     &from_id1, &from);
			if (rc)
				continue;
			to = from;
			to_id0 = from_id0;
			to_id1 = from_id1;
			if (*str == '-') {
				str++;
				rc = blacklist_busid(&str, &to_id0,
						     &to_id1, &to);
				if (rc)
					continue;
			}
			if (*str == '-') {
				printk(KERN_WARNING "invalid cio_ignore "
					"parameter '%s'\n",
					strsep(&str, ",\n"));
				continue;
			}
			if ((from_id0 != to_id0) || (from_id1 != to_id1)) {
				printk(KERN_WARNING "invalid cio_ignore range "
					"%x.%x.%04x-%x.%x.%04x\n",
					from_id0, from_id1, from,
					to_id0, to_id1, to);
				continue;
			}
		}
		/* FIXME: ignoring id0 and id1 here. */
		pr_debug("blacklist_setup: adding range "
			 "from 0.0.%04x to 0.0.%04x\n", from, to);
		blacklist_range (ra, from, to);
	}
	return 1;
}

/* Parsing the commandline for blacklist parameters, e.g. to blacklist
 * bus ids 0.0.1234, 0.0.1235 and 0.0.1236, you could use any of:
 * - cio_ignore=1234-1236
 * - cio_ignore=0x1234-0x1235,1236
 * - cio_ignore=0x1234,1235-1236
 * - cio_ignore=1236 cio_ignore=1234-0x1236
 * - cio_ignore=1234 cio_ignore=1236 cio_ignore=0x1235
 * - cio_ignore=0.0.1234-0.0.1236
 * - cio_ignore=0.0.1234,0x1235,1236
 * - ...
 */
static int __init
blacklist_setup (char *str)
{
	CIO_MSG_EVENT(6, "Reading blacklist parameters\n");
	return blacklist_parse_parameters (str, add);
}

__setup ("cio_ignore=", blacklist_setup);

/* Checking if devices are blacklisted */

/*
 * Function: is_blacklisted
 * Returns 1 if the given devicenumber can be found in the blacklist,
 * otherwise 0.
 * Used by validate_subchannel()
 */
int
is_blacklisted (int devno)
{
	return test_bit (devno, bl_dev);
}

#ifdef CONFIG_PROC_FS
/*
 * Function: s390_redo_validation
 * Look for no longer blacklisted devices
 * FIXME: there must be a better way to do this */
static inline void
s390_redo_validation (void)
{
	unsigned int irq;

	CIO_TRACE_EVENT (0, "redoval");
	for (irq = 0; irq < __MAX_SUBCHANNELS; irq++) {
		int ret;
		struct subchannel *sch;

		sch = get_subchannel_by_schid(irq);
		if (sch) {
			/* Already known. */
			put_device(&sch->dev);
			continue;
		}
		ret = css_probe_device(irq);
		if (ret == -ENXIO)
			break; /* We're through. */
		if (ret == -ENOMEM)
			/*
			 * Stop validation for now. Bad, but no need for a
			 * panic.
			 */
			break;
	}
}

/*
 * Function: blacklist_parse_proc_parameters
 * parse the stuff which is piped to /proc/cio_ignore
 */
static inline void
blacklist_parse_proc_parameters (char *buf)
{
	if (strncmp (buf, "free ", 5) == 0) {
		blacklist_parse_parameters (buf + 5, free);
	} else if (strncmp (buf, "add ", 4) == 0) {
		/* 
		 * We don't need to check for known devices since
		 * css_probe_device will handle this correctly. 
		 */
		blacklist_parse_parameters (buf + 4, add);
	} else {
		printk (KERN_WARNING "cio_ignore: Parse error; \n"
			KERN_WARNING "try using 'free all|<devno-range>,"
				     "<devno-range>,...'\n"
			KERN_WARNING "or 'add <devno-range>,"
				     "<devno-range>,...'\n");
		return;
	}

	s390_redo_validation ();
}

/* FIXME: These should be real bus ids and not home-grown ones! */
static int cio_ignore_read (char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	const unsigned int entry_size = 18; /* "0.0.ABCD-0.0.EFGH\n" */
	long devno;
	int len;

	len = 0;
	for (devno = off; /* abuse the page variable
			   * as counter, see fs/proc/generic.c */
	     devno <= __MAX_SUBCHANNELS && len + entry_size < count; devno++) {
		if (!test_bit(devno, bl_dev))
			continue;
		len += sprintf(page + len, "0.0.%04lx", devno);
		if (test_bit(devno + 1, bl_dev)) { /* print range */
			while (++devno < __MAX_SUBCHANNELS)
				if (!test_bit(devno, bl_dev))
					break;
			len += sprintf(page + len, "-0.0.%04lx", --devno);
		}
		len += sprintf(page + len, "\n");
	}

	if (devno <= __MAX_SUBCHANNELS)
		*eof = 1;
	*start = (char *) (devno - off); /* number of checked entries */
	return len;
}

static int cio_ignore_write(struct file *file, const char __user *user_buf,
			     unsigned long user_len, void *data)
{
	char *buf;

	if (user_len > 65536)
		user_len = 65536;
	buf = vmalloc (user_len + 1); /* maybe better use the stack? */
	if (buf == NULL)
		return -ENOMEM;
	if (strncpy_from_user (buf, user_buf, user_len) < 0) {
		vfree (buf);
		return -EFAULT;
	}
	buf[user_len] = '\0';

	blacklist_parse_proc_parameters (buf);

	vfree (buf);
	return user_len;
}

static int
cio_ignore_proc_init (void)
{
	struct proc_dir_entry *entry;

	entry = create_proc_entry ("cio_ignore", S_IFREG | S_IRUGO | S_IWUSR,
				   &proc_root);
	if (!entry)
		return 0;

	entry->read_proc  = cio_ignore_read;
	entry->write_proc = cio_ignore_write;

	return 1;
}

__initcall (cio_ignore_proc_init);

#endif /* CONFIG_PROC_FS */
