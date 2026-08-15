// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2026 Intel Corporation */
#include <linux/dma-mapping.h>

#include "adf_admin.h"
#include "adf_cfg_services.h"
#include "adf_common_drv.h"
#include "adf_kpt.h"

static bool adf_kpt_supported(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);

	return hw_data->accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_KPT;
}

int adf_enable_kpt(struct adf_accel_dev *accel_dev)
{
	struct adf_kpt_interface_data *user_data = GET_KPT_USER_DATA(accel_dev);
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	dma_addr_t paddr;
	void *vaddr;
	int ret;
	int svc;

	/* Return 0 if KPT is not supported by the hardware */
	if (!adf_kpt_supported(accel_dev))
		return 0;

	if (!user_data->enable) {
		/* Disable KPT capability if user has not enabled it */
		hw_data->accel_capabilities_mask &= ~ICP_ACCEL_CAPABILITIES_KPT;
		return 0;
	}

	svc = adf_get_service_enabled(accel_dev);
	if (svc < 0)
		return svc;

	if (svc != SVC_ASYM) {
		dev_err(&GET_DEV(accel_dev),
			"KPT can only be enabled when service is configured as 'asym'\n");
		return -EINVAL;
	}

	vaddr = dma_alloc_coherent(&GET_DEV(accel_dev), PAGE_SIZE, &paddr,
				   GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;

	ret = adf_send_admin_kpt_init(accel_dev, vaddr, PAGE_SIZE, paddr);

	dma_free_coherent(&GET_DEV(accel_dev), PAGE_SIZE, vaddr, paddr);

	return ret;
}
