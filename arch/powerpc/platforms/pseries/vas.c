// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2020-21 IBM Corp.
 */

#define pr_fmt(fmt) "vas: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <asm/machdep.h>
#include <asm/hvcall.h>
#include <asm/plpar_wrappers.h>
#include <asm/firmware.h>
#include <asm/vas.h>
#include "vas.h"

#define VAS_INVALID_WIN_ADDRESS	0xFFFFFFFFFFFFFFFFul
#define VAS_DEFAULT_DOMAIN_ID	0xFFFFFFFFFFFFFFFFul
/* The hypervisor allows one credit per window right now */
#define DEF_WIN_CREDS		1

static struct vas_all_caps caps_all;
static bool copypaste_feat;
static struct hv_vas_cop_feat_caps hv_cop_caps;

static struct vas_caps vascaps[VAS_MAX_FEAT_TYPE];
static DEFINE_MUTEX(vas_pseries_mutex);
static bool migration_in_progress;

static long hcall_return_busy_check(long rc)
{
	/* Check if we are stalled for some time */
	if (H_IS_LONG_BUSY(rc)) {
		msleep(get_longbusy_msecs(rc));
		rc = H_BUSY;
	} else if (rc == H_BUSY) {
		cond_resched();
	}

	return rc;
}

/*
 * Allocate VAS window hcall
 */
static int h_allocate_vas_window(struct pseries_vas_window *win, u64 *domain,
				     u8 wintype, u16 credits)
{
	long retbuf[PLPAR_HCALL9_BUFSIZE] = {0};
	long rc;

	do {
		rc = plpar_hcall9(H_ALLOCATE_VAS_WINDOW, retbuf, wintype,
				  credits, domain[0], domain[1], domain[2],
				  domain[3], domain[4], domain[5]);

		rc = hcall_return_busy_check(rc);
	} while (rc == H_BUSY);

	if (rc == H_SUCCESS) {
		if (win->win_addr == VAS_INVALID_WIN_ADDRESS) {
			pr_err("H_ALLOCATE_VAS_WINDOW: COPY/PASTE is not supported\n");
			return -ENOTSUPP;
		}
		win->vas_win.winid = retbuf[0];
		win->win_addr = retbuf[1];
		win->complete_irq = retbuf[2];
		win->fault_irq = retbuf[3];
		return 0;
	}

	pr_err("H_ALLOCATE_VAS_WINDOW error: %ld, wintype: %u, credits: %u\n",
		rc, wintype, credits);

	return -EIO;
}

/*
 * Deallocate VAS window hcall.
 */
static int h_deallocate_vas_window(u64 winid)
{
	long rc;

	do {
		rc = plpar_hcall_norets(H_DEALLOCATE_VAS_WINDOW, winid);

		rc = hcall_return_busy_check(rc);
	} while (rc == H_BUSY);

	if (rc == H_SUCCESS)
		return 0;

	pr_err("H_DEALLOCATE_VAS_WINDOW error: %ld, winid: %llu\n",
		rc, winid);
	return -EIO;
}

/*
 * Modify VAS window.
 * After the window is opened with allocate window hcall, configure it
 * with flags and LPAR PID before using.
 */
static int h_modify_vas_window(struct pseries_vas_window *win)
{
	long rc;

	/*
	 * AMR value is not supported in Linux VAS implementation.
	 * The hypervisor ignores it if 0 is passed.
	 */
	do {
		rc = plpar_hcall_norets(H_MODIFY_VAS_WINDOW,
					win->vas_win.winid, win->pid, 0,
					VAS_MOD_WIN_FLAGS, 0);

		rc = hcall_return_busy_check(rc);
	} while (rc == H_BUSY);

	if (rc == H_SUCCESS)
		return 0;

	pr_err("H_MODIFY_VAS_WINDOW error: %ld, winid %u pid %u\n",
			rc, win->vas_win.winid, win->pid);
	return -EIO;
}

/*
 * This hcall is used to determine the capabilities from the hypervisor.
 * @hcall: H_QUERY_VAS_CAPABILITIES or H_QUERY_NX_CAPABILITIES
 * @query_type: If 0 is passed, the hypervisor returns the overall
 *		capabilities which provides all feature(s) that are
 *		available. Then query the hypervisor to get the
 *		corresponding capabilities for the specific feature.
 *		Example: H_QUERY_VAS_CAPABILITIES provides VAS GZIP QoS
 *			and VAS GZIP Default capabilities.
 *			H_QUERY_NX_CAPABILITIES provides NX GZIP
 *			capabilities.
 * @result: Return buffer to save capabilities.
 */
int h_query_vas_capabilities(const u64 hcall, u8 query_type, u64 result)
{
	long rc;

	rc = plpar_hcall_norets(hcall, query_type, result);

	if (rc == H_SUCCESS)
		return 0;

	/* H_FUNCTION means HV does not support VAS so don't print an error */
	if (rc != H_FUNCTION) {
		pr_err("%s error %ld, query_type %u, result buffer 0x%llx\n",
			(hcall == H_QUERY_VAS_CAPABILITIES) ?
				"H_QUERY_VAS_CAPABILITIES" :
				"H_QUERY_NX_CAPABILITIES",
			rc, query_type, result);
	}

	return -EIO;
}
EXPORT_SYMBOL_GPL(h_query_vas_capabilities);

/*
 * hcall to get fault CRB from the hypervisor.
 */
static int h_get_nx_fault(u32 winid, u64 buffer)
{
	long rc;

	rc = plpar_hcall_norets(H_GET_NX_FAULT, winid, buffer);

	if (rc == H_SUCCESS)
		return 0;

	pr_err("H_GET_NX_FAULT error: %ld, winid %u, buffer 0x%llx\n",
		rc, winid, buffer);
	return -EIO;

}

/*
 * Handle the fault interrupt.
 * When the fault interrupt is received for each window, query the
 * hypervisor to get the fault CRB on the specific fault. Then
 * process the CRB by updating CSB or send signal if the user space
 * CSB is invalid.
 * Note: The hypervisor forwards an interrupt for each fault request.
 *	So one fault CRB to process for each H_GET_NX_FAULT hcall.
 */
static irqreturn_t pseries_vas_fault_thread_fn(int irq, void *data)
{
	struct pseries_vas_window *txwin = data;
	struct coprocessor_request_block crb;
	struct vas_user_win_ref *tsk_ref;
	int rc;

	while (atomic_read(&txwin->pending_faults)) {
		rc = h_get_nx_fault(txwin->vas_win.winid, (u64)virt_to_phys(&crb));
		if (!rc) {
			tsk_ref = &txwin->vas_win.task_ref;
			vas_dump_crb(&crb);
			vas_update_csb(&crb, tsk_ref);
		}
		atomic_dec(&txwin->pending_faults);
	}

	return IRQ_HANDLED;
}

/*
 * irq_default_primary_handler() can be used only with IRQF_ONESHOT
 * which disables IRQ before executing the thread handler and enables
 * it after. But this disabling interrupt sets the VAS IRQ OFF
 * state in the hypervisor. If the NX generates fault interrupt
 * during this window, the hypervisor will not deliver this
 * interrupt to the LPAR. So use VAS specific IRQ handler instead
 * of calling the default primary handler.
 */
static irqreturn_t pseries_vas_irq_handler(int irq, void *data)
{
	struct pseries_vas_window *txwin = data;

	/*
	 * The thread hanlder will process this interrupt if it is
	 * already running.
	 */
	atomic_inc(&txwin->pending_faults);

	return IRQ_WAKE_THREAD;
}

/*
 * Allocate window and setup IRQ mapping.
 */
static int allocate_setup_window(struct pseries_vas_window *txwin,
				 u64 *domain, u8 wintype)
{
	int rc;

	rc = h_allocate_vas_window(txwin, domain, wintype, DEF_WIN_CREDS);
	if (rc)
		return rc;
	/*
	 * On PowerVM, the hypervisor setup and forwards the fault
	 * interrupt per window. So the IRQ setup and fault handling
	 * will be done for each open window separately.
	 */
	txwin->fault_virq = irq_create_mapping(NULL, txwin->fault_irq);
	if (!txwin->fault_virq) {
		pr_err("Failed irq mapping %d\n", txwin->fault_irq);
		rc = -EINVAL;
		goto out_win;
	}

	txwin->name = kasprintf(GFP_KERNEL, "vas-win-%d",
				txwin->vas_win.winid);
	if (!txwin->name) {
		rc = -ENOMEM;
		goto out_irq;
	}

	rc = request_threaded_irq(txwin->fault_virq,
				  pseries_vas_irq_handler,
				  pseries_vas_fault_thread_fn, 0,
				  txwin->name, txwin);
	if (rc) {
		pr_err("VAS-Window[%d]: Request IRQ(%u) failed with %d\n",
		       txwin->vas_win.winid, txwin->fault_virq, rc);
		goto out_free;
	}

	txwin->vas_win.wcreds_max = DEF_WIN_CREDS;

	return 0;
out_free:
	kfree(txwin->name);
out_irq:
	irq_dispose_mapping(txwin->fault_virq);
out_win:
	h_deallocate_vas_window(txwin->vas_win.winid);
	return rc;
}

static inline void free_irq_setup(struct pseries_vas_window *txwin)
{
	free_irq(txwin->fault_virq, txwin);
	kfree(txwin->name);
	irq_dispose_mapping(txwin->fault_virq);
}

static struct vas_window *vas_allocate_window(int vas_id, u64 flags,
					      enum vas_cop_type cop_type)
{
	long domain[PLPAR_HCALL9_BUFSIZE] = {VAS_DEFAULT_DOMAIN_ID};
	struct vas_cop_feat_caps *cop_feat_caps;
	struct vas_caps *caps;
	struct pseries_vas_window *txwin;
	int rc;

	txwin = kzalloc(sizeof(*txwin), GFP_KERNEL);
	if (!txwin)
		return ERR_PTR(-ENOMEM);

	/*
	 * A VAS window can have many credits which means that many
	 * requests can be issued simultaneously. But the hypervisor
	 * restricts one credit per window.
	 * The hypervisor introduces 2 different types of credits:
	 * Default credit type (Uses normal priority FIFO):
	 *	A limited number of credits are assigned to partitions
	 *	based on processor entitlement. But these credits may be
	 *	over-committed on a system depends on whether the CPUs
	 *	are in shared or dedicated modes - that is, more requests
	 *	may be issued across the system than NX can service at
	 *	once which can result in paste command failure (RMA_busy).
	 *	Then the process has to resend requests or fall-back to
	 *	SW compression.
	 * Quality of Service (QoS) credit type (Uses high priority FIFO):
	 *	To avoid NX HW contention, the system admins can assign
	 *	QoS credits for each LPAR so that this partition is
	 *	guaranteed access to NX resources. These credits are
	 *	assigned to partitions via the HMC.
	 *	Refer PAPR for more information.
	 *
	 * Allocate window with QoS credits if user requested. Otherwise
	 * default credits are used.
	 */
	if (flags & VAS_TX_WIN_FLAG_QOS_CREDIT)
		caps = &vascaps[VAS_GZIP_QOS_FEAT_TYPE];
	else
		caps = &vascaps[VAS_GZIP_DEF_FEAT_TYPE];

	cop_feat_caps = &caps->caps;

	if (atomic_inc_return(&cop_feat_caps->nr_used_credits) >
			atomic_read(&cop_feat_caps->nr_total_credits)) {
		pr_err_ratelimited("Credits are not available to allocate window\n");
		rc = -EINVAL;
		goto out;
	}

	if (vas_id == -1) {
		/*
		 * The user space is requesting to allocate a window on
		 * a VAS instance where the process is executing.
		 * On PowerVM, domain values are passed to the hypervisor
		 * to select VAS instance. Useful if the process is
		 * affinity to NUMA node.
		 * The hypervisor selects VAS instance if
		 * VAS_DEFAULT_DOMAIN_ID (-1) is passed for domain values.
		 * The h_allocate_vas_window hcall is defined to take a
		 * domain values as specified by h_home_node_associativity,
		 * So no unpacking needs to be done.
		 */
		rc = plpar_hcall9(H_HOME_NODE_ASSOCIATIVITY, domain,
				  VPHN_FLAG_VCPU, hard_smp_processor_id());
		if (rc != H_SUCCESS) {
			pr_err("H_HOME_NODE_ASSOCIATIVITY error: %d\n", rc);
			goto out;
		}
	}

	txwin->pid = mfspr(SPRN_PID);

	/*
	 * Allocate / Deallocate window hcalls and setup / free IRQs
	 * have to be protected with mutex.
	 * Open VAS window: Allocate window hcall and setup IRQ
	 * Close VAS window: Deallocate window hcall and free IRQ
	 *	The hypervisor waits until all NX requests are
	 *	completed before closing the window. So expects OS
	 *	to handle NX faults, means IRQ can be freed only
	 *	after the deallocate window hcall is returned.
	 * So once the window is closed with deallocate hcall before
	 * the IRQ is freed, it can be assigned to new allocate
	 * hcall with the same fault IRQ by the hypervisor. It can
	 * result in setup IRQ fail for the new window since the
	 * same fault IRQ is not freed by the OS before.
	 */
	mutex_lock(&vas_pseries_mutex);
	if (migration_in_progress)
		rc = -EBUSY;
	else
		rc = allocate_setup_window(txwin, (u64 *)&domain[0],
				   cop_feat_caps->win_type);
	mutex_unlock(&vas_pseries_mutex);
	if (rc)
		goto out;

	/*
	 * Modify window and it is ready to use.
	 */
	rc = h_modify_vas_window(txwin);
	if (!rc)
		rc = get_vas_user_win_ref(&txwin->vas_win.task_ref);
	if (rc)
		goto out_free;

	txwin->win_type = cop_feat_caps->win_type;
	mutex_lock(&vas_pseries_mutex);
	/*
	 * Possible to lose the acquired credit with DLPAR core
	 * removal after the window is opened. So if there are any
	 * closed windows (means with lost credits), do not give new
	 * window to user space. New windows will be opened only
	 * after the existing windows are reopened when credits are
	 * available.
	 */
	if (!caps->nr_close_wins) {
		list_add(&txwin->win_list, &caps->list);
		caps->nr_open_windows++;
		mutex_unlock(&vas_pseries_mutex);
		vas_user_win_add_mm_context(&txwin->vas_win.task_ref);
		return &txwin->vas_win;
	}
	mutex_unlock(&vas_pseries_mutex);

	put_vas_user_win_ref(&txwin->vas_win.task_ref);
	rc = -EBUSY;
	pr_err_ratelimited("No credit is available to allocate window\n");

out_free:
	/*
	 * Window is not operational. Free IRQ before closing
	 * window so that do not have to hold mutex.
	 */
	free_irq_setup(txwin);
	h_deallocate_vas_window(txwin->vas_win.winid);
out:
	atomic_dec(&cop_feat_caps->nr_used_credits);
	kfree(txwin);
	return ERR_PTR(rc);
}

static u64 vas_paste_address(struct vas_window *vwin)
{
	struct pseries_vas_window *win;

	win = container_of(vwin, struct pseries_vas_window, vas_win);
	return win->win_addr;
}

static int deallocate_free_window(struct pseries_vas_window *win)
{
	int rc = 0;

	/*
	 * The hypervisor waits for all requests including faults
	 * are processed before closing the window - Means all
	 * credits have to be returned. In the case of fault
	 * request, a credit is returned after OS issues
	 * H_GET_NX_FAULT hcall.
	 * So free IRQ after executing H_DEALLOCATE_VAS_WINDOW
	 * hcall.
	 */
	rc = h_deallocate_vas_window(win->vas_win.winid);
	if (!rc)
		free_irq_setup(win);

	return rc;
}

static int vas_deallocate_window(struct vas_window *vwin)
{
	struct pseries_vas_window *win;
	struct vas_cop_feat_caps *caps;
	int rc = 0;

	if (!vwin)
		return -EINVAL;

	win = container_of(vwin, struct pseries_vas_window, vas_win);

	/* Should not happen */
	if (win->win_type >= VAS_MAX_FEAT_TYPE) {
		pr_err("Window (%u): Invalid window type %u\n",
				vwin->winid, win->win_type);
		return -EINVAL;
	}

	caps = &vascaps[win->win_type].caps;
	mutex_lock(&vas_pseries_mutex);
	/*
	 * VAS window is already closed in the hypervisor when
	 * lost the credit or with migration. So just remove the entry
	 * from the list, remove task references and free vas_window
	 * struct.
	 */
	if (!(win->vas_win.status & VAS_WIN_NO_CRED_CLOSE) &&
		!(win->vas_win.status & VAS_WIN_MIGRATE_CLOSE)) {
		rc = deallocate_free_window(win);
		if (rc) {
			mutex_unlock(&vas_pseries_mutex);
			return rc;
		}
	} else
		vascaps[win->win_type].nr_close_wins--;

	list_del(&win->win_list);
	atomic_dec(&caps->nr_used_credits);
	vascaps[win->win_type].nr_open_windows--;
	mutex_unlock(&vas_pseries_mutex);

	mm_context_remove_vas_window(vwin->task_ref.mm);
	put_vas_user_win_ref(&vwin->task_ref);

	kfree(win);
	return 0;
}

static const struct vas_user_win_ops vops_pseries = {
	.open_win	= vas_allocate_window,	/* Open and configure window */
	.paste_addr	= vas_paste_address,	/* To do copy/paste */
	.close_win	= vas_deallocate_window, /* Close window */
};

/*
 * Supporting only nx-gzip coprocessor type now, but this API code
 * extended to other coprocessor types later.
 */
int vas_register_api_pseries(struct module *mod, enum vas_cop_type cop_type,
			     const char *name)
{
	if (!copypaste_feat)
		return -ENOTSUPP;

	return vas_register_coproc_api(mod, cop_type, name, &vops_pseries);
}
EXPORT_SYMBOL_GPL(vas_register_api_pseries);

void vas_unregister_api_pseries(void)
{
	vas_unregister_coproc_api();
}
EXPORT_SYMBOL_GPL(vas_unregister_api_pseries);

/*
 * Get the specific capabilities based on the feature type.
 * Right now supports GZIP default and GZIP QoS capabilities.
 */
static int __init get_vas_capabilities(u8 feat, enum vas_cop_feat_type type,
				struct hv_vas_cop_feat_caps *hv_caps)
{
	struct vas_cop_feat_caps *caps;
	struct vas_caps *vcaps;
	int rc = 0;

	vcaps = &vascaps[type];
	memset(vcaps, 0, sizeof(*vcaps));
	INIT_LIST_HEAD(&vcaps->list);

	vcaps->feat = feat;
	caps = &vcaps->caps;

	rc = h_query_vas_capabilities(H_QUERY_VAS_CAPABILITIES, feat,
					  (u64)virt_to_phys(hv_caps));
	if (rc)
		return rc;

	caps->user_mode = hv_caps->user_mode;
	if (!(caps->user_mode & VAS_COPY_PASTE_USER_MODE)) {
		pr_err("User space COPY/PASTE is not supported\n");
		return -ENOTSUPP;
	}

	caps->descriptor = be64_to_cpu(hv_caps->descriptor);
	caps->win_type = hv_caps->win_type;
	if (caps->win_type >= VAS_MAX_FEAT_TYPE) {
		pr_err("Unsupported window type %u\n", caps->win_type);
		return -EINVAL;
	}
	caps->max_lpar_creds = be16_to_cpu(hv_caps->max_lpar_creds);
	caps->max_win_creds = be16_to_cpu(hv_caps->max_win_creds);
	atomic_set(&caps->nr_total_credits,
		   be16_to_cpu(hv_caps->target_lpar_creds));
	if (feat == VAS_GZIP_DEF_FEAT) {
		caps->def_lpar_creds = be16_to_cpu(hv_caps->def_lpar_creds);

		if (caps->max_win_creds < DEF_WIN_CREDS) {
			pr_err("Window creds(%u) > max allowed window creds(%u)\n",
			       DEF_WIN_CREDS, caps->max_win_creds);
			return -EINVAL;
		}
	}

	rc = sysfs_add_vas_caps(caps);
	if (rc)
		return rc;

	copypaste_feat = true;

	return 0;
}

/*
 * VAS windows can be closed due to lost credits when the core is
 * removed. So reopen them if credits are available due to DLPAR
 * core add and set the window active status. When NX sees the page
 * fault on the unmapped paste address, the kernel handles the fault
 * by setting the remapping to new paste address if the window is
 * active.
 */
static int reconfig_open_windows(struct vas_caps *vcaps, int creds,
				 bool migrate)
{
	long domain[PLPAR_HCALL9_BUFSIZE] = {VAS_DEFAULT_DOMAIN_ID};
	struct vas_cop_feat_caps *caps = &vcaps->caps;
	struct pseries_vas_window *win = NULL, *tmp;
	int rc, mv_ents = 0;
	int flag;

	/*
	 * Nothing to do if there are no closed windows.
	 */
	if (!vcaps->nr_close_wins)
		return 0;

	/*
	 * For the core removal, the hypervisor reduces the credits
	 * assigned to the LPAR and the kernel closes VAS windows
	 * in the hypervisor depends on reduced credits. The kernel
	 * uses LIFO (the last windows that are opened will be closed
	 * first) and expects to open in the same order when credits
	 * are available.
	 * For example, 40 windows are closed when the LPAR lost 2 cores
	 * (dedicated). If 1 core is added, this LPAR can have 20 more
	 * credits. It means the kernel can reopen 20 windows. So move
	 * 20 entries in the VAS windows lost and reopen next 20 windows.
	 * For partition migration, reopen all windows that are closed
	 * during resume.
	 */
	if ((vcaps->nr_close_wins > creds) && !migrate)
		mv_ents = vcaps->nr_close_wins - creds;

	list_for_each_entry_safe(win, tmp, &vcaps->list, win_list) {
		if (!mv_ents)
			break;

		mv_ents--;
	}

	/*
	 * Open windows if they are closed only with migration or
	 * DLPAR (lost credit) before.
	 */
	if (migrate)
		flag = VAS_WIN_MIGRATE_CLOSE;
	else
		flag = VAS_WIN_NO_CRED_CLOSE;

	list_for_each_entry_safe_from(win, tmp, &vcaps->list, win_list) {
		/*
		 * This window is closed with DLPAR and migration events.
		 * So reopen the window with the last event.
		 * The user space is not suspended with the current
		 * migration notifier. So the user space can issue DLPAR
		 * CPU hotplug while migration in progress. In this case
		 * this window will be opened with the last event.
		 */
		if ((win->vas_win.status & VAS_WIN_NO_CRED_CLOSE) &&
			(win->vas_win.status & VAS_WIN_MIGRATE_CLOSE)) {
			win->vas_win.status &= ~flag;
			continue;
		}

		/*
		 * Nothing to do on this window if it is not closed
		 * with this flag
		 */
		if (!(win->vas_win.status & flag))
			continue;

		rc = allocate_setup_window(win, (u64 *)&domain[0],
					   caps->win_type);
		if (rc)
			return rc;

		rc = h_modify_vas_window(win);
		if (rc)
			goto out;

		mutex_lock(&win->vas_win.task_ref.mmap_mutex);
		/*
		 * Set window status to active
		 */
		win->vas_win.status &= ~flag;
		mutex_unlock(&win->vas_win.task_ref.mmap_mutex);
		win->win_type = caps->win_type;
		if (!--vcaps->nr_close_wins)
			break;
	}

	return 0;
out:
	/*
	 * Window modify HCALL failed. So close the window to the
	 * hypervisor and return.
	 */
	free_irq_setup(win);
	h_deallocate_vas_window(win->vas_win.winid);
	return rc;
}

/*
 * The hypervisor reduces the available credits if the LPAR lost core. It
 * means the excessive windows should not be active and the user space
 * should not be using these windows to send compression requests to NX.
 * So the kernel closes the excessive windows and unmap the paste address
 * such that the user space receives paste instruction failure. Then up to
 * the user space to fall back to SW compression and manage with the
 * existing windows.
 */
static int reconfig_close_windows(struct vas_caps *vcap, int excess_creds,
									bool migrate)
{
	struct pseries_vas_window *win, *tmp;
	struct vas_user_win_ref *task_ref;
	struct vm_area_struct *vma;
	int rc = 0, flag;

	if (migrate)
		flag = VAS_WIN_MIGRATE_CLOSE;
	else
		flag = VAS_WIN_NO_CRED_CLOSE;

	list_for_each_entry_safe(win, tmp, &vcap->list, win_list) {
		/*
		 * This window is already closed due to lost credit
		 * or for migration before. Go for next window.
		 * For migration, nothing to do since this window
		 * closed for DLPAR and will be reopened even on
		 * the destination system with other DLPAR operation.
		 */
		if ((win->vas_win.status & VAS_WIN_MIGRATE_CLOSE) ||
			(win->vas_win.status & VAS_WIN_NO_CRED_CLOSE)) {
			win->vas_win.status |= flag;
			continue;
		}

		task_ref = &win->vas_win.task_ref;
		/*
		 * VAS mmap (coproc_mmap()) and its fault handler
		 * (vas_mmap_fault()) are called after holding mmap lock.
		 * So hold mmap mutex after mmap_lock to avoid deadlock.
		 */
		mmap_write_lock(task_ref->mm);
		mutex_lock(&task_ref->mmap_mutex);
		vma = task_ref->vma;
		/*
		 * Number of available credits are reduced, So select
		 * and close windows.
		 */
		win->vas_win.status |= flag;

		/*
		 * vma is set in the original mapping. But this mapping
		 * is done with mmap() after the window is opened with ioctl.
		 * so we may not see the original mapping if the core remove
		 * is done before the original mmap() and after the ioctl.
		 */
		if (vma)
			zap_page_range(vma, vma->vm_start,
					vma->vm_end - vma->vm_start);

		mutex_unlock(&task_ref->mmap_mutex);
		mmap_write_unlock(task_ref->mm);
		/*
		 * Close VAS window in the hypervisor, but do not
		 * free vas_window struct since it may be reused
		 * when the credit is available later (DLPAR with
		 * adding cores). This struct will be used
		 * later when the process issued with close(FD).
		 */
		rc = deallocate_free_window(win);
		/*
		 * This failure is from the hypervisor.
		 * No way to stop migration for these failures.
		 * So ignore error and continue closing other windows.
		 */
		if (rc && !migrate)
			return rc;

		vcap->nr_close_wins++;

		/*
		 * For migration, do not depend on lpar_creds in case if
		 * mismatch with the hypervisor value (should not happen).
		 * So close all active windows in the list and will be
		 * reopened windows based on the new lpar_creds on the
		 * destination system during resume.
		 */
		if (!migrate && !--excess_creds)
			break;
	}

	return 0;
}

/*
 * Get new VAS capabilities when the core add/removal configuration
 * changes. Reconfig window configurations based on the credits
 * availability from this new capabilities.
 */
int vas_reconfig_capabilties(u8 type, int new_nr_creds)
{
	struct vas_cop_feat_caps *caps;
	int old_nr_creds;
	struct vas_caps *vcaps;
	int rc = 0, nr_active_wins;

	if (type >= VAS_MAX_FEAT_TYPE) {
		pr_err("Invalid credit type %d\n", type);
		return -EINVAL;
	}

	vcaps = &vascaps[type];
	caps = &vcaps->caps;

	mutex_lock(&vas_pseries_mutex);

	old_nr_creds = atomic_read(&caps->nr_total_credits);

	atomic_set(&caps->nr_total_credits, new_nr_creds);
	/*
	 * The total number of available credits may be decreased or
	 * increased with DLPAR operation. Means some windows have to be
	 * closed / reopened. Hold the vas_pseries_mutex so that the
	 * user space can not open new windows.
	 */
	if (old_nr_creds <  new_nr_creds) {
		/*
		 * If the existing target credits is less than the new
		 * target, reopen windows if they are closed due to
		 * the previous DLPAR (core removal).
		 */
		rc = reconfig_open_windows(vcaps, new_nr_creds - old_nr_creds,
					   false);
	} else {
		/*
		 * # active windows is more than new LPAR available
		 * credits. So close the excessive windows.
		 * On pseries, each window will have 1 credit.
		 */
		nr_active_wins = vcaps->nr_open_windows - vcaps->nr_close_wins;
		if (nr_active_wins > new_nr_creds)
			rc = reconfig_close_windows(vcaps,
					nr_active_wins - new_nr_creds,
					false);
	}

	mutex_unlock(&vas_pseries_mutex);
	return rc;
}

int pseries_vas_dlpar_cpu(void)
{
	int new_nr_creds, rc;

	/*
	 * NX-GZIP is not enabled. Nothing to do for DLPAR event
	 */
	if (!copypaste_feat)
		return 0;


	rc = h_query_vas_capabilities(H_QUERY_VAS_CAPABILITIES,
				      vascaps[VAS_GZIP_DEF_FEAT_TYPE].feat,
				      (u64)virt_to_phys(&hv_cop_caps));
	if (!rc) {
		new_nr_creds = be16_to_cpu(hv_cop_caps.target_lpar_creds);
		rc = vas_reconfig_capabilties(VAS_GZIP_DEF_FEAT_TYPE, new_nr_creds);
	}

	if (rc)
		pr_err("Failed reconfig VAS capabilities with DLPAR\n");

	return rc;
}

/*
 * Total number of default credits available (target_credits)
 * in LPAR depends on number of cores configured. It varies based on
 * whether processors are in shared mode or dedicated mode.
 * Get the notifier when CPU configuration is changed with DLPAR
 * operation so that get the new target_credits (vas default capabilities)
 * and then update the existing windows usage if needed.
 */
static int pseries_vas_notifier(struct notifier_block *nb,
				unsigned long action, void *data)
{
	struct of_reconfig_data *rd = data;
	struct device_node *dn = rd->dn;
	const __be32 *intserv = NULL;
	int len;

	/*
	 * For shared CPU partition, the hypervisor assigns total credits
	 * based on entitled core capacity. So updating VAS windows will
	 * be called from lparcfg_write().
	 */
	if (is_shared_processor())
		return NOTIFY_OK;

	if ((action == OF_RECONFIG_ATTACH_NODE) ||
		(action == OF_RECONFIG_DETACH_NODE))
		intserv = of_get_property(dn, "ibm,ppc-interrupt-server#s",
					  &len);
	/*
	 * Processor config is not changed
	 */
	if (!intserv)
		return NOTIFY_OK;

	return pseries_vas_dlpar_cpu();
}

static struct notifier_block pseries_vas_nb = {
	.notifier_call = pseries_vas_notifier,
};

/*
 * For LPM, all windows have to be closed on the source partition
 * before migration and reopen them on the destination partition
 * after migration. So closing windows during suspend and
 * reopen them during resume.
 */
int vas_migration_handler(int action)
{
	struct vas_cop_feat_caps *caps;
	int old_nr_creds, new_nr_creds = 0;
	struct vas_caps *vcaps;
	int i, rc = 0;

	/*
	 * NX-GZIP is not enabled. Nothing to do for migration.
	 */
	if (!copypaste_feat)
		return rc;

	mutex_lock(&vas_pseries_mutex);

	if (action == VAS_SUSPEND)
		migration_in_progress = true;
	else
		migration_in_progress = false;

	for (i = 0; i < VAS_MAX_FEAT_TYPE; i++) {
		vcaps = &vascaps[i];
		caps = &vcaps->caps;
		old_nr_creds = atomic_read(&caps->nr_total_credits);

		rc = h_query_vas_capabilities(H_QUERY_VAS_CAPABILITIES,
					      vcaps->feat,
					      (u64)virt_to_phys(&hv_cop_caps));
		if (!rc) {
			new_nr_creds = be16_to_cpu(hv_cop_caps.target_lpar_creds);
			/*
			 * Should not happen. But incase print messages, close
			 * all windows in the list during suspend and reopen
			 * windows based on new lpar_creds on the destination
			 * system.
			 */
			if (old_nr_creds != new_nr_creds) {
				pr_err("Target credits mismatch with the hypervisor\n");
				pr_err("state(%d): lpar creds: %d HV lpar creds: %d\n",
					action, old_nr_creds, new_nr_creds);
				pr_err("Used creds: %d, Active creds: %d\n",
					atomic_read(&caps->nr_used_credits),
					vcaps->nr_open_windows - vcaps->nr_close_wins);
			}
		} else {
			pr_err("state(%d): Get VAS capabilities failed with %d\n",
				action, rc);
			/*
			 * We can not stop migration with the current lpm
			 * implementation. So continue closing all windows in
			 * the list (during suspend) and return without
			 * opening windows (during resume) if VAS capabilities
			 * HCALL failed.
			 */
			if (action == VAS_RESUME)
				goto out;
		}

		switch (action) {
		case VAS_SUSPEND:
			rc = reconfig_close_windows(vcaps, vcaps->nr_open_windows,
							true);
			break;
		case VAS_RESUME:
			atomic_set(&caps->nr_total_credits, new_nr_creds);
			rc = reconfig_open_windows(vcaps, new_nr_creds, true);
			break;
		default:
			/* should not happen */
			pr_err("Invalid migration action %d\n", action);
			rc = -EINVAL;
			goto out;
		}

		/*
		 * Ignore errors during suspend and return for resume.
		 */
		if (rc && (action == VAS_RESUME))
			goto out;
	}

out:
	mutex_unlock(&vas_pseries_mutex);
	return rc;
}

static int __init pseries_vas_init(void)
{
	struct hv_vas_all_caps *hv_caps;
	int rc = 0;

	/*
	 * Linux supports user space COPY/PASTE only with Radix
	 */
	if (!radix_enabled()) {
		copypaste_feat = false;
		pr_err("API is supported only with radix page tables\n");
		return -ENOTSUPP;
	}

	hv_caps = kmalloc(sizeof(*hv_caps), GFP_KERNEL);
	if (!hv_caps)
		return -ENOMEM;
	/*
	 * Get VAS overall capabilities by passing 0 to feature type.
	 */
	rc = h_query_vas_capabilities(H_QUERY_VAS_CAPABILITIES, 0,
					  (u64)virt_to_phys(hv_caps));
	if (rc)
		goto out;

	caps_all.descriptor = be64_to_cpu(hv_caps->descriptor);
	caps_all.feat_type = be64_to_cpu(hv_caps->feat_type);

	sysfs_pseries_vas_init(&caps_all);

	/*
	 * QOS capabilities available
	 */
	if (caps_all.feat_type & VAS_GZIP_QOS_FEAT_BIT) {
		rc = get_vas_capabilities(VAS_GZIP_QOS_FEAT,
					  VAS_GZIP_QOS_FEAT_TYPE, &hv_cop_caps);

		if (rc)
			goto out;
	}
	/*
	 * Default capabilities available
	 */
	if (caps_all.feat_type & VAS_GZIP_DEF_FEAT_BIT)
		rc = get_vas_capabilities(VAS_GZIP_DEF_FEAT,
					  VAS_GZIP_DEF_FEAT_TYPE, &hv_cop_caps);

	if (!rc && copypaste_feat) {
		if (firmware_has_feature(FW_FEATURE_LPAR))
			of_reconfig_notifier_register(&pseries_vas_nb);

		pr_info("GZIP feature is available\n");
	} else {
		/*
		 * Should not happen, but only when get default
		 * capabilities HCALL failed. So disable copy paste
		 * feature.
		 */
		copypaste_feat = false;
	}

out:
	kfree(hv_caps);
	return rc;
}
machine_device_initcall(pseries, pseries_vas_init);
