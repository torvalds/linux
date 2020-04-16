/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_INTEL_PMC_IPC_H_
#define  _ASM_X86_INTEL_PMC_IPC_H_

/* Commands */
#define PMC_IPC_USB_PWR_CTRL		0xF0
#define PMC_IPC_PMIC_BLACKLIST_SEL	0xEF
#define PMC_IPC_PHY_CONFIG		0xEE
#define PMC_IPC_NORTHPEAK_CTRL		0xED
#define PMC_IPC_PM_DEBUG		0xEC
#define PMC_IPC_PMC_FW_MSG_CTRL		0xEA

/* IPC return code */
#define IPC_ERR_NONE			0
#define IPC_ERR_CMD_NOT_SUPPORTED	1
#define IPC_ERR_CMD_NOT_SERVICED	2
#define IPC_ERR_UNABLE_TO_SERVICE	3
#define IPC_ERR_CMD_INVALID		4
#define IPC_ERR_CMD_FAILED		5
#define IPC_ERR_EMSECURITY		6
#define IPC_ERR_UNSIGNEDKERNEL		7

/* GCR reg offsets from gcr base*/
#define PMC_GCR_PMC_CFG_REG		0x08
#define PMC_GCR_TELEM_DEEP_S0IX_REG	0x78
#define PMC_GCR_TELEM_SHLW_S0IX_REG	0x80

#if IS_ENABLED(CONFIG_INTEL_PMC_IPC)

int intel_pmc_s0ix_counter_read(u64 *data);
int intel_pmc_gcr_read64(u32 offset, u64 *data);

#else

static inline int intel_pmc_s0ix_counter_read(u64 *data)
{
	return -EINVAL;
}

static inline int intel_pmc_gcr_read64(u32 offset, u64 *data)
{
	return -EINVAL;
}

#endif /*CONFIG_INTEL_PMC_IPC*/

#endif
