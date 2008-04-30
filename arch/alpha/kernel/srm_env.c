/*
 * srm_env.c - Access to SRM environment
 *             variables through linux' procfs
 *
 * (C) 2001,2002,2006 by Jan-Benedict Glaw <jbglaw@lug-owl.de>
 *
 * This driver is at all a modified version of Erik Mouw's
 * Documentation/DocBook/procfs_example.c, so: thank
 * you, Erik! He can be reached via email at
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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
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
	struct proc_dir_entry	*proc_entry;
} srm_env_t;

static struct proc_dir_entry	*base_dir;
static struct proc_dir_entry	*named_dir;
static struct proc_dir_entry	*numbered_dir;
static char			number[256][4];

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
static srm_env_t	srm_numbered_entries[256];


static int
srm_env_read(char *page, char **start, off_t off, int count, int *eof,
		void *data)
{
	int		nbytes;
	unsigned long	ret;
	srm_env_t	*entry;

	if (off != 0) {
		*eof = 1;
		return 0;
	}

	entry	= (srm_env_t *) data;
	ret	= callback_getenv(entry->id, page, count);

	if ((ret >> 61) == 0) {
		nbytes = (int) ret;
		*eof = 1;
	} else
		nbytes = -EFAULT;

	return nbytes;
}

static int
srm_env_write(struct file *file, const char __user *buffer, unsigned long count,
		void *data)
{
	int res;
	srm_env_t	*entry;
	char		*buf = (char *) __get_free_page(GFP_USER);
	unsigned long	ret1, ret2;

	entry = (srm_env_t *) data;

	if (!buf)
		return -ENOMEM;

	res = -EINVAL;
	if (count >= PAGE_SIZE)
		goto out;

	res = -EFAULT;
	if (copy_from_user(buf, buffer, count))
		goto out;
	buf[count] = '\0';

	ret1 = callback_setenv(entry->id, buf, count);
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

static void
srm_env_cleanup(void)
{
	srm_env_t	*entry;
	unsigned long	var_num;

	if (base_dir) {
		/*
		 * Remove named entries
		 */
		if (named_dir) {
			entry = srm_named_entries;
			while (entry->name != NULL && entry->id != 0) {
				if (entry->proc_entry) {
					remove_proc_entry(entry->name,
							named_dir);
					entry->proc_entry = NULL;
				}
				entry++;
			}
			remove_proc_entry(NAMED_DIR, base_dir);
		}

		/*
		 * Remove numbered entries
		 */
		if (numbered_dir) {
			for (var_num = 0; var_num <= 255; var_num++) {
				entry =	&srm_numbered_entries[var_num];

				if (entry->proc_entry) {
					remove_proc_entry(entry->name,
							numbered_dir);
					entry->proc_entry	= NULL;
					entry->name		= NULL;
				}
			}
			remove_proc_entry(NUMBERED_DIR, base_dir);
		}

		remove_proc_entry(BASE_DIR, NULL);
	}

	return;
}

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
	 * Init numbers
	 */
	for (var_num = 0; var_num <= 255; var_num++)
		sprintf(number[var_num], "%ld", var_num);

	/*
	 * Create base directory
	 */
	base_dir = proc_mkdir(BASE_DIR, NULL);
	if (!base_dir) {
		printk(KERN_ERR "Couldn't create base dir /proc/%s\n",
				BASE_DIR);
		goto cleanup;
	}
	base_dir->owner = THIS_MODULE;

	/*
	 * Create per-name subdirectory
	 */
	named_dir = proc_mkdir(NAMED_DIR, base_dir);
	if (!named_dir) {
		printk(KERN_ERR "Couldn't create dir /proc/%s/%s\n",
				BASE_DIR, NAMED_DIR);
		goto cleanup;
	}
	named_dir->owner = THIS_MODULE;

	/*
	 * Create per-number subdirectory
	 */
	numbered_dir = proc_mkdir(NUMBERED_DIR, base_dir);
	if (!numbered_dir) {
		printk(KERN_ERR "Couldn't create dir /proc/%s/%s\n",
				BASE_DIR, NUMBERED_DIR);
		goto cleanup;

	}
	numbered_dir->owner = THIS_MODULE;

	/*
	 * Create all named nodes
	 */
	entry = srm_named_entries;
	while (entry->name && entry->id) {
		entry->proc_entry = create_proc_entry(entry->name,
				0644, named_dir);
		if (!entry->proc_entry)
			goto cleanup;

		entry->proc_entry->data		= (void *) entry;
		entry->proc_entry->owner	= THIS_MODULE;
		entry->proc_entry->read_proc	= srm_env_read;
		entry->proc_entry->write_proc	= srm_env_write;

		entry++;
	}

	/*
	 * Create all numbered nodes
	 */
	for (var_num = 0; var_num <= 255; var_num++) {
		entry = &srm_numbered_entries[var_num];
		entry->name = number[var_num];

		entry->proc_entry = create_proc_entry(entry->name,
				0644, numbered_dir);
		if (!entry->proc_entry)
			goto cleanup;

		entry->id			= var_num;
		entry->proc_entry->data		= (void *) entry;
		entry->proc_entry->owner	= THIS_MODULE;
		entry->proc_entry->read_proc	= srm_env_read;
		entry->proc_entry->write_proc	= srm_env_write;
	}

	printk(KERN_INFO "%s: version %s loaded successfully\n", NAME,
			VERSION);

	return 0;

cleanup:
	srm_env_cleanup();

	return -ENOMEM;
}

static void __exit
srm_env_exit(void)
{
	srm_env_cleanup();
	printk(KERN_INFO "%s: unloaded successfully\n", NAME);

	return;
}

module_init(srm_env_init);
module_exit(srm_env_exit);
