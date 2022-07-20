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
#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/mmu_context.h>
#include <linux/io.h>
#include <asm/vas.h>
#include <uapi/asm/vas-api.h>

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
 * documentation for exact copy/paste usage and completion / error
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
	const struct vas_user_win_ops *vops;
} coproc_device;

struct coproc_instance {
	struct coproc_dev *coproc;
	struct vas_window *txwin;
};

static char *coproc_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "crypto/%s", dev_name(dev));
}

/*
 * Take reference to pid and mm
 */
int get_vas_user_win_ref(struct vas_user_win_ref *task_ref)
{
	/*
	 * Window opened by a child thread may not be closed when
	 * it exits. So take reference to its pid and release it
	 * when the window is free by parent thread.
	 * Acquire a reference to the task's pid to make sure
	 * pid will not be re-used - needed only for multithread
	 * applications.
	 */
	task_ref->pid = get_task_pid(current, PIDTYPE_PID);
	/*
	 * Acquire a reference to the task's mm.
	 */
	task_ref->mm = get_task_mm(current);
	if (!task_ref->mm) {
		put_pid(task_ref->pid);
		pr_err("VAS: pid(%d): mm_struct is not found\n",
				current->pid);
		return -EPERM;
	}

	mmgrab(task_ref->mm);
	mmput(task_ref->mm);
	/*
	 * Process closes window during exit. In the case of
	 * multithread application, the child thread can open
	 * window and can exit without closing it. So takes tgid
	 * reference until window closed to make sure tgid is not
	 * reused.
	 */
	task_ref->tgid = find_get_pid(task_tgid_vnr(current));

	return 0;
}

/*
 * Successful return must release the task reference with
 * put_task_struct
 */
static bool ref_get_pid_and_task(struct vas_user_win_ref *task_ref,
			  struct task_struct **tskp, struct pid **pidp)
{
	struct task_struct *tsk;
	struct pid *pid;

	pid = task_ref->pid;
	tsk = get_pid_task(pid, PIDTYPE_PID);
	if (!tsk) {
		pid = task_ref->tgid;
		tsk = get_pid_task(pid, PIDTYPE_PID);
		/*
		 * Parent thread (tgid) will be closing window when it
		 * exits. So should not get here.
		 */
		if (WARN_ON_ONCE(!tsk))
			return false;
	}

	/* Return if the task is exiting. */
	if (tsk->flags & PF_EXITING) {
		put_task_struct(tsk);
		return false;
	}

	*tskp = tsk;
	*pidp = pid;

	return true;
}

/*
 * Update the CSB to indicate a translation error.
 *
 * User space will be polling on CSB after the request is issued.
 * If NX can handle the request without any issues, it updates CSB.
 * Whereas if NX encounters page fault, the kernel will handle the
 * fault and update CSB with translation error.
 *
 * If we are unable to update the CSB means copy_to_user failed due to
 * invalid csb_addr, send a signal to the process.
 */
void vas_update_csb(struct coprocessor_request_block *crb,
		    struct vas_user_win_ref *task_ref)
{
	struct coprocessor_status_block csb;
	struct kernel_siginfo info;
	struct task_struct *tsk;
	void __user *csb_addr;
	struct pid *pid;
	int rc;

	/*
	 * NX user space windows can not be opened for task->mm=NULL
	 * and faults will not be generated for kernel requests.
	 */
	if (WARN_ON_ONCE(!task_ref->mm))
		return;

	csb_addr = (void __user *)be64_to_cpu(crb->csb_addr);

	memset(&csb, 0, sizeof(csb));
	csb.cc = CSB_CC_FAULT_ADDRESS;
	csb.ce = CSB_CE_TERMINATION;
	csb.cs = 0;
	csb.count = 0;

	/*
	 * NX operates and returns in BE format as defined CRB struct.
	 * So saves fault_storage_addr in BE as NX pastes in FIFO and
	 * expects user space to convert to CPU format.
	 */
	csb.address = crb->stamp.nx.fault_storage_addr;
	csb.flags = 0;

	/*
	 * Process closes send window after all pending NX requests are
	 * completed. In multi-thread applications, a child thread can
	 * open a window and can exit without closing it. May be some
	 * requests are pending or this window can be used by other
	 * threads later. We should handle faults if NX encounters
	 * pages faults on these requests. Update CSB with translation
	 * error and fault address. If csb_addr passed by user space is
	 * invalid, send SEGV signal to pid saved in window. If the
	 * child thread is not running, send the signal to tgid.
	 * Parent thread (tgid) will close this window upon its exit.
	 *
	 * pid and mm references are taken when window is opened by
	 * process (pid). So tgid is used only when child thread opens
	 * a window and exits without closing it.
	 */

	if (!ref_get_pid_and_task(task_ref, &tsk, &pid))
		return;

	kthread_use_mm(task_ref->mm);
	rc = copy_to_user(csb_addr, &csb, sizeof(csb));
	/*
	 * User space polls on csb.flags (first byte). So add barrier
	 * then copy first byte with csb flags update.
	 */
	if (!rc) {
		csb.flags = CSB_V;
		/* Make sure update to csb.flags is visible now */
		smp_mb();
		rc = copy_to_user(csb_addr, &csb, sizeof(u8));
	}
	kthread_unuse_mm(task_ref->mm);
	put_task_struct(tsk);

	/* Success */
	if (!rc)
		return;


	pr_debug("Invalid CSB address 0x%p signalling pid(%d)\n",
			csb_addr, pid_vnr(pid));

	clear_siginfo(&info);
	info.si_signo = SIGSEGV;
	info.si_errno = EFAULT;
	info.si_code = SEGV_MAPERR;
	info.si_addr = csb_addr;
	/*
	 * process will be polling on csb.flags after request is sent to
	 * NX. So generally CSB update should not fail except when an
	 * application passes invalid csb_addr. So an error message will
	 * be displayed and leave it to user space whether to ignore or
	 * handle this signal.
	 */
	rcu_read_lock();
	rc = kill_pid_info(SIGSEGV, &info, pid);
	rcu_read_unlock();

	pr_devel("%s(): pid %d kill_proc_info() rc %d\n", __func__,
			pid_vnr(pid), rc);
}

void vas_dump_crb(struct coprocessor_request_block *crb)
{
	struct data_descriptor_entry *dde;
	struct nx_fault_stamp *nx;

	dde = &crb->source;
	pr_devel("SrcDDE: addr 0x%llx, len %d, count %d, idx %d, flags %d\n",
		be64_to_cpu(dde->address), be32_to_cpu(dde->length),
		dde->count, dde->index, dde->flags);

	dde = &crb->target;
	pr_devel("TgtDDE: addr 0x%llx, len %d, count %d, idx %d, flags %d\n",
		be64_to_cpu(dde->address), be32_to_cpu(dde->length),
		dde->count, dde->index, dde->flags);

	nx = &crb->stamp.nx;
	pr_devel("NX Stamp: PSWID 0x%x, FSA 0x%llx, flags 0x%x, FS 0x%x\n",
		be32_to_cpu(nx->pswid),
		be64_to_cpu(crb->stamp.nx.fault_storage_addr),
		nx->flags, nx->fault_status);
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
	struct vas_tx_win_open_attr uattr;
	struct coproc_instance *cp_inst;
	struct vas_window *txwin;
	int rc;

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
		pr_err("Invalid window open API version\n");
		return -EINVAL;
	}

	if (!cp_inst->coproc->vops || !cp_inst->coproc->vops->open_win) {
		pr_err("VAS API is not registered\n");
		return -EACCES;
	}

	txwin = cp_inst->coproc->vops->open_win(uattr.vas_id, uattr.flags,
						cp_inst->coproc->cop_type);
	if (IS_ERR(txwin)) {
		pr_err("%s() VAS window open failed, %ld\n", __func__,
				PTR_ERR(txwin));
		return PTR_ERR(txwin);
	}

	mutex_init(&txwin->task_ref.mmap_mutex);
	cp_inst->txwin = txwin;

	return 0;
}

static int coproc_release(struct inode *inode, struct file *fp)
{
	struct coproc_instance *cp_inst = fp->private_data;
	int rc;

	if (cp_inst->txwin) {
		if (cp_inst->coproc->vops &&
			cp_inst->coproc->vops->close_win) {
			rc = cp_inst->coproc->vops->close_win(cp_inst->txwin);
			if (rc)
				return rc;
		}
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

/*
 * If the executed instruction that caused the fault was a paste, then
 * clear regs CR0[EQ], advance NIP, and return 0. Else return error code.
 */
static int do_fail_paste(void)
{
	struct pt_regs *regs = current->thread.regs;
	u32 instword;

	if (WARN_ON_ONCE(!regs))
		return -EINVAL;

	if (WARN_ON_ONCE(!user_mode(regs)))
		return -EINVAL;

	/*
	 * If we couldn't translate the instruction, the driver should
	 * return success without handling the fault, it will be retried
	 * or the instruction fetch will fault.
	 */
	if (get_user(instword, (u32 __user *)(regs->nip)))
		return -EAGAIN;

	/*
	 * Not a paste instruction, driver may fail the fault.
	 */
	if ((instword & PPC_INST_PASTE_MASK) != PPC_INST_PASTE)
		return -ENOENT;

	regs->ccr &= ~0xe0000000;	/* Clear CR0[0-2] to fail paste */
	regs_add_return_ip(regs, 4);	/* Emulate the paste */

	return 0;
}

/*
 * This fault handler is invoked when the core generates page fault on
 * the paste address. Happens if the kernel closes window in hypervisor
 * (on pseries) due to lost credit or the paste address is not mapped.
 */
static vm_fault_t vas_mmap_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct file *fp = vma->vm_file;
	struct coproc_instance *cp_inst = fp->private_data;
	struct vas_window *txwin;
	vm_fault_t fault;
	u64 paste_addr;
	int ret;

	/*
	 * window is not opened. Shouldn't expect this error.
	 */
	if (!cp_inst || !cp_inst->txwin) {
		pr_err("%s(): Unexpected fault on paste address with TX window closed\n",
				__func__);
		return VM_FAULT_SIGBUS;
	}

	txwin = cp_inst->txwin;
	/*
	 * When the LPAR lost credits due to core removal or during
	 * migration, invalidate the existing mapping for the current
	 * paste addresses and set windows in-active (zap_page_range in
	 * reconfig_close_windows()).
	 * New mapping will be done later after migration or new credits
	 * available. So continue to receive faults if the user space
	 * issue NX request.
	 */
	if (txwin->task_ref.vma != vmf->vma) {
		pr_err("%s(): No previous mapping with paste address\n",
			__func__);
		return VM_FAULT_SIGBUS;
	}

	mutex_lock(&txwin->task_ref.mmap_mutex);
	/*
	 * The window may be inactive due to lost credit (Ex: core
	 * removal with DLPAR). If the window is active again when
	 * the credit is available, map the new paste address at the
	 * the window virtual address.
	 */
	if (txwin->status == VAS_WIN_ACTIVE) {
		paste_addr = cp_inst->coproc->vops->paste_addr(txwin);
		if (paste_addr) {
			fault = vmf_insert_pfn(vma, vma->vm_start,
					(paste_addr >> PAGE_SHIFT));
			mutex_unlock(&txwin->task_ref.mmap_mutex);
			return fault;
		}
	}
	mutex_unlock(&txwin->task_ref.mmap_mutex);

	/*
	 * Received this fault due to closing the actual window.
	 * It can happen during migration or lost credits.
	 * Since no mapping, return the paste instruction failure
	 * to the user space.
	 */
	ret = do_fail_paste();
	/*
	 * The user space can retry several times until success (needed
	 * for migration) or should fallback to SW compression or
	 * manage with the existing open windows if available.
	 * Looking at sysfs interface, it can determine whether these
	 * failures are coming during migration or core removal:
	 * nr_used_credits > nr_total_credits when lost credits
	 */
	if (!ret || (ret == -EAGAIN))
		return VM_FAULT_NOPAGE;

	return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct vas_vm_ops = {
	.fault = vas_mmap_fault,
};

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

	if (!cp_inst->coproc->vops || !cp_inst->coproc->vops->paste_addr) {
		pr_err("%s(): VAS API is not registered\n", __func__);
		return -EACCES;
	}

	/*
	 * The initial mmap is done after the window is opened
	 * with ioctl. But before mmap(), this window can be closed in
	 * the hypervisor due to lost credit (core removal on pseries).
	 * So if the window is not active, return mmap() failure with
	 * -EACCES and expects the user space reissue mmap() when it
	 * is active again or open new window when the credit is available.
	 * mmap_mutex protects the paste address mmap() with DLPAR
	 * close/open event and allows mmap() only when the window is
	 * active.
	 */
	mutex_lock(&txwin->task_ref.mmap_mutex);
	if (txwin->status != VAS_WIN_ACTIVE) {
		pr_err("%s(): Window is not active\n", __func__);
		rc = -EACCES;
		goto out;
	}

	paste_addr = cp_inst->coproc->vops->paste_addr(txwin);
	if (!paste_addr) {
		pr_err("%s(): Window paste address failed\n", __func__);
		rc = -EINVAL;
		goto out;
	}

	pfn = paste_addr >> PAGE_SHIFT;

	/* flags, page_prot from cxl_mmap(), except we want cachable */
	vma->vm_flags |= VM_IO | VM_PFNMAP;
	vma->vm_page_prot = pgprot_cached(vma->vm_page_prot);

	prot = __pgprot(pgprot_val(vma->vm_page_prot) | _PAGE_DIRTY);

	rc = remap_pfn_range(vma, vma->vm_start, pfn + vma->vm_pgoff,
			vma->vm_end - vma->vm_start, prot);

	pr_devel("%s(): paste addr %llx at %lx, rc %d\n", __func__,
			paste_addr, vma->vm_start, rc);

	txwin->task_ref.vma = vma;
	vma->vm_ops = &vas_vm_ops;

out:
	mutex_unlock(&txwin->task_ref.mmap_mutex);
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
			    const char *name,
			    const struct vas_user_win_ops *vops)
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
	coproc_device.vops = vops;

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

void vas_unregister_coproc_api(void)
{
	dev_t devno;

	cdev_del(&coproc_device.cdev);
	devno = MKDEV(MAJOR(coproc_device.devt), 0);
	device_destroy(coproc_device.class, devno);

	class_destroy(coproc_device.class);
	unregister_chrdev_region(coproc_device.devt, 1);
}
