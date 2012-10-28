/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#include "ozconfig.h"
#ifdef WANT_EVENT_TRACE
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include "oztrace.h"
#include "ozevent.h"
#include "ozappif.h"
/*------------------------------------------------------------------------------
 * Although the event mask is logically part of the oz_evtdev structure, it is
 * needed outside of this file so define it separately to avoid the need to
 * export definition of struct oz_evtdev.
 */
u32 g_evt_mask;
/*------------------------------------------------------------------------------
 */
#define OZ_MAX_EVTS	2048	/* Must be power of 2 */
struct oz_evtdev {
	struct dentry *root_dir;
	int evt_in;
	int evt_out;
	int missed_events;
	int present;
	atomic_t users;
	spinlock_t lock;
	struct oz_event evts[OZ_MAX_EVTS];
};

static struct oz_evtdev g_evtdev;

/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_event_init(void)
{
	/* Because g_evtdev is static external all fields initially zero so no
	 * need to reinitialized those.
	 */
	oz_trace("Event tracing initialized\n");
	spin_lock_init(&g_evtdev.lock);
	atomic_set(&g_evtdev.users, 0);
}
/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_event_term(void)
{
	oz_trace("Event tracing terminated\n");
}
/*------------------------------------------------------------------------------
 * Context: any
 */
void oz_event_log2(u8 evt, u8 ctx1, u16 ctx2, void *ctx3, unsigned ctx4)
{
	unsigned long irqstate;
	int ix;
	spin_lock_irqsave(&g_evtdev.lock, irqstate);
	ix = (g_evtdev.evt_in + 1) & (OZ_MAX_EVTS - 1);
	if (ix != g_evtdev.evt_out) {
		struct oz_event *e = &g_evtdev.evts[g_evtdev.evt_in];
		e->jiffies = jiffies;
		e->evt = evt;
		e->ctx1 = ctx1;
		e->ctx2 = ctx2;
		e->ctx3 = (__u32)(unsigned long)ctx3;
		e->ctx4 = ctx4;
		g_evtdev.evt_in = ix;
	} else {
		g_evtdev.missed_events++;
	}
	spin_unlock_irqrestore(&g_evtdev.lock, irqstate);
}
/*------------------------------------------------------------------------------
 * Context: process
 */
static void oz_events_clear(struct oz_evtdev *dev)
{
	unsigned long irqstate;
	oz_trace("Clearing events\n");
	spin_lock_irqsave(&dev->lock, irqstate);
	dev->evt_in = dev->evt_out = 0;
	dev->missed_events = 0;
	spin_unlock_irqrestore(&dev->lock, irqstate);
}
#ifdef CONFIG_DEBUG_FS
/*------------------------------------------------------------------------------
 * Context: process
 */
int oz_events_open(struct inode *inode, struct file *filp)
{
	oz_trace("oz_evt_open()\n");
	oz_trace("Open flags: 0x%x\n", filp->f_flags);
	if (atomic_add_return(1, &g_evtdev.users) == 1) {
		oz_events_clear(&g_evtdev);
		return nonseekable_open(inode, filp);
	} else {
		atomic_dec(&g_evtdev.users);
		return -EBUSY;
	}
}
/*------------------------------------------------------------------------------
 * Context: process
 */
int oz_events_release(struct inode *inode, struct file *filp)
{
	oz_events_clear(&g_evtdev);
	atomic_dec(&g_evtdev.users);
	g_evt_mask = 0;
	oz_trace("oz_evt_release()\n");
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
ssize_t oz_events_read(struct file *filp, char __user *buf, size_t count,
		loff_t *fpos)
{
	struct oz_evtdev *dev = &g_evtdev;
	int rc = 0;
	int nb_evts = count / sizeof(struct oz_event);
	int n;
	int sz;

	n = dev->evt_in - dev->evt_out;
	if (n < 0)
		n += OZ_MAX_EVTS;
	if (nb_evts > n)
		nb_evts = n;
	if (nb_evts == 0)
		goto out;
	n = OZ_MAX_EVTS - dev->evt_out;
	if (n > nb_evts)
		n = nb_evts;
	sz = n * sizeof(struct oz_event);
	if (copy_to_user(buf, &dev->evts[dev->evt_out], sz)) {
		rc = -EFAULT;
		goto out;
	}
	if (n == nb_evts)
		goto out2;
	n = nb_evts - n;
	if (copy_to_user(buf + sz, dev->evts, n * sizeof(struct oz_event))) {
		rc = -EFAULT;
		goto out;
	}
out2:
	dev->evt_out = (dev->evt_out + nb_evts) & (OZ_MAX_EVTS - 1);
	rc = nb_evts * sizeof(struct oz_event);
out:
	return rc;
}
/*------------------------------------------------------------------------------
 */
const struct file_operations oz_events_fops = {
	.owner =	THIS_MODULE,
	.open =		oz_events_open,
	.release =	oz_events_release,
	.read =		oz_events_read,
};
/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_debugfs_init(void)
{
	struct dentry *parent;

	parent = debugfs_create_dir("ozwpan", NULL);
	if (parent  == NULL) {
		oz_trace("Failed to create debugfs directory ozmo\n");
		return;
	} else {
		g_evtdev.root_dir = parent;
		if (debugfs_create_file("events", S_IRUSR, parent, NULL,
						&oz_events_fops) == NULL)
			oz_trace("Failed to create file ozmo/events\n");
		if (debugfs_create_x32("event_mask", S_IRUSR | S_IWUSR, parent,
							&g_evt_mask) == NULL)
			oz_trace("Failed to create file ozmo/event_mask\n");
	}
}
/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_debugfs_remove(void)
{
	debugfs_remove_recursive(g_evtdev.root_dir);
}
#endif /* CONFIG_DEBUG_FS */
#endif /* WANT_EVENT_TRACE */
