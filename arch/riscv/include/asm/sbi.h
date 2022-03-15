/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#ifndef _ASM_RISCV_SBI_H
#define _ASM_RISCV_SBI_H

#include <linux/types.h>
#include <linux/cpumask.h>

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

	/* Experimentals extensions must lie within this range */
	SBI_EXT_EXPERIMENTAL_START = 0x08000000,
	SBI_EXT_EXPERIMENTAL_END = 0x08FFFFFF,

	/* Vendor extensions must lie within this range */
	SBI_EXT_VENDOR_START = 0x09000000,
	SBI_EXT_VENDOR_END = 0x09FFFFFF,
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
int sbi_send_ipi(const struct cpumask *cpu_mask);
int sbi_remote_fence_i(const struct cpumask *cpu_mask);
int sbi_remote_sfence_vma(const struct cpumask *cpu_mask,
			   unsigned long start,
			   unsigned long size);

int sbi_remote_sfence_vma_asid(const struct cpumask *cpu_mask,
				unsigned long start,
				unsigned long size,
				unsigned long asid);
int sbi_remote_hfence_gvma(const struct cpumask *cpu_mask,
			   unsigned long start,
			   unsigned long size);
int sbi_remote_hfence_gvma_vmid(const struct cpumask *cpu_mask,
				unsigned long start,
				unsigned long size,
				unsigned long vmid);
int sbi_remote_hfence_vvma(const struct cpumask *cpu_mask,
			   unsigned long start,
			   unsigned long size);
int sbi_remote_hfence_vvma_asid(const struct cpumask *cpu_mask,
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
static inline int sbi_remote_fence_i(const struct cpumask *cpu_mask) { return -1; }
static inline void sbi_init(void) {}
#endif /* CONFIG_RISCV_SBI */
#endif /* _ASM_RISCV_SBI_H */
