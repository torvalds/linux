// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * VIDEO MOTION CODECs internal API for video devices
 *
 * Interface for MJPEG (and maybe later MPEG/WAVELETS) codec's
 * bound to a master device.
 *
 * (c) 2002 Wolfgang Scherr <scherr@net4you.at>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>

#include "videocodec.h"

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

static struct codec_list *codeclist_top;

/* ================================================= */
/* function prototypes of the master/slave interface */
/* ================================================= */

struct videocodec *videocodec_attach(struct videocodec_master *master)
{
	struct codec_list *h = codeclist_top;
	struct zoran *zr;
	struct attached_list *a, *ptr;
	struct videocodec *codec;
	int res;

	if (!master) {
		pr_err("%s: no data\n", __func__);
		return NULL;
	}

	zr = videocodec_master_to_zoran(master);

	zrdev_dbg(zr, "%s: '%s', flags %lx, magic %lx\n", __func__,
		  master->name, master->flags, master->magic);

	if (!h) {
		zrdev_err(zr, "%s: no device available\n", __func__);
		return NULL;
	}

	while (h) {
		// attach only if the slave has at least the flags
		// expected by the master
		if ((master->flags & h->codec->flags) == master->flags) {
			zrdev_dbg(zr, "%s: try '%s'\n", __func__, h->codec->name);

			codec = kmemdup(h->codec, sizeof(struct videocodec), GFP_KERNEL);
			if (!codec)
				goto out_kfree;

			res = strlen(codec->name);
			snprintf(codec->name + res, sizeof(codec->name) - res, "[%d]", h->attached);
			codec->master_data = master;
			res = codec->setup(codec);
			if (res == 0) {
				zrdev_dbg(zr, "%s: '%s'\n", __func__, codec->name);
				ptr = kzalloc(sizeof(*ptr), GFP_KERNEL);
				if (!ptr)
					goto out_kfree;
				ptr->codec = codec;

				a = h->list;
				if (!a) {
					h->list = ptr;
					zrdev_dbg(zr, "videocodec: first element\n");
				} else {
					while (a->next)
						a = a->next;	// find end
					a->next = ptr;
					zrdev_dbg(zr, "videocodec: in after '%s'\n",
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

	zrdev_err(zr, "%s: no codec found!\n", __func__);
	return NULL;

 out_kfree:
	kfree(codec);
	return NULL;
}

int videocodec_detach(struct videocodec *codec)
{
	struct codec_list *h = codeclist_top;
	struct zoran *zr;
	struct attached_list *a, *prev;
	int res;

	if (!codec) {
		pr_err("%s: no data\n", __func__);
		return -EINVAL;
	}

	zr = videocodec_to_zoran(codec);

	zrdev_dbg(zr, "%s: '%s', type: %x, flags %lx, magic %lx\n", __func__,
		  codec->name, codec->type, codec->flags, codec->magic);

	if (!h) {
		zrdev_err(zr, "%s: no device left...\n", __func__);
		return -ENXIO;
	}

	while (h) {
		a = h->list;
		prev = NULL;
		while (a) {
			if (codec == a->codec) {
				res = a->codec->unset(a->codec);
				if (res >= 0) {
					zrdev_dbg(zr, "%s: '%s'\n", __func__,
						  a->codec->name);
					a->codec->master_data = NULL;
				} else {
					zrdev_err(zr, "%s: '%s'\n", __func__, a->codec->name);
					a->codec->master_data = NULL;
				}
				if (!prev) {
					h->list = a->next;
					zrdev_dbg(zr, "videocodec: delete first\n");
				} else {
					prev->next = a->next;
					zrdev_dbg(zr, "videocodec: delete middle\n");
				}
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

	zrdev_err(zr, "%s: given codec not found!\n", __func__);
	return -EINVAL;
}

int videocodec_register(const struct videocodec *codec)
{
	struct codec_list *ptr, *h = codeclist_top;
	struct zoran *zr;

	if (!codec) {
		pr_err("%s: no data!\n", __func__);
		return -EINVAL;
	}

	zr = videocodec_to_zoran((struct videocodec *)codec);

	zrdev_dbg(zr,
		  "videocodec: register '%s', type: %x, flags %lx, magic %lx\n",
		  codec->name, codec->type, codec->flags, codec->magic);

	ptr = kzalloc(sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;
	ptr->codec = codec;

	if (!h) {
		codeclist_top = ptr;
		zrdev_dbg(zr, "videocodec: hooked in as first element\n");
	} else {
		while (h->next)
			h = h->next;	// find the end
		h->next = ptr;
		zrdev_dbg(zr, "videocodec: hooked in after '%s'\n",
			  h->codec->name);
	}

	return 0;
}

int videocodec_unregister(const struct videocodec *codec)
{
	struct codec_list *prev = NULL, *h = codeclist_top;
	struct zoran *zr;

	if (!codec) {
		pr_err("%s: no data!\n", __func__);
		return -EINVAL;
	}

	zr = videocodec_to_zoran((struct videocodec *)codec);

	zrdev_dbg(zr,
		  "videocodec: unregister '%s', type: %x, flags %lx, magic %lx\n",
		  codec->name, codec->type, codec->flags, codec->magic);

	if (!h) {
		zrdev_err(zr, "%s: no device left...\n", __func__);
		return -ENXIO;
	}

	while (h) {
		if (codec == h->codec) {
			if (h->attached) {
				zrdev_err(zr, "videocodec: '%s' is used\n",
					  h->codec->name);
				return -EBUSY;
			}
			zrdev_dbg(zr, "videocodec: unregister '%s' is ok.\n",
				  h->codec->name);
			if (!prev) {
				codeclist_top = h->next;
				zrdev_dbg(zr,
					  "videocodec: delete first element\n");
			} else {
				prev->next = h->next;
				zrdev_dbg(zr,
					  "videocodec: delete middle element\n");
			}
			kfree(h);
			return 0;
		}
		prev = h;
		h = h->next;
	}

	zrdev_err(zr, "%s: given codec not found!\n", __func__);
	return -EINVAL;
}

int videocodec_debugfs_show(struct seq_file *m)
{
	struct codec_list *h = codeclist_top;
	struct attached_list *a;

	seq_printf(m, "<S>lave or attached <M>aster name  type flags    magic    ");
	seq_printf(m, "(connected as)\n");

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
