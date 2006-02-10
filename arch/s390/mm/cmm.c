/*
 *  arch/s390/mm/cmm.c
 *
 *  S390 version
 *    Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Collaborative memory management interface.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sysctl.h>
#include <linux/ctype.h>

#include <asm/pgalloc.h>
#include <asm/uaccess.h>

static char *sender = "VMRMSVM";
module_param(sender, charp, 0400);
MODULE_PARM_DESC(sender,
		 "Guest name that may send SMSG messages (default VMRMSVM)");

#include "../../../drivers/s390/net/smsgiucv.h"

#define CMM_NR_PAGES ((PAGE_SIZE / sizeof(unsigned long)) - 2)

struct cmm_page_array {
	struct cmm_page_array *next;
	unsigned long index;
	unsigned long pages[CMM_NR_PAGES];
};

static long cmm_pages = 0;
static long cmm_timed_pages = 0;
static volatile long cmm_pages_target = 0;
static volatile long cmm_timed_pages_target = 0;
static long cmm_timeout_pages = 0;
static long cmm_timeout_seconds = 0;

static struct cmm_page_array *cmm_page_list = NULL;
static struct cmm_page_array *cmm_timed_page_list = NULL;

static unsigned long cmm_thread_active = 0;
static struct work_struct cmm_thread_starter;
static wait_queue_head_t cmm_thread_wait;
static struct timer_list cmm_timer;

static void cmm_timer_fn(unsigned long);
static void cmm_set_timer(void);

static long
cmm_strtoul(const char *cp, char **endp)
{
	unsigned int base = 10;

	if (*cp == '0') {
		base = 8;
		cp++;
		if ((*cp == 'x' || *cp == 'X') && isxdigit(cp[1])) {
			base = 16;
			cp++;
		}
	}
	return simple_strtoul(cp, endp, base);
}

static long
cmm_alloc_pages(long pages, long *counter, struct cmm_page_array **list)
{
	struct cmm_page_array *pa;
	unsigned long page;

	pa = *list;
	while (pages) {
		page = __get_free_page(GFP_NOIO);
		if (!page)
			break;
		if (!pa || pa->index >= CMM_NR_PAGES) {
			/* Need a new page for the page list. */
			pa = (struct cmm_page_array *)
				__get_free_page(GFP_NOIO);
			if (!pa) {
				free_page(page);
				break;
			}
			pa->next = *list;
			pa->index = 0;
			*list = pa;
		}
		diag10(page);
		pa->pages[pa->index++] = page;
		(*counter)++;
		pages--;
	}
	return pages;
}

static void
cmm_free_pages(long pages, long *counter, struct cmm_page_array **list)
{
	struct cmm_page_array *pa;
	unsigned long page;

	pa = *list;
	while (pages) {
		if (!pa || pa->index <= 0)
			break;
		page = pa->pages[--pa->index];
		if (pa->index == 0) {
			pa = pa->next;
			free_page((unsigned long) *list);
			*list = pa;
		}
		free_page(page);
		(*counter)--;
		pages--;
	}
}

static int
cmm_thread(void *dummy)
{
	int rc;

	daemonize("cmmthread");
	while (1) {
		rc = wait_event_interruptible(cmm_thread_wait,
			(cmm_pages != cmm_pages_target ||
			 cmm_timed_pages != cmm_timed_pages_target));
		if (rc == -ERESTARTSYS) {
			/* Got kill signal. End thread. */
			clear_bit(0, &cmm_thread_active);
			cmm_pages_target = cmm_pages;
			cmm_timed_pages_target = cmm_timed_pages;
			break;
		}
		if (cmm_pages_target > cmm_pages) {
			if (cmm_alloc_pages(1, &cmm_pages, &cmm_page_list))
				cmm_pages_target = cmm_pages;
		} else if (cmm_pages_target < cmm_pages) {
			cmm_free_pages(1, &cmm_pages, &cmm_page_list);
		}
		if (cmm_timed_pages_target > cmm_timed_pages) {
			if (cmm_alloc_pages(1, &cmm_timed_pages,
					   &cmm_timed_page_list))
				cmm_timed_pages_target = cmm_timed_pages;
		} else if (cmm_timed_pages_target < cmm_timed_pages) {
			cmm_free_pages(1, &cmm_timed_pages,
			       	       &cmm_timed_page_list);
		}
		if (cmm_timed_pages > 0 && !timer_pending(&cmm_timer))
			cmm_set_timer();
	}
	return 0;
}

static void
cmm_start_thread(void)
{
	kernel_thread(cmm_thread, 0, 0);
}

static void
cmm_kick_thread(void)
{
	if (!test_and_set_bit(0, &cmm_thread_active))
		schedule_work(&cmm_thread_starter);
	wake_up(&cmm_thread_wait);
}

static void
cmm_set_timer(void)
{
	if (cmm_timed_pages_target <= 0 || cmm_timeout_seconds <= 0) {
		if (timer_pending(&cmm_timer))
			del_timer(&cmm_timer);
		return;
	}
	if (timer_pending(&cmm_timer)) {
		if (mod_timer(&cmm_timer, jiffies + cmm_timeout_seconds*HZ))
			return;
	}
	cmm_timer.function = cmm_timer_fn;
	cmm_timer.data = 0;
	cmm_timer.expires = jiffies + cmm_timeout_seconds*HZ;
	add_timer(&cmm_timer);
}

static void
cmm_timer_fn(unsigned long ignored)
{
	long pages;

	pages = cmm_timed_pages_target - cmm_timeout_pages;
	if (pages < 0)
		cmm_timed_pages_target = 0;
	else
		cmm_timed_pages_target = pages;
	cmm_kick_thread();
	cmm_set_timer();
}

void
cmm_set_pages(long pages)
{
	cmm_pages_target = pages;
	cmm_kick_thread();
}

long
cmm_get_pages(void)
{
	return cmm_pages;
}

void
cmm_add_timed_pages(long pages)
{
	cmm_timed_pages_target += pages;
	cmm_kick_thread();
}

long
cmm_get_timed_pages(void)
{
	return cmm_timed_pages;
}

void
cmm_set_timeout(long pages, long seconds)
{
	cmm_timeout_pages = pages;
	cmm_timeout_seconds = seconds;
	cmm_set_timer();
}

static inline int
cmm_skip_blanks(char *cp, char **endp)
{
	char *str;

	for (str = cp; *str == ' ' || *str == '\t'; str++);
	*endp = str;
	return str != cp;
}

#ifdef CONFIG_CMM_PROC
/* These will someday get removed. */
#define VM_CMM_PAGES		1111
#define VM_CMM_TIMED_PAGES	1112
#define VM_CMM_TIMEOUT		1113

static struct ctl_table cmm_table[];

static int
cmm_pages_handler(ctl_table *ctl, int write, struct file *filp,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	char buf[16], *p;
	long pages;
	int len;

	if (!*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		len = *lenp;
		if (copy_from_user(buf, buffer,
				   len > sizeof(buf) ? sizeof(buf) : len))
			return -EFAULT;
		buf[sizeof(buf) - 1] = '\0';
		cmm_skip_blanks(buf, &p);
		pages = cmm_strtoul(p, &p);
		if (ctl == &cmm_table[0])
			cmm_set_pages(pages);
		else
			cmm_add_timed_pages(pages);
	} else {
		if (ctl == &cmm_table[0])
			pages = cmm_get_pages();
		else
			pages = cmm_get_timed_pages();
		len = sprintf(buf, "%ld\n", pages);
		if (len > *lenp)
			len = *lenp;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
	}
	*lenp = len;
	*ppos += len;
	return 0;
}

static int
cmm_timeout_handler(ctl_table *ctl, int write, struct file *filp,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	char buf[64], *p;
	long pages, seconds;
	int len;

	if (!*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		len = *lenp;
		if (copy_from_user(buf, buffer,
				   len > sizeof(buf) ? sizeof(buf) : len))
			return -EFAULT;
		buf[sizeof(buf) - 1] = '\0';
		cmm_skip_blanks(buf, &p);
		pages = cmm_strtoul(p, &p);
		cmm_skip_blanks(p, &p);
		seconds = cmm_strtoul(p, &p);
		cmm_set_timeout(pages, seconds);
	} else {
		len = sprintf(buf, "%ld %ld\n",
			      cmm_timeout_pages, cmm_timeout_seconds);
		if (len > *lenp)
			len = *lenp;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
	}
	*lenp = len;
	*ppos += len;
	return 0;
}

static struct ctl_table cmm_table[] = {
	{
		.ctl_name	= VM_CMM_PAGES,
		.procname	= "cmm_pages",
		.mode		= 0600,
		.proc_handler	= &cmm_pages_handler,
	},
	{
		.ctl_name	= VM_CMM_TIMED_PAGES,
		.procname	= "cmm_timed_pages",
		.mode		= 0600,
		.proc_handler	= &cmm_pages_handler,
	},
	{
		.ctl_name	= VM_CMM_TIMEOUT,
		.procname	= "cmm_timeout",
		.mode		= 0600,
		.proc_handler	= &cmm_timeout_handler,
	},
	{ .ctl_name = 0 }
};

static struct ctl_table cmm_dir_table[] = {
	{
		.ctl_name	= CTL_VM,
		.procname	= "vm",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= cmm_table,
	},
	{ .ctl_name = 0 }
};
#endif

#ifdef CONFIG_CMM_IUCV
#define SMSG_PREFIX "CMM"
static void
cmm_smsg_target(char *from, char *msg)
{
	long pages, seconds;

	if (strlen(sender) > 0 && strcmp(from, sender) != 0)
		return;
	if (!cmm_skip_blanks(msg + strlen(SMSG_PREFIX), &msg))
		return;
	if (strncmp(msg, "SHRINK", 6) == 0) {
		if (!cmm_skip_blanks(msg + 6, &msg))
			return;
		pages = cmm_strtoul(msg, &msg);
		cmm_skip_blanks(msg, &msg);
		if (*msg == '\0')
			cmm_set_pages(pages);
	} else if (strncmp(msg, "RELEASE", 7) == 0) {
		if (!cmm_skip_blanks(msg + 7, &msg))
			return;
		pages = cmm_strtoul(msg, &msg);
		cmm_skip_blanks(msg, &msg);
		if (*msg == '\0')
			cmm_add_timed_pages(pages);
	} else if (strncmp(msg, "REUSE", 5) == 0) {
		if (!cmm_skip_blanks(msg + 5, &msg))
			return;
		pages = cmm_strtoul(msg, &msg);
		if (!cmm_skip_blanks(msg, &msg))
			return;
		seconds = cmm_strtoul(msg, &msg);
		cmm_skip_blanks(msg, &msg);
		if (*msg == '\0')
			cmm_set_timeout(pages, seconds);
	}
}
#endif

struct ctl_table_header *cmm_sysctl_header;

static int
cmm_init (void)
{
#ifdef CONFIG_CMM_PROC
	cmm_sysctl_header = register_sysctl_table(cmm_dir_table, 1);
#endif
#ifdef CONFIG_CMM_IUCV
	smsg_register_callback(SMSG_PREFIX, cmm_smsg_target);
#endif
	INIT_WORK(&cmm_thread_starter, (void *) cmm_start_thread, NULL);
	init_waitqueue_head(&cmm_thread_wait);
	init_timer(&cmm_timer);
	return 0;
}

static void
cmm_exit(void)
{
	cmm_free_pages(cmm_pages, &cmm_pages, &cmm_page_list);
	cmm_free_pages(cmm_timed_pages, &cmm_timed_pages, &cmm_timed_page_list);
#ifdef CONFIG_CMM_PROC
	unregister_sysctl_table(cmm_sysctl_header);
#endif
#ifdef CONFIG_CMM_IUCV
	smsg_unregister_callback(SMSG_PREFIX, cmm_smsg_target);
#endif
}

module_init(cmm_init);
module_exit(cmm_exit);

EXPORT_SYMBOL(cmm_set_pages);
EXPORT_SYMBOL(cmm_get_pages);
EXPORT_SYMBOL(cmm_add_timed_pages);
EXPORT_SYMBOL(cmm_get_timed_pages);
EXPORT_SYMBOL(cmm_set_timeout);

MODULE_LICENSE("GPL");
