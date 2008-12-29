/*

  Broadcom B43 wireless driver

  debugfs driver debugging code

  Copyright (c) 2005-2007 Michael Buesch <mb@bu3sch.de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/mutex.h>

#include "b43.h"
#include "main.h"
#include "debugfs.h"
#include "dma.h"
#include "xmit.h"


/* The root directory. */
static struct dentry *rootdir;

struct b43_debugfs_fops {
	ssize_t (*read)(struct b43_wldev *dev, char *buf, size_t bufsize);
	int (*write)(struct b43_wldev *dev, const char *buf, size_t count);
	struct file_operations fops;
	/* Offset of struct b43_dfs_file in struct b43_dfsentry */
	size_t file_struct_offset;
	/* Take wl->irq_lock before calling read/write? */
	bool take_irqlock;
};

static inline
struct b43_dfs_file * fops_to_dfs_file(struct b43_wldev *dev,
				       const struct b43_debugfs_fops *dfops)
{
	void *p;

	p = dev->dfsentry;
	p += dfops->file_struct_offset;

	return p;
}


#define fappend(fmt, x...)	\
	do {							\
		if (bufsize - count)				\
			count += snprintf(buf + count,		\
					  bufsize - count,	\
					  fmt , ##x);		\
		else						\
			printk(KERN_ERR "b43: fappend overflow\n"); \
	} while (0)


/* The biggest address values for SHM access from the debugfs files. */
#define B43_MAX_SHM_ROUTING	4
#define B43_MAX_SHM_ADDR	0xFFFF

static ssize_t shm16read__read_file(struct b43_wldev *dev,
				    char *buf, size_t bufsize)
{
	ssize_t count = 0;
	unsigned int routing, addr;
	u16 val;

	routing = dev->dfsentry->shm16read_routing_next;
	addr = dev->dfsentry->shm16read_addr_next;
	if ((routing > B43_MAX_SHM_ROUTING) ||
	    (addr > B43_MAX_SHM_ADDR))
		return -EDESTADDRREQ;

	val = b43_shm_read16(dev, routing, addr);
	fappend("0x%04X\n", val);

	return count;
}

static int shm16read__write_file(struct b43_wldev *dev,
				 const char *buf, size_t count)
{
	unsigned int routing, addr;
	int res;

	res = sscanf(buf, "0x%X 0x%X", &routing, &addr);
	if (res != 2)
		return -EINVAL;
	if (routing > B43_MAX_SHM_ROUTING)
		return -EADDRNOTAVAIL;
	if (addr > B43_MAX_SHM_ADDR)
		return -EADDRNOTAVAIL;
	if (routing == B43_SHM_SHARED) {
		if ((addr % 2) != 0)
			return -EADDRNOTAVAIL;
	}

	dev->dfsentry->shm16read_routing_next = routing;
	dev->dfsentry->shm16read_addr_next = addr;

	return 0;
}

static int shm16write__write_file(struct b43_wldev *dev,
				  const char *buf, size_t count)
{
	unsigned int routing, addr, mask, set;
	u16 val;
	int res;
	unsigned long flags;

	res = sscanf(buf, "0x%X 0x%X 0x%X 0x%X",
		     &routing, &addr, &mask, &set);
	if (res != 4)
		return -EINVAL;
	if (routing > B43_MAX_SHM_ROUTING)
		return -EADDRNOTAVAIL;
	if (addr > B43_MAX_SHM_ADDR)
		return -EADDRNOTAVAIL;
	if (routing == B43_SHM_SHARED) {
		if ((addr % 2) != 0)
			return -EADDRNOTAVAIL;
	}
	if ((mask > 0xFFFF) || (set > 0xFFFF))
		return -E2BIG;

	spin_lock_irqsave(&dev->wl->shm_lock, flags);
	if (mask == 0)
		val = 0;
	else
		val = __b43_shm_read16(dev, routing, addr);
	val &= mask;
	val |= set;
	__b43_shm_write16(dev, routing, addr, val);
	spin_unlock_irqrestore(&dev->wl->shm_lock, flags);

	return 0;
}

static ssize_t shm32read__read_file(struct b43_wldev *dev,
				    char *buf, size_t bufsize)
{
	ssize_t count = 0;
	unsigned int routing, addr;
	u32 val;

	routing = dev->dfsentry->shm32read_routing_next;
	addr = dev->dfsentry->shm32read_addr_next;
	if ((routing > B43_MAX_SHM_ROUTING) ||
	    (addr > B43_MAX_SHM_ADDR))
		return -EDESTADDRREQ;

	val = b43_shm_read32(dev, routing, addr);
	fappend("0x%08X\n", val);

	return count;
}

static int shm32read__write_file(struct b43_wldev *dev,
				 const char *buf, size_t count)
{
	unsigned int routing, addr;
	int res;

	res = sscanf(buf, "0x%X 0x%X", &routing, &addr);
	if (res != 2)
		return -EINVAL;
	if (routing > B43_MAX_SHM_ROUTING)
		return -EADDRNOTAVAIL;
	if (addr > B43_MAX_SHM_ADDR)
		return -EADDRNOTAVAIL;
	if (routing == B43_SHM_SHARED) {
		if ((addr % 2) != 0)
			return -EADDRNOTAVAIL;
	}

	dev->dfsentry->shm32read_routing_next = routing;
	dev->dfsentry->shm32read_addr_next = addr;

	return 0;
}

static int shm32write__write_file(struct b43_wldev *dev,
				  const char *buf, size_t count)
{
	unsigned int routing, addr, mask, set;
	u32 val;
	int res;
	unsigned long flags;

	res = sscanf(buf, "0x%X 0x%X 0x%X 0x%X",
		     &routing, &addr, &mask, &set);
	if (res != 4)
		return -EINVAL;
	if (routing > B43_MAX_SHM_ROUTING)
		return -EADDRNOTAVAIL;
	if (addr > B43_MAX_SHM_ADDR)
		return -EADDRNOTAVAIL;
	if (routing == B43_SHM_SHARED) {
		if ((addr % 2) != 0)
			return -EADDRNOTAVAIL;
	}
	if ((mask > 0xFFFFFFFF) || (set > 0xFFFFFFFF))
		return -E2BIG;

	spin_lock_irqsave(&dev->wl->shm_lock, flags);
	if (mask == 0)
		val = 0;
	else
		val = __b43_shm_read32(dev, routing, addr);
	val &= mask;
	val |= set;
	__b43_shm_write32(dev, routing, addr, val);
	spin_unlock_irqrestore(&dev->wl->shm_lock, flags);

	return 0;
}

/* The biggest MMIO address that we allow access to from the debugfs files. */
#define B43_MAX_MMIO_ACCESS	(0xF00 - 1)

static ssize_t mmio16read__read_file(struct b43_wldev *dev,
				     char *buf, size_t bufsize)
{
	ssize_t count = 0;
	unsigned int addr;
	u16 val;

	addr = dev->dfsentry->mmio16read_next;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EDESTADDRREQ;

	val = b43_read16(dev, addr);
	fappend("0x%04X\n", val);

	return count;
}

static int mmio16read__write_file(struct b43_wldev *dev,
				  const char *buf, size_t count)
{
	unsigned int addr;
	int res;

	res = sscanf(buf, "0x%X", &addr);
	if (res != 1)
		return -EINVAL;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EADDRNOTAVAIL;
	if ((addr % 2) != 0)
		return -EINVAL;

	dev->dfsentry->mmio16read_next = addr;

	return 0;
}

static int mmio16write__write_file(struct b43_wldev *dev,
				   const char *buf, size_t count)
{
	unsigned int addr, mask, set;
	int res;
	u16 val;

	res = sscanf(buf, "0x%X 0x%X 0x%X", &addr, &mask, &set);
	if (res != 3)
		return -EINVAL;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EADDRNOTAVAIL;
	if ((mask > 0xFFFF) || (set > 0xFFFF))
		return -E2BIG;
	if ((addr % 2) != 0)
		return -EINVAL;

	if (mask == 0)
		val = 0;
	else
		val = b43_read16(dev, addr);
	val &= mask;
	val |= set;
	b43_write16(dev, addr, val);

	return 0;
}

static ssize_t mmio32read__read_file(struct b43_wldev *dev,
				     char *buf, size_t bufsize)
{
	ssize_t count = 0;
	unsigned int addr;
	u32 val;

	addr = dev->dfsentry->mmio32read_next;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EDESTADDRREQ;

	val = b43_read32(dev, addr);
	fappend("0x%08X\n", val);

	return count;
}

static int mmio32read__write_file(struct b43_wldev *dev,
				  const char *buf, size_t count)
{
	unsigned int addr;
	int res;

	res = sscanf(buf, "0x%X", &addr);
	if (res != 1)
		return -EINVAL;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EADDRNOTAVAIL;
	if ((addr % 4) != 0)
		return -EINVAL;

	dev->dfsentry->mmio32read_next = addr;

	return 0;
}

static int mmio32write__write_file(struct b43_wldev *dev,
				   const char *buf, size_t count)
{
	unsigned int addr, mask, set;
	int res;
	u32 val;

	res = sscanf(buf, "0x%X 0x%X 0x%X", &addr, &mask, &set);
	if (res != 3)
		return -EINVAL;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EADDRNOTAVAIL;
	if ((mask > 0xFFFFFFFF) || (set > 0xFFFFFFFF))
		return -E2BIG;
	if ((addr % 4) != 0)
		return -EINVAL;

	if (mask == 0)
		val = 0;
	else
		val = b43_read32(dev, addr);
	val &= mask;
	val |= set;
	b43_write32(dev, addr, val);

	return 0;
}

/* wl->irq_lock is locked */
static ssize_t tsf_read_file(struct b43_wldev *dev,
			     char *buf, size_t bufsize)
{
	ssize_t count = 0;
	u64 tsf;

	b43_tsf_read(dev, &tsf);
	fappend("0x%08x%08x\n",
		(unsigned int)((tsf & 0xFFFFFFFF00000000ULL) >> 32),
		(unsigned int)(tsf & 0xFFFFFFFFULL));

	return count;
}

/* wl->irq_lock is locked */
static int tsf_write_file(struct b43_wldev *dev,
			  const char *buf, size_t count)
{
	u64 tsf;

	if (sscanf(buf, "%llu", (unsigned long long *)(&tsf)) != 1)
		return -EINVAL;
	b43_tsf_write(dev, tsf);

	return 0;
}

static ssize_t txstat_read_file(struct b43_wldev *dev,
				char *buf, size_t bufsize)
{
	struct b43_txstatus_log *log = &dev->dfsentry->txstatlog;
	ssize_t count = 0;
	unsigned long flags;
	int i, idx;
	struct b43_txstatus *stat;

	spin_lock_irqsave(&log->lock, flags);
	if (log->end < 0) {
		fappend("Nothing transmitted, yet\n");
		goto out_unlock;
	}
	fappend("b43 TX status reports:\n\n"
		"index | cookie | seq | phy_stat | frame_count | "
		"rts_count | supp_reason | pm_indicated | "
		"intermediate | for_ampdu | acked\n" "---\n");
	i = log->end + 1;
	idx = 0;
	while (1) {
		if (i == B43_NR_LOGGED_TXSTATUS)
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
static int restart_write_file(struct b43_wldev *dev,
			      const char *buf, size_t count)
{
	int err = 0;

	if (count > 0 && buf[0] == '1') {
		b43_controller_restart(dev, "manually restarted");
	} else
		err = -EINVAL;

	return err;
}

static unsigned long calc_expire_secs(unsigned long now,
				      unsigned long time,
				      unsigned long expire)
{
	expire = time + expire;

	if (time_after(now, expire))
		return 0; /* expired */
	if (expire < now) {
		/* jiffies wrapped */
		expire -= MAX_JIFFY_OFFSET;
		now -= MAX_JIFFY_OFFSET;
	}
	B43_WARN_ON(expire < now);

	return (expire - now) / HZ;
}

static ssize_t loctls_read_file(struct b43_wldev *dev,
				char *buf, size_t bufsize)
{
	ssize_t count = 0;
	struct b43_txpower_lo_control *lo;
	int i, err = 0;
	struct b43_lo_calib *cal;
	unsigned long now = jiffies;
	struct b43_phy *phy = &dev->phy;

	if (phy->type != B43_PHYTYPE_G) {
		fappend("Device is not a G-PHY\n");
		err = -ENODEV;
		goto out;
	}
	lo = phy->g->lo_control;
	fappend("-- Local Oscillator calibration data --\n\n");
	fappend("HW-power-control enabled: %d\n",
		dev->phy.hardware_power_control);
	fappend("TX Bias: 0x%02X,  TX Magn: 0x%02X  (expire in %lu sec)\n",
		lo->tx_bias, lo->tx_magn,
		calc_expire_secs(now, lo->txctl_measured_time,
				 B43_LO_TXCTL_EXPIRE));
	fappend("Power Vector: 0x%08X%08X  (expires in %lu sec)\n",
		(unsigned int)((lo->power_vector & 0xFFFFFFFF00000000ULL) >> 32),
		(unsigned int)(lo->power_vector & 0x00000000FFFFFFFFULL),
		calc_expire_secs(now, lo->pwr_vec_read_time,
				 B43_LO_PWRVEC_EXPIRE));

	fappend("\nCalibrated settings:\n");
	list_for_each_entry(cal, &lo->calib_list, list) {
		bool active;

		active = (b43_compare_bbatt(&cal->bbatt, &phy->g->bbatt) &&
			  b43_compare_rfatt(&cal->rfatt, &phy->g->rfatt));
		fappend("BB(%d), RF(%d,%d)  ->  I=%d, Q=%d  "
			"(expires in %lu sec)%s\n",
			cal->bbatt.att,
			cal->rfatt.att, cal->rfatt.with_padmix,
			cal->ctl.i, cal->ctl.q,
			calc_expire_secs(now, cal->calib_time,
					 B43_LO_CALIB_EXPIRE),
			active ? "  ACTIVE" : "");
	}

	fappend("\nUsed RF attenuation values:  Value(WithPadmix flag)\n");
	for (i = 0; i < lo->rfatt_list.len; i++) {
		fappend("%u(%d), ",
			lo->rfatt_list.list[i].att,
			lo->rfatt_list.list[i].with_padmix);
	}
	fappend("\n");
	fappend("\nUsed Baseband attenuation values:\n");
	for (i = 0; i < lo->bbatt_list.len; i++) {
		fappend("%u, ",
			lo->bbatt_list.list[i].att);
	}
	fappend("\n");

out:
	return err ? err : count;
}

#undef fappend

static int b43_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t b43_debugfs_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	struct b43_wldev *dev;
	struct b43_debugfs_fops *dfops;
	struct b43_dfs_file *dfile;
	ssize_t uninitialized_var(ret);
	char *buf;
	const size_t bufsize = 1024 * 16; /* 16 kiB buffer */
	const size_t buforder = get_order(bufsize);
	int err = 0;

	if (!count)
		return 0;
	dev = file->private_data;
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->wl->mutex);
	if (b43_status(dev) < B43_STAT_INITIALIZED) {
		err = -ENODEV;
		goto out_unlock;
	}

	dfops = container_of(file->f_op, struct b43_debugfs_fops, fops);
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

static ssize_t b43_debugfs_write(struct file *file,
				 const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct b43_wldev *dev;
	struct b43_debugfs_fops *dfops;
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
	if (b43_status(dev) < B43_STAT_INITIALIZED) {
		err = -ENODEV;
		goto out_unlock;
	}

	dfops = container_of(file->f_op, struct b43_debugfs_fops, fops);
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


#define B43_DEBUGFS_FOPS(name, _read, _write, _take_irqlock)	\
	static struct b43_debugfs_fops fops_##name = {		\
		.read	= _read,				\
		.write	= _write,				\
		.fops	= {					\
			.open	= b43_debugfs_open,		\
			.read	= b43_debugfs_read,		\
			.write	= b43_debugfs_write,		\
		},						\
		.file_struct_offset = offsetof(struct b43_dfsentry, \
					       file_##name),	\
		.take_irqlock	= _take_irqlock,		\
	}

B43_DEBUGFS_FOPS(shm16read, shm16read__read_file, shm16read__write_file, 1);
B43_DEBUGFS_FOPS(shm16write, NULL, shm16write__write_file, 1);
B43_DEBUGFS_FOPS(shm32read, shm32read__read_file, shm32read__write_file, 1);
B43_DEBUGFS_FOPS(shm32write, NULL, shm32write__write_file, 1);
B43_DEBUGFS_FOPS(mmio16read, mmio16read__read_file, mmio16read__write_file, 1);
B43_DEBUGFS_FOPS(mmio16write, NULL, mmio16write__write_file, 1);
B43_DEBUGFS_FOPS(mmio32read, mmio32read__read_file, mmio32read__write_file, 1);
B43_DEBUGFS_FOPS(mmio32write, NULL, mmio32write__write_file, 1);
B43_DEBUGFS_FOPS(tsf, tsf_read_file, tsf_write_file, 1);
B43_DEBUGFS_FOPS(txstat, txstat_read_file, NULL, 0);
B43_DEBUGFS_FOPS(restart, NULL, restart_write_file, 1);
B43_DEBUGFS_FOPS(loctls, loctls_read_file, NULL, 0);


int b43_debug(struct b43_wldev *dev, enum b43_dyndbg feature)
{
	return !!(dev->dfsentry && dev->dfsentry->dyn_debug[feature]);
}

static void b43_remove_dynamic_debug(struct b43_wldev *dev)
{
	struct b43_dfsentry *e = dev->dfsentry;
	int i;

	for (i = 0; i < __B43_NR_DYNDBG; i++)
		debugfs_remove(e->dyn_debug_dentries[i]);
}

static void b43_add_dynamic_debug(struct b43_wldev *dev)
{
	struct b43_dfsentry *e = dev->dfsentry;
	struct dentry *d;

#define add_dyn_dbg(name, id, initstate) do {		\
	e->dyn_debug[id] = (initstate);			\
	d = debugfs_create_bool(name, 0600, e->subdir,	\
				&(e->dyn_debug[id]));	\
	if (!IS_ERR(d))					\
		e->dyn_debug_dentries[id] = d;		\
				} while (0)

	add_dyn_dbg("debug_xmitpower", B43_DBG_XMITPOWER, 0);
	add_dyn_dbg("debug_dmaoverflow", B43_DBG_DMAOVERFLOW, 0);
	add_dyn_dbg("debug_dmaverbose", B43_DBG_DMAVERBOSE, 0);
	add_dyn_dbg("debug_pwork_fast", B43_DBG_PWORK_FAST, 0);
	add_dyn_dbg("debug_pwork_stop", B43_DBG_PWORK_STOP, 0);
	add_dyn_dbg("debug_lo", B43_DBG_LO, 0);
	add_dyn_dbg("debug_firmware", B43_DBG_FIRMWARE, 0);
	add_dyn_dbg("debug_keys", B43_DBG_KEYS, 0);

#undef add_dyn_dbg
}

void b43_debugfs_add_device(struct b43_wldev *dev)
{
	struct b43_dfsentry *e;
	struct b43_txstatus_log *log;
	char devdir[16];

	B43_WARN_ON(!dev);
	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e) {
		b43err(dev->wl, "debugfs: add device OOM\n");
		return;
	}
	e->dev = dev;
	log = &e->txstatlog;
	log->log = kcalloc(B43_NR_LOGGED_TXSTATUS,
			   sizeof(struct b43_txstatus), GFP_KERNEL);
	if (!log->log) {
		b43err(dev->wl, "debugfs: add device txstatus OOM\n");
		kfree(e);
		return;
	}
	log->end = -1;
	spin_lock_init(&log->lock);

	dev->dfsentry = e;

	snprintf(devdir, sizeof(devdir), "%s", wiphy_name(dev->wl->hw->wiphy));
	e->subdir = debugfs_create_dir(devdir, rootdir);
	if (!e->subdir || IS_ERR(e->subdir)) {
		if (e->subdir == ERR_PTR(-ENODEV)) {
			b43dbg(dev->wl, "DebugFS (CONFIG_DEBUG_FS) not "
			       "enabled in kernel config\n");
		} else {
			b43err(dev->wl, "debugfs: cannot create %s directory\n",
			       devdir);
		}
		dev->dfsentry = NULL;
		kfree(log->log);
		kfree(e);
		return;
	}

	e->mmio16read_next = 0xFFFF; /* invalid address */
	e->mmio32read_next = 0xFFFF; /* invalid address */
	e->shm16read_routing_next = 0xFFFFFFFF; /* invalid routing */
	e->shm16read_addr_next = 0xFFFFFFFF; /* invalid address */
	e->shm32read_routing_next = 0xFFFFFFFF; /* invalid routing */
	e->shm32read_addr_next = 0xFFFFFFFF; /* invalid address */

#define ADD_FILE(name, mode)	\
	do {							\
		struct dentry *d;				\
		d = debugfs_create_file(__stringify(name),	\
					mode, e->subdir, dev,	\
					&fops_##name.fops);	\
		e->file_##name.dentry = NULL;			\
		if (!IS_ERR(d))					\
			e->file_##name.dentry = d;		\
	} while (0)


	ADD_FILE(shm16read, 0600);
	ADD_FILE(shm16write, 0200);
	ADD_FILE(shm32read, 0600);
	ADD_FILE(shm32write, 0200);
	ADD_FILE(mmio16read, 0600);
	ADD_FILE(mmio16write, 0200);
	ADD_FILE(mmio32read, 0600);
	ADD_FILE(mmio32write, 0200);
	ADD_FILE(tsf, 0600);
	ADD_FILE(txstat, 0400);
	ADD_FILE(restart, 0200);
	ADD_FILE(loctls, 0400);

#undef ADD_FILE

	b43_add_dynamic_debug(dev);
}

void b43_debugfs_remove_device(struct b43_wldev *dev)
{
	struct b43_dfsentry *e;

	if (!dev)
		return;
	e = dev->dfsentry;
	if (!e)
		return;
	b43_remove_dynamic_debug(dev);

	debugfs_remove(e->file_shm16read.dentry);
	debugfs_remove(e->file_shm16write.dentry);
	debugfs_remove(e->file_shm32read.dentry);
	debugfs_remove(e->file_shm32write.dentry);
	debugfs_remove(e->file_mmio16read.dentry);
	debugfs_remove(e->file_mmio16write.dentry);
	debugfs_remove(e->file_mmio32read.dentry);
	debugfs_remove(e->file_mmio32write.dentry);
	debugfs_remove(e->file_tsf.dentry);
	debugfs_remove(e->file_txstat.dentry);
	debugfs_remove(e->file_restart.dentry);
	debugfs_remove(e->file_loctls.dentry);

	debugfs_remove(e->subdir);
	kfree(e->txstatlog.log);
	kfree(e);
}

/* Called with IRQs disabled. */
void b43_debugfs_log_txstat(struct b43_wldev *dev,
			    const struct b43_txstatus *status)
{
	struct b43_dfsentry *e = dev->dfsentry;
	struct b43_txstatus_log *log;
	struct b43_txstatus *cur;
	int i;

	if (!e)
		return;
	log = &e->txstatlog;
	spin_lock(&log->lock); /* IRQs are already disabled. */
	i = log->end + 1;
	if (i == B43_NR_LOGGED_TXSTATUS)
		i = 0;
	log->end = i;
	cur = &(log->log[i]);
	memcpy(cur, status, sizeof(*cur));
	spin_unlock(&log->lock);
}

void b43_debugfs_init(void)
{
	rootdir = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(rootdir))
		rootdir = NULL;
}

void b43_debugfs_exit(void)
{
	debugfs_remove(rootdir);
}
