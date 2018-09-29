/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#include <linux/list.h>
#include "amdgpu.h"
#include "amdgpu_psp.h"


static DEFINE_MUTEX(xgmi_mutex);

#define AMDGPU_MAX_XGMI_HIVE			8
#define AMDGPU_MAX_XGMI_DEVICE_PER_HIVE		4

struct amdgpu_hive_info {
	uint64_t		hive_id;
	struct list_head	device_list;
};

static struct amdgpu_hive_info xgmi_hives[AMDGPU_MAX_XGMI_HIVE];
static unsigned hive_count = 0;

static struct amdgpu_hive_info *amdgpu_get_xgmi_hive(struct amdgpu_device *adev)
{
	int i;
	struct amdgpu_hive_info *tmp;

	if (!adev->gmc.xgmi.hive_id)
		return NULL;
	for (i = 0 ; i < hive_count; ++i) {
		tmp = &xgmi_hives[i];
		if (tmp->hive_id == adev->gmc.xgmi.hive_id)
			return tmp;
	}
	if (i >= AMDGPU_MAX_XGMI_HIVE)
		return NULL;

	/* initialize new hive if not exist */
	tmp = &xgmi_hives[hive_count++];
	tmp->hive_id = adev->gmc.xgmi.hive_id;
	INIT_LIST_HEAD(&tmp->device_list);
	return tmp;
}

int amdgpu_xgmi_add_device(struct amdgpu_device *adev)
{
	struct psp_xgmi_topology_info tmp_topology;
	struct amdgpu_hive_info *hive;
	struct amdgpu_xgmi	*entry;
	struct amdgpu_device 	*tmp_adev;

	int count = 0, ret = -EINVAL;

	if ((adev->asic_type < CHIP_VEGA20) ||
		(adev->flags & AMD_IS_APU) )
		return 0;
	adev->gmc.xgmi.node_id = psp_xgmi_get_node_id(&adev->psp);
	adev->gmc.xgmi.hive_id = psp_xgmi_get_hive_id(&adev->psp);

	memset(&tmp_topology, 0, sizeof(tmp_topology));
	mutex_lock(&xgmi_mutex);
	hive = amdgpu_get_xgmi_hive(adev);
	if (!hive)
		goto exit;

	list_add_tail(&adev->gmc.xgmi.head, &hive->device_list);
	list_for_each_entry(entry, &hive->device_list, head)
		tmp_topology.nodes[count++].node_id = entry->node_id;

	ret = psp_xgmi_get_topology_info(&adev->psp, count, &tmp_topology);
	if (ret) {
		dev_err(adev->dev,
			"XGMI: Get topology failure on device %llx, hive %llx, ret %d",
			adev->gmc.xgmi.node_id,
			adev->gmc.xgmi.hive_id, ret);
		goto exit;
	}
	/* Each psp need to set the latest topology */
	list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head) {
		ret = psp_xgmi_set_topology_info(&tmp_adev->psp, count, &tmp_topology);
		if (ret) {
			dev_err(tmp_adev->dev,
				"XGMI: Set topology failure on device %llx, hive %llx, ret %d",
				tmp_adev->gmc.xgmi.node_id,
				tmp_adev->gmc.xgmi.hive_id, ret);
			/* To do : continue with some  node failed or disable the  whole  hive */
			break;
		}
	}
	if (!ret)
		dev_info(adev->dev, "XGMI: Add node %d to hive 0x%llx.\n",
			adev->gmc.xgmi.physical_node_id,
			adev->gmc.xgmi.hive_id);

exit:
	mutex_unlock(&xgmi_mutex);
	return ret;
}
