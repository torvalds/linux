/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_CTX_H_
#define _AMDXDNA_CTX_H_

/* Exec buffer command header format */
#define AMDXDNA_CMD_STATE		GENMASK(3, 0)
#define AMDXDNA_CMD_EXTRA_CU_MASK	GENMASK(11, 10)
#define AMDXDNA_CMD_COUNT		GENMASK(22, 12)
#define AMDXDNA_CMD_OPCODE		GENMASK(27, 23)
struct amdxdna_cmd {
	u32 header;
	u32 data[];
};

struct amdxdna_hwctx {
	struct amdxdna_client		*client;
	struct amdxdna_hwctx_priv	*priv;
	char				*name;

	u32				id;
	u32				max_opc;
	u32				num_tiles;
	u32				mem_size;
	u32				fw_ctx_id;
	u32				col_list_len;
	u32				*col_list;
	u32				start_col;
	u32				num_col;
#define HWCTX_STAT_INIT  0
#define HWCTX_STAT_READY 1
#define HWCTX_STAT_STOP  2
	u32				status;
	u32				old_status;

	struct amdxdna_qos_info		     qos;
	struct amdxdna_hwctx_param_config_cu *cus;
	u32				syncobj_hdl;
};

void amdxdna_hwctx_remove_all(struct amdxdna_client *client);
int amdxdna_drm_create_hwctx_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_config_hwctx_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int amdxdna_drm_destroy_hwctx_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);

#endif /* _AMDXDNA_CTX_H_ */
