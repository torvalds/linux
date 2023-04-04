// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"
#include "hab_ghs.h"

const char * const dt_gipc_path_name[] = {
	"testgipc1",
	"testgipc2",
	"testgipc3",
	"testgipc4",
	"testgipc5",
	"testgipc6",
	"testgipc7",
	"testgipc8",
	"testgipc9",
	"testgipc10",
	"testgipc11",
	"testgipc12",
	"testgipc13",
	"testgipc14",
	"testgipc15",
	"testgipc16",
	"testgipc17",
	"testgipc18",
	"testgipc19",
	"testgipc20",
	"testgipc21",
	"testgipc22",
};

static void ghs_irq_handler(void *cookie)
{
	struct physical_channel *pchan = cookie;
	struct ghs_vdev *dev =
		(struct ghs_vdev *) (pchan ? pchan->hyp_data : NULL);

	if (dev)
		tasklet_hi_schedule(&dev->os_data->task);
}

int hab_gipc_ep_attach(int is_be, char *name, int vmid_remote,
		struct hab_device *mmid_device, struct ghs_vdev *dev)
{
	int dt_name_idx = 0;
	int ret = 0;

	if (is_be) {
		/* role is backend */
		dev->be = 1;
	} else {
		/* role is FE */
		struct device_node *gvh_dn;

		gvh_dn = of_find_node_by_path("/aliases");
		if (gvh_dn) {
			const char *ep_path = NULL;
			struct device_node *ep_dn = NULL;

			dt_name_idx = get_dt_name_idx(vmid_remote,
							mmid_device->id,
							&ghs_vmm_plugin_info);
			if (dt_name_idx < 0) {
				pr_err("failed to find %s for vmid %d ret %d\n",
						mmid_device->name,
						mmid_device->id,
						dt_name_idx);
				of_node_put(gvh_dn);
				ret = -ENOENT;
				goto exit;
			}

			ret = of_property_read_string(gvh_dn,
				ghs_vmm_plugin_info.dt_name[dt_name_idx],
				&ep_path);
			if (ret) {
				pr_err("failed to read endpoint str ret %d\n",
					ret);
				of_node_put(gvh_dn);
				ret = -ENOENT;
				goto exit;
			}
			of_node_put(gvh_dn);

			ep_dn = of_find_node_by_path(ep_path);
			if (ep_dn) {
				dev->endpoint = kgipc_endpoint_alloc(ep_dn);
				of_node_put(ep_dn);
				if (IS_ERR(dev->endpoint)) {
					ret = PTR_ERR(dev->endpoint);
					pr_err("alloc failed %d %s ret %d\n",
						dt_name_idx, mmid_device->name,
						ret);
				} else {
					pr_debug("gipc ep found for %d %s\n",
						dt_name_idx, mmid_device->name);
				}
			} else {
				pr_err("of_parse_phandle failed id %d %s\n",
					   dt_name_idx, mmid_device->name);
				ret = -ENOENT;
			}
		} else {
			pr_err("of_find_compatible_node failed id %d %s\n",
				   dt_name_idx, mmid_device->name);
			ret = -ENOENT;
		}
	}

exit:
	return ret;
}

int habhyp_commdev_create_dispatcher(struct physical_channel *pchan)
{
	struct ghs_vdev *dev = (struct ghs_vdev *)pchan->hyp_data;
	int ret = 0;

	tasklet_init(&dev->os_data->task, physical_channel_rx_dispatch,
		(unsigned long) pchan);

	ret = kgipc_endpoint_start_with_irq_callback(dev->endpoint,
		ghs_irq_handler,
		pchan);

	if (ret)
		pr_err("irq alloc failed id: %d %s, ret: %d\n",
				ghs_vmm_plugin_info.curr, pchan->name, ret);
	else
		pr_debug("ep irq handler started for %d %s, ret %d\n",
				ghs_vmm_plugin_info.curr, pchan->name, ret);

	return ret;
}

void habhyp_commdev_dealloc_os(void *commdev)
{
	struct physical_channel *pchan = (struct physical_channel *)commdev;
	struct ghs_vdev *dev = pchan->hyp_data;

	kgipc_endpoint_free(dev->endpoint);
}

int hab_hypervisor_register_os(void)
{
	ghs_vmm_plugin_info.probe_cnt = ARRAY_SIZE(dt_gipc_path_name);

	hab_driver.b_server_dom = 0;

	return 0;
}

void dump_hab_wq(struct physical_channel *pchan) {};
void hab_pipe_read_dump(struct physical_channel *pchan) {};
int hab_stat_log(struct physical_channel **pchans, int pchan_cnt, char *dest,
			int dest_size)
{
	return 0;
};
