/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/init.h"
#include "linux/ctype.h"
#include "linux/proc_fs.h"
#include "asm/uaccess.h"

/* If read and write race, the read will still atomically read a valid
 * value.
 */
int uml_exitcode = 0;

static int read_proc_exitcode(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", uml_exitcode);
	len -= off;
	if(len <= off+count) *eof = 1;
	*start = page + off;
	if(len > count) len = count;
	if(len < 0) len = 0;
	return(len);
}

static int write_proc_exitcode(struct file *file, const char __user *buffer,
			       unsigned long count, void *data)
{
	char *end, buf[sizeof("nnnnn\0")];
	int tmp;

	if(copy_from_user(buf, buffer, count))
		return(-EFAULT);
	tmp = simple_strtol(buf, &end, 0);
	if((*end != '\0') && !isspace(*end))
		return(-EINVAL);
	uml_exitcode = tmp;
	return(count);
}

static int make_proc_exitcode(void)
{
	struct proc_dir_entry *ent;

	ent = create_proc_entry("exitcode", 0600, &proc_root);
	if(ent == NULL){
		printk("make_proc_exitcode : Failed to register "
		       "/proc/exitcode\n");
		return(0);
	}

	ent->read_proc = read_proc_exitcode;
	ent->write_proc = write_proc_exitcode;
	
	return(0);
}

__initcall(make_proc_exitcode);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
