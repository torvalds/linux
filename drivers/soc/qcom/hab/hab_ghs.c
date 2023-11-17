// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"
#include "hab_ghs.h"

#define GIPC_VM_SET_CNT    22

/* same vmid assignment for all the vms. it should matches dt_gipc_path_name */
static int mmid_order[GIPC_VM_SET_CNT] = {
	MM_AUD_1,
	MM_AUD_2,
	MM_AUD_3,
	MM_AUD_4,
	MM_CAM_1,
	MM_CAM_2,
	MM_DISP_1,
	MM_DISP_2,
	MM_DISP_3,
	MM_DISP_4,
	MM_DISP_5,
	MM_GFX,
	MM_VID,
	MM_MISC,
	MM_QCPE_VM1,
	MM_VID_2, /* newly recycled */
	0,
	0,
	MM_CLK_VM1,
	MM_CLK_VM2,
	MM_FDE_1,
	MM_BUFFERQ_1,
};

struct ghs_vmm_plugin_info_s ghs_vmm_plugin_info = {
	dt_gipc_path_name,
	mmid_order,
	0,
	0,
};

int get_dt_name_idx(int vmid_base, int mmid,
				struct ghs_vmm_plugin_info_s *plugin_info)
{
	int idx = -1;
	int i;

	if (vmid_base < 0 || vmid_base > plugin_info->probe_cnt /
						GIPC_VM_SET_CNT) {
		pr_err("vmid %d overflow expected max %d\n", vmid_base,
				plugin_info->probe_cnt / GIPC_VM_SET_CNT);
		return idx;
	}

	for (i = 0; i < GIPC_VM_SET_CNT; i++) {
		if (mmid == plugin_info->mmid_dt_mapping[i]) {
			idx = vmid_base * GIPC_VM_SET_CNT + i;
			if (idx > plugin_info->probe_cnt) {
				pr_err("dt name idx %d overflow max %d\n",
						idx, plugin_info->probe_cnt);
				idx = -1;
			}
			break;
		}
	}
	return idx;
}

/* static struct physical_channel *habhyp_commdev_alloc(int id) */
int habhyp_commdev_alloc(void **commdev, int is_be, char *name, int vmid_remote,
		struct hab_device *mmid_device)
{
	struct ghs_vdev *dev = NULL;
	struct ghs_vdev_os *dev_os = NULL;
	struct physical_channel *pchan = NULL;
	struct physical_channel **ppchan = (struct physical_channel **)commdev;
	int ret = 0;

	if (ghs_vmm_plugin_info.curr > ghs_vmm_plugin_info.probe_cnt) {
		pr_err("too many commdev alloc %d, supported is %d\n",
			ghs_vmm_plugin_info.curr,
			ghs_vmm_plugin_info.probe_cnt);
		ret = -ENOENT;
		goto err;
	}

	/* common part for hyp_data */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		pr_err("allocate struct ghs_vdev failed %zu bytes on pchan %s\n",
			sizeof(*dev), name);
		goto err;
	}

	memset(dev, 0, sizeof(*dev));

	/* os specific part for hyp_data */
	dev_os = kzalloc(sizeof(*dev_os), GFP_KERNEL);
	if (!dev_os) {
		ret = -ENOMEM;
		pr_err("allocate ghs_vdev_os failed %zu bytes on pchan %s\n",
			sizeof(*dev_os), name);
		goto err;
	}

	dev->os_data = dev_os;

	spin_lock_init(&dev->io_lock);

	/*
	 * TODO: ExtractEndpoint is in ghs_comm.c because it blocks.
	 *	Extrace and Request should be in roughly the same spot
	 */
	ret = hab_gipc_ep_attach(is_be, name, vmid_remote, mmid_device, dev);

	if (ret)
		goto err;

	/* add pchan into the mmid_device list */
	pchan = hab_pchan_alloc(mmid_device, vmid_remote);
	if (!pchan) {
		pr_err("hab_pchan_alloc failed for %s, cnt %d\n",
			   mmid_device->name, mmid_device->pchan_cnt);
		ret = -ENOMEM;
		goto err;
	}
	pchan->closed = 0;
	pchan->hyp_data = (void *)dev;
	pchan->is_be = is_be;
	strscpy(dev->name, name, sizeof(dev->name));
	strscpy(pchan->name, name, sizeof(pchan->name));
	*ppchan = pchan;
	dev->read_data = kmalloc(GIPC_RECV_BUFF_SIZE_BYTES, GFP_KERNEL);
	if (!dev->read_data) {
		ret = -ENOMEM;
		goto err;
	}

	ret = habhyp_commdev_create_dispatcher(pchan);
	if (ret)
		goto err;

	/* this value could be more than devp total */
	ghs_vmm_plugin_info.curr++;
	return 0;
err:
	hab_pchan_put(pchan);
	kfree(dev);
	kfree(dev_os);
	return ret;
}

int habhyp_commdev_dealloc(void *commdev)
{
	struct physical_channel *pchan = (struct physical_channel *)commdev;
	struct ghs_vdev *dev = pchan->hyp_data;

	/* os specific deallocation for this commdev */
	habhyp_commdev_dealloc_os(commdev);

	kfree(dev->read_data);
	kfree(dev->os_data);
	kfree(dev);

	pchan->closed = 1;
	pchan->hyp_data = NULL;

	if (get_refcnt(pchan->refcount) > 1) {
		pr_warn("potential leak pchan %s vchans %d refcnt %d\n",
			pchan->name, pchan->vcnt, get_refcnt(pchan->refcount));
	}
	hab_pchan_put(pchan);
	return 0;
}

void hab_hypervisor_unregister(void)
{
	pr_debug("total %d\n", hab_driver.ndevices);

	hab_hypervisor_unregister_common();

	ghs_vmm_plugin_info.curr = 0;
}

int hab_hypervisor_register(void)
{
	int ret = 0;

	/* os-specific registration work */
	ret = hab_hypervisor_register_os();

	return ret;
}

int hab_hypervisor_register_post(void) { return 0; }
