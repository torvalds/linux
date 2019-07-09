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
#include "amdgpu_xgmi.h"
#include "amdgpu_smu.h"


static DEFINE_MUTEX(xgmi_mutex);

#define AMDGPU_MAX_XGMI_HIVE			8
#define AMDGPU_MAX_XGMI_DEVICE_PER_HIVE		4

static struct amdgpu_hive_info xgmi_hives[AMDGPU_MAX_XGMI_HIVE];
static unsigned hive_count = 0;

void *amdgpu_xgmi_hive_try_lock(struct amdgpu_hive_info *hive)
{
	return &hive->device_list;
}

static ssize_t amdgpu_xgmi_show_hive_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct amdgpu_hive_info *hive =
			container_of(attr, struct amdgpu_hive_info, dev_attr);

	return snprintf(buf, PAGE_SIZE, "%llu\n", hive->hive_id);
}

static int amdgpu_xgmi_sysfs_create(struct amdgpu_device *adev,
				    struct amdgpu_hive_info *hive)
{
	int ret = 0;

	if (WARN_ON(hive->kobj))
		return -EINVAL;

	hive->kobj = kobject_create_and_add("xgmi_hive_info", &adev->dev->kobj);
	if (!hive->kobj) {
		dev_err(adev->dev, "XGMI: Failed to allocate sysfs entry!\n");
		return -EINVAL;
	}

	hive->dev_attr = (struct device_attribute) {
		.attr = {
			.name = "xgmi_hive_id",
			.mode = S_IRUGO,

		},
		.show = amdgpu_xgmi_show_hive_id,
	};

	ret = sysfs_create_file(hive->kobj, &hive->dev_attr.attr);
	if (ret) {
		dev_err(adev->dev, "XGMI: Failed to create device file xgmi_hive_id\n");
		kobject_del(hive->kobj);
		kobject_put(hive->kobj);
		hive->kobj = NULL;
	}

	return ret;
}

static void amdgpu_xgmi_sysfs_destroy(struct amdgpu_device *adev,
				    struct amdgpu_hive_info *hive)
{
	sysfs_remove_file(hive->kobj, &hive->dev_attr.attr);
	kobject_del(hive->kobj);
	kobject_put(hive->kobj);
	hive->kobj = NULL;
}

static ssize_t amdgpu_xgmi_show_device_id(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = ddev->dev_private;

	return snprintf(buf, PAGE_SIZE, "%llu\n", adev->gmc.xgmi.node_id);

}


static DEVICE_ATTR(xgmi_device_id, S_IRUGO, amdgpu_xgmi_show_device_id, NULL);


static int amdgpu_xgmi_sysfs_add_dev_info(struct amdgpu_device *adev,
					 struct amdgpu_hive_info *hive)
{
	int ret = 0;
	char node[10] = { 0 };

	/* Create xgmi device id file */
	ret = device_create_file(adev->dev, &dev_attr_xgmi_device_id);
	if (ret) {
		dev_err(adev->dev, "XGMI: Failed to create device file xgmi_device_id\n");
		return ret;
	}

	/* Create sysfs link to hive info folder on the first device */
	if (adev != hive->adev) {
		ret = sysfs_create_link(&adev->dev->kobj, hive->kobj,
					"xgmi_hive_info");
		if (ret) {
			dev_err(adev->dev, "XGMI: Failed to create link to hive info");
			goto remove_file;
		}
	}

	sprintf(node, "node%d", hive->number_devices);
	/* Create sysfs link form the hive folder to yourself */
	ret = sysfs_create_link(hive->kobj, &adev->dev->kobj, node);
	if (ret) {
		dev_err(adev->dev, "XGMI: Failed to create link from hive info");
		goto remove_link;
	}

	goto success;


remove_link:
	sysfs_remove_link(&adev->dev->kobj, adev->ddev->unique);

remove_file:
	device_remove_file(adev->dev, &dev_attr_xgmi_device_id);

success:
	return ret;
}

static void amdgpu_xgmi_sysfs_rem_dev_info(struct amdgpu_device *adev,
					  struct amdgpu_hive_info *hive)
{
	device_remove_file(adev->dev, &dev_attr_xgmi_device_id);
	sysfs_remove_link(&adev->dev->kobj, adev->ddev->unique);
	sysfs_remove_link(hive->kobj, adev->ddev->unique);
}



struct amdgpu_hive_info *amdgpu_get_xgmi_hive(struct amdgpu_device *adev, int lock)
{
	int i;
	struct amdgpu_hive_info *tmp;

	if (!adev->gmc.xgmi.hive_id)
		return NULL;

	mutex_lock(&xgmi_mutex);

	for (i = 0 ; i < hive_count; ++i) {
		tmp = &xgmi_hives[i];
		if (tmp->hive_id == adev->gmc.xgmi.hive_id) {
			if (lock)
				mutex_lock(&tmp->hive_lock);
			mutex_unlock(&xgmi_mutex);
			return tmp;
		}
	}
	if (i >= AMDGPU_MAX_XGMI_HIVE) {
		mutex_unlock(&xgmi_mutex);
		return NULL;
	}

	/* initialize new hive if not exist */
	tmp = &xgmi_hives[hive_count++];

	if (amdgpu_xgmi_sysfs_create(adev, tmp)) {
		mutex_unlock(&xgmi_mutex);
		return NULL;
	}

	tmp->adev = adev;
	tmp->hive_id = adev->gmc.xgmi.hive_id;
	INIT_LIST_HEAD(&tmp->device_list);
	mutex_init(&tmp->hive_lock);
	mutex_init(&tmp->reset_lock);

	if (lock)
		mutex_lock(&tmp->hive_lock);
	tmp->pstate = -1;
	mutex_unlock(&xgmi_mutex);

	return tmp;
}

int amdgpu_xgmi_set_pstate(struct amdgpu_device *adev, int pstate)
{
	int ret = 0;
	struct amdgpu_hive_info *hive = amdgpu_get_xgmi_hive(adev, 0);

	if (!hive)
		return 0;

	if (hive->pstate == pstate)
		return 0;

	dev_dbg(adev->dev, "Set xgmi pstate %d.\n", pstate);

	if (is_support_sw_smu(adev))
		ret = smu_set_xgmi_pstate(&adev->smu, pstate);
	if (ret)
		dev_err(adev->dev,
			"XGMI: Set pstate failure on device %llx, hive %llx, ret %d",
			adev->gmc.xgmi.node_id,
			adev->gmc.xgmi.hive_id, ret);

	return ret;
}

int amdgpu_xgmi_update_topology(struct amdgpu_hive_info *hive, struct amdgpu_device *adev)
{
	int ret = -EINVAL;

	/* Each psp need to set the latest topology */
	ret = psp_xgmi_set_topology_info(&adev->psp,
					 hive->number_devices,
					 &hive->topology_info);
	if (ret)
		dev_err(adev->dev,
			"XGMI: Set topology failure on device %llx, hive %llx, ret %d",
			adev->gmc.xgmi.node_id,
			adev->gmc.xgmi.hive_id, ret);

	return ret;
}

int amdgpu_xgmi_add_device(struct amdgpu_device *adev)
{
	struct psp_xgmi_topology_info *hive_topology;
	struct amdgpu_hive_info *hive;
	struct amdgpu_xgmi	*entry;
	struct amdgpu_device *tmp_adev = NULL;

	int count = 0, ret = -EINVAL;

	if (!adev->gmc.xgmi.supported)
		return 0;

	ret = psp_xgmi_get_node_id(&adev->psp, &adev->gmc.xgmi.node_id);
	if (ret) {
		dev_err(adev->dev,
			"XGMI: Failed to get node id\n");
		return ret;
	}

	ret = psp_xgmi_get_hive_id(&adev->psp, &adev->gmc.xgmi.hive_id);
	if (ret) {
		dev_err(adev->dev,
			"XGMI: Failed to get hive id\n");
		return ret;
	}

	hive = amdgpu_get_xgmi_hive(adev, 1);
	if (!hive) {
		ret = -EINVAL;
		dev_err(adev->dev,
			"XGMI: node 0x%llx, can not match hive 0x%llx in the hive list.\n",
			adev->gmc.xgmi.node_id, adev->gmc.xgmi.hive_id);
		goto exit;
	}

	hive_topology = &hive->topology_info;

	list_add_tail(&adev->gmc.xgmi.head, &hive->device_list);
	list_for_each_entry(entry, &hive->device_list, head)
		hive_topology->nodes[count++].node_id = entry->node_id;
	hive->number_devices = count;

	/* Each psp need to get the latest topology */
	list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head) {
		ret = psp_xgmi_get_topology_info(&tmp_adev->psp, count, hive_topology);
		if (ret) {
			dev_err(tmp_adev->dev,
				"XGMI: Get topology failure on device %llx, hive %llx, ret %d",
				tmp_adev->gmc.xgmi.node_id,
				tmp_adev->gmc.xgmi.hive_id, ret);
			/* To do : continue with some node failed or disable the whole hive */
			break;
		}
	}

	list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head) {
		ret = amdgpu_xgmi_update_topology(hive, tmp_adev);
		if (ret)
			break;
	}

	if (!ret)
		ret = amdgpu_xgmi_sysfs_add_dev_info(adev, hive);

	if (!ret)
		dev_info(adev->dev, "XGMI: Add node %d, hive 0x%llx.\n",
			 adev->gmc.xgmi.physical_node_id, adev->gmc.xgmi.hive_id);
	else
		dev_err(adev->dev, "XGMI: Failed to add node %d, hive 0x%llx ret: %d\n",
			adev->gmc.xgmi.physical_node_id, adev->gmc.xgmi.hive_id,
			ret);


	mutex_unlock(&hive->hive_lock);
exit:
	return ret;
}

void amdgpu_xgmi_remove_device(struct amdgpu_device *adev)
{
	struct amdgpu_hive_info *hive;

	if (!adev->gmc.xgmi.supported)
		return;

	hive = amdgpu_get_xgmi_hive(adev, 1);
	if (!hive)
		return;

	if (!(hive->number_devices--)) {
		amdgpu_xgmi_sysfs_destroy(adev, hive);
		mutex_destroy(&hive->hive_lock);
		mutex_destroy(&hive->reset_lock);
	} else {
		amdgpu_xgmi_sysfs_rem_dev_info(adev, hive);
		mutex_unlock(&hive->hive_lock);
	}
}
