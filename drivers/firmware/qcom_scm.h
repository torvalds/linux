/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2010-2015,2019,2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __QCOM_SCM_INT_H
#define __QCOM_SCM_INT_H

enum qcom_scm_convention {
	SMC_CONVENTION_UNKNOWN,
	SMC_CONVENTION_LEGACY,
	SMC_CONVENTION_ARM_32,
	SMC_CONVENTION_ARM_64,
};

extern enum qcom_scm_convention qcom_scm_convention;

#define MAX_QCOM_SCM_ARGS 10
#define MAX_QCOM_SCM_RETS 3

enum qcom_scm_arg_types {
	QCOM_SCM_VAL,
	QCOM_SCM_RO,
	QCOM_SCM_RW,
	QCOM_SCM_BUFVAL,
};

#define QCOM_SCM_ARGS_IMPL(num, a, b, c, d, e, f, g, h, i, j, ...) (\
			   (((a) & 0x3) << 4) | \
			   (((b) & 0x3) << 6) | \
			   (((c) & 0x3) << 8) | \
			   (((d) & 0x3) << 10) | \
			   (((e) & 0x3) << 12) | \
			   (((f) & 0x3) << 14) | \
			   (((g) & 0x3) << 16) | \
			   (((h) & 0x3) << 18) | \
			   (((i) & 0x3) << 20) | \
			   (((j) & 0x3) << 22) | \
			   ((num) & 0xf))

#define QCOM_SCM_ARGS(...) QCOM_SCM_ARGS_IMPL(__VA_ARGS__, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)


/**
 * struct qcom_scm_desc
 * @arginfo:	Metadata describing the arguments in args[]
 * @args:	The array of arguments for the secure syscall
 */
struct qcom_scm_desc {
	u32 svc;
	u32 cmd;
	u32 arginfo;
	u64 args[MAX_QCOM_SCM_ARGS];
	u32 owner;
	bool multicall_allowed;
};

/**
 * struct qcom_scm_res
 * @result:	The values returned by the secure syscall
 */
struct qcom_scm_res {
	u64 result[MAX_QCOM_SCM_RETS];
};

enum qcom_scm_call_type {
	QCOM_SCM_CALL_NORMAL,
	QCOM_SCM_CALL_ATOMIC,
	QCOM_SCM_CALL_NORETRY,
};

enum qcom_scm_wq_feature {
	QCOM_SCM_SINGLE_SMC_ALLOW,
	QCOM_SCM_MULTI_SMC_WHITE_LIST_ALLOW, /* Release global lock for certain allowed SMC calls */
};

struct qcom_scm;
extern struct completion *qcom_scm_lookup_wq(struct qcom_scm *scm, u32 wq_ctx);
extern void scm_waitq_flag_handler(struct completion *wq, u32 flags);
extern int scm_get_wq_ctx(u32 *wq_ctx, u32 *flags, u32 *more_pending);
extern bool qcom_scm_multi_call_allow(struct device *dev, bool multicall_allowed);

#define SCM_SMC_FNID(s, c)	((((s) & 0xFF) << 8) | ((c) & 0xFF))
extern int __scm_smc_call(struct device *dev, const struct qcom_scm_desc *desc,
			  enum qcom_scm_convention qcom_convention,
			  struct qcom_scm_res *res,
			  enum qcom_scm_call_type call_type);
#define scm_smc_call(dev, desc, res, atomic) \
	__scm_smc_call((dev), (desc), qcom_scm_convention, (res), (atomic))

#define SCM_LEGACY_FNID(s, c)	(((s) << 10) | ((c) & 0x3ff))
extern int scm_legacy_call_atomic(struct device *dev,
				  const struct qcom_scm_desc *desc,
				  struct qcom_scm_res *res);
extern int scm_legacy_call(struct device *dev, const struct qcom_scm_desc *desc,
			   struct qcom_scm_res *res);

#define QCOM_SCM_SVC_BOOT		0x01
#define QCOM_SCM_BOOT_SET_ADDR		0x01
#define QCOM_SCM_BOOT_TERMINATE_PC	0x02
#define QCOM_SCM_BOOT_SET_DLOAD_MODE	0x10
#define QCOM_SCM_BOOT_SEC_WDOG_DIS		0x07
#define QCOM_SCM_BOOT_SEC_WDOG_TRIGGER		0x08
#define QCOM_SCM_BOOT_WDOG_DEBUG_PART		0x09
#define QCOM_SCM_BOOT_SET_REMOTE_STATE	0x0a
#define QCOM_SCM_BOOT_SPIN_CPU			0x0d
#define QCOM_SCM_BOOT_SWITCH_MODE		0x0f
#define QCOM_SCM_BOOT_SET_DLOAD_MODE	0x10
#define QCOM_SCM_BOOT_CONFIG_CPU_ERRATA		0x12
#define QCOM_SCM_QUSB2PHY_LVL_SHIFTER_CMD_ID    0x1B
#define QCOM_SCM_FLUSH_FLAG_MASK	0x3

#define QCOM_SCM_SVC_PIL		0x02
#define QCOM_SCM_PIL_PAS_INIT_IMAGE	0x01
#define QCOM_SCM_PIL_PAS_MEM_SETUP	0x02
#define QCOM_SCM_PIL_PAS_AUTH_AND_RESET	0x05
#define QCOM_SCM_PIL_PAS_SHUTDOWN	0x06
#define QCOM_SCM_PIL_PAS_IS_SUPPORTED	0x07
#define QCOM_SCM_PIL_PAS_MSS_RESET	0x0a
#define QCOM_SCM_SVC_UTIL			0x03
#define QCOM_SCM_UTIL_GET_SEC_DUMP_STATE	0x10
#define QCOM_SCM_UTIL_DUMP_TABLE_ASSIGN		0x13

#define QCOM_SCM_SVC_TZ				0x04
#define QOCM_SCM_TZ_BLSP_MODIFY_OWNER		0x03

#define QCOM_SCM_SVC_IO			0x05
#define QCOM_SCM_IO_READ		0x01
#define QCOM_SCM_IO_WRITE		0x02
#define QCOM_SCM_IO_RESET			0x03

#define QCOM_SCM_SVC_INFO		0x06
#define QCOM_SCM_INFO_IS_CALL_AVAIL	0x01
#define QCOM_SCM_INFO_GET_FEAT_VERSION_CMD	0x03

#define QCOM_SCM_SVC_PWR			0x09
#define QCOM_SCM_PWR_IO_DISABLE_PMIC_ARBITER	0x01
#define QCOM_SCM_PWR_IO_DEASSERT_PS_HOLD	0x02
#define QCOM_SCM_PWR_MMU_SYNC			0x08

#define QCOM_SCM_SVC_MP				0x0c
#define QCOM_SCM_MP_RESTORE_SEC_CFG		0x02
#define QCOM_SCM_MP_IOMMU_SECURE_PTBL_SIZE	0x03
#define QCOM_SCM_MP_IOMMU_SECURE_PTBL_INIT	0x04
#define QCOM_SCM_MP_VIDEO_VAR			0x08
#define QCOM_SCM_MP_MEM_PROTECT_REGION_ID		0x10
#define QCOM_SCM_MP_MEM_PROTECT_LOCK_ID2_FLAT		0x11
#define QCOM_SCM_MP_IOMMU_SECURE_MAP2_FLAT		0x12
#define QCOM_SCM_MP_IOMMU_SECURE_UNMAP2_FLAT		0x13
#define QCOM_SCM_MP_ASSIGN			0x16
#define QCOM_SCM_MP_CMD_SD_CTRL				0x18
#define QCOM_SCM_MP_CP_SMMU_APERTURE_ID			0x1b
#define QCOM_SCM_MEMP_SHM_BRIDGE_ENABLE			0x1c
#define QCOM_SCM_MEMP_SHM_BRIDGE_DELETE			0x1d
#define QCOM_SCM_MEMP_SHM_BRDIGE_CREATE			0x1e
#define QCOM_SCM_MP_SMMU_PREPARE_ATOS_ID		0x21
#define QCOM_SCM_MP_MPU_LOCK_NS_REGION			0x25
#define QCOM_SCM_IOMMU_TLBINVAL_FLAG    0x00000001
#define QCOM_SCM_CP_APERTURE_REG	0x0
#define QCOM_SCM_CP_LPAC_APERTURE_REG	0x1

#define QCOM_SCM_SVC_DCVS			0x0D
#define QCOM_SCM_DCVS_RESET			0x07
#define QCOM_SCM_DCVS_UPDATE			0x08
#define QCOM_SCM_DCVS_INIT			0x09
#define QCOM_SCM_DCVS_UPDATE_V2			0x0a
#define QCOM_SCM_DCVS_INIT_V2			0x0b
#define QCOM_SCM_DCVS_INIT_CA_V2		0x0c
#define QCOM_SCM_DCVS_UPDATE_CA_V2		0x0d

#define QCOM_SCM_SVC_OCMEM		0x0f
#define QCOM_SCM_OCMEM_LOCK_CMD		0x01
#define QCOM_SCM_OCMEM_UNLOCK_CMD	0x02

#define QCOM_SCM_SVC_ES				0x10 /* Enterprise Security */
#define QCOM_SCM_ES_INVALIDATE_ICE_KEY		0x03
#define QCOM_SCM_ES_CONFIG_SET_ICE_KEY		0x04
#define QCOM_SCM_ES_CONFIG_SET_ICE_KEY_V2	0x05
#define QCOM_SCM_ES_CLEAR_ICE_KEY		0x06
#define QCOM_SCM_ES_DERIVE_RAW_SECRET	0x07

#define QCOM_SCM_SVC_HDCP		0x11
#define QCOM_SCM_HDCP_INVOKE		0x01

#define QCOM_SCM_SVC_LMH			0x13
#define QCOM_SCM_LMH_LIMIT_PROFILE_CHANGE	0x01
#define QCOM_SCM_LMH_DEBUG_SET			0x08
#define QCOM_SCM_LMH_DEBUG_READ_BUF_SIZE	0x09
#define QCOM_SCM_LMH_LIMIT_DCVSH		0x10
#define QCOM_SCM_LMH_DEBUG_READ			0x0A
#define QCOM_SCM_LMH_DEBUG_GET_TYPE		0x0B
#define QCOM_SCM_LMH_DEBUG_FETCH_DATA		0x0D

#define QCOM_SCM_SVC_SMMU_PROGRAM		0x15
#define QCOM_SCM_SMMU_CHANGE_PGTBL_FORMAT	0x01
#define QCOM_SCM_SMMU_SECURE_LUT		0x03
#define QCOM_SCM_SMMU_CONFIG_ERRATA1		0x03
#define QCOM_SCM_SMMU_CONFIG_ERRATA1_CLIENT_ALL	0x02

#define QCOM_SCM_SVC_QDSS			0x16
#define QCOM_SCM_QDSS_INVOKE			0x01

#define QCOM_SCM_SVC_CAMERA			0x18
#define QCOM_SCM_CAMERA_PROTECT_ALL		0x06
#define QCOM_SCM_CAMERA_PROTECT_PHY_LANES	0x07

#define QCOM_SCM_SVC_WAITQ			0x24
#define QCOM_SCM_WAITQ_ACK			0x01
#define QCOM_SCM_WAITQ_RESUME			0x02
#define QCOM_SCM_WAITQ_GET_WQ_CTX		0x03
#define QCOM_SCM_GET_WQ_QUEUE_INFO		0x04
#define QCOM_SCM_SVC_TSENS			0x1E
#define QCOM_SCM_TSENS_INIT_ID			0x5

/* OEM Services and Function IDs */
#define QCOM_SCM_SVC_OEM_POWER			0x09
#define QCOM_SCM_OEM_POWER_REBOOT		0x22

/* GPU Service IDs */
#define QCOM_SCM_SVC_GPU		0x28
#define QCOM_SCM_SVC_GPU_INIT_REGS		0x1

/* TOS Services and Function IDs */
#define QCOM_SCM_SVC_QSEELOG			0x01
#define QCOM_SCM_QSEELOG_REGISTER		0x06
#define QCOM_SCM_QUERY_ENCR_LOG_FEAT_ID		0x0b
#define QCOM_SCM_REQUEST_ENCR_LOG_ID		0x0c

#define QCOM_SCM_SVC_KEYSTORE			0x05
#define QCOM_SCM_ICE_RESTORE_KEY_ID		0x06

#define QCOM_SCM_SVC_SMCINVOKE			0x06
#define QCOM_SCM_SMCINVOKE_INVOKE_LEGACY	0x00
#define QCOM_SCM_SMCINVOKE_INVOKE		0x02
#define QCOM_SCM_SMCINVOKE_CB_RSP		0x01

/* Feature IDs for QCOM_SCM_INFO_GET_FEAT_VERSION */
#define QCOM_SCM_TZ_DBG_ETM_FEAT_ID		0x08
#define QCOM_SCM_FEAT_LOG_ID			0x0a
#define QCOM_SCM_MP_CP_FEAT_ID			0x0c

#define QCOM_SCM_SVC_CPUCFG			0x29
#define QCOM_SCM_CPUCFG_PREFETCH_TGT_CMD	0x1

extern void __qcom_scm_init(void);
extern void __qcom_scm_qcpe_exit(void);

/* common error codes */
#define QCOM_SCM_V2_EBUSY	-12
#define QCOM_SCM_ENOMEM		-5
#define QCOM_SCM_EOPNOTSUPP	-4
#define QCOM_SCM_EINVAL_ADDR	-3
#define QCOM_SCM_EINVAL_ARG	-2
#define QCOM_SCM_ERROR		-1
#define QCOM_SCM_INTERRUPTED	1
#define QCOM_SCM_WAITQ_SLEEP	2
#define QCOM_SCM_WAITQ_WAKE	3

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
