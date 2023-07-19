// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define DRIVER_NAME "msm_sharedmem"
#define pr_fmt(fmt) DRIVER_NAME ": %s: " fmt, __func__

#include <linux/uio_driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include <linux/qcom_scm.h>

#include <soc/qcom/secure_buffer.h>

#define CLIENT_ID_PROP "qcom,client-id"
#define MPSS_RMTS_CLIENT_ID 1

static int uio_get_mem_index(struct uio_info *info, struct vm_area_struct *vma)
{
	if (vma->vm_pgoff >= MAX_UIO_MAPS)
		return -EINVAL;

	if (info->mem[vma->vm_pgoff].size == 0)
		return -EINVAL;

	return (int)vma->vm_pgoff;
}

static int sharedmem_mmap(struct uio_info *info, struct vm_area_struct *vma)
{
	int result;
	struct uio_mem *mem;
	int mem_index = uio_get_mem_index(info, vma);

	if (mem_index < 0) {
		pr_err("mem_index is invalid errno %d\n", mem_index);
		return mem_index;
	}

	mem = info->mem + mem_index;

	if (vma->vm_end - vma->vm_start > mem->size) {
		pr_err("vm_end[%lu] - vm_start[%lu] [%lu] > mem->size[%pa]\n",
			vma->vm_end, vma->vm_start,
			(vma->vm_end - vma->vm_start), &mem->size);
		return -EINVAL;
	}
	pr_debug("Attempting to setup mmap.\n");

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	result = remap_pfn_range(vma,
				 vma->vm_start,
				 mem->addr >> PAGE_SHIFT,
				 vma->vm_end - vma->vm_start,
				 vma->vm_page_prot);
	if (result != 0)
		pr_err("mmap Failed with errno %d\n", result);
	else
		pr_debug("mmap success\n");

	return result;
}

/* Setup the shared ram permissions.
 * This function currently supports the mpss and nav clients only.
 */
static void setup_shared_ram_perms(u32 client_id, phys_addr_t addr, u32 size,
				   bool vm_nav_path)
{
	struct qcom_scm_vmperm *dest_vmids;
	u64 source_vmlist = BIT(VMID_HLOS);
	int ret, nr;

	if (client_id != MPSS_RMTS_CLIENT_ID)
		return;

	if (vm_nav_path) {
		nr = 3;
		dest_vmids = kcalloc(nr, sizeof(struct qcom_scm_vmperm), GFP_KERNEL);
		if (!dest_vmids) {
			pr_err("failed to alloc memory when vm_nav_path=true\n");
			return;
		}

		*dest_vmids = (struct qcom_scm_vmperm){VMID_HLOS, PERM_READ|PERM_WRITE};
		*(dest_vmids + 1) = (struct qcom_scm_vmperm){VMID_MSS_MSA, PERM_READ|PERM_WRITE};
		*(dest_vmids + 2) = (struct qcom_scm_vmperm){VMID_NAV, PERM_READ|PERM_WRITE};
	} else {
		nr = 2;
		dest_vmids = kcalloc(nr, sizeof(struct qcom_scm_vmperm), GFP_KERNEL);
		if (!dest_vmids) {
			pr_err("failed to alloc memory when vm_nav_path=false\n");
			return;
		}
		*dest_vmids = (struct qcom_scm_vmperm){VMID_HLOS, PERM_READ|PERM_WRITE};
		*(dest_vmids + 1) = (struct qcom_scm_vmperm){VMID_MSS_MSA, PERM_READ|PERM_WRITE};
	}

	ret = qcom_scm_assign_mem(addr, size, &source_vmlist,
					dest_vmids, nr);
	kfree(dest_vmids);
	if (ret != 0) {
		if (ret == -EINVAL)
			pr_warn("qcom_scm_assign_mem is not supported!\n");
		else
			pr_err("qcom_scm_assign_mem failed IPA=0x016%pa size=%u err=%d\n",
				&addr, size, ret);
	}
}

static int msm_sharedmem_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct uio_info *info = NULL;
	struct resource *clnt_res = NULL;
	u32 client_id = ((u32)~0U);
	u32 shared_mem_size = 0;
	u32 shared_mem_tot_sz = 0;
	void *shared_mem = NULL;
	phys_addr_t shared_mem_pyhsical = 0;
	bool is_addr_dynamic = false;
	bool guard_memory = false;
	bool vm_nav_path = false;

	/* Get the addresses from platform-data */
	if (!pdev->dev.of_node) {
		pr_err("Node not found\n");
		ret = -ENODEV;
		goto out;
	}
	clnt_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!clnt_res) {
		pr_err("resource not found\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(pdev->dev.of_node, CLIENT_ID_PROP,
				   &client_id);
	if (ret) {
		client_id = ((u32)~0U);
		pr_warn("qcom,client-id property not found\n");
	}

	info = devm_kzalloc(&pdev->dev, sizeof(struct uio_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	shared_mem_size = resource_size(clnt_res);
	shared_mem_pyhsical = clnt_res->start;

	if (shared_mem_size == 0) {
		pr_err("Shared memory size is zero\n");
		return -EINVAL;
	}

	if (shared_mem_pyhsical == 0) {
		is_addr_dynamic = true;

		/*
		 * If guard_memory is set, then the shared memory region
		 * will be guarded by SZ_4K at the start and at the end.
		 * This is needed to overcome the XPU limitation on few
		 * MSM HW, so as to make this memory not contiguous with
		 * other allocations that may possibly happen from other
		 * clients in the system.
		 */
		guard_memory = of_property_read_bool(pdev->dev.of_node,
				"qcom,guard-memory");

		shared_mem_tot_sz = guard_memory ? shared_mem_size + SZ_8K :
					shared_mem_size;

		shared_mem = dma_alloc_coherent(&pdev->dev, shared_mem_tot_sz,
					&shared_mem_pyhsical, GFP_KERNEL);
		if (shared_mem == NULL) {
			pr_err("failed to alloc memory %d\n", shared_mem_tot_sz);
			return -ENOMEM;
		}
		if (guard_memory)
			shared_mem_pyhsical += SZ_4K;
	}

	/*
	 * If this dtsi property is set, then the shared memory region
	 * will be given access to vm-nav-path also.
	 */
	vm_nav_path = of_property_read_bool(pdev->dev.of_node,
			"qcom,vm-nav-path");

	/* Set up the permissions for the shared ram that was allocated. */
	setup_shared_ram_perms(client_id, shared_mem_pyhsical, shared_mem_size,
				vm_nav_path);

	/* Setup device */
	info->mmap = sharedmem_mmap; /* Custom mmap function. */
	info->name = clnt_res->name;
	info->version = "1.0";
	info->mem[0].addr = shared_mem_pyhsical;
	info->mem[0].size = shared_mem_size;
	info->mem[0].memtype = UIO_MEM_PHYS;

	ret = uio_register_device(&pdev->dev, info);
	if (ret) {
		pr_err("uio register failed ret=%d\n", ret);
		goto out;
	}
	dev_set_drvdata(&pdev->dev, info);

	pr_info("Device created for client '%s'\n", clnt_res->name);
out:
	return ret;
}

static int msm_sharedmem_remove(struct platform_device *pdev)
{
	struct uio_info *info = dev_get_drvdata(&pdev->dev);

	uio_unregister_device(info);

	return 0;
}

static const struct of_device_id msm_sharedmem_of_match[] = {
	{.compatible = "qcom,sharedmem-uio",},
	{},
};
MODULE_DEVICE_TABLE(of, msm_sharedmem_of_match);

static struct platform_driver msm_sharedmem_driver = {
	.probe          = msm_sharedmem_probe,
	.remove         = msm_sharedmem_remove,
	.driver         = {
		.name   = DRIVER_NAME,
		.of_match_table = msm_sharedmem_of_match,
	},
};


static int __init msm_sharedmem_init(void)
{
	int result;

	result = platform_driver_register(&msm_sharedmem_driver);
	if (result != 0) {
		pr_err("Platform driver registration failed\n");
		return result;
	}
	return 0;
}

static void __exit msm_sharedmem_exit(void)
{
	platform_driver_unregister(&msm_sharedmem_driver);
}

module_init(msm_sharedmem_init);
module_exit(msm_sharedmem_exit);

MODULE_LICENSE("GPL");
