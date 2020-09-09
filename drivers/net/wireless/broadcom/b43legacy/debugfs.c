// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43legacy wireless driver

  debugfs driver debugging code

  Copyright (c) 2005-2007 Michael Buesch <m@bues.ch>


*/

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/mutex.h>

#include "b43legacy.h"
#include "main.h"
#include "debugfs.h"
#include "dma.h"
#include "pio.h"
#include "xmit.h"


/* The root directory. */
static struct dentry *rootdir;

struct b43legacy_debugfs_fops {
	ssize_t (*read)(struct b43legacy_wldev *dev, char *buf, size_t bufsize);
	int (*write)(struct b43legacy_wldev *dev, const char *buf, size_t count);
	struct file_operations fops;
	/* Offset of struct b43legacy_dfs_file in struct b43legacy_dfsentry */
	size_t file_struct_offset;
	/* Take wl->irq_lock before calling read/write? */
	bool take_irqlock;
};

static inline
struct b43legacy_dfs_file * fops_to_dfs_file(struct b43legacy_wldev *dev,
				       const struct b43legacy_debugfs_fops *dfops)
{
	void *p;

	p = dev->dfsentry;
	p += dfops->file_struct_offset;

	return p;
}


#define fappend(fmt, x...)	\
	do {							\
		if (bufsize - count)				\
			count += scnprintf(buf + count,		\
					  bufsize - count,	\
					  fmt , ##x);		\
		else						\
			printk(KERN_ERR "b43legacy: fappend overflow\n"); \
	} while (0)


/* wl->irq_lock is locked */
static ssize_t tsf_read_file(struct b43legacy_wldev *dev, char *buf, size_t bufsize)
{
	ssize_t count = 0;
	u64 tsf;

	b43legacy_tsf_read(dev, &tsf);
	fappend("0x%08x%08x\n",
		(unsigned int)((tsf & 0xFFFFFFFF00000000ULL) >> 32),
		(unsigned int)(tsf & 0xFFFFFFFFULL));

	return count;
}

/* wl->irq_lock is locked */
static int tsf_write_file(struct b43legacy_wldev *dev, const char *buf, size_t count)
{
	u64 tsf;

	if (sscanf(buf, "%llu", (unsigned long long *)(&tsf)) != 1)
		return -EINVAL;
	b43legacy_tsf_write(dev, tsf);

	return 0;
}

/* wl->irq_lock is locked */
static ssize_t ucode_regs_read_file(struct b43legacy_wldev *dev, char *buf, size_t bufsize)
{
	ssize_t count = 0;
	int i;

	for (i = 0; i < 64; i++) {
		fappend("r%d = 0x%04x\n", i,
			b43legacy_shm_read16(dev, B43legacy_SHM_WIRELESS, i));
	}

	return count;
}

/* wl->irq_lock is locked */
static ssize_t shm_read_file(struct b43legacy_wldev *dev, char *buf, size_t bufsize)
{
	ssize_t count = 0;
	int i;
	u16 tmp;
	__le16 *le16buf = (__le16 *)buf;

	for (i = 0; i < 0x1000; i++) {
		if (bufsize < sizeof(tmp))
			break;
		tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED, 2 * i);
		le16buf[i] = cpu_to_le16(tmp);
		count += sizeof(tmp);
		bufsize -= sizeof(tmp);
	}

	return count;
}

static ssize_t txstat_read_file(struct b43legacy_wldev *dev, char *buf, size_t bufsize)
{
	struct b43legacy_txstatus_log *log = &dev->dfsentry->txstatlog;
	ssize_t count = 0;
	unsigned long flags;
	int i, idx;
	struct b43legacy_txstatus *stat;

	spin_lock_irqsave(&log->lock, flags);
	if (log->end < 0) {
		fappend("Nothing transmitted, yet\n");
		goto out_unlock;
	}
	fappend("b43legacy TX status reports:\n\n"
		"index | cookie | seq | phy_stat | frame_count | "
		"rts_count | supp_reason | pm_indicated | "
		"intermediate | for_ampdu | acked\n" "---\n");
	i = log->end + 1;
	idx = 0;
	while (1) {
		if (i == B43legacy_NR_LOGGED_TXSTATUS)
			i = 0;
		stat = &(log->log[i]);
		if (stat->cookie) {
			fappend("%03d | "
				"0x%04X | 0x%04X | 0x%02X | "
				"0x%X | 0x%X | "
				"%u | %u | "
				"%u | %u | %u\n",
				idx,
				stat->cookie, stat->seq, stat->phy_stat,
				stat->frame_count, stat->rts_count,
				stat->supp_reason, stat->pm_indicated,
				stat->intermediate, stat->for_ampdu,
				stat->acked);
			idx++;
		}
		if (i == log->end)
			break;
		i++;
	}
out_unlock:
	spin_unlock_irqrestore(&log->lock, flags);

	return count;
}

/* wl->irq_lock is locked */
static int restart_write_file(struct b43legacy_wldev *dev, const char *buf, size_t count)
{
	int err = 0;

	if (count > 0 && buf[0] == '1') {
		b43legacy_controller_restart(dev, "manually restarted");
	} else
		err = -EINVAL;

	return err;
}

#undef fappend

static ssize_t b43legacy_debugfs_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	struct b43legacy_wldev *dev;
	struct b43legacy_debugfs_fops *dfops;
	struct b43legacy_dfs_file *dfile;
	ssize_t ret;
	char *buf;
	const size_t bufsize = 1024 * 16; /* 16 KiB buffer */
	const size_t buforder = get_order(bufsize);
	int err = 0;

	if (!count)
		return 0;
	dev = file->private_data;
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->wl->mutex);
	if (b43legacy_status(dev) < B43legacy_STAT_INITIALIZED) {
		err = -ENODEV;
		goto out_unlock;
	}

	dfops = container_of(debugfs_real_fops(file),
			     struct b43legacy_debugfs_fops, fops);
	if (!dfops->read) {
		err = -ENOSYS;
		goto out_unlock;
	}
	dfile = fops_to_dfs_file(dev, dfops);

	if (!dfile->buffer) {
		buf = (char *)__get_free_pages(GFP_KERNEL, buforder);
		if (!buf) {
			err = -ENOMEM;
			goto out_unlock;
		}
		memset(buf, 0, bufsize);
		if (dfops->take_irqlock) {
			spin_lock_irq(&dev->wl->irq_lock);
			ret = dfops->read(dev, buf, bufsize);
			spin_unlock_irq(&dev->wl->irq_lock);
		} else
			ret = dfops->read(dev, buf, bufsize);
		if (ret <= 0) {
			free_pages((unsigned long)buf, buforder);
			err = ret;
			goto out_unlock;
		}
		dfile->data_len = ret;
		dfile->buffer = buf;
	}

	ret = simple_read_from_buffer(userbuf, count, ppos,
				      dfile->buffer,
				      dfile->data_len);
	if (*ppos >= dfile->data_len) {
		free_pages((unsigned long)dfile->buffer, buforder);
		dfile->buffer = NULL;
		dfile->data_len = 0;
	}
out_unlock:
	mutex_unlock(&dev->wl->mutex);

	return err ? err : ret;
}

static ssize_t b43legacy_debugfs_write(struct file *file,
				 const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct b43legacy_wldev *dev;
	struct b43legacy_debugfs_fops *dfops;
	char *buf;
	int err = 0;

	if (!count)
		return 0;
	if (count > PAGE_SIZE)
		return -E2BIG;
	dev = file->private_data;
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->wl->mutex);
	if (b43legacy_status(dev) < B43legacy_STAT_INITIALIZED) {
		err = -ENODEV;
		goto out_unlock;
	}

	dfops = container_of(debugfs_real_fops(file),
			     struct b43legacy_debugfs_fops, fops);
	if (!dfops->write) {
		err = -ENOSYS;
		goto out_unlock;
	}

	buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto out_unlock;
	}
	if (copy_from_user(buf, userbuf, count)) {
		err = -EFAULT;
		goto out_freepage;
	}
	if (dfops->take_irqlock) {
		spin_lock_irq(&dev->wl->irq_lock);
		err = dfops->write(dev, buf, count);
		spin_unlock_irq(&dev->wl->irq_lock);
	} else
		err = dfops->write(dev, buf, count);
	if (err)
		goto out_freepage;

out_freepage:
	free_page((unsigned long)buf);
out_unlock:
	mutex_unlock(&dev->wl->mutex);

	return err ? err : count;
}


#define B43legacy_DEBUGFS_FOPS(name, _read, _write, _take_irqlock)	\
	static struct b43legacy_debugfs_fops fops_##name = {		\
		.read	= _read,				\
		.write	= _write,				\
		.fops	= {					\
			.open	= simple_open,				\
			.read	= b43legacy_debugfs_read,		\
			.write	= b43legacy_debugfs_write,		\
			.llseek = generic_file_llseek,			\
		},						\
		.file_struct_offset = offsetof(struct b43legacy_dfsentry, \
					       file_##name),	\
		.take_irqlock	= _take_irqlock,		\
	}

B43legacy_DEBUGFS_FOPS(tsf, tsf_read_file, tsf_write_file, 1);
B43legacy_DEBUGFS_FOPS(ucode_regs, ucode_regs_read_file, NULL, 1);
B43legacy_DEBUGFS_FOPS(shm, shm_read_file, NULL, 1);
B43legacy_DEBUGFS_FOPS(txstat, txstat_read_file, NULL, 0);
B43legacy_DEBUGFS_FOPS(restart, NULL, restart_write_file, 1);


int b43legacy_debug(struct b43legacy_wldev *dev, enum b43legacy_dyndbg feature)
{
	return !!(dev->dfsentry && dev->dfsentry->dyn_debug[feature]);
}

static void b43legacy_remove_dynamic_debug(struct b43legacy_wldev *dev)
{
	struct b43legacy_dfsentry *e = dev->dfsentry;
	int i;

	for (i = 0; i < __B43legacy_NR_DYNDBG; i++)
		debugfs_remove(e->dyn_debug_dentries[i]);
}

static void b43legacy_add_dynamic_debug(struct b43legacy_wldev *dev)
{
	struct b43legacy_dfsentry *e = dev->dfsentry;

#define add_dyn_dbg(name, id, initstate) do {			\
	e->dyn_debug[id] = (initstate);				\
	e->dyn_debug_dentries[id] =				\
		debugfs_create_bool(name, 0600, e->subdir,	\
				&(e->dyn_debug[id]));		\
	} while (0)

	add_dyn_dbg("debug_xmitpower", B43legacy_DBG_XMITPOWER, false);
	add_dyn_dbg("debug_dmaoverflow", B43legacy_DBG_DMAOVERFLOW, false);
	add_dyn_dbg("debug_dmaverbose", B43legacy_DBG_DMAVERBOSE, false);
	add_dyn_dbg("debug_pwork_fast", B43legacy_DBG_PWORK_FAST, false);
	add_dyn_dbg("debug_pwork_stop", B43legacy_DBG_PWORK_STOP, false);

#undef add_dyn_dbg
}

void b43legacy_debugfs_add_device(struct b43legacy_wldev *dev)
{
	struct b43legacy_dfsentry *e;
	struct b43legacy_txstatus_log *log;
	char devdir[16];

	B43legacy_WARN_ON(!dev);
	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e) {
		b43legacyerr(dev->wl, "debugfs: add device OOM\n");
		return;
	}
	e->dev = dev;
	log = &e->txstatlog;
	log->log = kcalloc(B43legacy_NR_LOGGED_TXSTATUS,
			   sizeof(struct b43legacy_txstatus), GFP_KERNEL);
	if (!log->log) {
		b43legacyerr(dev->wl, "debugfs: add device txstatus OOM\n");
		kfree(e);
		return;
	}
	log->end = -1;
	spin_lock_init(&log->lock);

	dev->dfsentry = e;

	snprintf(devdir, sizeof(devdir), "%s", wiphy_name(dev->wl->hw->wiphy));
	e->subdir = debugfs_create_dir(devdir, rootdir);

#define ADD_FILE(name, mode)	\
	do {							\
		e->file_##name.dentry =				\
			debugfs_create_file(__stringify(name),	\
					mode, e->subdir, dev,	\
					&fops_##name.fops);	\
		e->file_##name.dentry = NULL;			\
	} while (0)


	ADD_FILE(tsf, 0600);
	ADD_FILE(ucode_regs, 0400);
	ADD_FILE(shm, 0400);
	ADD_FILE(txstat, 0400);
	ADD_FILE(restart, 0200);

#undef ADD_FILE

	b43legacy_add_dynamic_debug(dev);
}

void b43legacy_debugfs_remove_device(struct b43legacy_wldev *dev)
{
	struct b43legacy_dfsentry *e;

	if (!dev)
		return;
	e = dev->dfsentry;
	if (!e)
		return;
	b43legacy_remove_dynamic_debug(dev);

	debugfs_remove(e->file_tsf.dentry);
	debugfs_remove(e->file_ucode_regs.dentry);
	debugfs_remove(e->file_shm.dentry);
	debugfs_remove(e->file_txstat.dentry);
	debugfs_remove(e->file_restart.dentry);

	debugfs_remove(e->subdir);
	kfree(e->txstatlog.log);
	kfree(e);
}

void b43legacy_debugfs_log_txstat(struct b43legacy_wldev *dev,
			    const struct b43legacy_txstatus *status)
{
	struct b43legacy_dfsentry *e = dev->dfsentry;
	struct b43legacy_txstatus_log *log;
	struct b43legacy_txstatus *cur;
	int i;

	if (!e)
		return;
	log = &e->txstatlog;
	B43legacy_WARN_ON(!irqs_disabled());
	spin_lock(&log->lock);
	i = log->end + 1;
	if (i == B43legacy_NR_LOGGED_TXSTATUS)
		i = 0;
	log->end = i;
	cur = &(log->log[i]);
	memcpy(cur, status, sizeof(*cur));
	spin_unlock(&log->lock);
}

void b43legacy_debugfs_init(void)
{
	rootdir = debugfs_create_dir(KBUILD_MODNAME, NULL);
}

void b43legacy_debugfs_exit(void)
{
	debugfs_remove(rootdir);
}
