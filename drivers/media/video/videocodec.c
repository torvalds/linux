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
#include <linux/config.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#endif

#include "videocodec.h"

static int debug = 0;
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
		"videocodec_attach: '%s', type: %x, flags %lx, magic %lx\n",
		master->name, master->type, master->flags, master->magic);

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

			codec =
			    kmalloc(sizeof(struct videocodec), GFP_KERNEL);
			if (!codec) {
				dprintk(1,
					KERN_ERR
					"videocodec_attach: no mem\n");
				goto out_module_put;
			}
			memcpy(codec, h->codec, sizeof(struct videocodec));

			snprintf(codec->name, sizeof(codec->name),
				 "%s[%d]", codec->name, h->attached);
			codec->master_data = master;
			res = codec->setup(codec);
			if (res == 0) {
				dprintk(3, "videocodec_attach '%s'\n",
					codec->name);
				ptr = (struct attached_list *)
				    kmalloc(sizeof(struct attached_list),
					    GFP_KERNEL);
				if (!ptr) {
					dprintk(1,
						KERN_ERR
						"videocodec_attach: no memory\n");
					goto out_kfree;
				}
				memset(ptr, 0,
				       sizeof(struct attached_list));
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

	ptr =
	    (struct codec_list *) kmalloc(sizeof(struct codec_list),
					  GFP_KERNEL);
	if (!ptr) {
		dprintk(1, KERN_ERR "videocodec_register: no memory\n");
		return -ENOMEM;
	}
	memset(ptr, 0, sizeof(struct codec_list));
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
/* ============ */
/* procfs stuff */
/* ============ */

static char *videocodec_buf = NULL;
static int videocodec_bufsize = 0;

static int
videocodec_build_table (void)
{
	struct codec_list *h = codeclist_top;
	struct attached_list *a;
	int i = 0, size;

	// sum up amount of slaves plus their attached masters
	while (h) {
		i += h->attached + 1;
		h = h->next;
	}
#define LINESIZE 100
	size = LINESIZE * (i + 1);

	dprintk(3, "videocodec_build table: %d entries, %d bytes\n", i,
		size);

	if (videocodec_buf)
		kfree(videocodec_buf);
	videocodec_buf = (char *) kmalloc(size, GFP_KERNEL);

	i = 0;
	i += scnprintf(videocodec_buf + i, size - 1,
		      "<S>lave or attached <M>aster name  type flags    magic    ");
	i += scnprintf(videocodec_buf + i, size -i - 1, "(connected as)\n");

	h = codeclist_top;
	while (h) {
		if (i > (size - LINESIZE))
			break;	// security check
		i += scnprintf(videocodec_buf + i, size -i -1,
			      "S %32s %04x %08lx %08lx (TEMPLATE)\n",
			      h->codec->name, h->codec->type,
			      h->codec->flags, h->codec->magic);
		a = h->list;
		while (a) {
			if (i > (size - LINESIZE))
				break;	// security check
			i += scnprintf(videocodec_buf + i, size -i -1,
				      "M %32s %04x %08lx %08lx (%s)\n",
				      a->codec->master_data->name,
				      a->codec->master_data->type,
				      a->codec->master_data->flags,
				      a->codec->master_data->magic,
				      a->codec->name);
			a = a->next;
		}
		h = h->next;
	}

	return i;
}

//The definition:
//typedef int (read_proc_t)(char *page, char **start, off_t off,
//                          int count, int *eof, void *data);

static int
videocodec_info (char  *buffer,
		 char **buffer_location,
		 off_t  offset,
		 int    buffer_length,
		 int   *eof,
		 void  *data)
{
	int size;

	dprintk(3, "videocodec_info: offset: %ld, len %d / size %d\n",
		offset, buffer_length, videocodec_bufsize);

	if (offset == 0) {
		videocodec_bufsize = videocodec_build_table();
	}
	if ((offset < 0) || (offset >= videocodec_bufsize)) {
		dprintk(4,
			"videocodec_info: call delivers no result, return 0\n");
		*eof = 1;
		return 0;
	}

	if (buffer_length < (videocodec_bufsize - offset)) {
		dprintk(4, "videocodec_info: %ld needed, %d got\n",
			videocodec_bufsize - offset, buffer_length);
		size = buffer_length;
	} else {
		dprintk(4, "videocodec_info: last reading of %ld bytes\n",
			videocodec_bufsize - offset);
		size = videocodec_bufsize - offset;
		*eof = 1;
	}

	memcpy(buffer, videocodec_buf + offset, size);
	/* doesn't work...                           */
	/* copy_to_user(buffer, videocodec_buf+offset, size); */
	/* *buffer_location = videocodec_buf+offset; */

	return size;
}
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
	videocodec_buf = NULL;
	videocodec_bufsize = 0;

	videocodec_proc_entry = create_proc_entry("videocodecs", 0, NULL);
	if (videocodec_proc_entry) {
		videocodec_proc_entry->read_proc = videocodec_info;
		videocodec_proc_entry->write_proc = NULL;
		videocodec_proc_entry->data = NULL;
		videocodec_proc_entry->owner = THIS_MODULE;
	} else {
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
	if (videocodec_buf)
		kfree(videocodec_buf);
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
