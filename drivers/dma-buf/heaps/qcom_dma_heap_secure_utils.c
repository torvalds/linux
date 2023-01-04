// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/slab.h>
#include <soc/qcom/secure_buffer.h>
#include <linux/qcom_dma_heap.h>
#include <linux/qcom_scm.h>

int get_secure_vmid(unsigned long flags)
{
	if (flags & QCOM_DMA_HEAP_FLAG_CP_TZ)
		return VMID_TZ;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_TOUCH)
		return VMID_CP_TOUCH;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_BITSTREAM)
		return VMID_CP_BITSTREAM;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_PIXEL)
		return VMID_CP_PIXEL;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_NON_PIXEL)
		return VMID_CP_NON_PIXEL;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_CAMERA)
		return VMID_CP_CAMERA;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_SEC_DISPLAY)
		return VMID_CP_SEC_DISPLAY;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_APP)
		return VMID_CP_APP;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_CAMERA_PREVIEW)
		return VMID_CP_CAMERA_PREVIEW;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_SPSS_SP)
		return VMID_CP_SPSS_SP;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_SPSS_SP_SHARED)
		return VMID_CP_SPSS_SP_SHARED;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_SPSS_HLOS_SHARED)
		return VMID_CP_SPSS_HLOS_SHARED;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_CDSP)
		return VMID_CP_CDSP;
	if (flags & QCOM_DMA_HEAP_FLAG_CP_MSS_MSA)
		return VMID_MSS_MSA;

	return -EINVAL;
}

static unsigned int count_set_bits(unsigned long val)
{
	return ((unsigned int)bitmap_weight(&val, BITS_PER_LONG));
}

bool qcom_is_buffer_hlos_accessible(unsigned long flags)
{
	if (!(flags & QCOM_DMA_HEAP_FLAG_CP_HLOS) &&
	    !(flags & QCOM_DMA_HEAP_FLAG_CP_SPSS_HLOS_SHARED))
		return false;

	return true;
}

static int get_vmid(unsigned long flags)
{
	int vmid;

	vmid = get_secure_vmid(flags);
	if (vmid < 0) {
		if (flags & QCOM_DMA_HEAP_FLAG_CP_HLOS)
			vmid = VMID_HLOS;
	}
	return vmid;
}

static int populate_vm_list(unsigned long flags, unsigned int *vm_list,
			 int nelems)
{
	unsigned int itr = 0;
	int vmid;

	flags = flags & QCOM_DMA_HEAP_FLAGS_CP_MASK;
	if (!flags)
		return -EINVAL;

	for_each_set_bit(itr, &flags, BITS_PER_LONG) {
		vmid = get_vmid(0x1UL << itr);
		if (vmid < 0 || !nelems)
			return -EINVAL;

		vm_list[nelems - 1] = vmid;
		nelems--;
	}
	return 0;
}

static int hyp_unassign_sg(struct sg_table *sgt, int *source_vm_list,
			   int source_nelems, bool clear_page_private)
{
	u32 dest_vmid = VMID_HLOS;
	u32 dest_perms = PERM_READ | PERM_WRITE | PERM_EXEC;
	struct scatterlist *sg;
	int ret, i;

	if (source_nelems <= 0) {
		pr_err("%s: source_nelems invalid\n",
		       __func__);
		ret = -EINVAL;
		goto out;
	}

	ret = hyp_assign_table(sgt, source_vm_list, source_nelems, &dest_vmid,
			       &dest_perms, 1);
	if (ret)
		goto out;

	if (clear_page_private)
		for_each_sg(sgt->sgl, sg, sgt->nents, i)
			ClearPagePrivate(sg_page(sg));
out:
	return ret;
}

static int hyp_assign_sg(struct sg_table *sgt, int *dest_vm_list,
			 int dest_nelems, bool set_page_private)
{
	u32 source_vmid = VMID_HLOS;
	struct scatterlist *sg;
	int *dest_perms;
	int ret, i;

	if (dest_nelems <= 0) {
		pr_err("%s: dest_nelems invalid\n",
		       __func__);
		ret = -EINVAL;
		goto out;
	}

	dest_perms = kcalloc(dest_nelems, sizeof(*dest_perms), GFP_KERNEL);
	if (!dest_perms) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < dest_nelems; i++)
		dest_perms[i] = msm_secure_get_vmid_perms(dest_vm_list[i]);

	ret = hyp_assign_table(sgt, &source_vmid, 1,
			       dest_vm_list, dest_perms, dest_nelems);

	if (ret) {
		pr_err("%s: Assign call failed\n",
		       __func__);
		goto out_free_dest;
	}
	if (set_page_private)
		for_each_sg(sgt->sgl, sg, sgt->nents, i)
			SetPagePrivate(sg_page(sg));

out_free_dest:
	kfree(dest_perms);
out:
	return ret;
}

int hyp_unassign_sg_from_flags(struct sg_table *sgt, unsigned long flags,
			       bool set_page_private)
{
	int ret;
	int *source_vm_list;
	int source_nelems;

	source_nelems = count_set_bits(flags & QCOM_DMA_HEAP_FLAGS_CP_MASK);
	source_vm_list = kcalloc(source_nelems, sizeof(*source_vm_list),
				 GFP_KERNEL);
	if (!source_vm_list)
		return -ENOMEM;
	ret = populate_vm_list(flags, source_vm_list, source_nelems);
	if (ret) {
		pr_err("%s: Failed to get secure vmids\n", __func__);
		goto out_free_source;
	}

	ret = hyp_unassign_sg(sgt, source_vm_list, source_nelems,
				  set_page_private);

out_free_source:
	kfree(source_vm_list);
	return ret;
}

int hyp_assign_sg_from_flags(struct sg_table *sgt, unsigned long flags,
			     bool set_page_private)
{
	int ret;
	int *dest_vm_list = NULL;
	int dest_nelems;

	dest_nelems = count_set_bits(flags & QCOM_DMA_HEAP_FLAGS_CP_MASK);
	dest_vm_list = kcalloc(dest_nelems, sizeof(*dest_vm_list), GFP_KERNEL);
	if (!dest_vm_list) {
		ret = -ENOMEM;
		goto out;
	}

	ret = populate_vm_list(flags, dest_vm_list, dest_nelems);
	if (ret) {
		pr_err("%s: Failed to get secure vmid(s)\n", __func__);
		goto out_free_dest_vm;
	}

	ret = hyp_assign_sg(sgt, dest_vm_list, dest_nelems,
				set_page_private);

out_free_dest_vm:
	kfree(dest_vm_list);
out:
	return ret;
}

int get_vmperm_from_ion_flags(unsigned long flags, int **_vmids,
			int **_perms, u32 *_nr)
{
	int *vmids, *modes;
	u32 nr, i;

	if (!_vmids || !_perms || !_nr)
		return -EINVAL;

	nr = count_set_bits(flags);
	vmids = kcalloc(nr, sizeof(*vmids), GFP_KERNEL);
	if (!vmids)
		return -ENOMEM;

	modes = kcalloc(nr, sizeof(*modes), GFP_KERNEL);
	if (!modes) {
		kfree(vmids);
		return -ENOMEM;
	}

	if ((flags & ~QCOM_DMA_HEAP_FLAGS_CP_MASK) ||
	    populate_vm_list(flags, vmids, nr)) {
		pr_err("%s: Failed to parse secure flags 0x%lx\n", __func__,
		       flags);
		kfree(modes);
		kfree(vmids);
		return -EINVAL;
	}

	for (i = 0; i < nr; i++)
		modes[i] = msm_secure_get_vmid_perms(vmids[i]);

	*_vmids = vmids;
	*_perms = modes;
	*_nr = nr;
	return 0;
}

int hyp_assign_from_flags(u64 base, u64 size, unsigned long flags)
{
	int *vmids, *modes;
	u32 nr;
	u64 src_vm = BIT(QCOM_SCM_VMID_HLOS);
	struct qcom_scm_vmperm *newvm;
	int ret;
	int i;

	ret = get_vmperm_from_ion_flags(flags, &vmids, &modes, &nr);
	if (ret)
		return ret;

	newvm = kcalloc(nr, sizeof(struct qcom_scm_vmperm), GFP_KERNEL);
	if (!newvm) {
		ret = -ENOMEM;
		goto kcalloc_fail;
	}
	for (i = 0; i < nr; i++) {
		newvm[i].vmid = vmids[i];
		newvm[i].perm = modes[i];
	}
	ret = qcom_scm_assign_mem(base, size, &src_vm, newvm, nr);
	if (ret)
		pr_err("%s: Assign call failed, flags 0x%lx\n", __func__,
		       flags);

	kfree(newvm);
kcalloc_fail:
	kfree(modes);
	kfree(vmids);
	return ret;
}
