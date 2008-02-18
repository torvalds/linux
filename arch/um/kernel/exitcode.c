/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <asm/uaccess.h>

/*
 * If read and write race, the read will still atomically read a valid
 * value.
 */
int uml_exitcode = 0;

static int read_proc_exitcode(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	int len, val;

	/*
	 * Save uml_exitcode in a local so that we don't need to guarantee
	 * that sprintf accesses it atomically.
	 */
	val = uml_exitcode;
	len = sprintf(page, "%d\n", val);
	len -= off;
	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int write_proc_exitcode(struct file *file, const char __user *buffer,
			       unsigned long count, void *data)
{
	char *end, buf[sizeof("nnnnn\0")];
	int tmp;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	tmp = simple_strtol(buf, &end, 0);
	if ((*end != '\0') && !isspace(*end))
		return -EINVAL;

	uml_exitcode = tmp;
	return count;
}

static int make_proc_exitcode(void)
{
	struct proc_dir_entry *ent;

	ent = create_proc_entry("exitcode", 0600, &proc_root);
	if (ent == NULL) {
		printk(KERN_WARNING "make_proc_exitcode : Failed to register "
		       "/proc/exitcode\n");
		return 0;
	}

	ent->read_proc = read_proc_exitcode;
	ent->write_proc = write_proc_exitcode;

	return 0;
}

__initcall(make_proc_exitcode);
