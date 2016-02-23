/*
 * srm_env.c - Access to SRM environment
 *             variables through linux' procfs
 *
 * (C) 2001,2002,2006 by Jan-Benedict Glaw <jbglaw@lug-owl.de>
 *
 * This driver is a modified version of Erik Mouw's example proc
 * interface, so: thank you, Erik! He can be reached via email at
 * <J.A.K.Mouw@its.tudelft.nl>. It is based on an idea
 * provided by DEC^WCompaq^WIntel's "Jumpstart" CD. They
 * included a patch like this as well. Thanks for idea!
 *
 * This program is free software; you can redistribute
 * it and/or modify it under the terms of the GNU General
 * Public License version 2 as published by the Free Software
 * Foundation.
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

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/console.h>
#include <asm/uaccess.h>
#include <asm/machvec.h>

#define BASE_DIR	"srm_environment"	/* Subdir in /proc/		*/
#define NAMED_DIR	"named_variables"	/* Subdir for known variables	*/
#define NUMBERED_DIR	"numbered_variables"	/* Subdir for all variables	*/
#define VERSION		"0.0.6"			/* Module version		*/
#define NAME		"srm_env"		/* Module name			*/

MODULE_AUTHOR("Jan-Benedict Glaw <jbglaw@lug-owl.de>");
MODULE_DESCRIPTION("Accessing Alpha SRM environment through procfs interface");
MODULE_LICENSE("GPL");

typedef struct _srm_env {
	char			*name;
	unsigned long		id;
} srm_env_t;

static struct proc_dir_entry	*base_dir;
static struct proc_dir_entry	*named_dir;
static struct proc_dir_entry	*numbered_dir;

static srm_env_t	srm_named_entries[] = {
	{ "auto_action",	ENV_AUTO_ACTION		},
	{ "boot_dev",		ENV_BOOT_DEV		},
	{ "bootdef_dev",	ENV_BOOTDEF_DEV		},
	{ "booted_dev",		ENV_BOOTED_DEV		},
	{ "boot_file",		ENV_BOOT_FILE		},
	{ "booted_file",	ENV_BOOTED_FILE		},
	{ "boot_osflags",	ENV_BOOT_OSFLAGS	},
	{ "booted_osflags",	ENV_BOOTED_OSFLAGS	},
	{ "boot_reset",		ENV_BOOT_RESET		},
	{ "dump_dev",		ENV_DUMP_DEV		},
	{ "enable_audit",	ENV_ENABLE_AUDIT	},
	{ "license",		ENV_LICENSE		},
	{ "char_set",		ENV_CHAR_SET		},
	{ "language",		ENV_LANGUAGE		},
	{ "tty_dev",		ENV_TTY_DEV		},
	{ NULL,			0			},
};

static int srm_env_proc_show(struct seq_file *m, void *v)
{
	unsigned long	ret;
	unsigned long	id = (unsigned long)m->private;
	char		*page;

	page = (char *)__get_free_page(GFP_USER);
	if (!page)
		return -ENOMEM;

	ret = callback_getenv(id, page, PAGE_SIZE);

	if ((ret >> 61) == 0) {
		seq_write(m, page, ret);
		ret = 0;
	} else
		ret = -EFAULT;
	free_page((unsigned long)page);
	return ret;
}

static int srm_env_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, srm_env_proc_show, PDE_DATA(inode));
}

static ssize_t srm_env_proc_write(struct file *file, const char __user *buffer,
				  size_t count, loff_t *pos)
{
	int res;
	unsigned long	id = (unsigned long)PDE_DATA(file_inode(file));
	char		*buf = (char *) __get_free_page(GFP_USER);
	unsigned long	ret1, ret2;

	if (!buf)
		return -ENOMEM;

	res = -EINVAL;
	if (count >= PAGE_SIZE)
		goto out;

	res = -EFAULT;
	if (copy_from_user(buf, buffer, count))
		goto out;
	buf[count] = '\0';

	ret1 = callback_setenv(id, buf, count);
	if ((ret1 >> 61) == 0) {
		do
			ret2 = callback_save_env();
		while((ret2 >> 61) == 1);
		res = (int) ret1;
	}

 out:
	free_page((unsigned long)buf);
	return res;
}

static const struct file_operations srm_env_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= srm_env_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= srm_env_proc_write,
};

static int __init
srm_env_init(void)
{
	srm_env_t	*entry;
	unsigned long	var_num;

	/*
	 * Check system
	 */
	if (!alpha_using_srm) {
		printk(KERN_INFO "%s: This Alpha system doesn't "
				"know about SRM (or you've booted "
				"SRM->MILO->Linux, which gets "
				"misdetected)...\n", __func__);
		return -ENODEV;
	}

	/*
	 * Create base directory
	 */
	base_dir = proc_mkdir(BASE_DIR, NULL);
	if (!base_dir) {
		printk(KERN_ERR "Couldn't create base dir /proc/%s\n",
				BASE_DIR);
		return -ENOMEM;
	}

	/*
	 * Create per-name subdirectory
	 */
	named_dir = proc_mkdir(NAMED_DIR, base_dir);
	if (!named_dir) {
		printk(KERN_ERR "Couldn't create dir /proc/%s/%s\n",
				BASE_DIR, NAMED_DIR);
		goto cleanup;
	}

	/*
	 * Create per-number subdirectory
	 */
	numbered_dir = proc_mkdir(NUMBERED_DIR, base_dir);
	if (!numbered_dir) {
		printk(KERN_ERR "Couldn't create dir /proc/%s/%s\n",
				BASE_DIR, NUMBERED_DIR);
		goto cleanup;

	}

	/*
	 * Create all named nodes
	 */
	entry = srm_named_entries;
	while (entry->name && entry->id) {
		if (!proc_create_data(entry->name, 0644, named_dir,
			     &srm_env_proc_fops, (void *)entry->id))
			goto cleanup;
		entry++;
	}

	/*
	 * Create all numbered nodes
	 */
	for (var_num = 0; var_num <= 255; var_num++) {
		char name[4];
		sprintf(name, "%ld", var_num);
		if (!proc_create_data(name, 0644, numbered_dir,
			     &srm_env_proc_fops, (void *)var_num))
			goto cleanup;
	}

	printk(KERN_INFO "%s: version %s loaded successfully\n", NAME,
			VERSION);

	return 0;

cleanup:
	remove_proc_subtree(BASE_DIR, NULL);
	return -ENOMEM;
}

static void __exit
srm_env_exit(void)
{
	remove_proc_subtree(BASE_DIR, NULL);
	printk(KERN_INFO "%s: unloaded successfully\n", NAME);
}

module_init(srm_env_init);
module_exit(srm_env_exit);
