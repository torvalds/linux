/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QCOM_PAS_INT_H
#define __QCOM_PAS_INT_H

struct device;

/**
 * struct qcom_pas_ops - Qcom Peripheral Authentication Service (PAS) ops
 * @drv_name:			PAS driver name.
 * @dev:			PAS device pointer.
 * @supported:			Peripheral supported callback.
 * @init_image:			Peripheral image initialization callback.
 * @mem_setup:			Peripheral memory setup callback.
 * @get_rsc_table:		Peripheral get resource table callback.
 * @prepare_and_auth_reset:	Peripheral prepare firmware authentication and
 *				reset callback.
 * @auth_and_reset:		Peripheral firmware authentication and reset
 *				callback.
 * @set_remote_state:		Peripheral set remote state callback.
 * @shutdown:			Peripheral shutdown callback.
 * @metadata_release:		Image metadata release callback.
 */
struct qcom_pas_ops {
	const char *drv_name;
	struct device *dev;
	bool (*supported)(struct device *dev, u32 pas_id);
	int (*init_image)(struct device *dev, u32 pas_id, const void *metadata,
			  size_t size, struct qcom_pas_context *ctx);
	int (*mem_setup)(struct device *dev, u32 pas_id, phys_addr_t addr,
			 phys_addr_t size);
	void *(*get_rsc_table)(struct device *dev, struct qcom_pas_context *ctx,
			       void *input_rt, size_t input_rt_size,
			       size_t *output_rt_size);
	int (*prepare_and_auth_reset)(struct device *dev,
				      struct qcom_pas_context *ctx);
	int (*auth_and_reset)(struct device *dev, u32 pas_id);
	int (*set_remote_state)(struct device *dev, u32 state, u32 pas_id);
	int (*shutdown)(struct device *dev, u32 pas_id);
	void (*metadata_release)(struct device *dev,
				 struct qcom_pas_context *ctx);
};

void qcom_pas_ops_register(struct qcom_pas_ops *ops);
void qcom_pas_ops_unregister(void);

#endif /* __QCOM_PAS_INT_H */
