/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#ifndef _ASM_RISCV_SBI_H
#define _ASM_RISCV_SBI_H

#include <linux/types.h>

#ifdef CONFIG_RISCV_SBI
enum sbi_ext_id {
#ifdef CONFIG_RISCV_SBI_V01
	SBI_EXT_0_1_SET_TIMER = 0x0,
	SBI_EXT_0_1_CONSOLE_PUTCHAR = 0x1,
	SBI_EXT_0_1_CONSOLE_GETCHAR = 0x2,
	SBI_EXT_0_1_CLEAR_IPI = 0x3,
	SBI_EXT_0_1_SEND_IPI = 0x4,
	SBI_EXT_0_1_REMOTE_FENCE_I = 0x5,
	SBI_EXT_0_1_REMOTE_SFENCE_VMA = 0x6,
	SBI_EXT_0_1_REMOTE_SFENCE_VMA_ASID = 0x7,
	SBI_EXT_0_1_SHUTDOWN = 0x8,
#endif
	SBI_EXT_BASE = 0x10,
	SBI_EXT_TIME = 0x54494D45,
	SBI_EXT_IPI = 0x735049,
	SBI_EXT_RFENCE = 0x52464E43,
	SBI_EXT_HSM = 0x48534D,
	SBI_EXT_SRST = 0x53525354,
	SBI_EXT_PMU = 0x504D55,
};

enum sbi_ext_base_fid {
	SBI_EXT_BASE_GET_SPEC_VERSION = 0,
	SBI_EXT_BASE_GET_IMP_ID,
	SBI_EXT_BASE_GET_IMP_VERSION,
	SBI_EXT_BASE_PROBE_EXT,
	SBI_EXT_BASE_GET_MVENDORID,
	SBI_EXT_BASE_GET_MARCHID,
	SBI_EXT_BASE_GET_MIMPID,
};

enum sbi_ext_time_fid {
	SBI_EXT_TIME_SET_TIMER = 0,
};

enum sbi_ext_ipi_fid {
	SBI_EXT_IPI_SEND_IPI = 0,
};

enum sbi_ext_rfence_fid {
	SBI_EXT_RFENCE_REMOTE_FENCE_I = 0,
	SBI_EXT_RFENCE_REMOTE_SFENCE_VMA,
	SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID,
	SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA_VMID,
	SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA,
	SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA_ASID,
	SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA,
};

enum sbi_ext_hsm_fid {
	SBI_EXT_HSM_HART_START = 0,
	SBI_EXT_HSM_HART_STOP,
	SBI_EXT_HSM_HART_STATUS,
	SBI_EXT_HSM_HART_SUSPEND,
};

enum sbi_hsm_hart_state {
	SBI_HSM_STATE_STARTED = 0,
	SBI_HSM_STATE_STOPPED,
	SBI_HSM_STATE_START_PENDING,
	SBI_HSM_STATE_STOP_PENDING,
	SBI_HSM_STATE_SUSPENDED,
	SBI_HSM_STATE_SUSPEND_PENDING,
	SBI_HSM_STATE_RESUME_PENDING,
};
#define SBI_HSM_SUSP_BASE_MASK			0x7fffffff
#define SBI_HSM_SUSP_NON_RET_BIT		0x80000000
#define SBI_HSM_SUSP_PLAT_BASE			0x10000000

#define SBI_HSM_SUSPEND_RET_DEFAULT		0x00000000
#define SBI_HSM_SUSPEND_RET_PLATFORM		SBI_HSM_SUSP_PLAT_BASE
#define SBI_HSM_SUSPEND_RET_LAST		SBI_HSM_SUSP_BASE_MASK
#define SBI_HSM_SUSPEND_NON_RET_DEFAULT		SBI_HSM_SUSP_NON_RET_BIT
#define SBI_HSM_SUSPEND_NON_RET_PLATFORM	(SBI_HSM_SUSP_NON_RET_BIT | \
						 SBI_HSM_SUSP_PLAT_BASE)
#define SBI_HSM_SUSPEND_NON_RET_LAST		(SBI_HSM_SUSP_NON_RET_BIT | \
						 SBI_HSM_SUSP_BASE_MASK)

enum sbi_ext_srst_fid {
	SBI_EXT_SRST_RESET = 0,
};

enum sbi_srst_reset_type {
	SBI_SRST_RESET_TYPE_SHUTDOWN = 0,
	SBI_SRST_RESET_TYPE_COLD_REBOOT,
	SBI_SRST_RESET_TYPE_WARM_REBOOT,
};

enum sbi_srst_reset_reason {
	SBI_SRST_RESET_REASON_NONE = 0,
	SBI_SRST_RESET_REASON_SYS_FAILURE,
};

enum sbi_ext_pmu_fid {
	SBI_EXT_PMU_NUM_COUNTERS = 0,
	SBI_EXT_PMU_COUNTER_GET_INFO,
	SBI_EXT_PMU_COUNTER_CFG_MATCH,
	SBI_EXT_PMU_COUNTER_START,
	SBI_EXT_PMU_COUNTER_STOP,
	SBI_EXT_PMU_COUNTER_FW_READ,
};

#define RISCV_PMU_RAW_EVENT_MASK GENMASK_ULL(55, 0)
#define RISCV_PMU_RAW_EVENT_IDX 0x20000

/** General pmu event codes specified in SBI PMU extension */
enum sbi_pmu_hw_generic_events_t {
	SBI_PMU_HW_NO_EVENT			= 0,
	SBI_PMU_HW_CPU_CYCLES			= 1,
	SBI_PMU_HW_INSTRUCTIONS			= 2,
	SBI_PMU_HW_CACHE_REFERENCES		= 3,
	SBI_PMU_HW_CACHE_MISSES			= 4,
	SBI_PMU_HW_BRANCH_INSTRUCTIONS		= 5,
	SBI_PMU_HW_BRANCH_MISSES		= 6,
	SBI_PMU_HW_BUS_CYCLES			= 7,
	SBI_PMU_HW_STALLED_CYCLES_FRONTEND	= 8,
	SBI_PMU_HW_STALLED_CYCLES_BACKEND	= 9,
	SBI_PMU_HW_REF_CPU_CYCLES		= 10,

	SBI_PMU_HW_GENERAL_MAX,
};

/**
 * Special "firmware" events provided by the firmware, even if the hardware
 * does not support performance events. These events are encoded as a raw
 * event type in Linux kernel perf framework.
 */
enum sbi_pmu_fw_generic_events_t {
	SBI_PMU_FW_MISALIGNED_LOAD	= 0,
	SBI_PMU_FW_MISALIGNED_STORE	= 1,
	SBI_PMU_FW_ACCESS_LOAD		= 2,
	SBI_PMU_FW_ACCESS_STORE		= 3,
	SBI_PMU_FW_ILLEGAL_INSN		= 4,
	SBI_PMU_FW_SET_TIMER		= 5,
	SBI_PMU_FW_IPI_SENT		= 6,
	SBI_PMU_FW_IPI_RECVD		= 7,
	SBI_PMU_FW_FENCE_I_SENT		= 8,
	SBI_PMU_FW_FENCE_I_RECVD	= 9,
	SBI_PMU_FW_SFENCE_VMA_SENT	= 10,
	SBI_PMU_FW_SFENCE_VMA_RCVD	= 11,
	SBI_PMU_FW_SFENCE_VMA_ASID_SENT	= 12,
	SBI_PMU_FW_SFENCE_VMA_ASID_RCVD	= 13,

	SBI_PMU_FW_HFENCE_GVMA_SENT	= 14,
	SBI_PMU_FW_HFENCE_GVMA_RCVD	= 15,
	SBI_PMU_FW_HFENCE_GVMA_VMID_SENT = 16,
	SBI_PMU_FW_HFENCE_GVMA_VMID_RCVD = 17,

	SBI_PMU_FW_HFENCE_VVMA_SENT	= 18,
	SBI_PMU_FW_HFENCE_VVMA_RCVD	= 19,
	SBI_PMU_FW_HFENCE_VVMA_ASID_SENT = 20,
	SBI_PMU_FW_HFENCE_VVMA_ASID_RCVD = 21,
	SBI_PMU_FW_MAX,
};

/* SBI PMU event types */
enum sbi_pmu_event_type {
	SBI_PMU_EVENT_TYPE_HW = 0x0,
	SBI_PMU_EVENT_TYPE_CACHE = 0x1,
	SBI_PMU_EVENT_TYPE_RAW = 0x2,
	SBI_PMU_EVENT_TYPE_FW = 0xf,
};

/* SBI PMU event types */
enum sbi_pmu_ctr_type {
	SBI_PMU_CTR_TYPE_HW = 0x0,
	SBI_PMU_CTR_TYPE_FW,
};

/* Flags defined for config matching function */
#define SBI_PMU_CFG_FLAG_SKIP_MATCH	(1 << 0)
#define SBI_PMU_CFG_FLAG_CLEAR_VALUE	(1 << 1)
#define SBI_PMU_CFG_FLAG_AUTO_START	(1 << 2)
#define SBI_PMU_CFG_FLAG_SET_VUINH	(1 << 3)
#define SBI_PMU_CFG_FLAG_SET_VSNH	(1 << 4)
#define SBI_PMU_CFG_FLAG_SET_UINH	(1 << 5)
#define SBI_PMU_CFG_FLAG_SET_SINH	(1 << 6)
#define SBI_PMU_CFG_FLAG_SET_MINH	(1 << 7)

/* Flags defined for counter start function */
#define SBI_PMU_START_FLAG_SET_INIT_VALUE (1 << 0)

/* Flags defined for counter stop function */
#define SBI_PMU_STOP_FLAG_RESET (1 << 0)

#define SBI_SPEC_VERSION_DEFAULT	0x1
#define SBI_SPEC_VERSION_MAJOR_SHIFT	24
#define SBI_SPEC_VERSION_MAJOR_MASK	0x7f
#define SBI_SPEC_VERSION_MINOR_MASK	0xffffff

/* SBI return error codes */
#define SBI_SUCCESS		0
#define SBI_ERR_FAILURE		-1
#define SBI_ERR_NOT_SUPPORTED	-2
#define SBI_ERR_INVALID_PARAM	-3
#define SBI_ERR_DENIED		-4
#define SBI_ERR_INVALID_ADDRESS	-5
#define SBI_ERR_ALREADY_AVAILABLE -6
#define SBI_ERR_ALREADY_STARTED -7
#define SBI_ERR_ALREADY_STOPPED -8

extern unsigned long sbi_spec_version;
struct sbiret {
	long error;
	long value;
};

void sbi_init(void);
struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
			unsigned long arg1, unsigned long arg2,
			unsigned long arg3, unsigned long arg4,
			unsigned long arg5);

void sbi_console_putchar(int ch);
int sbi_console_getchar(void);
long sbi_get_mvendorid(void);
long sbi_get_marchid(void);
long sbi_get_mimpid(void);
void sbi_set_timer(uint64_t stime_value);
void sbi_shutdown(void);
void sbi_clear_ipi(void);
int sbi_send_ipi(const unsigned long *hart_mask);
int sbi_remote_fence_i(const unsigned long *hart_mask);
int sbi_remote_sfence_vma(const unsigned long *hart_mask,
			   unsigned long start,
			   unsigned long size);

int sbi_remote_sfence_vma_asid(const unsigned long *hart_mask,
				unsigned long start,
				unsigned long size,
				unsigned long asid);
int sbi_remote_hfence_gvma(const unsigned long *hart_mask,
			   unsigned long start,
			   unsigned long size);
int sbi_remote_hfence_gvma_vmid(const unsigned long *hart_mask,
				unsigned long start,
				unsigned long size,
				unsigned long vmid);
int sbi_remote_hfence_vvma(const unsigned long *hart_mask,
			   unsigned long start,
			   unsigned long size);
int sbi_remote_hfence_vvma_asid(const unsigned long *hart_mask,
				unsigned long start,
				unsigned long size,
				unsigned long asid);
int sbi_probe_extension(int ext);

/* Check if current SBI specification version is 0.1 or not */
static inline int sbi_spec_is_0_1(void)
{
	return (sbi_spec_version == SBI_SPEC_VERSION_DEFAULT) ? 1 : 0;
}

/* Get the major version of SBI */
static inline unsigned long sbi_major_version(void)
{
	return (sbi_spec_version >> SBI_SPEC_VERSION_MAJOR_SHIFT) &
		SBI_SPEC_VERSION_MAJOR_MASK;
}

/* Get the minor version of SBI */
static inline unsigned long sbi_minor_version(void)
{
	return sbi_spec_version & SBI_SPEC_VERSION_MINOR_MASK;
}

/* Make SBI version */
static inline unsigned long sbi_mk_version(unsigned long major,
					    unsigned long minor)
{
	return ((major & SBI_SPEC_VERSION_MAJOR_MASK) <<
		SBI_SPEC_VERSION_MAJOR_SHIFT) | minor;
}

int sbi_err_map_linux_errno(int err);
#else /* CONFIG_RISCV_SBI */
static inline int sbi_remote_fence_i(const unsigned long *hart_mask) { return -1; }
static inline void sbi_init(void) {}
#endif /* CONFIG_RISCV_SBI */
#endif /* _ASM_RISCV_SBI_H */
