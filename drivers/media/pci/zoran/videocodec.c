/*
 * VIDEO MOTION CODECs internal API for video devices
 *
 * Interface for MJPEG (and maybe later MPEG/WAVELETS) codec's
 * bound to a master device.
 *
 * (c) 2002 Wolfgang Scherr <scherr@net4you.at>
 *
 * $Id: videocodec.c,v 1.1.2.8 2003/03/29 07:16:04 rbultje Exp $
 *
 * ------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * ------------------------------------------------------------------------
 */

#define VIDEOCODEC_VERSION "v0.2"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>

// kernel config is here (procfs flag)

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#endif

#include "videocodec.h"

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-4)");

#define dprintk(num, format, args...) \
	do { \
		if (debug >= num) \
			printk(format, ##args); \
	} while (0)

struct attached_list {
	struct videocodec *codec;
	struct attached_list *next;
};

struct codec_list {
	const struct videocodec *codec;
	int attached;
	struct attached_list *list;
	struct codec_list *next;
};

static struct codec_list *codeclist_top = NULL;

/* ================================================= */
/* function prototypes of the master/slave interface */
/* ================================================= */

struct videocodec *
videocodec_attach (struct videocodec_master *master)
{
	struct codec_list *h = codeclist_top;
	struct attached_list *a, *ptr;
	struct videocodec *codec;
	int res;

	if (!master) {
		dprintk(1, KERN_ERR "videocodec_attach: no data\n");
		return NULL;
	}

	dprintk(2,
		"videocodec_attach: '%s', flags %lx, magic %lx\n",
		master->name, master->flags, master->magic);

	if (!h) {
		dprintk(1,
			KERN_ERR
			"videocodec_attach: no device available\n");
		return NULL;
	}

	while (h) {
		// attach only if the slave has at least the flags
		// expected by the master
		if ((master->flags & h->codec->flags) == master->flags) {
			dprintk(4, "videocodec_attach: try '%s'\n",
				h->codec->name);

			if (!try_module_get(h->codec->owner))
				return NULL;

			codec = kmemdup(h->codec, sizeof(struct videocodec),
					GFP_KERNEL);
			if (!codec) {
				dprintk(1,
					KERN_ERR
					"videocodec_attach: no mem\n");
				goto out_module_put;
			}

			res = strlen(codec->name);
			snprintf(codec->name + res, sizeof(codec->name) - res,
				 "[%d]", h->attached);
			codec->master_data = master;
			res = codec->setup(codec);
			if (res == 0) {
				dprintk(3, "videocodec_attach '%s'\n",
					codec->name);
				ptr = kzalloc(sizeof(struct attached_list), GFP_KERNEL);
				if (!ptr) {
					dprintk(1,
						KERN_ERR
						"videocodec_attach: no memory\n");
					goto out_kfree;
				}
				ptr->codec = codec;

				a = h->list;
				if (!a) {
					h->list = ptr;
					dprintk(4,
						"videocodec: first element\n");
				} else {
					while (a->next)
						a = a->next;	// find end
					a->next = ptr;
					dprintk(4,
						"videocodec: in after '%s'\n",
						h->codec->name);
				}

				h->attached += 1;
				return codec;
			} else {
				kfree(codec);
			}
		}
		h = h->next;
	}

	dprintk(1, KERN_ERR "videocodec_attach: no codec found!\n");
	return NULL;

 out_module_put:
	module_put(h->codec->owner);
 out_kfree:
	kfree(codec);
	return NULL;
}

int
videocodec_detach (struct videocodec *codec)
{
	struct codec_list *h = codeclist_top;
	struct attached_list *a, *prev;
	int res;

	if (!codec) {
		dprintk(1, KERN_ERR "videocodec_detach: no data\n");
		return -EINVAL;
	}

	dprintk(2,
		"videocodec_detach: '%s', type: %x, flags %lx, magic %lx\n",
		codec->name, codec->type, codec->flags, codec->magic);

	if (!h) {
		dprintk(1,
			KERN_ERR "videocodec_detach: no device left...\n");
		return -ENXIO;
	}

	while (h) {
		a = h->list;
		prev = NULL;
		while (a) {
			if (codec == a->codec) {
				res = a->codec->unset(a->codec);
				if (res >= 0) {
					dprintk(3,
						"videocodec_detach: '%s'\n",
						a->codec->name);
					a->codec->master_data = NULL;
				} else {
					dprintk(1,
						KERN_ERR
						"videocodec_detach: '%s'\n",
						a->codec->name);
					a->codec->master_data = NULL;
				}
				if (prev == NULL) {
					h->list = a->next;
					dprintk(4,
						"videocodec: delete first\n");
				} else {
					prev->next = a->next;
					dprintk(4,
						"videocodec: delete middle\n");
				}
				module_put(a->codec->owner);
				kfree(a->codec);
				kfree(a);
				h->attached -= 1;
				return 0;
			}
			prev = a;
			a = a->next;
		}
		h = h->next;
	}

	dprintk(1, KERN_ERR "videocodec_detach: given codec not found!\n");
	return -EINVAL;
}

int
videocodec_register (const struct videocodec *codec)
{
	struct codec_list *ptr, *h = codeclist_top;

	if (!codec) {
		dprintk(1, KERN_ERR "videocodec_register: no data!\n");
		return -EINVAL;
	}

	dprintk(2,
		"videocodec: register '%s', type: %x, flags %lx, magic %lx\n",
		codec->name, codec->type, codec->flags, codec->magic);

	ptr = kzalloc(sizeof(struct codec_list), GFP_KERNEL);
	if (!ptr) {
		dprintk(1, KERN_ERR "videocodec_register: no memory\n");
		return -ENOMEM;
	}
	ptr->codec = codec;

	if (!h) {
		codeclist_top = ptr;
		dprintk(4, "videocodec: hooked in as first element\n");
	} else {
		while (h->next)
			h = h->next;	// find the end
		h->next = ptr;
		dprintk(4, "videocodec: hooked in after '%s'\n",
			h->codec->name);
	}

	return 0;
}

int
videocodec_unregister (const struct videocodec *codec)
{
	struct codec_list *prev = NULL, *h = codeclist_top;

	if (!codec) {
		dprintk(1, KERN_ERR "videocodec_unregister: no data!\n");
		return -EINVAL;
	}

	dprintk(2,
		"videocodec: unregister '%s', type: %x, flags %lx, magic %lx\n",
		codec->name, codec->type, codec->flags, codec->magic);

	if (!h) {
		dprintk(1,
			KERN_ERR
			"videocodec_unregister: no device left...\n");
		return -ENXIO;
	}

	while (h) {
		if (codec == h->codec) {
			if (h->attached) {
				dprintk(1,
					KERN_ERR
					"videocodec: '%s' is used\n",
					h->codec->name);
				return -EBUSY;
			}
			dprintk(3, "videocodec: unregister '%s' is ok.\n",
				h->codec->name);
			if (prev == NULL) {
				codeclist_top = h->next;
				dprintk(4,
					"videocodec: delete first element\n");
			} else {
				prev->next = h->next;
				dprintk(4,
					"videocodec: delete middle element\n");
			}
			kfree(h);
			return 0;
		}
		prev = h;
		h = h->next;
	}

	dprintk(1,
		KERN_ERR
		"videocodec_unregister: given codec not found!\n");
	return -EINVAL;
}

#ifdef CONFIG_PROC_FS
static int proc_videocodecs_show(struct seq_file *m, void *v)
{
	struct codec_list *h = codeclist_top;
	struct attached_list *a;

	seq_printf(m, "<S>lave or attached <M>aster name  type flags    magic    ");
	seq_printf(m, "(connected as)\n");

	h = codeclist_top;
	while (h) {
		seq_printf(m, "S %32s %04x %08lx %08lx (TEMPLATE)\n",
			      h->codec->name, h->codec->type,
			      h->codec->flags, h->codec->magic);
		a = h->list;
		while (a) {
			seq_printf(m, "M %32s %04x %08lx %08lx (%s)\n",
				      a->codec->master_data->name,
				      a->codec->master_data->type,
				      a->codec->master_data->flags,
				      a->codec->master_data->magic,
				      a->codec->name);
			a = a->next;
		}
		h = h->next;
	}

	return 0;
}

static int proc_videocodecs_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_videocodecs_show, NULL);
}

static const struct file_operations videocodecs_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_videocodecs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

/* ===================== */
/* hook in driver module */
/* ===================== */
static int __init
videocodec_init (void)
{
#ifdef CONFIG_PROC_FS
	static struct proc_dir_entry *videocodec_proc_entry;
#endif

	printk(KERN_INFO "Linux video codec intermediate layer: %s\n",
	       VIDEOCODEC_VERSION);

#ifdef CONFIG_PROC_FS
	videocodec_proc_entry = proc_create("videocodecs", 0, NULL, &videocodecs_proc_fops);
	if (!videocodec_proc_entry) {
		dprintk(1, KERN_ERR "videocodec: can't init procfs.\n");
	}
#endif
	return 0;
}

static void __exit
videocodec_exit (void)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry("videocodecs", NULL);
#endif
}

EXPORT_SYMBOL(videocodec_attach);
EXPORT_SYMBOL(videocodec_detach);
EXPORT_SYMBOL(videocodec_register);
EXPORT_SYMBOL(videocodec_unregister);

module_init(videocodec_init);
module_exit(videocodec_exit);

MODULE_AUTHOR("Wolfgang Scherr <scherr@net4you.at>");
MODULE_DESCRIPTION("Intermediate API module for video codecs "
		   VIDEOCODEC_VERSION);
MODULE_LICENSE("GPL");
