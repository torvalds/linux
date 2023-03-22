// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/qcom_scm.h>
#include <soc/qcom/secure_buffer.h>
#include "linux/gunyah/gh_mem_notifier.h"
#include "linux/gunyah/gh_rm_drv.h"
#include "linux/gh_cpusys_vm_mem_access.h"

struct gh_cpusys_vm_data {
	struct device *dev;
	struct resource res;
	u32 label;
	u32 peer_name;
	struct notifier_block rm_nb;
	u32 memparcel;
	bool mem_shared;
};

struct gh_cpusys_vm_data *gh_cpusys_vm_drv_data;

/**
 * gh_cpusys_vm_get_share_mem_info: returns the resource information with the address and size
 *                                  of the memory shared to cpusys vm.
 *
 * @res: resource where the data of the shared memory is returned.
 *
 * Returns zero for success, a negative number on error.
 */
int gh_cpusys_vm_get_share_mem_info(struct resource *res)
{
	struct gh_cpusys_vm_data *drv_data = gh_cpusys_vm_drv_data;

	if (!res || !drv_data)
		return -EINVAL;

	if (!gh_cpusys_vm_drv_data->mem_shared)
		return -ENXIO;

	memcpy(res, &drv_data->res, sizeof(*res));

	return 0;
}
EXPORT_SYMBOL(gh_cpusys_vm_get_share_mem_info);

static int gh_cpusys_vm_share_mem(struct gh_cpusys_vm_data *drv_data,
				gh_vmid_t self, gh_vmid_t peer)
{
	struct qcom_scm_vmperm src_vmlist[] = {{self, PERM_READ | PERM_WRITE | PERM_EXEC}};
	struct qcom_scm_vmperm dst_vmlist[] = {{self, PERM_READ | PERM_WRITE},
					       {peer, PERM_READ | PERM_WRITE}};
	u64 srcvmids = BIT(src_vmlist[0].vmid);
	u64 dstvmids = BIT(dst_vmlist[0].vmid) | BIT(dst_vmlist[1].vmid);
	struct gh_acl_desc *acl;
	struct gh_sgl_desc *sgl;
	int ret;

	ret = qcom_scm_assign_mem(drv_data->res.start, resource_size(&drv_data->res), &srcvmids,
			dst_vmlist, ARRAY_SIZE(dst_vmlist));
	if (ret) {
		dev_err(drv_data->dev, "%s: qcom_scm_assign_mem failed addr=%x size=%u err=%d\n",
			__func__, drv_data->res.start, resource_size(&drv_data->res), ret);
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
	sgl->sgl_entries[0].ipa_base = drv_data->res.start;
	sgl->sgl_entries[0].size = resource_size(&drv_data->res);

	ret = ghd_rm_mem_share(GH_RM_MEM_TYPE_NORMAL, 0, drv_data->label,
			acl, sgl, NULL, &drv_data->memparcel);
	if (ret) {
		dev_err(drv_data->dev, "%s: gh_rm_mem_share failed addr=%x size=%u err=%d\n",
			__func__, drv_data->res.start, resource_size(&drv_data->res), ret);
		/* Attempt to give resource back to HLOS */
		qcom_scm_assign_mem(drv_data->res.start, resource_size(&drv_data->res),
				&dstvmids, src_vmlist, ARRAY_SIZE(src_vmlist));
	}

	kfree(acl);
	kfree(sgl);

	return ret;
}

static int gh_cpusys_vm_mem_rm_cb(struct notifier_block *nb, unsigned long cmd, void *data)
{
	struct gh_rm_notif_vm_status_payload *vm_status_payload;
	struct gh_cpusys_vm_data *drv_data;
	gh_vmid_t peer_vmid;
	gh_vmid_t self_vmid;

	drv_data = container_of(nb, struct gh_cpusys_vm_data, rm_nb);

	dev_dbg(drv_data->dev, "cmd:0x%lx\n", cmd);
	if (cmd != GH_RM_NOTIF_VM_STATUS)
		goto end;

	vm_status_payload = data;
	dev_dbg(drv_data->dev, "payload vm_status:%d\n", vm_status_payload->vm_status);
	if (vm_status_payload->vm_status != GH_RM_VM_STATUS_READY &&
	    vm_status_payload->vm_status != GH_RM_VM_STATUS_RESET)
		goto end;

	if (ghd_rm_get_vmid(drv_data->peer_name, &peer_vmid))
		goto end;

	if (ghd_rm_get_vmid(GH_PRIMARY_VM, &self_vmid))
		goto end;

	if (peer_vmid != vm_status_payload->vmid)
		goto end;

	switch (vm_status_payload->vm_status) {
	case GH_RM_VM_STATUS_READY:
		if (gh_cpusys_vm_share_mem(drv_data, self_vmid, peer_vmid)) {
			dev_err(drv_data->dev, "failed to share memory\n");
		} else {
			drv_data->mem_shared = true;
			dev_dbg(drv_data->dev, "cpusys vm mem shared\n");
		}
		break;
	case GH_RM_VM_STATUS_RESET:
		dev_dbg(drv_data->dev, "reset\n");
		break;
	case GH_RM_VM_STATUS_INIT_FAILED:
		dev_dbg(drv_data->dev, "cpusys vm shmem failed\n");
		break;
	}

end:
	return NOTIFY_DONE;
}

static int gh_cpusys_vm_init(struct gh_cpusys_vm_data *drv_data)
{
	struct device_node *node = drv_data->dev->of_node;
	struct device_node *np;
	int notifier_ret, ret;

	ret = of_property_read_u32(node, "gunyah-label", &drv_data->label);
	if (ret) {
		dev_err(drv_data->dev, "failed to find label info %d\n", ret);
		return ret;
	}

	np = of_parse_phandle(node, "shared-buffer", 0);
	if (!np) {
		dev_err(drv_data->dev, "failed to read shared-buffer info\n");
		return -ENOMEM;
	}

	ret = of_address_to_resource(np, 0, &drv_data->res);
	of_node_put(np);
	if (ret) {
		dev_err(drv_data->dev, "of_address_to_resource failed %d\n", ret);
		return -EINVAL;
	}

	dev_dbg(drv_data->dev, "start:0x%x end:0x%x size:0x%x name:%s\n",
		drv_data->res.start, drv_data->res.end, resource_size(&drv_data->res),
		drv_data->res.name);

	/* Register memory with HYP */
	ret = of_property_read_u32(node, "peer-name", &drv_data->peer_name);
	if (ret)
		drv_data->peer_name = GH_SELF_VM;

	drv_data->rm_nb.notifier_call = gh_cpusys_vm_mem_rm_cb;
	drv_data->rm_nb.priority = INT_MAX;
	notifier_ret = gh_rm_register_notifier(&drv_data->rm_nb);
	dev_dbg(drv_data->dev, "notifier: ret:%d peer_name:%d notifier_ret:%d\n", ret,
		drv_data->peer_name, notifier_ret);
	if (notifier_ret) {
		dev_err(drv_data->dev, "fail to register notifier ret:%d\n", notifier_ret);
		return -EPROBE_DEFER;
	}

	return 0;
}

static int gh_cpusys_vm_mem_access_probe(struct platform_device *pdev)
{
	struct gh_cpusys_vm_data *drv_data;
	int ret;

	drv_data = devm_kzalloc(&pdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, drv_data);
	drv_data->dev = &pdev->dev;
	gh_cpusys_vm_drv_data = drv_data;

	ret = gh_cpusys_vm_init(drv_data);
	if (ret)
		dev_err(drv_data->dev, "fail to init cpusys vm mem access:%d\n", ret);

	return 0;
}

static int gh_cpusys_vm_mem_access_remove(struct platform_device *pdev)
{
	struct gh_cpusys_vm_data *drv_data = platform_get_drvdata(pdev);

	gh_cpusys_vm_drv_data = NULL;
	gh_rm_unregister_notifier(&drv_data->rm_nb);

	return 0;
}

static const struct of_device_id gh_cpusys_vm_mem_access_of_match[] = {
	{ .compatible = "qcom,cpusys-vm-shmem-access"},
	{}
};
MODULE_DEVICE_TABLE(of, gh_cpusys_vm_mem_access_of_match);

static struct platform_driver gh_cpusys_vm_mem_access_driver = {
	.probe = gh_cpusys_vm_mem_access_probe,
	.remove = gh_cpusys_vm_mem_access_remove,
	.driver = {
		.name = "gh_cpusys_vm_mem_access",
		.of_match_table = gh_cpusys_vm_mem_access_of_match,
	},
};

static int __init gh_cpusys_vm_mem_access_init(void)
{
	return platform_driver_register(&gh_cpusys_vm_mem_access_driver);
}
module_init(gh_cpusys_vm_mem_access_init);

static __exit void gh_cpusys_vm_mem_access_exit(void)
{
	platform_driver_unregister(&gh_cpusys_vm_mem_access_driver);
}
module_exit(gh_cpusys_vm_mem_access_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. CPUSYS VM Memory Access Driver");
MODULE_LICENSE("GPL");
