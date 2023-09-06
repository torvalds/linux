// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.*/

#include <linux/dma-direct.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kmsg_dump.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/suspend.h>
#include <linux/types.h>
#include <linux/gunyah/gh_dbl.h>
#include <linux/gunyah/gh_panic_notifier.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/qcom_scm.h>
#include <soc/qcom/secure_buffer.h>

#include "dmesg_dumper_private.h"

#define DDUMP_DBL_MASK				0x1
#define DDUMP_PROFS_NAME			"vmkmsg"
#define DDUMP_WAIT_WAKEIRQ_TIMEOUT	msecs_to_jiffies(1000)

static bool vm_status_ready;

static void qcom_ddump_to_shm(struct kmsg_dumper *dumper,
			  enum kmsg_dump_reason reason)
{
	struct qcom_dmesg_dumper *qdd = container_of(dumper,
					struct qcom_dmesg_dumper, dump);
	size_t len;

	dev_warn(qdd->dev, "reason = %d\n", reason);
	kmsg_dump_rewind(&qdd->iter);
	memset(qdd->base, 0, qdd->size);
	kmsg_dump_get_buffer(&qdd->iter, false, qdd->base, qdd->size, &len);
	dev_warn(qdd->dev, "size of dmesg logbuf logged = %lld\n", len);
}

static int qcom_ddump_gh_panic_handler(struct notifier_block *nb,
				 unsigned long cmd, void *data)
{
	struct qcom_dmesg_dumper *qdd;

	qdd = container_of(nb, struct qcom_dmesg_dumper, gh_panic_nb);
	qcom_ddump_to_shm(&qdd->dump, KMSG_DUMP_PANIC);

	return NOTIFY_DONE;
}

static struct device_node *qcom_ddump_svm_of_parse(struct qcom_dmesg_dumper *qdd)
{
	const char *compat = "qcom,ddump-gunyah-gen";
	struct device_node *np = NULL;
	u32 label;
	int ret;

	while ((np = of_find_compatible_node(np, NULL, compat))) {
		ret = of_property_read_u32(np, "qcom,label", &label);
		if (ret) {
			of_node_put(np);
			continue;
		}
		if (label == qdd->label)
			break;

		of_node_put(np);
	}

	return np;
}

static int qcom_ddump_map_memory(struct qcom_dmesg_dumper *qdd)
{
	struct device *dev = qdd->dev;
	struct device_node *shm_np;
	struct device_node *np;
	u32 size = 0;
	int ret;

	np = dev->of_node;
	if (!qdd->primary_vm) {
		np = qcom_ddump_svm_of_parse(qdd);
		if (!np) {
			dev_err(dev, "Unable to parse shared mem node\n");
			return -EINVAL;
		}
	}

	shm_np = of_parse_phandle(np, "memory-region", 0);
	if (!shm_np)
		return -EINVAL;

	ret = of_address_to_resource(shm_np, 0, &qdd->res);
	of_node_put(shm_np);
	if (!qdd->primary_vm)
		of_node_put(np);

	if (!ret) {
		qdd->is_static = true;
		qdd->size = resource_size(&qdd->res);
		return ret;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "%s: dma_set_mask_and_coherent failed\n", __func__);
		return ret;
	}

	ret = of_reserved_mem_device_init_by_idx(dev, dev->of_node, 0);
	if (ret) {
		dev_err(dev, "%s: Failed to initialize CMA mem, ret %d\n",
			__func__, ret);
		return ret;
	}

	ret = of_property_read_u32(qdd->dev->of_node, "shared-buffer-size", &size);
	if (ret) {
		dev_err(dev, "%s: Failed to get shared memory size, ret %d\n",
					__func__, ret);
		return ret;
	}

	qdd->size = size;
	qdd->is_static = false;
	return 0;
}

static int qcom_ddump_share_mem(struct qcom_dmesg_dumper *qdd, gh_vmid_t self,
			   gh_vmid_t peer)
{
	struct qcom_scm_vmperm dst_vmlist[] = {{self, PERM_READ | PERM_WRITE},
						{peer, PERM_READ | PERM_WRITE}};
	struct qcom_scm_vmperm src_vmlist[] = {{self,
					   PERM_READ | PERM_WRITE | PERM_EXEC}};
	u64 dst_vmid = BIT(dst_vmlist[0].vmid) | BIT(dst_vmlist[1].vmid);
	u64 src_vmid = BIT(src_vmlist[0].vmid);
	struct gh_acl_desc *acl;
	struct gh_sgl_desc *sgl;
	int ret, assign_mem_ret;

	ret = qcom_scm_assign_mem(qdd->res.start, resource_size(&qdd->res),
			     &src_vmid, dst_vmlist, ARRAY_SIZE(dst_vmlist));
	if (ret) {
		dev_err(qdd->dev, "qcom_scm_assign_mem addr=%x size=%u failed: %d\n",
		       qdd->res.start, qdd->size, ret);
		return ret;
	}

	acl = kzalloc(offsetof(struct gh_acl_desc, acl_entries[2]), GFP_KERNEL);
	if (!acl)
		return -ENOMEM;
	sgl = kzalloc(offsetof(struct gh_sgl_desc, sgl_entries[1]), GFP_KERNEL);
	if (!sgl) {
		kfree(acl);
		return -ENOMEM;
	}
	acl->n_acl_entries = 2;
	acl->acl_entries[0].vmid = (u16)self;
	acl->acl_entries[0].perms = GH_RM_ACL_R | GH_RM_ACL_W;
	acl->acl_entries[1].vmid = (u16)peer;
	acl->acl_entries[1].perms = GH_RM_ACL_R | GH_RM_ACL_W;

	sgl->n_sgl_entries = 1;
	sgl->sgl_entries[0].ipa_base = qdd->res.start;
	sgl->sgl_entries[0].size = resource_size(&qdd->res);

	ret = ghd_rm_mem_share(GH_RM_MEM_TYPE_NORMAL, 0, qdd->label,
			      acl, sgl, NULL, &qdd->memparcel);
	if (ret) {
		dev_err(qdd->dev, "Gunyah mem share addr=%x size=%u failed: %d\n",
		       qdd->res.start, qdd->size, ret);
		/* Attempt to give resource back to HLOS */
		assign_mem_ret = qcom_scm_assign_mem(qdd->res.start, resource_size(&qdd->res),
				&dst_vmid, src_vmlist, ARRAY_SIZE(src_vmlist));
		if (assign_mem_ret) {
			dev_err(qdd->dev, "qcom_scm_assign_mem addr=%x size=%u failed: %d\n",
				qdd->res.start, qdd->size, ret);
		}
	}

	kfree(acl);
	kfree(sgl);

	return ret;
}

static int qcom_ddump_unshare_mem(struct qcom_dmesg_dumper *qdd, gh_vmid_t self,
			      gh_vmid_t peer)
{
	struct qcom_scm_vmperm dst_vmlist[] = {{self,
					       PERM_READ | PERM_WRITE | PERM_EXEC}};
	u64 src_vmid = BIT(self) | BIT(peer);
	int ret;

	ret = ghd_rm_mem_reclaim(qdd->memparcel, 0);
	if (ret)
		dev_err(qdd->dev, "Gunyah mem reclaim failed: %d\n", ret);

	ret = qcom_scm_assign_mem(qdd->res.start, resource_size(&qdd->res),
			&src_vmid, dst_vmlist, ARRAY_SIZE(dst_vmlist));
	if (ret)
		dev_err(qdd->dev, "unshare mem assign call failed with %d\n",
			ret);

	return ret;
}

static int qcom_ddump_rm_cb(struct notifier_block *nb, unsigned long cmd,
			     void *data)
{
	struct gh_rm_notif_vm_status_payload *vm_status_payload;
	struct qcom_dmesg_dumper *qdd;
	dma_addr_t dma_handle;
	gh_vmid_t peer_vmid;
	gh_vmid_t self_vmid;
	int ret;

	qdd = container_of(nb, struct qcom_dmesg_dumper, rm_nb);

	if (cmd != GH_RM_NOTIF_VM_STATUS)
		return NOTIFY_DONE;

	vm_status_payload = data;
	if (vm_status_payload->vm_status != GH_RM_VM_STATUS_READY &&
	    vm_status_payload->vm_status != GH_RM_VM_STATUS_RESET)
		return NOTIFY_DONE;
	if (ghd_rm_get_vmid(qdd->peer_name, &peer_vmid))
		return NOTIFY_DONE;
	if (ghd_rm_get_vmid(GH_PRIMARY_VM, &self_vmid))
		return NOTIFY_DONE;
	if (peer_vmid != vm_status_payload->vmid)
		return NOTIFY_DONE;

	if (vm_status_payload->vm_status == GH_RM_VM_STATUS_READY) {
		if (!qdd->is_static) {
			qdd->base = dma_alloc_coherent(qdd->dev, qdd->size,
							&dma_handle, GFP_KERNEL);
			if (!qdd->base)
				return NOTIFY_DONE;

			qdd->res.start = dma_to_phys(qdd->dev, dma_handle);
			qdd->res.end = qdd->res.start + qdd->size - 1;
		}

		strscpy(qdd->md_entry.name, "VM_LOG", sizeof(qdd->md_entry.name));
		qdd->md_entry.virt_addr = (uintptr_t)qdd->base;
		qdd->md_entry.phys_addr = qdd->res.start;
		qdd->md_entry.size = qdd->size;
		ret = msm_minidump_add_region(&qdd->md_entry);
		if (ret < 0)
			dev_err(qdd->dev, "Failed to add vm log entry in minidump table %d\n", ret);

		if (qcom_ddump_share_mem(qdd, self_vmid, peer_vmid)) {
			dev_err(qdd->dev, "Failed to share memory\n");
			goto free_mem;
		}

		vm_status_ready = true;
	}

	if (vm_status_payload->vm_status == GH_RM_VM_STATUS_RESET) {
		vm_status_ready = false;
		if (!qcom_ddump_unshare_mem(qdd, self_vmid, peer_vmid))
			goto free_mem;
	}

	return NOTIFY_DONE;

free_mem:
	if (!qdd->is_static)
		dma_free_coherent(qdd->dev, qdd->size, qdd->base,
				phys_to_dma(qdd->dev, qdd->res.start));

	return NOTIFY_DONE;
}

static inline int qcom_ddump_gh_kick(struct qcom_dmesg_dumper *qdd)
{
	gh_dbl_flags_t dbl_mask = DDUMP_DBL_MASK;
	int ret;

	ret = gh_dbl_send(qdd->tx_dbl, &dbl_mask, 0);
	if (ret)
		dev_err(qdd->dev, "failed to raise virq to the sender %d\n", ret);

	return ret;
}

static void qcom_ddump_gh_cb(int irq, void *data)
{
	gh_dbl_flags_t dbl_mask = DDUMP_DBL_MASK;
	struct qcom_dmesg_dumper *qdd;
	struct ddump_shm_hdr *hdr;
	int ret;

	qdd = data;
	hdr = qdd->base;
	gh_dbl_read_and_clean(qdd->rx_dbl, &dbl_mask, GH_DBL_NONBLOCK);

	if (qdd->primary_vm) {
		complete(&qdd->ddump_completion);
	} else {
		/* avoid system enter suspend */
		pm_wakeup_ws_event(qdd->wakeup_source, 2000, true);
		ret = qcom_ddump_alive_log_to_shm(qdd, hdr->user_buf_len);
		if (ret)
			dev_err(qdd->dev, "dump alive log error %d\n", ret);

		qcom_ddump_gh_kick(qdd);
		if (hdr->svm_dump_len == 0)
			pm_wakeup_ws_event(qdd->wakeup_source, 0, true);
	}
}

static ssize_t qcom_ddump_vmkmsg_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct qcom_dmesg_dumper *qdd = pde_data(file_inode(file));
	struct ddump_shm_hdr *hdr = qdd->base;
	int ret;

	if (!vm_status_ready)
		return -ENODEV;

	if (count < LOG_LINE_MAX) {
		dev_err(qdd->dev, "user buffer size should greater than %d\n", LOG_LINE_MAX);
		return -EINVAL;
	}

	/**
	 * If SVM is in suspend mode and the log size more than 1k byte,
	 * we think SVM has log need to be read. Otherwise, we think the
	 * log is only suspend log that we need skip the unnecessary log.
	 */
	if (hdr->svm_is_suspend && hdr->svm_dump_len < 1024)
		return 0;

	hdr->user_buf_len = count;
	qcom_ddump_gh_kick(qdd);
	ret = wait_for_completion_timeout(&qdd->ddump_completion, DDUMP_WAIT_WAKEIRQ_TIMEOUT);
	if (!ret) {
		dev_err(qdd->dev, "wait for completion timeout\n");
		return -ETIMEDOUT;
	}

	if (hdr->svm_dump_len > count) {
		dev_err(qdd->dev, "can not read the correct length of svm kmsg\n");
		return -EINVAL;
	}

	if (hdr->svm_dump_len &&
		copy_to_user(buf, &hdr->data, hdr->svm_dump_len)) {
		dev_err(qdd->dev, "copy_to_user fail\n");
		return -EFAULT;
	}

	return hdr->svm_dump_len;
}

static const struct proc_ops ddump_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_read	= qcom_ddump_vmkmsg_read,
};

static int qcom_ddump_alive_log_probe(struct qcom_dmesg_dumper *qdd)
{
	struct device_node *node = qdd->dev->of_node;
	struct device *dev = qdd->dev;
	struct proc_dir_entry *dent;
	struct ddump_shm_hdr *hdr;
	enum gh_dbl_label dbl_label;
	struct resource *res;
	size_t shm_min_size;
	int ret;

	shm_min_size = LOG_LINE_MAX + DDUMP_GET_SHM_HDR;
	if (qdd->size < shm_min_size) {
		dev_err(dev, "Shared memory size should greater than %d\n", shm_min_size);
		return -EINVAL;
	}

	if (qdd->primary_vm) {
		if (qdd->is_static) {
			res = devm_request_mem_region(dev, qdd->res.start,
					qdd->size, dev_name(dev));
			if (!res) {
				dev_err(dev, "request mem region fail\n");
				return -ENXIO;
			}

			qdd->base = devm_ioremap_wc(dev, qdd->res.start, qdd->size);
			if (!qdd->base) {
				dev_err(dev, "ioremap fail\n");
				return -ENOMEM;
			}
		}

		init_completion(&qdd->ddump_completion);
		dent = proc_create_data(DDUMP_PROFS_NAME, 0400, NULL, &ddump_proc_ops, qdd);
		if (!dent) {
			dev_err(dev, "proc_create_data fail\n");
			return -ENOMEM;
		}
	} else {
		/* init shared memory header */
		hdr = qdd->base;
		hdr->svm_is_suspend = false;

		ret = qcom_ddump_encrypt_init(node);
		if (ret)
			return ret;

		qdd->wakeup_source = wakeup_source_register(dev, dev_name(dev));
		if (!qdd->wakeup_source)
			return -ENOMEM;

		qdd->gh_panic_nb.notifier_call = qcom_ddump_gh_panic_handler;
		qdd->gh_panic_nb.priority = INT_MAX;
		ret = gh_panic_notifier_register(&qdd->gh_panic_nb);
		if (ret)
			goto err_panic_notifier_register;
	}

	dbl_label = qdd->label;
	qdd->tx_dbl = gh_dbl_tx_register(dbl_label);
	if (IS_ERR_OR_NULL(qdd->tx_dbl)) {
		ret = PTR_ERR(qdd->tx_dbl);
		dev_err(dev, "%s:Failed to get gunyah tx dbl %d\n", __func__, ret);
		goto err_dbl_tx_register;
	}

	qdd->rx_dbl = gh_dbl_rx_register(dbl_label, qcom_ddump_gh_cb, qdd);
	if (IS_ERR_OR_NULL(qdd->rx_dbl)) {
		ret = PTR_ERR(qdd->rx_dbl);
		dev_err(dev, "%s:Failed to get gunyah rx dbl %d\n", __func__, ret);
		goto err_dbl_rx_register;
	}

	return 0;

err_dbl_rx_register:
	gh_dbl_tx_unregister(qdd->tx_dbl);
err_dbl_tx_register:
	if (qdd->primary_vm)
		remove_proc_entry(DDUMP_PROFS_NAME, NULL);
	else
		gh_panic_notifier_unregister(&qdd->gh_panic_nb);
err_panic_notifier_register:
	if (!qdd->primary_vm)
		wakeup_source_unregister(qdd->wakeup_source);

	return ret;
}

static int qcom_ddump_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct qcom_dmesg_dumper *qdd;
	struct device *dev;
	int ret;
	struct resource *res;

	qdd = devm_kzalloc(&pdev->dev, sizeof(*qdd), GFP_KERNEL);
	if (!qdd)
		return -ENOMEM;

	qdd->dev = &pdev->dev;
	platform_set_drvdata(pdev, qdd);

	dev = qdd->dev;
	ret = of_property_read_u32(node, "gunyah-label", &qdd->label);
	if (ret) {
		dev_err(dev, "Failed to read label %d\n", ret);
		return ret;
	}

	qdd->primary_vm = of_property_read_bool(node, "qcom,primary-vm");

	ret = qcom_ddump_map_memory(qdd);
	if (ret)
		return ret;

	if (qdd->primary_vm) {
		ret = of_property_read_u32(node, "peer-name", &qdd->peer_name);
		if (ret)
			qdd->peer_name = GH_SELF_VM;

		qdd->rm_nb.notifier_call = qcom_ddump_rm_cb;
		qdd->rm_nb.priority = INT_MAX;
		gh_rm_register_notifier(&qdd->rm_nb);
	} else {
		res = devm_request_mem_region(dev, qdd->res.start, qdd->size, dev_name(dev));
		if (!res) {
			dev_err(dev, "request mem region fail\n");
			return -ENXIO;
		}

		qdd->base = devm_ioremap_wc(dev, qdd->res.start, qdd->size);
		if (!qdd->base) {
			dev_err(dev, "ioremap fail\n");
			return -ENOMEM;
		}

		kmsg_dump_rewind(&qdd->iter);
		qdd->dump.dump = qcom_ddump_to_shm;
		ret = kmsg_dump_register(&qdd->dump);
		if (ret)
			return ret;
	}

	if (IS_ENABLED(CONFIG_QCOM_VM_ALIVE_LOG_DUMPER)) {
		ret = qcom_ddump_alive_log_probe(qdd);
		if (ret) {
			if (qdd->primary_vm)
				gh_rm_unregister_notifier(&qdd->rm_nb);
			else
				kmsg_dump_unregister(&qdd->dump);
			return ret;
		}
	}

	return 0;
}

static int qcom_ddump_remove(struct platform_device *pdev)
{
	gh_vmid_t peer_vmid;
	gh_vmid_t self_vmid;
	int ret;
	struct qcom_dmesg_dumper *qdd = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_QCOM_VM_ALIVE_LOG_DUMPER)) {
		gh_dbl_tx_unregister(qdd->tx_dbl);
		gh_dbl_rx_unregister(qdd->rx_dbl);
		if (qdd->primary_vm) {
			remove_proc_entry(DDUMP_PROFS_NAME, NULL);
		} else {
			gh_panic_notifier_unregister(&qdd->gh_panic_nb);
			wakeup_source_unregister(qdd->wakeup_source);
			qcom_ddump_encrypt_exit();
		}
	}

	if (qdd->primary_vm) {
		gh_rm_unregister_notifier(&qdd->rm_nb);
		ret = ghd_rm_get_vmid(qdd->peer_name, &peer_vmid);
		if (ret)
			return ret;

		ret = ghd_rm_get_vmid(GH_PRIMARY_VM, &self_vmid);
		if (ret)
			return ret;

		ret = qcom_ddump_unshare_mem(qdd, self_vmid, peer_vmid);
		if (ret)
			return ret;

		if (!qdd->is_static)
			dma_free_coherent(qdd->dev, qdd->size, qdd->base,
				phys_to_dma(qdd->dev, qdd->res.start));
	} else {
		ret = kmsg_dump_unregister(&qdd->dump);
		if (ret)
			return ret;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_PM_SLEEP) && IS_ENABLED(CONFIG_ARCH_QTI_VM) && \
	IS_ENABLED(CONFIG_QCOM_VM_ALIVE_LOG_DUMPER)
static int qcom_ddump_suspend(struct device *pdev)
{
	struct qcom_dmesg_dumper *qdd = dev_get_drvdata(pdev);
	struct ddump_shm_hdr *hdr = qdd->base;
	u64 seq_backup;
	int ret;

	hdr->svm_is_suspend = true;
	seq_backup = qdd->iter.cur_seq;
	ret = qcom_ddump_alive_log_to_shm(qdd, qdd->size);
	if (ret)
		dev_err(qdd->dev, "dump alive log error %d\n", ret);

	qdd->iter.cur_seq = seq_backup;
	return 0;
}

static int qcom_ddump_resume(struct device *pdev)
{
	struct qcom_dmesg_dumper *qdd = dev_get_drvdata(pdev);
	struct ddump_shm_hdr *hdr = qdd->base;

	hdr->svm_is_suspend = false;
	return 0;
}

static SIMPLE_DEV_PM_OPS(ddump_pm_ops, qcom_ddump_suspend, qcom_ddump_resume);
#endif

static const struct of_device_id ddump_match_table[] = {
	{ .compatible = "qcom,dmesg-dump" },
	{}
};

static struct platform_driver ddump_driver = {
	.driver = {
		.name = "qcom_dmesg_dumper",
#if IS_ENABLED(CONFIG_PM_SLEEP) && IS_ENABLED(CONFIG_ARCH_QTI_VM) && \
	IS_ENABLED(CONFIG_QCOM_VM_ALIVE_LOG_DUMPER)
		.pm = &ddump_pm_ops,
#endif
		.of_match_table = ddump_match_table,
	 },
	.probe = qcom_ddump_probe,
	.remove = qcom_ddump_remove,
};

static int __init qcom_ddump_init(void)
{
	return platform_driver_register(&ddump_driver);
}

#if IS_ENABLED(CONFIG_ARCH_QTI_VM)
arch_initcall(qcom_ddump_init);
#else
module_init(qcom_ddump_init);
#endif

static __exit void qcom_ddump_exit(void)
{
	platform_driver_unregister(&ddump_driver);
}
module_exit(qcom_ddump_exit);

MODULE_DESCRIPTION("QTI Virtual Machine dmesg log buffer dumper");
MODULE_LICENSE("GPL");
