/*
 * procfs_example.c: an example proc interface
 *
 * Copyright (C) 2001, Erik Mouw (mouw@nl.linux.org)
 *
 * This file accompanies the procfs-guide in the Linux kernel
 * source. Its main use is to demonstrate the concepts and
 * functions described in the guide.
 *
 * This software has been developed while working on the LART
 * computing board (http://www.lartmaker.nl), which was sponsored
 * by the Delt University of Technology projects Mobile Multi-media
 * Communications and Ubiquitous Communications.
 *
 * This program is free software; you can redistribute
 * it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more
 * details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place,
 * Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/jiffies.h>
#include <asm/uaccess.h>


#define MODULE_VERS "1.0"
#define MODULE_NAME "procfs_example"

#define FOOBAR_LEN 8

struct fb_data_t {
	char name[FOOBAR_LEN + 1];
	char value[FOOBAR_LEN + 1];
};


static struct proc_dir_entry *example_dir, *foo_file,
	*bar_file, *jiffies_file, *symlink;


struct fb_data_t foo_data, bar_data;


static int proc_read_jiffies(char *page, char **start,
			     off_t off, int count,
			     int *eof, void *data)
{
	int len;

	len = sprintf(page, "jiffies = %ld\n",
                      jiffies);

	return len;
}


static int proc_read_foobar(char *page, char **start,
			    off_t off, int count, 
			    int *eof, void *data)
{
	int len;
	struct fb_data_t *fb_data = (struct fb_data_t *)data;

	/* DON'T DO THAT - buffer overruns are bad */
	len = sprintf(page, "%s = '%s'\n", 
		      fb_data->name, fb_data->value);

	return len;
}


static int proc_write_foobar(struct file *file,
			     const char *buffer,
			     unsigned long count, 
			     void *data)
{
	int len;
	struct fb_data_t *fb_data = (struct fb_data_t *)data;

	if(count > FOOBAR_LEN)
		len = FOOBAR_LEN;
	else
		len = count;

	if(copy_from_user(fb_data->value, buffer, len))
		return -EFAULT;

	fb_data->value[len] = '\0';

	return len;
}


static int __init init_procfs_example(void)
{
	int rv = 0;

	/* create directory */
	example_dir = proc_mkdir(MODULE_NAME, NULL);
	if(example_dir == NULL) {
		rv = -ENOMEM;
		goto out;
	}
	/* create jiffies using convenience function */
	jiffies_file = create_proc_read_entry("jiffies", 
					      0444, example_dir, 
					      proc_read_jiffies,
					      NULL);
	if(jiffies_file == NULL) {
		rv  = -ENOMEM;
		goto no_jiffies;
	}

	/* create foo and bar files using same callback
	 * functions 
	 */
	foo_file = create_proc_entry("foo", 0644, example_dir);
	if(foo_file == NULL) {
		rv = -ENOMEM;
		goto no_foo;
	}

	strcpy(foo_data.name, "foo");
	strcpy(foo_data.value, "foo");
	foo_file->data = &foo_data;
	foo_file->read_proc = proc_read_foobar;
	foo_file->write_proc = proc_write_foobar;
		
	bar_file = create_proc_entry("bar", 0644, example_dir);
	if(bar_file == NULL) {
		rv = -ENOMEM;
		goto no_bar;
	}

	strcpy(bar_data.name, "bar");
	strcpy(bar_data.value, "bar");
	bar_file->data = &bar_data;
	bar_file->read_proc = proc_read_foobar;
	bar_file->write_proc = proc_write_foobar;
		
	/* create symlink */
	symlink = proc_symlink("jiffies_too", example_dir, 
			       "jiffies");
	if(symlink == NULL) {
		rv = -ENOMEM;
		goto no_symlink;
	}

	/* everything OK */
	printk(KERN_INFO "%s %s initialised\n",
	       MODULE_NAME, MODULE_VERS);
	return 0;

no_symlink:
	remove_proc_entry("bar", example_dir);
no_bar:
	remove_proc_entry("foo", example_dir);
no_foo:
	remove_proc_entry("jiffies", example_dir);
no_jiffies:			      
	remove_proc_entry(MODULE_NAME, NULL);
out:
	return rv;
}


static void __exit cleanup_procfs_example(void)
{
	remove_proc_entry("jiffies_too", example_dir);
	remove_proc_entry("bar", example_dir);
	remove_proc_entry("foo", example_dir);
	remove_proc_entry("jiffies", example_dir);
	remove_proc_entry(MODULE_NAME, NULL);

	printk(KERN_INFO "%s %s removed\n",
	       MODULE_NAME, MODULE_VERS);
}


module_init(init_procfs_example);
module_exit(cleanup_procfs_example);

MODULE_AUTHOR("Erik Mouw");
MODULE_DESCRIPTION("procfs examples");
MODULE_LICENSE("GPL");
