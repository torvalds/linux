// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015,2019 The Linux Foundation. All rights reserved.
 */

#include <linux/io.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/qcom_scm.h>
#include <linux/arm-smccc.h>
#include <linux/dma-mapping.h>

#include "qcom_scm.h"

#define SCM_SMC_FNID(s, c) ((((s) & 0xFF) << 8) | ((c) & 0xFF))

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
 * @res:	The values returned by the secure syscall
 */
struct qcom_scm_desc {
	u32 arginfo;
	u64 args[MAX_QCOM_SCM_ARGS];
};

static u64 qcom_smccc_convention = -1;
static DEFINE_MUTEX(qcom_scm_lock);

#define QCOM_SCM_EBUSY_WAIT_MS 30
#define QCOM_SCM_EBUSY_MAX_RETRY 20

#define SCM_SMC_N_REG_ARGS	4
#define SCM_SMC_FIRST_EXT_IDX	(SCM_SMC_N_REG_ARGS - 1)
#define SCM_SMC_N_EXT_ARGS	(MAX_QCOM_SCM_ARGS - SCM_SMC_N_REG_ARGS + 1)

static void __scm_smc_do_quirk(const struct qcom_scm_desc *desc,
			       struct arm_smccc_res *res, u32 fn_id,
			       u64 x5, u32 type)
{
	u64 cmd;
	struct arm_smccc_quirk quirk = { .id = ARM_SMCCC_QUIRK_QCOM_A6 };

	cmd = ARM_SMCCC_CALL_VAL(type, qcom_smccc_convention,
				 ARM_SMCCC_OWNER_SIP, fn_id);

	quirk.state.a6 = 0;

	do {
		arm_smccc_smc_quirk(cmd, desc->arginfo, desc->args[0],
				    desc->args[1], desc->args[2], x5,
				    quirk.state.a6, 0, res, &quirk);

		if (res->a0 == QCOM_SCM_INTERRUPTED)
			cmd = res->a0;

	} while (res->a0 == QCOM_SCM_INTERRUPTED);
}

static void __scm_smc_do(const struct qcom_scm_desc *desc,
			     struct arm_smccc_res *res, u32 fn_id,
			     u64 x5, bool atomic)
{
	int retry_count = 0;

	if (atomic) {
		__scm_smc_do_quirk(desc, res, fn_id, x5,
					 ARM_SMCCC_FAST_CALL);
		return;
	}

	do {
		mutex_lock(&qcom_scm_lock);

		__scm_smc_do_quirk(desc, res, fn_id, x5,
					 ARM_SMCCC_STD_CALL);

		mutex_unlock(&qcom_scm_lock);

		if (res->a0 == QCOM_SCM_V2_EBUSY) {
			if (retry_count++ > QCOM_SCM_EBUSY_MAX_RETRY)
				break;
			msleep(QCOM_SCM_EBUSY_WAIT_MS);
		}
	}  while (res->a0 == QCOM_SCM_V2_EBUSY);
}

static int __scm_smc_call(struct device *dev, u32 svc_id, u32 cmd_id,
				  const struct qcom_scm_desc *desc,
				  struct arm_smccc_res *res, bool atomic)
{
	int arglen = desc->arginfo & 0xf;
	int i;
	u32 fn_id = SCM_SMC_FNID(svc_id, cmd_id);
	u64 x5 = desc->args[SCM_SMC_FIRST_EXT_IDX];
	dma_addr_t args_phys = 0;
	void *args_virt = NULL;
	size_t alloc_len;
	gfp_t flag = atomic ? GFP_ATOMIC : GFP_KERNEL;

	if (unlikely(arglen > SCM_SMC_N_REG_ARGS)) {
		alloc_len = SCM_SMC_N_EXT_ARGS * sizeof(u64);
		args_virt = kzalloc(PAGE_ALIGN(alloc_len), flag);

		if (!args_virt)
			return -ENOMEM;

		if (qcom_smccc_convention == ARM_SMCCC_SMC_32) {
			__le32 *args = args_virt;

			for (i = 0; i < SCM_SMC_N_EXT_ARGS; i++)
				args[i] = cpu_to_le32(desc->args[i +
						      SCM_SMC_FIRST_EXT_IDX]);
		} else {
			__le64 *args = args_virt;

			for (i = 0; i < SCM_SMC_N_EXT_ARGS; i++)
				args[i] = cpu_to_le64(desc->args[i +
						      SCM_SMC_FIRST_EXT_IDX]);
		}

		args_phys = dma_map_single(dev, args_virt, alloc_len,
					   DMA_TO_DEVICE);

		if (dma_mapping_error(dev, args_phys)) {
			kfree(args_virt);
			return -ENOMEM;
		}

		x5 = args_phys;
	}

	__scm_smc_do(desc, res, fn_id, x5, atomic);

	if (args_virt) {
		dma_unmap_single(dev, args_phys, alloc_len, DMA_TO_DEVICE);
		kfree(args_virt);
	}

	if ((long)res->a0 < 0)
		return qcom_scm_remap_error(res->a0);

	return 0;
}

/**
 * qcom_scm_call() - Invoke a syscall in the secure world
 * @dev:	device
 * @svc_id:	service identifier
 * @cmd_id:	command identifier
 * @desc:	Descriptor structure containing arguments and return values
 *
 * Sends a command to the SCM and waits for the command to finish processing.
 * This should *only* be called in pre-emptible context.
 */
static int qcom_scm_call(struct device *dev, u32 svc_id, u32 cmd_id,
			 const struct qcom_scm_desc *desc,
			 struct arm_smccc_res *res)
{
	might_sleep();
	return __scm_smc_call(dev, svc_id, cmd_id, desc, res, false);
}

/**
 * qcom_scm_call_atomic() - atomic variation of qcom_scm_call()
 * @dev:	device
 * @svc_id:	service identifier
 * @cmd_id:	command identifier
 * @desc:	Descriptor structure containing arguments and return values
 * @res:	Structure containing results from SMC/HVC call
 *
 * Sends a command to the SCM and waits for the command to finish processing.
 * This can be called in atomic context.
 */
static int qcom_scm_call_atomic(struct device *dev, u32 svc_id, u32 cmd_id,
				const struct qcom_scm_desc *desc,
				struct arm_smccc_res *res)
{
	return __scm_smc_call(dev, svc_id, cmd_id, desc, res, true);
}

/**
 * qcom_scm_set_cold_boot_addr() - Set the cold boot address for cpus
 * @entry: Entry point function for the cpus
 * @cpus: The cpumask of cpus that will use the entry point
 *
 * Set the cold boot address of the cpus. Any cpu outside the supported
 * range would be removed from the cpu present mask.
 */
int __qcom_scm_set_cold_boot_addr(void *entry, const cpumask_t *cpus)
{
	return -ENOTSUPP;
}

/**
 * qcom_scm_set_warm_boot_addr() - Set the warm boot address for cpus
 * @dev: Device pointer
 * @entry: Entry point function for the cpus
 * @cpus: The cpumask of cpus that will use the entry point
 *
 * Set the Linux entry point for the SCM to transfer control to when coming
 * out of a power down. CPU power down may be executed on cpuidle or hotplug.
 */
int __qcom_scm_set_warm_boot_addr(struct device *dev, void *entry,
				  const cpumask_t *cpus)
{
	return -ENOTSUPP;
}

/**
 * qcom_scm_cpu_power_down() - Power down the cpu
 * @flags - Flags to flush cache
 *
 * This is an end point to power down cpu. If there was a pending interrupt,
 * the control would return from this function, otherwise, the cpu jumps to the
 * warm boot entry point set for this cpu upon reset.
 */
void __qcom_scm_cpu_power_down(u32 flags)
{
}

int __qcom_scm_is_call_available(struct device *dev, u32 svc_id, u32 cmd_id)
{
	int ret;
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	desc.arginfo = QCOM_SCM_ARGS(1);
	desc.args[0] = SCM_SMC_FNID(svc_id, cmd_id) |
			(ARM_SMCCC_OWNER_SIP << ARM_SMCCC_OWNER_SHIFT);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_INFO, QCOM_SCM_INFO_IS_CALL_AVAIL,
			    &desc, &res);

	return ret ? : res.a1;
}

int __qcom_scm_hdcp_req(struct device *dev, struct qcom_scm_hdcp_req *req,
			u32 req_cnt, u32 *resp)
{
	int ret;
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	if (req_cnt > QCOM_SCM_HDCP_MAX_REQ_CNT)
		return -ERANGE;

	desc.args[0] = req[0].addr;
	desc.args[1] = req[0].val;
	desc.args[2] = req[1].addr;
	desc.args[3] = req[1].val;
	desc.args[4] = req[2].addr;
	desc.args[5] = req[2].val;
	desc.args[6] = req[3].addr;
	desc.args[7] = req[3].val;
	desc.args[8] = req[4].addr;
	desc.args[9] = req[4].val;
	desc.arginfo = QCOM_SCM_ARGS(10);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_HDCP, QCOM_SCM_HDCP_INVOKE, &desc,
			    &res);
	*resp = res.a1;

	return ret;
}

int __qcom_scm_ocmem_lock(struct device *dev, uint32_t id, uint32_t offset,
			  uint32_t size, uint32_t mode)
{
	return -ENOTSUPP;
}

int __qcom_scm_ocmem_unlock(struct device *dev, uint32_t id, uint32_t offset,
			    uint32_t size)
{
	return -ENOTSUPP;
}

void __qcom_scm_init(void)
{
	u64 cmd;
	struct arm_smccc_res res;
	u32 fnid = SCM_SMC_FNID(QCOM_SCM_SVC_INFO, QCOM_SCM_INFO_IS_CALL_AVAIL);

	/* First try a SMC64 call */
	cmd = ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64,
				 ARM_SMCCC_OWNER_SIP, fnid);

	arm_smccc_smc(cmd, QCOM_SCM_ARGS(1), cmd & (~BIT(ARM_SMCCC_TYPE_SHIFT)),
		      0, 0, 0, 0, 0, &res);

	if (!res.a0 && res.a1)
		qcom_smccc_convention = ARM_SMCCC_SMC_64;
	else
		qcom_smccc_convention = ARM_SMCCC_SMC_32;
}

bool __qcom_scm_pas_supported(struct device *dev, u32 peripheral)
{
	int ret;
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	desc.args[0] = peripheral;
	desc.arginfo = QCOM_SCM_ARGS(1);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_PIL,
				QCOM_SCM_PIL_PAS_IS_SUPPORTED,
				&desc, &res);

	return ret ? false : !!res.a1;
}

int __qcom_scm_pas_init_image(struct device *dev, u32 peripheral,
			      dma_addr_t metadata_phys)
{
	int ret;
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	desc.args[0] = peripheral;
	desc.args[1] = metadata_phys;
	desc.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_VAL, QCOM_SCM_RW);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_INIT_IMAGE,
				&desc, &res);

	return ret ? : res.a1;
}

int __qcom_scm_pas_mem_setup(struct device *dev, u32 peripheral,
			      phys_addr_t addr, phys_addr_t size)
{
	int ret;
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	desc.args[0] = peripheral;
	desc.args[1] = addr;
	desc.args[2] = size;
	desc.arginfo = QCOM_SCM_ARGS(3);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_MEM_SETUP,
				&desc, &res);

	return ret ? : res.a1;
}

int __qcom_scm_pas_auth_and_reset(struct device *dev, u32 peripheral)
{
	int ret;
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	desc.args[0] = peripheral;
	desc.arginfo = QCOM_SCM_ARGS(1);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_PIL,
				QCOM_SCM_PIL_PAS_AUTH_AND_RESET,
				&desc, &res);

	return ret ? : res.a1;
}

int __qcom_scm_pas_shutdown(struct device *dev, u32 peripheral)
{
	int ret;
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	desc.args[0] = peripheral;
	desc.arginfo = QCOM_SCM_ARGS(1);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_SHUTDOWN,
			&desc, &res);

	return ret ? : res.a1;
}

int __qcom_scm_pas_mss_reset(struct device *dev, bool reset)
{
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;
	int ret;

	desc.args[0] = reset;
	desc.args[1] = 0;
	desc.arginfo = QCOM_SCM_ARGS(2);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_MSS_RESET, &desc,
			    &res);

	return ret ? : res.a1;
}

int __qcom_scm_set_remote_state(struct device *dev, u32 state, u32 id)
{
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;
	int ret;

	desc.args[0] = state;
	desc.args[1] = id;
	desc.arginfo = QCOM_SCM_ARGS(2);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_BOOT, QCOM_SCM_BOOT_SET_REMOTE_STATE,
			    &desc, &res);

	return ret ? : res.a1;
}

int __qcom_scm_assign_mem(struct device *dev, phys_addr_t mem_region,
			  size_t mem_sz, phys_addr_t src, size_t src_sz,
			  phys_addr_t dest, size_t dest_sz)
{
	int ret;
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	desc.args[0] = mem_region;
	desc.args[1] = mem_sz;
	desc.args[2] = src;
	desc.args[3] = src_sz;
	desc.args[4] = dest;
	desc.args[5] = dest_sz;
	desc.args[6] = 0;

	desc.arginfo = QCOM_SCM_ARGS(7, QCOM_SCM_RO, QCOM_SCM_VAL,
				     QCOM_SCM_RO, QCOM_SCM_VAL, QCOM_SCM_RO,
				     QCOM_SCM_VAL, QCOM_SCM_VAL);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_MP,
			    QCOM_SCM_MP_ASSIGN,
			    &desc, &res);

	return ret ? : res.a1;
}

int __qcom_scm_restore_sec_cfg(struct device *dev, u32 device_id, u32 spare)
{
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;
	int ret;

	desc.args[0] = device_id;
	desc.args[1] = spare;
	desc.arginfo = QCOM_SCM_ARGS(2);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_MP, QCOM_SCM_MP_RESTORE_SEC_CFG,
			    &desc, &res);

	return ret ? : res.a1;
}

int __qcom_scm_iommu_secure_ptbl_size(struct device *dev, u32 spare,
				      size_t *size)
{
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;
	int ret;

	desc.args[0] = spare;
	desc.arginfo = QCOM_SCM_ARGS(1);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_MP,
			    QCOM_SCM_MP_IOMMU_SECURE_PTBL_SIZE, &desc, &res);

	if (size)
		*size = res.a1;

	return ret ? : res.a2;
}

int __qcom_scm_iommu_secure_ptbl_init(struct device *dev, u64 addr, u32 size,
				      u32 spare)
{
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;
	int ret;

	desc.args[0] = addr;
	desc.args[1] = size;
	desc.args[2] = spare;
	desc.arginfo = QCOM_SCM_ARGS(3, QCOM_SCM_RW, QCOM_SCM_VAL,
				     QCOM_SCM_VAL);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_MP,
			    QCOM_SCM_MP_IOMMU_SECURE_PTBL_INIT, &desc, &res);

	/* the pg table has been initialized already, ignore the error */
	if (ret == -EPERM)
		ret = 0;

	return ret;
}

int __qcom_scm_set_dload_mode(struct device *dev, bool enable)
{
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	desc.args[0] = QCOM_SCM_BOOT_SET_DLOAD_MODE;
	desc.args[1] = enable ? QCOM_SCM_BOOT_SET_DLOAD_MODE : 0;
	desc.arginfo = QCOM_SCM_ARGS(2);

	return qcom_scm_call(dev, QCOM_SCM_SVC_BOOT, QCOM_SCM_BOOT_SET_DLOAD_MODE,
			     &desc, &res);
}

int __qcom_scm_io_readl(struct device *dev, phys_addr_t addr,
			unsigned int *val)
{
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;
	int ret;

	desc.args[0] = addr;
	desc.arginfo = QCOM_SCM_ARGS(1);

	ret = qcom_scm_call(dev, QCOM_SCM_SVC_IO, QCOM_SCM_IO_READ,
			    &desc, &res);
	if (ret >= 0)
		*val = res.a1;

	return ret < 0 ? ret : 0;
}

int __qcom_scm_io_writel(struct device *dev, phys_addr_t addr, unsigned int val)
{
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	desc.args[0] = addr;
	desc.args[1] = val;
	desc.arginfo = QCOM_SCM_ARGS(2);

	return qcom_scm_call(dev, QCOM_SCM_SVC_IO, QCOM_SCM_IO_WRITE,
			     &desc, &res);
}

int __qcom_scm_qsmmu500_wait_safe_toggle(struct device *dev, bool en)
{
	struct qcom_scm_desc desc = {0};
	struct arm_smccc_res res;

	desc.args[0] = QCOM_SCM_SMMU_CONFIG_ERRATA1_CLIENT_ALL;
	desc.args[1] = en;
	desc.arginfo = QCOM_SCM_ARGS(2);

	return qcom_scm_call_atomic(dev, QCOM_SCM_SVC_SMMU_PROGRAM,
				    QCOM_SCM_SMMU_CONFIG_ERRATA1, &desc, &res);
}
