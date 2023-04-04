/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __HAB_GHS_H
#define __HAB_GHS_H

#include "hab_ghs_os.h"

#define GIPC_RECV_BUFF_SIZE_BYTES   (256*1024)

struct ghs_vdev {
	int be;
	void *read_data; /* buffer to receive from gipc */
	size_t read_size;
	int read_offset;
	GIPC_Endpoint endpoint;
	spinlock_t io_lock;
	char name[32];
	struct ghs_vdev_os *os_data; /* os-specific for this comm dev */
};

struct ghs_vmm_plugin_info_s {
	const char * const *dt_name;
	int *mmid_dt_mapping;
	int curr;
	int probe_cnt;
};

extern struct ghs_vmm_plugin_info_s ghs_vmm_plugin_info;
extern const char * const dt_gipc_path_name[];

int get_dt_name_idx(int vmid_base, int mmid,
		struct ghs_vmm_plugin_info_s *plugin_info);

int hab_gipc_wait_to_send(GIPC_Endpoint endpoint);
int hab_gipc_ep_attach(int is_be, char *name, int vmid_remote,
		struct hab_device *mmid_device, struct ghs_vdev *dev);
#endif /* __HAB_GHS_H */
