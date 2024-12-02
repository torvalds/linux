/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef __GZVM_ARCH_COMMON_H__
#define __GZVM_ARCH_COMMON_H__

#include <linux/arm-smccc.h>

enum {
	GZVM_FUNC_CREATE_VM = 0,
	GZVM_FUNC_DESTROY_VM = 1,
	GZVM_FUNC_CREATE_VCPU = 2,
	GZVM_FUNC_DESTROY_VCPU = 3,
	GZVM_FUNC_SET_MEMREGION = 4,
	GZVM_FUNC_RUN = 5,
	GZVM_FUNC_GET_ONE_REG = 8,
	GZVM_FUNC_SET_ONE_REG = 9,
	GZVM_FUNC_IRQ_LINE = 10,
	GZVM_FUNC_CREATE_DEVICE = 11,
	GZVM_FUNC_PROBE = 12,
	GZVM_FUNC_ENABLE_CAP = 13,
	GZVM_FUNC_INFORM_EXIT = 14,
	GZVM_FUNC_MEMREGION_PURPOSE = 15,
	GZVM_FUNC_SET_DTB_CONFIG = 16,
	GZVM_FUNC_MAP_GUEST = 17,
	GZVM_FUNC_MAP_GUEST_BLOCK = 18,
	NR_GZVM_FUNC,
};

#define SMC_ENTITY_MTK			59
#define GZVM_FUNCID_START		(0x1000)
#define GZVM_HCALL_ID(func)						\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32,	\
			   SMC_ENTITY_MTK, (GZVM_FUNCID_START + (func)))

#define MT_HVC_GZVM_CREATE_VM		GZVM_HCALL_ID(GZVM_FUNC_CREATE_VM)
#define MT_HVC_GZVM_DESTROY_VM		GZVM_HCALL_ID(GZVM_FUNC_DESTROY_VM)
#define MT_HVC_GZVM_CREATE_VCPU		GZVM_HCALL_ID(GZVM_FUNC_CREATE_VCPU)
#define MT_HVC_GZVM_DESTROY_VCPU	GZVM_HCALL_ID(GZVM_FUNC_DESTROY_VCPU)
#define MT_HVC_GZVM_SET_MEMREGION	GZVM_HCALL_ID(GZVM_FUNC_SET_MEMREGION)
#define MT_HVC_GZVM_RUN			GZVM_HCALL_ID(GZVM_FUNC_RUN)
#define MT_HVC_GZVM_GET_ONE_REG		GZVM_HCALL_ID(GZVM_FUNC_GET_ONE_REG)
#define MT_HVC_GZVM_SET_ONE_REG		GZVM_HCALL_ID(GZVM_FUNC_SET_ONE_REG)
#define MT_HVC_GZVM_IRQ_LINE		GZVM_HCALL_ID(GZVM_FUNC_IRQ_LINE)
#define MT_HVC_GZVM_CREATE_DEVICE	GZVM_HCALL_ID(GZVM_FUNC_CREATE_DEVICE)
#define MT_HVC_GZVM_PROBE		GZVM_HCALL_ID(GZVM_FUNC_PROBE)
#define MT_HVC_GZVM_ENABLE_CAP		GZVM_HCALL_ID(GZVM_FUNC_ENABLE_CAP)
#define MT_HVC_GZVM_INFORM_EXIT		GZVM_HCALL_ID(GZVM_FUNC_INFORM_EXIT)
#define MT_HVC_GZVM_MEMREGION_PURPOSE	GZVM_HCALL_ID(GZVM_FUNC_MEMREGION_PURPOSE)
#define MT_HVC_GZVM_SET_DTB_CONFIG	GZVM_HCALL_ID(GZVM_FUNC_SET_DTB_CONFIG)
#define MT_HVC_GZVM_MAP_GUEST		GZVM_HCALL_ID(GZVM_FUNC_MAP_GUEST)
#define MT_HVC_GZVM_MAP_GUEST_BLOCK	GZVM_HCALL_ID(GZVM_FUNC_MAP_GUEST_BLOCK)

#define GIC_V3_NR_LRS			16

/**
 * gzvm_hypcall_wrapper() - the wrapper for hvc calls
 * @a0-a7: arguments passed in registers 0 to 7
 * @res: result values from registers 0 to 3
 *
 * Return: The wrapper helps caller to convert geniezone errno to Linux errno.
 */
static inline int gzvm_hypcall_wrapper(unsigned long a0, unsigned long a1,
				       unsigned long a2, unsigned long a3,
				       unsigned long a4, unsigned long a5,
				       unsigned long a6, unsigned long a7,
				       struct arm_smccc_res *res)
{
	arm_smccc_hvc(a0, a1, a2, a3, a4, a5, a6, a7, res);
	return gzvm_err_to_errno(res->a0);
}

static inline u16 get_vmid_from_tuple(unsigned int tuple)
{
	return (u16)(tuple >> 16);
}

static inline u16 get_vcpuid_from_tuple(unsigned int tuple)
{
	return (u16)(tuple & 0xffff);
}

/**
 * struct gzvm_vcpu_hwstate: Sync architecture state back to host for handling
 * @nr_lrs: The available LRs(list registers) in Soc.
 * @__pad: add an explicit '__u32 __pad;' in the middle to make it clear
 *         what the actual layout is.
 * @lr: The array of LRs(list registers).
 *
 * - Keep the same layout of hypervisor data struct.
 * - Sync list registers back for acking virtual device interrupt status.
 */
struct gzvm_vcpu_hwstate {
	__le32 nr_lrs;
	__le32 __pad;
	__le64 lr[GIC_V3_NR_LRS];
};

static inline unsigned int
assemble_vm_vcpu_tuple(u16 vmid, u16 vcpuid)
{
	return ((unsigned int)vmid << 16 | vcpuid);
}

static inline void
disassemble_vm_vcpu_tuple(unsigned int tuple, u16 *vmid, u16 *vcpuid)
{
	*vmid = get_vmid_from_tuple(tuple);
	*vcpuid = get_vcpuid_from_tuple(tuple);
}

#endif /* __GZVM_ARCH_COMMON_H__ */
