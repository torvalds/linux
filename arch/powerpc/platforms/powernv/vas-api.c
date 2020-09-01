// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * VAS user space API for its accelerators (Only NX-GZIP is supported now)
 * Copyright (C) 2019 Haren Myneni, IBM Corp
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/vas.h>
#include <uapi/asm/vas-api.h>
#include "vas.h"

/*
 * The driver creates the device node that can be used as follows:
 * For NX-GZIP
 *
 *	fd = open("/dev/crypto/nx-gzip", O_RDWR);
 *	rc = ioctl(fd, VAS_TX_WIN_OPEN, &attr);
 *	paste_addr = mmap(NULL, PAGE_SIZE, prot, MAP_SHARED, fd, 0ULL).
 *	vas_copy(&crb, 0, 1);
 *	vas_paste(paste_addr, 0, 1);
 *	close(fd) or exit process to close window.
 *
 * where "vas_copy" and "vas_paste" are defined in copy-paste.h.
 * copy/paste returns to the user space directly. So refer NX hardware
 * documententation for exact copy/paste usage and completion / error
 * conditions.
 */

/*
 * Wrapper object for the nx-gzip device - there is just one instance of
 * this node for the whole system.
 */
static struct coproc_dev {
	struct cdev cdev;
	struct device *device;
	char *name;
	dev_t devt;
	struct class *class;
	enum vas_cop_type cop_type;
} coproc_device;

struct coproc_instance {
	struct coproc_dev *coproc;
	struct vas_window *txwin;
};

static char *coproc_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "crypto/%s", dev_name(dev));
}

static int coproc_open(struct inode *inode, struct file *fp)
{
	struct coproc_instance *cp_inst;

	cp_inst = kzalloc(sizeof(*cp_inst), GFP_KERNEL);
	if (!cp_inst)
		return -ENOMEM;

	cp_inst->coproc = container_of(inode->i_cdev, struct coproc_dev,
					cdev);
	fp->private_data = cp_inst;

	return 0;
}

static int coproc_ioc_tx_win_open(struct file *fp, unsigned long arg)
{
	void __user *uptr = (void __user *)arg;
	struct vas_tx_win_attr txattr = {};
	struct vas_tx_win_open_attr uattr;
	struct coproc_instance *cp_inst;
	struct vas_window *txwin;
	int rc, vasid;

	cp_inst = fp->private_data;

	/*
	 * One window for file descriptor
	 */
	if (cp_inst->txwin)
		return -EEXIST;

	rc = copy_from_user(&uattr, uptr, sizeof(uattr));
	if (rc) {
		pr_err("%s(): copy_from_user() returns %d\n", __func__, rc);
		return -EFAULT;
	}

	if (uattr.version != 1) {
		pr_err("Invalid version\n");
		return -EINVAL;
	}

	vasid = uattr.vas_id;

	vas_init_tx_win_attr(&txattr, cp_inst->coproc->cop_type);

	txattr.lpid = mfspr(SPRN_LPID);
	txattr.pidr = mfspr(SPRN_PID);
	txattr.user_win = true;
	txattr.rsvd_txbuf_count = false;
	txattr.pswid = false;

	pr_devel("Pid %d: Opening txwin, PIDR %ld\n", txattr.pidr,
				mfspr(SPRN_PID));

	txwin = vas_tx_win_open(vasid, cp_inst->coproc->cop_type, &txattr);
	if (IS_ERR(txwin)) {
		pr_err("%s() vas_tx_win_open() failed, %ld\n", __func__,
					PTR_ERR(txwin));
		return PTR_ERR(txwin);
	}

	cp_inst->txwin = txwin;

	return 0;
}

static int coproc_release(struct inode *inode, struct file *fp)
{
	struct coproc_instance *cp_inst = fp->private_data;

	if (cp_inst->txwin) {
		vas_win_close(cp_inst->txwin);
		cp_inst->txwin = NULL;
	}

	kfree(cp_inst);
	fp->private_data = NULL;

	/*
	 * We don't know here if user has other receive windows
	 * open, so we can't really call clear_thread_tidr().
	 * So, once the process calls set_thread_tidr(), the
	 * TIDR value sticks around until process exits, resulting
	 * in an extra copy in restore_sprs().
	 */

	return 0;
}

static int coproc_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct coproc_instance *cp_inst = fp->private_data;
	struct vas_window *txwin;
	unsigned long pfn;
	u64 paste_addr;
	pgprot_t prot;
	int rc;

	txwin = cp_inst->txwin;

	if ((vma->vm_end - vma->vm_start) > PAGE_SIZE) {
		pr_debug("%s(): size 0x%zx, PAGE_SIZE 0x%zx\n", __func__,
				(vma->vm_end - vma->vm_start), PAGE_SIZE);
		return -EINVAL;
	}

	/* Ensure instance has an open send window */
	if (!txwin) {
		pr_err("%s(): No send window open?\n", __func__);
		return -EINVAL;
	}

	vas_win_paste_addr(txwin, &paste_addr, NULL);
	pfn = paste_addr >> PAGE_SHIFT;

	/* flags, page_prot from cxl_mmap(), except we want cachable */
	vma->vm_flags |= VM_IO | VM_PFNMAP;
	vma->vm_page_prot = pgprot_cached(vma->vm_page_prot);

	prot = __pgprot(pgprot_val(vma->vm_page_prot) | _PAGE_DIRTY);

	rc = remap_pfn_range(vma, vma->vm_start, pfn + vma->vm_pgoff,
			vma->vm_end - vma->vm_start, prot);

	pr_devel("%s(): paste addr %llx at %lx, rc %d\n", __func__,
			paste_addr, vma->vm_start, rc);

	return rc;
}

static long coproc_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case VAS_TX_WIN_OPEN:
		return coproc_ioc_tx_win_open(fp, arg);
	default:
		return -EINVAL;
	}
}

static struct file_operations coproc_fops = {
	.open = coproc_open,
	.release = coproc_release,
	.mmap = coproc_mmap,
	.unlocked_ioctl = coproc_ioctl,
};

/*
 * Supporting only nx-gzip coprocessor type now, but this API code
 * extended to other coprocessor types later.
 */
int vas_register_coproc_api(struct module *mod, enum vas_cop_type cop_type,
				const char *name)
{
	int rc = -EINVAL;
	dev_t devno;

	rc = alloc_chrdev_region(&coproc_device.devt, 1, 1, name);
	if (rc) {
		pr_err("Unable to allocate coproc major number: %i\n", rc);
		return rc;
	}

	pr_devel("%s device allocated, dev [%i,%i]\n", name,
			MAJOR(coproc_device.devt), MINOR(coproc_device.devt));

	coproc_device.class = class_create(mod, name);
	if (IS_ERR(coproc_device.class)) {
		rc = PTR_ERR(coproc_device.class);
		pr_err("Unable to create %s class %d\n", name, rc);
		goto err_class;
	}
	coproc_device.class->devnode = coproc_devnode;
	coproc_device.cop_type = cop_type;

	coproc_fops.owner = mod;
	cdev_init(&coproc_device.cdev, &coproc_fops);

	devno = MKDEV(MAJOR(coproc_device.devt), 0);
	rc = cdev_add(&coproc_device.cdev, devno, 1);
	if (rc) {
		pr_err("cdev_add() failed %d\n", rc);
		goto err_cdev;
	}

	coproc_device.device = device_create(coproc_device.class, NULL,
			devno, NULL, name, MINOR(devno));
	if (IS_ERR(coproc_device.device)) {
		rc = PTR_ERR(coproc_device.device);
		pr_err("Unable to create coproc-%d %d\n", MINOR(devno), rc);
		goto err;
	}

	pr_devel("%s: Added dev [%d,%d]\n", __func__, MAJOR(devno),
			MINOR(devno));

	return 0;

err:
	cdev_del(&coproc_device.cdev);
err_cdev:
	class_destroy(coproc_device.class);
err_class:
	unregister_chrdev_region(coproc_device.devt, 1);
	return rc;
}
EXPORT_SYMBOL_GPL(vas_register_coproc_api);

void vas_unregister_coproc_api(void)
{
	dev_t devno;

	cdev_del(&coproc_device.cdev);
	devno = MKDEV(MAJOR(coproc_device.devt), 0);
	device_destroy(coproc_device.class, devno);

	class_destroy(coproc_device.class);
	unregister_chrdev_region(coproc_device.devt, 1);
}
EXPORT_SYMBOL_GPL(vas_unregister_coproc_api);
