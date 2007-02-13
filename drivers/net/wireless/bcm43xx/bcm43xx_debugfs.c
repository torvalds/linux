/*

  Broadcom BCM43xx wireless driver

  debugfs driver debugging code

  Copyright (c) 2005 Michael Buesch <mbuesch@freenet.de>

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
#include <asm/io.h>

#include "bcm43xx.h"
#include "bcm43xx_main.h"
#include "bcm43xx_debugfs.h"
#include "bcm43xx_dma.h"
#include "bcm43xx_pio.h"
#include "bcm43xx_xmit.h"

#define REALLY_BIG_BUFFER_SIZE	(1024*256)

static struct bcm43xx_debugfs fs;
static char really_big_buffer[REALLY_BIG_BUFFER_SIZE];
static DECLARE_MUTEX(big_buffer_sem);


static ssize_t write_file_dummy(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	return count;
}

static int open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

#define fappend(fmt, x...)	pos += snprintf(buf + pos, len - pos, fmt , ##x)

static ssize_t devinfo_read_file(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	const size_t len = REALLY_BIG_BUFFER_SIZE;

	struct bcm43xx_private *bcm = file->private_data;
	char *buf = really_big_buffer;
	size_t pos = 0;
	ssize_t res;
	struct net_device *net_dev;
	struct pci_dev *pci_dev;
	unsigned long flags;
	u16 tmp16;
	int i;

	down(&big_buffer_sem);

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	if (bcm43xx_status(bcm) != BCM43xx_STAT_INITIALIZED) {
		fappend("Board not initialized.\n");
		goto out;
	}
	net_dev = bcm->net_dev;
	pci_dev = bcm->pci_dev;

	/* This is where the information is written to the "devinfo" file */
	fappend("*** %s devinfo ***\n", net_dev->name);
	fappend("vendor:           0x%04x   device:           0x%04x\n",
		pci_dev->vendor, pci_dev->device);
	fappend("subsystem_vendor: 0x%04x   subsystem_device: 0x%04x\n",
		pci_dev->subsystem_vendor, pci_dev->subsystem_device);
	fappend("IRQ: %d\n", bcm->irq);
	fappend("mmio_addr: 0x%p\n", bcm->mmio_addr);
	fappend("chip_id: 0x%04x   chip_rev: 0x%02x\n", bcm->chip_id, bcm->chip_rev);
	if ((bcm->core_80211[0].rev >= 3) && (bcm43xx_read32(bcm, 0x0158) & (1 << 16)))
		fappend("Radio disabled by hardware!\n");
	if ((bcm->core_80211[0].rev < 3) && !(bcm43xx_read16(bcm, 0x049A) & (1 << 4)))
		fappend("Radio disabled by hardware!\n");
	fappend("board_vendor: 0x%04x   board_type: 0x%04x\n", bcm->board_vendor,
	        bcm->board_type);

	fappend("\nCores:\n");
#define fappend_core(name, info) fappend("core \"" name "\" %s, %s, id: 0x%04x, "	\
					 "rev: 0x%02x, index: 0x%02x\n",		\
					 (info).available				\
						? "available" : "nonavailable",		\
					 (info).enabled					\
						? "enabled" : "disabled",		\
					 (info).id, (info).rev, (info).index)
	fappend_core("CHIPCOMMON", bcm->core_chipcommon);
	fappend_core("PCI", bcm->core_pci);
	fappend_core("first 80211", bcm->core_80211[0]);
	fappend_core("second 80211", bcm->core_80211[1]);
#undef fappend_core
	tmp16 = bcm43xx_read16(bcm, BCM43xx_MMIO_GPIO_CONTROL);
	fappend("LEDs: ");
	for (i = 0; i < BCM43xx_NR_LEDS; i++)
		fappend("%d ", !!(tmp16 & (1 << i)));
	fappend("\n");

out:
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);
	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	up(&big_buffer_sem);
	return res;
}

static ssize_t drvinfo_read_file(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	const size_t len = REALLY_BIG_BUFFER_SIZE;

	char *buf = really_big_buffer;
	size_t pos = 0;
	ssize_t res;

	down(&big_buffer_sem);

	/* This is where the information is written to the "driver" file */
	fappend(KBUILD_MODNAME " driver\n");
	fappend("Compiled at: %s %s\n", __DATE__, __TIME__);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	up(&big_buffer_sem);
	return res;
}

static ssize_t spromdump_read_file(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	const size_t len = REALLY_BIG_BUFFER_SIZE;

	struct bcm43xx_private *bcm = file->private_data;
	char *buf = really_big_buffer;
	size_t pos = 0;
	ssize_t res;
	unsigned long flags;

	down(&big_buffer_sem);
	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	if (bcm43xx_status(bcm) != BCM43xx_STAT_INITIALIZED) {
		fappend("Board not initialized.\n");
		goto out;
	}

	/* This is where the information is written to the "sprom_dump" file */
	fappend("boardflags: 0x%04x\n", bcm->sprom.boardflags);

out:
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);
	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	up(&big_buffer_sem);
	return res;
}

static ssize_t tsf_read_file(struct file *file, char __user *userbuf,
			     size_t count, loff_t *ppos)
{
	const size_t len = REALLY_BIG_BUFFER_SIZE;

	struct bcm43xx_private *bcm = file->private_data;
	char *buf = really_big_buffer;
	size_t pos = 0;
	ssize_t res;
	unsigned long flags;
	u64 tsf;

	down(&big_buffer_sem);
	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	if (bcm43xx_status(bcm) != BCM43xx_STAT_INITIALIZED) {
		fappend("Board not initialized.\n");
		goto out;
	}
	bcm43xx_tsf_read(bcm, &tsf);
	fappend("0x%08x%08x\n",
		(unsigned int)((tsf & 0xFFFFFFFF00000000ULL) >> 32),
		(unsigned int)(tsf & 0xFFFFFFFFULL));

out:
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);
	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	up(&big_buffer_sem);
	return res;
}

static ssize_t tsf_write_file(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct bcm43xx_private *bcm = file->private_data;
	char *buf = really_big_buffer;
	ssize_t buf_size;
	ssize_t res;
	unsigned long flags;
	u64 tsf;

	buf_size = min(count, sizeof (really_big_buffer) - 1);
	down(&big_buffer_sem);
	if (copy_from_user(buf, user_buf, buf_size)) {
	        res = -EFAULT;
		goto out_up;
	}
	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	if (bcm43xx_status(bcm) != BCM43xx_STAT_INITIALIZED) {
		printk(KERN_INFO PFX "debugfs: Board not initialized.\n");
		res = -EFAULT;
		goto out_unlock;
	}
	if (sscanf(buf, "%lli", &tsf) != 1) {
		printk(KERN_INFO PFX "debugfs: invalid values for \"tsf\"\n");
		res = -EINVAL;
		goto out_unlock;
	}
	bcm43xx_tsf_write(bcm, tsf);
	mmiowb();
	res = buf_size;
	
out_unlock:
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);
out_up:
	up(&big_buffer_sem);
	return res;
}

static ssize_t txstat_read_file(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	const size_t len = REALLY_BIG_BUFFER_SIZE;

	struct bcm43xx_private *bcm = file->private_data;
	char *buf = really_big_buffer;
	size_t pos = 0;
	ssize_t res;
	unsigned long flags;
	struct bcm43xx_dfsentry *e;
	struct bcm43xx_xmitstatus *status;
	int i, cnt, j = 0;

	down(&big_buffer_sem);
	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);

	fappend("Last %d logged xmitstatus blobs (Latest first):\n\n",
		BCM43xx_NR_LOGGED_XMITSTATUS);
	e = bcm->dfsentry;
	if (e->xmitstatus_printing == 0) {
		/* At the beginning, make a copy of all data to avoid
		 * concurrency, as this function is called multiple
		 * times for big logs. Without copying, the data might
		 * change between reads. This would result in total trash.
		 */
		e->xmitstatus_printing = 1;
		e->saved_xmitstatus_ptr = e->xmitstatus_ptr;
		e->saved_xmitstatus_cnt = e->xmitstatus_cnt;
		memcpy(e->xmitstatus_print_buffer, e->xmitstatus_buffer,
		       BCM43xx_NR_LOGGED_XMITSTATUS * sizeof(*(e->xmitstatus_buffer)));
	}
	i = e->saved_xmitstatus_ptr - 1;
	if (i < 0)
		i = BCM43xx_NR_LOGGED_XMITSTATUS - 1;
	cnt = e->saved_xmitstatus_cnt;
	while (cnt) {
		status = e->xmitstatus_print_buffer + i;
		fappend("0x%02x:   cookie: 0x%04x,  flags: 0x%02x,  "
			"cnt1: 0x%02x,  cnt2: 0x%02x,  seq: 0x%04x,  "
			"unk: 0x%04x\n", j,
			status->cookie, status->flags,
			status->cnt1, status->cnt2, status->seq,
			status->unknown);
		j++;
		cnt--;
		i--;
		if (i < 0)
			i = BCM43xx_NR_LOGGED_XMITSTATUS - 1;
	}

	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	if (*ppos == pos) {
		/* Done. Drop the copied data. */
		e->xmitstatus_printing = 0;
	}
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);
	up(&big_buffer_sem);
	return res;
}

static ssize_t restart_write_file(struct file *file, const char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct bcm43xx_private *bcm = file->private_data;
	char *buf = really_big_buffer;
	ssize_t buf_size;
	ssize_t res;
	unsigned long flags;

	buf_size = min(count, sizeof (really_big_buffer) - 1);
	down(&big_buffer_sem);
	if (copy_from_user(buf, user_buf, buf_size)) {
	        res = -EFAULT;
		goto out_up;
	}
	mutex_lock(&(bcm)->mutex);
	spin_lock_irqsave(&(bcm)->irq_lock, flags);
	if (bcm43xx_status(bcm) != BCM43xx_STAT_INITIALIZED) {
		printk(KERN_INFO PFX "debugfs: Board not initialized.\n");
		res = -EFAULT;
		goto out_unlock;
	}
	if (count > 0 && buf[0] == '1') {
		bcm43xx_controller_restart(bcm, "manually restarted");
		res = count;
	} else
		res = -EINVAL;

out_unlock:
	spin_unlock_irqrestore(&(bcm)->irq_lock, flags);
	mutex_unlock(&(bcm)->mutex);
out_up:
	up(&big_buffer_sem);
	return res;
}

#undef fappend


static const struct file_operations devinfo_fops = {
	.read = devinfo_read_file,
	.write = write_file_dummy,
	.open = open_file_generic,
};

static const struct file_operations spromdump_fops = {
	.read = spromdump_read_file,
	.write = write_file_dummy,
	.open = open_file_generic,
};

static const struct file_operations drvinfo_fops = {
	.read = drvinfo_read_file,
	.write = write_file_dummy,
	.open = open_file_generic,
};

static const struct file_operations tsf_fops = {
	.read = tsf_read_file,
	.write = tsf_write_file,
	.open = open_file_generic,
};

static const struct file_operations txstat_fops = {
	.read = txstat_read_file,
	.write = write_file_dummy,
	.open = open_file_generic,
};

static const struct file_operations restart_fops = {
	.write = restart_write_file,
	.open = open_file_generic,
};


void bcm43xx_debugfs_add_device(struct bcm43xx_private *bcm)
{
	struct bcm43xx_dfsentry *e;
	char devdir[IFNAMSIZ];

	assert(bcm);
	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e) {
		printk(KERN_ERR PFX "out of memory\n");
		return;
	}
	e->bcm = bcm;
	e->xmitstatus_buffer = kzalloc(BCM43xx_NR_LOGGED_XMITSTATUS
				       * sizeof(*(e->xmitstatus_buffer)),
				       GFP_KERNEL);
	if (!e->xmitstatus_buffer) {
		printk(KERN_ERR PFX "out of memory\n");
		kfree(e);
		return;
	}
	e->xmitstatus_print_buffer = kzalloc(BCM43xx_NR_LOGGED_XMITSTATUS
					     * sizeof(*(e->xmitstatus_buffer)),
					     GFP_KERNEL);
	if (!e->xmitstatus_print_buffer) {
		printk(KERN_ERR PFX "out of memory\n");
		kfree(e);
		return;
	}


	bcm->dfsentry = e;

	strncpy(devdir, bcm->net_dev->name, ARRAY_SIZE(devdir));
	e->subdir = debugfs_create_dir(devdir, fs.root);
	e->dentry_devinfo = debugfs_create_file("devinfo", 0444, e->subdir,
						bcm, &devinfo_fops);
	if (!e->dentry_devinfo)
		printk(KERN_ERR PFX "debugfs: creating \"devinfo\" for \"%s\" failed!\n", devdir);
	e->dentry_spromdump = debugfs_create_file("sprom_dump", 0444, e->subdir,
						  bcm, &spromdump_fops);
	if (!e->dentry_spromdump)
		printk(KERN_ERR PFX "debugfs: creating \"sprom_dump\" for \"%s\" failed!\n", devdir);
	e->dentry_tsf = debugfs_create_file("tsf", 0666, e->subdir,
	                                    bcm, &tsf_fops);
	if (!e->dentry_tsf)
		printk(KERN_ERR PFX "debugfs: creating \"tsf\" for \"%s\" failed!\n", devdir);
	e->dentry_txstat = debugfs_create_file("tx_status", 0444, e->subdir,
						bcm, &txstat_fops);
	if (!e->dentry_txstat)
		printk(KERN_ERR PFX "debugfs: creating \"tx_status\" for \"%s\" failed!\n", devdir);
	e->dentry_restart = debugfs_create_file("restart", 0222, e->subdir,
						bcm, &restart_fops);
	if (!e->dentry_restart)
		printk(KERN_ERR PFX "debugfs: creating \"restart\" for \"%s\" failed!\n", devdir);
}

void bcm43xx_debugfs_remove_device(struct bcm43xx_private *bcm)
{
	struct bcm43xx_dfsentry *e;

	if (!bcm)
		return;

	e = bcm->dfsentry;
	assert(e);
	debugfs_remove(e->dentry_spromdump);
	debugfs_remove(e->dentry_devinfo);
	debugfs_remove(e->dentry_tsf);
	debugfs_remove(e->dentry_txstat);
	debugfs_remove(e->dentry_restart);
	debugfs_remove(e->subdir);
	kfree(e->xmitstatus_buffer);
	kfree(e->xmitstatus_print_buffer);
	kfree(e);
}

void bcm43xx_debugfs_log_txstat(struct bcm43xx_private *bcm,
				struct bcm43xx_xmitstatus *status)
{
	struct bcm43xx_dfsentry *e;
	struct bcm43xx_xmitstatus *savedstatus;

	/* This is protected by bcm->_lock */
	e = bcm->dfsentry;
	assert(e);
	savedstatus = e->xmitstatus_buffer + e->xmitstatus_ptr;
	memcpy(savedstatus, status, sizeof(*status));
	e->xmitstatus_ptr++;
	if (e->xmitstatus_ptr >= BCM43xx_NR_LOGGED_XMITSTATUS)
		e->xmitstatus_ptr = 0;
	if (e->xmitstatus_cnt < BCM43xx_NR_LOGGED_XMITSTATUS)
		e->xmitstatus_cnt++;
}

void bcm43xx_debugfs_init(void)
{
	memset(&fs, 0, sizeof(fs));
	fs.root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!fs.root)
		printk(KERN_ERR PFX "debugfs: creating \"" KBUILD_MODNAME "\" subdir failed!\n");
	fs.dentry_driverinfo = debugfs_create_file("driver", 0444, fs.root, NULL, &drvinfo_fops);
	if (!fs.dentry_driverinfo)
		printk(KERN_ERR PFX "debugfs: creating \"" KBUILD_MODNAME "/driver\" failed!\n");
}

void bcm43xx_debugfs_exit(void)
{
	debugfs_remove(fs.dentry_driverinfo);
	debugfs_remove(fs.root);
}

void bcm43xx_printk_dump(const char *data,
			 size_t size,
			 const char *description)
{
	size_t i;
	char c;

	printk(KERN_INFO PFX "Data dump (%s, %zd bytes):",
	       description, size);
	for (i = 0; i < size; i++) {
		c = data[i];
		if (i % 8 == 0)
			printk("\n" KERN_INFO PFX "0x%08zx:  0x%02x, ", i, c & 0xff);
		else
			printk("0x%02x, ", c & 0xff);
	}
	printk("\n");
}

void bcm43xx_printk_bitdump(const unsigned char *data,
			    size_t bytes, int msb_to_lsb,
			    const char *description)
{
	size_t i;
	int j;
	const unsigned char *d;

	printk(KERN_INFO PFX "*** Bitdump (%s, %zd bytes, %s) ***",
	       description, bytes, msb_to_lsb ? "MSB to LSB" : "LSB to MSB");
	for (i = 0; i < bytes; i++) {
		d = data + i;
		if (i % 8 == 0)
			printk("\n" KERN_INFO PFX "0x%08zx:  ", i);
		if (msb_to_lsb) {
			for (j = 7; j >= 0; j--) {
				if (*d & (1 << j))
					printk("1");
				else
					printk("0");
			}
		} else {
			for (j = 0; j < 8; j++) {
				if (*d & (1 << j))
					printk("1");
				else
					printk("0");
			}
		}
		printk(" ");
	}
	printk("\n");
}
