// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "hvc_gunyah: " fmt

#include <linux/console.h>
#include <linux/init.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/printk.h>
#include <linux/workqueue.h>

#include <linux/gunyah/gh_msgq.h>
#include <linux/gunyah/gh_common.h>
#include <linux/gunyah/gh_rm_drv.h>

#include "hvc_console.h"

/*
 * Note: hvc_alloc follows first-come, first-served for assigning
 * numbers to registered hvc instances. Thus, the following assignments occur
 * when both DCC and GUNYAH consoles are compiled:
 *            | DCC connected | DCC not connected
 *      (dcc) |      hvc0     | (not present)
 *       SELF |      hvc1     | hvc0
 * PRIMARY_VM |      hvc2     | hvc1
 * TRUSTED_VM |      hvc3     | hvc2
 * "DCC connected" means a DCC terminal is open with device
 */

#define HVC_GH_VTERM_COOKIE	0x474E5948
/* # of payload bytes that can fit in a 1-fragment CONSOLE_WRITE message */
#define GH_HVC_WRITE_MSG_SIZE	((1 * (GH_MSGQ_MAX_MSG_SIZE_BYTES - 8)) - 4)

struct gh_hvc_prv {
	struct hvc_struct *hvc;
	enum gh_vm_names vm_name;
	DECLARE_KFIFO(get_fifo, char, 1024);
	DECLARE_KFIFO(put_fifo, char, 1024);
	struct work_struct put_work;
};

static DEFINE_SPINLOCK(fifo_lock);
static struct gh_hvc_prv gh_hvc_data[GH_VM_MAX];

static inline int gh_vm_name_to_vtermno(enum gh_vm_names vmname)
{
	return vmname + HVC_GH_VTERM_COOKIE;
}

static inline int vtermno_to_gh_vm_name(int vtermno)
{
	return vtermno - HVC_GH_VTERM_COOKIE;
}

static int gh_hvc_notify_console_chars(struct notifier_block *this,
				       unsigned long cmd, void *data)
{
	struct gh_rm_notif_vm_console_chars *msg = data;
	enum gh_vm_names vm_name;
	int ret;

	if (cmd != GH_RM_NOTIF_VM_CONSOLE_CHARS)
		return NOTIFY_DONE;

	ret = gh_rm_get_vm_name(msg->vmid, &vm_name);
	if (ret) {
		pr_warn_ratelimited("don't know VMID %d ret: %d\n", msg->vmid,
				    ret);
		return NOTIFY_OK;
	}

	ret = kfifo_in_spinlocked(&gh_hvc_data[vm_name].get_fifo,
				  msg->bytes, msg->num_bytes,
				  &fifo_lock);

	if (ret < 0)
		pr_warn_ratelimited("dropped %d bytes from VM%d - error %d\n",
				    msg->num_bytes, vm_name, ret);
	else if (ret < msg->num_bytes)
		pr_warn_ratelimited("dropped %d bytes from VM%d - full fifo\n",
				    msg->num_bytes - ret, vm_name);

	if (hvc_poll(gh_hvc_data[vm_name].hvc))
		hvc_kick();

	return NOTIFY_OK;
}

static void gh_hvc_put_work_fn(struct work_struct *ws)
{
	gh_vmid_t vmid;
	char buf[GH_HVC_WRITE_MSG_SIZE];
	int count, ret;
	struct gh_hvc_prv *prv = container_of(ws, struct gh_hvc_prv, put_work);

	ret = ghd_rm_get_vmid(prv->vm_name, &vmid);
	if (ret) {
		pr_warn_once("%s: gh_rm_get_vmid failed for %d: %d\n",
			     __func__, prv->vm_name, ret);
		return;
	}

	while (!kfifo_is_empty(&prv->put_fifo)) {
		count = kfifo_out_spinlocked(&prv->put_fifo, buf, sizeof(buf),
					     &fifo_lock);
		if (count <= 0)
			continue;

		ret = gh_rm_console_write(vmid, buf, count);
		if (ret) {
			pr_warn_once("%s gh_rm_console_write failed for %d: %d\n",
				__func__, prv->vm_name, ret);
			break;
		}
	}
}

static int gh_hvc_get_chars(uint32_t vtermno, char *buf, int count)
{
	int vm_name = vtermno_to_gh_vm_name(vtermno);

	if (vm_name < 0 || vm_name >= GH_VM_MAX)
		return -EINVAL;

	return kfifo_out_spinlocked(&gh_hvc_data[vm_name].get_fifo,
				    buf, count, &fifo_lock);
}

static int gh_hvc_put_chars(uint32_t vtermno, const char *buf, int count)
{
	int ret, vm_name = vtermno_to_gh_vm_name(vtermno);

	if (vm_name < 0 || vm_name >= GH_VM_MAX)
		return -EINVAL;

	ret = kfifo_in_spinlocked(&gh_hvc_data[vm_name].put_fifo,
				   buf, count, &fifo_lock);
	if (ret > 0)
		schedule_work(&gh_hvc_data[vm_name].put_work);
	return ret;
}

static int gh_hvc_flush(uint32_t vtermno, bool wait)
{
	int ret, vm_name = vtermno_to_gh_vm_name(vtermno);
	gh_vmid_t vmid;

	/* RM calls will all sleep. A flush without waiting isn't possible */
	if (!wait)
		return 0;
	might_sleep();

	if (vm_name < 0 || vm_name >= GH_VM_MAX)
		return -EINVAL;

	ret = ghd_rm_get_vmid(vm_name, &vmid);
	if (ret)
		return ret;

	if (cancel_work_sync(&gh_hvc_data[vm_name].put_work)) {
		/* flush the fifo */
		gh_hvc_put_work_fn(&gh_hvc_data[vm_name].put_work);
	}

	return gh_rm_console_flush(vmid);
}

static int gh_hvc_notify_add(struct hvc_struct *hp, int vm_name)
{
	int ret;
	gh_vmid_t vmid;

#ifdef CONFIG_HVC_GUNYAH_CONSOLE
	/* tty layer is opening, but kernel has already opened for printk */
	if (vm_name == GH_SELF_VM)
		return 0;
#endif /* CONFIG_HVC_GUNYAH_CONSOLE */

	ret = ghd_rm_get_vmid(vm_name, &vmid);
	if (ret) {
		pr_err("%s: gh_rm_get_vmid failed for %d: %d\n", __func__,
			vm_name, ret);
		return ret;
	}

	return gh_rm_console_open(vmid);
}

static void gh_hvc_notify_del(struct hvc_struct *hp, int vm_name)
{
	int ret;
	gh_vmid_t vmid;

	if (vm_name < 0 || vm_name >= GH_VM_MAX)
		return;

#ifdef CONFIG_HVC_GUNYAH_CONSOLE
	/* tty layer is closing, but kernel is still using for printk. */
	if (vm_name == GH_SELF_VM)
		return;
#endif /* CONFIG_HVC_GUNYAH_CONSOLE */

	if (cancel_work_sync(&gh_hvc_data[vm_name].put_work)) {
		/* flush the fifo */
		gh_hvc_put_work_fn(&gh_hvc_data[vm_name].put_work);
	}

	ret = ghd_rm_get_vmid(vm_name, &vmid);
	if (ret)
		return;

	ret = gh_rm_console_close(vmid);

	if (ret)
		pr_err("%s: failed close VM%d console - %d\n", __func__,
			vm_name, ret);

	kfifo_reset(&gh_hvc_data[vm_name].get_fifo);
}

static struct notifier_block gh_hvc_nb = {
	.notifier_call = gh_hvc_notify_console_chars,
};

static const struct hv_ops gh_hv_ops = {
	.get_chars = gh_hvc_get_chars,
	.put_chars = gh_hvc_put_chars,
	.flush = gh_hvc_flush,
	.notifier_add = gh_hvc_notify_add,
	.notifier_del = gh_hvc_notify_del,
};

#ifdef CONFIG_HVC_GUNYAH_CONSOLE
static int __init hvc_gh_console_init(void)
{
	int ret;

	/* Need to call RM CONSOLE_OPEN before console can be used */
	ret = gh_rm_console_open(0);
	if (ret)
		return ret;

	ret = hvc_instantiate(gh_vm_name_to_vtermno(GH_SELF_VM), 0,
			      &gh_hv_ops);

	return ret < 0 ? -ENODEV : 0;
}
#else
static int __init hvc_gh_console_init(void)
{
	return 0;
}
#endif /* CONFIG_HVC_GUNYAH_CONSOLE */

static int __init hvc_gh_init(void)
{
	int i, ret = 0;
	struct gh_hvc_prv *prv;

	/* Must initialize fifos and work before calling hvc_gh_console_init */
	for (i = 0; i < GH_VM_MAX; i++) {
		prv = &gh_hvc_data[i];
		prv->vm_name = i;
		INIT_KFIFO(prv->get_fifo);
		INIT_KFIFO(prv->put_fifo);
		INIT_WORK(&prv->put_work, gh_hvc_put_work_fn);
	}

	/* Must instantiate console before calling hvc_alloc */
	hvc_gh_console_init();

	for (i = 0; i < GH_VM_MAX; i++) {
		prv = &gh_hvc_data[i];
		prv->hvc = hvc_alloc(gh_vm_name_to_vtermno(i), i, &gh_hv_ops,
				     256);
		ret = PTR_ERR_OR_ZERO(prv->hvc);
		if (ret)
			goto bail;
	}

	ret = gh_rm_register_notifier(&gh_hvc_nb);
	if (ret)
		goto bail;

	return 0;
bail:
	for (--i; i >= 0; i--) {
		hvc_remove(gh_hvc_data[i].hvc);
		gh_hvc_data[i].hvc = NULL;
	}
	return ret;
}
late_initcall(hvc_gh_init);

static __exit void hvc_gh_exit(void)
{
	int i;

	gh_rm_unregister_notifier(&gh_hvc_nb);

	for (i = 0; i < GH_VM_MAX; i++)
		if (gh_hvc_data[i].hvc) {
			hvc_remove(gh_hvc_data[i].hvc);
			gh_hvc_data[i].hvc = NULL;
		}
}
module_exit(hvc_gh_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Gunyah Hypervisor Console Driver");
