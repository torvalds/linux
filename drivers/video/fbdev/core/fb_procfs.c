// SPDX-License-Identifier: GPL-2.0

#include <linux/proc_fs.h>

#include "fb_internal.h"

static struct proc_dir_entry *fb_proc_dir_entry;

static void *fb_seq_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&registration_lock);

	return (*pos < FB_MAX) ? pos : NULL;
}

static void fb_seq_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&registration_lock);
}

static void *fb_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;

	return (*pos < FB_MAX) ? pos : NULL;
}

static int fb_seq_show(struct seq_file *m, void *v)
{
	int i = *(loff_t *)v;
	struct fb_info *fi = registered_fb[i];

	if (fi)
		seq_printf(m, "%d %s\n", fi->node, fi->fix.id);

	return 0;
}

static const struct seq_operations __maybe_unused fb_proc_seq_ops = {
	.start	= fb_seq_start,
	.stop	= fb_seq_stop,
	.next	= fb_seq_next,
	.show	= fb_seq_show,
};

int fb_init_procfs(void)
{
	struct proc_dir_entry *proc;

	proc = proc_create_seq("fb", 0, NULL, &fb_proc_seq_ops);
	if (!proc)
		return -ENOMEM;

	fb_proc_dir_entry = proc;

	return 0;
}

void fb_cleanup_procfs(void)
{
	proc_remove(fb_proc_dir_entry);
}
