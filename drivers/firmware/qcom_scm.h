/* Copyright (c) 2010-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __QCOM_SCM_INT_H
#define __QCOM_SCM_INT_H

#define QCOM_SCM_SVC_BOOT		0x1
#define QCOM_SCM_BOOT_ADDR		0x1
#define QCOM_SCM_BOOT_ADDR_MC		0x11
#define QCOM_SCM_SET_REMOTE_STATE	0xa
extern int __qcom_scm_set_remote_state(struct device *dev, u32 state, u32 id);

#define QCOM_SCM_FLAG_HLOS		0x01
#define QCOM_SCM_FLAG_COLDBOOT_MC	0x02
#define QCOM_SCM_FLAG_WARMBOOT_MC	0x04
extern int __qcom_scm_set_warm_boot_addr(struct device *dev, void *entry,
		const cpumask_t *cpus);
extern int __qcom_scm_set_cold_boot_addr(void *entry, const cpumask_t *cpus);

#define QCOM_SCM_CMD_TERMINATE_PC	0x2
#define QCOM_SCM_FLUSH_FLAG_MASK	0x3
#define QCOM_SCM_CMD_CORE_HOTPLUGGED	0x10
extern void __qcom_scm_cpu_power_down(u32 flags);

#define QCOM_SCM_SVC_INFO		0x6
#define QCOM_IS_CALL_AVAIL_CMD		0x1
extern int __qcom_scm_is_call_available(struct device *dev, u32 svc_id,
		u32 cmd_id);

#define QCOM_SCM_SVC_HDCP		0x11
#define QCOM_SCM_CMD_HDCP		0x01
extern int __qcom_scm_hdcp_req(struct device *dev,
		struct qcom_scm_hdcp_req *req, u32 req_cnt, u32 *resp);

extern void __qcom_scm_init(void);

#define QCOM_SCM_SVC_PIL		0x2
#define QCOM_SCM_PAS_INIT_IMAGE_CMD	0x1
#define QCOM_SCM_PAS_MEM_SETUP_CMD	0x2
#define QCOM_SCM_PAS_AUTH_AND_RESET_CMD	0x5
#define QCOM_SCM_PAS_SHUTDOWN_CMD	0x6
#define QCOM_SCM_PAS_IS_SUPPORTED_CMD	0x7
#define QCOM_SCM_PAS_MSS_RESET		0xa
extern bool __qcom_scm_pas_supported(struct device *dev, u32 peripheral);
extern int  __qcom_scm_pas_init_image(struct device *dev, u32 peripheral,
		dma_addr_t metadata_phys);
extern int  __qcom_scm_pas_mem_setup(struct device *dev, u32 peripheral,
		phys_addr_t addr, phys_addr_t size);
extern int  __qcom_scm_pas_auth_and_reset(struct device *dev, u32 peripheral);
extern int  __qcom_scm_pas_shutdown(struct device *dev, u32 peripheral);
extern int  __qcom_scm_pas_mss_reset(struct device *dev, bool reset);

/* common error codes */
#define QCOM_SCM_V2_EBUSY	-12
#define QCOM_SCM_ENOMEM		-5
#define QCOM_SCM_EOPNOTSUPP	-4
#define QCOM_SCM_EINVAL_ADDR	-3
#define QCOM_SCM_EINVAL_ARG	-2
#define QCOM_SCM_ERROR		-1
#define QCOM_SCM_INTERRUPTED	1

static inline int qcom_scm_remap_error(int err)
{
	switch (err) {
	case QCOM_SCM_ERROR:
		return -EIO;
	case QCOM_SCM_EINVAL_ADDR:
	case QCOM_SCM_EINVAL_ARG:
		return -EINVAL;
	case QCOM_SCM_EOPNOTSUPP:
		return -EOPNOTSUPP;
	case QCOM_SCM_ENOMEM:
		return -ENOMEM;
	case QCOM_SCM_V2_EBUSY:
		return -EBUSY;
	}
	return -EINVAL;
}

#define QCOM_SCM_SVC_MP			0xc
#define QCOM_SCM_RESTORE_SEC_CFG	2
extern int __qcom_scm_restore_sec_cfg(struct device *dev, u32 device_id,
				      u32 spare);
#define QCOM_SCM_IOMMU_SECURE_PTBL_SIZE	3
#define QCOM_SCM_IOMMU_SECURE_PTBL_INIT	4
extern int __qcom_scm_iommu_secure_ptbl_size(struct device *dev, u32 spare,
					     size_t *size);
extern int __qcom_scm_iommu_secure_ptbl_init(struct device *dev, u64 addr,
					     u32 size, u32 spare);

#endif
