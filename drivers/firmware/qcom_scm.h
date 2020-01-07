/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2010-2015,2019 The Linux Foundation. All rights reserved.
 */
#ifndef __QCOM_SCM_INT_H
#define __QCOM_SCM_INT_H

#define QCOM_SCM_SVC_BOOT		0x01
#define QCOM_SCM_BOOT_SET_ADDR		0x01
#define QCOM_SCM_BOOT_TERMINATE_PC	0x02
#define QCOM_SCM_BOOT_SET_DLOAD_MODE	0x10
#define QCOM_SCM_BOOT_SET_REMOTE_STATE	0x0a
extern int __qcom_scm_set_warm_boot_addr(struct device *dev, void *entry,
		const cpumask_t *cpus);
extern int __qcom_scm_set_cold_boot_addr(struct device *dev, void *entry,
		const cpumask_t *cpus);
extern void __qcom_scm_cpu_power_down(struct device *dev, u32 flags);
extern int __qcom_scm_set_remote_state(struct device *dev, u32 state, u32 id);
extern int __qcom_scm_set_dload_mode(struct device *dev, bool enable);
#define QCOM_SCM_FLUSH_FLAG_MASK	0x3

#define QCOM_SCM_SVC_PIL		0x02
#define QCOM_SCM_PIL_PAS_INIT_IMAGE	0x01
#define QCOM_SCM_PIL_PAS_MEM_SETUP	0x02
#define QCOM_SCM_PIL_PAS_AUTH_AND_RESET	0x05
#define QCOM_SCM_PIL_PAS_SHUTDOWN	0x06
#define QCOM_SCM_PIL_PAS_IS_SUPPORTED	0x07
#define QCOM_SCM_PIL_PAS_MSS_RESET	0x0a
extern bool __qcom_scm_pas_supported(struct device *dev, u32 peripheral);
extern int  __qcom_scm_pas_init_image(struct device *dev, u32 peripheral,
		dma_addr_t metadata_phys);
extern int  __qcom_scm_pas_mem_setup(struct device *dev, u32 peripheral,
		phys_addr_t addr, phys_addr_t size);
extern int  __qcom_scm_pas_auth_and_reset(struct device *dev, u32 peripheral);
extern int  __qcom_scm_pas_shutdown(struct device *dev, u32 peripheral);
extern int  __qcom_scm_pas_mss_reset(struct device *dev, bool reset);

#define QCOM_SCM_SVC_IO			0x05
#define QCOM_SCM_IO_READ		0x01
#define QCOM_SCM_IO_WRITE		0x02
extern int __qcom_scm_io_readl(struct device *dev, phys_addr_t addr, unsigned int *val);
extern int __qcom_scm_io_writel(struct device *dev, phys_addr_t addr, unsigned int val);

#define QCOM_SCM_SVC_INFO		0x06
#define QCOM_SCM_INFO_IS_CALL_AVAIL	0x01
extern int __qcom_scm_is_call_available(struct device *dev, u32 svc_id,
		u32 cmd_id);

#define QCOM_SCM_SVC_MP				0x0c
#define QCOM_SCM_MP_RESTORE_SEC_CFG		0x02
#define QCOM_SCM_MP_IOMMU_SECURE_PTBL_SIZE	0x03
#define QCOM_SCM_MP_IOMMU_SECURE_PTBL_INIT	0x04
#define QCOM_SCM_MP_ASSIGN			0x16
extern int __qcom_scm_restore_sec_cfg(struct device *dev, u32 device_id,
				      u32 spare);
extern int __qcom_scm_iommu_secure_ptbl_size(struct device *dev, u32 spare,
					     size_t *size);
extern int __qcom_scm_iommu_secure_ptbl_init(struct device *dev, u64 addr,
					     u32 size, u32 spare);
extern int  __qcom_scm_assign_mem(struct device *dev,
				  phys_addr_t mem_region, size_t mem_sz,
				  phys_addr_t src, size_t src_sz,
				  phys_addr_t dest, size_t dest_sz);

#define QCOM_SCM_SVC_OCMEM		0x0f
#define QCOM_SCM_OCMEM_LOCK_CMD		0x01
#define QCOM_SCM_OCMEM_UNLOCK_CMD	0x02
extern int __qcom_scm_ocmem_lock(struct device *dev, u32 id, u32 offset,
				 u32 size, u32 mode);
extern int __qcom_scm_ocmem_unlock(struct device *dev, u32 id, u32 offset,
				   u32 size);

#define QCOM_SCM_SVC_HDCP		0x11
#define QCOM_SCM_HDCP_INVOKE		0x01
extern int __qcom_scm_hdcp_req(struct device *dev,
		struct qcom_scm_hdcp_req *req, u32 req_cnt, u32 *resp);

#define QCOM_SCM_SVC_SMMU_PROGRAM		0x15
#define QCOM_SCM_SMMU_CONFIG_ERRATA1		0x03
#define QCOM_SCM_SMMU_CONFIG_ERRATA1_CLIENT_ALL	0x02
extern int __qcom_scm_qsmmu500_wait_safe_toggle(struct device *dev,
						bool enable);

extern void __qcom_scm_init(void);

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

#endif
