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
#include <asm/vas.h>
#include "vas.h"

#define VAS_INVALID_WIN_ADDRESS	0xFFFFFFFFFFFFFFFFul
#define VAS_DEFAULT_DOMAIN_ID	0xFFFFFFFFFFFFFFFFul
/* The hypervisor allows one credit per window right now */
#define DEF_WIN_CREDS		1

static struct vas_all_caps caps_all;
static bool copypaste_feat;

static struct vas_caps vascaps[VAS_MAX_FEAT_TYPE];
static DEFINE_MUTEX(vas_pseries_mutex);

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
	u32 lpid = mfspr(SPRN_PID);

	/*
	 * AMR value is not supported in Linux VAS implementation.
	 * The hypervisor ignores it if 0 is passed.
	 */
	do {
		rc = plpar_hcall_norets(H_MODIFY_VAS_WINDOW,
					win->vas_win.winid, lpid, 0,
					VAS_MOD_WIN_FLAGS, 0);

		rc = hcall_return_busy_check(rc);
	} while (rc == H_BUSY);

	if (rc == H_SUCCESS)
		return 0;

	pr_err("H_MODIFY_VAS_WINDOW error: %ld, winid %u lpid %u\n",
			rc, win->vas_win.winid, lpid);
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

	pr_err("HCALL(%llx) error %ld, query_type %u, result buffer 0x%llx\n",
			hcall, rc, query_type, result);
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
irqreturn_t pseries_vas_fault_thread_fn(int irq, void *data)
{
	struct pseries_vas_window *txwin = data;
	struct coprocessor_request_block crb;
	struct vas_user_win_ref *tsk_ref;
	int rc;

	rc = h_get_nx_fault(txwin->vas_win.winid, (u64)virt_to_phys(&crb));
	if (!rc) {
		tsk_ref = &txwin->vas_win.task_ref;
		vas_dump_crb(&crb);
		vas_update_csb(&crb, tsk_ref);
	}

	return IRQ_HANDLED;
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

	rc = request_threaded_irq(txwin->fault_virq, NULL,
				  pseries_vas_fault_thread_fn, IRQF_ONESHOT,
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

	if (atomic_inc_return(&cop_feat_caps->used_lpar_creds) >
			atomic_read(&cop_feat_caps->target_lpar_creds)) {
		pr_err("Credits are not available to allocate window\n");
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
				  VPHN_FLAG_VCPU, smp_processor_id());
		if (rc != H_SUCCESS) {
			pr_err("H_HOME_NODE_ASSOCIATIVITY error: %d\n", rc);
			goto out;
		}
	}

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

	vas_user_win_add_mm_context(&txwin->vas_win.task_ref);
	txwin->win_type = cop_feat_caps->win_type;
	mutex_lock(&vas_pseries_mutex);
	list_add(&txwin->win_list, &caps->list);
	mutex_unlock(&vas_pseries_mutex);

	return &txwin->vas_win;

out_free:
	/*
	 * Window is not operational. Free IRQ before closing
	 * window so that do not have to hold mutex.
	 */
	free_irq_setup(txwin);
	h_deallocate_vas_window(txwin->vas_win.winid);
out:
	atomic_dec(&cop_feat_caps->used_lpar_creds);
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
	rc = deallocate_free_window(win);
	if (rc) {
		mutex_unlock(&vas_pseries_mutex);
		return rc;
	}

	list_del(&win->win_list);
	atomic_dec(&caps->used_lpar_creds);
	mutex_unlock(&vas_pseries_mutex);

	put_vas_user_win_ref(&vwin->task_ref);
	mm_context_remove_vas_window(vwin->task_ref.mm);

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
	int rc;

	if (!copypaste_feat)
		return -ENOTSUPP;

	rc = vas_register_coproc_api(mod, cop_type, name, &vops_pseries);

	return rc;
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
static int get_vas_capabilities(u8 feat, enum vas_cop_feat_type type,
				struct hv_vas_cop_feat_caps *hv_caps)
{
	struct vas_cop_feat_caps *caps;
	struct vas_caps *vcaps;
	int rc = 0;

	vcaps = &vascaps[type];
	memset(vcaps, 0, sizeof(*vcaps));
	INIT_LIST_HEAD(&vcaps->list);

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
	atomic_set(&caps->target_lpar_creds,
		   be16_to_cpu(hv_caps->target_lpar_creds));
	if (feat == VAS_GZIP_DEF_FEAT) {
		caps->def_lpar_creds = be16_to_cpu(hv_caps->def_lpar_creds);

		if (caps->max_win_creds < DEF_WIN_CREDS) {
			pr_err("Window creds(%u) > max allowed window creds(%u)\n",
			       DEF_WIN_CREDS, caps->max_win_creds);
			return -EINVAL;
		}
	}

	copypaste_feat = true;

	return 0;
}

static int __init pseries_vas_init(void)
{
	struct hv_vas_cop_feat_caps *hv_cop_caps;
	struct hv_vas_all_caps *hv_caps;
	int rc;

	/*
	 * Linux supports user space COPY/PASTE only with Radix
	 */
	if (!radix_enabled()) {
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

	hv_cop_caps = kmalloc(sizeof(*hv_cop_caps), GFP_KERNEL);
	if (!hv_cop_caps) {
		rc = -ENOMEM;
		goto out;
	}
	/*
	 * QOS capabilities available
	 */
	if (caps_all.feat_type & VAS_GZIP_QOS_FEAT_BIT) {
		rc = get_vas_capabilities(VAS_GZIP_QOS_FEAT,
					  VAS_GZIP_QOS_FEAT_TYPE, hv_cop_caps);

		if (rc)
			goto out_cop;
	}
	/*
	 * Default capabilities available
	 */
	if (caps_all.feat_type & VAS_GZIP_DEF_FEAT_BIT) {
		rc = get_vas_capabilities(VAS_GZIP_DEF_FEAT,
					  VAS_GZIP_DEF_FEAT_TYPE, hv_cop_caps);
		if (rc)
			goto out_cop;
	}

	pr_info("GZIP feature is available\n");

out_cop:
	kfree(hv_cop_caps);
out:
	kfree(hv_caps);
	return rc;
}
machine_device_initcall(pseries, pseries_vas_init);
