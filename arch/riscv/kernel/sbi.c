// SPDX-License-Identifier: GPL-2.0-only
/*
 * SBI initialilization and all extension implementation.
 *
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/init.h>
#include <linux/pm.h>
#include <asm/sbi.h>

/* default SBI version is 0.1 */
unsigned long sbi_spec_version = SBI_SPEC_VERSION_DEFAULT;
EXPORT_SYMBOL(sbi_spec_version);

static void (*__sbi_set_timer)(uint64_t stime);
static int (*__sbi_send_ipi)(const unsigned long *hart_mask);
static int (*__sbi_rfence)(int fid, const unsigned long *hart_mask,
			   unsigned long start, unsigned long size,
			   unsigned long arg4, unsigned long arg5);

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
			unsigned long arg1, unsigned long arg2,
			unsigned long arg3, unsigned long arg4,
			unsigned long arg5)
{
	struct sbiret ret;

	register uintptr_t a0 asm ("a0") = (uintptr_t)(arg0);
	register uintptr_t a1 asm ("a1") = (uintptr_t)(arg1);
	register uintptr_t a2 asm ("a2") = (uintptr_t)(arg2);
	register uintptr_t a3 asm ("a3") = (uintptr_t)(arg3);
	register uintptr_t a4 asm ("a4") = (uintptr_t)(arg4);
	register uintptr_t a5 asm ("a5") = (uintptr_t)(arg5);
	register uintptr_t a6 asm ("a6") = (uintptr_t)(fid);
	register uintptr_t a7 asm ("a7") = (uintptr_t)(ext);
	asm volatile ("ecall"
		      : "+r" (a0), "+r" (a1)
		      : "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a6), "r" (a7)
		      : "memory");
	ret.error = a0;
	ret.value = a1;

	return ret;
}
EXPORT_SYMBOL(sbi_ecall);

static int sbi_err_map_linux_errno(int err)
{
	switch (err) {
	case SBI_SUCCESS:
		return 0;
	case SBI_ERR_DENIED:
		return -EPERM;
	case SBI_ERR_INVALID_PARAM:
		return -EINVAL;
	case SBI_ERR_INVALID_ADDRESS:
		return -EFAULT;
	case SBI_ERR_NOT_SUPPORTED:
	case SBI_ERR_FAILURE:
	default:
		return -ENOTSUPP;
	};
}

#ifdef CONFIG_RISCV_SBI_V01
/**
 * sbi_console_putchar() - Writes given character to the console device.
 * @ch: The data to be written to the console.
 *
 * Return: None
 */
void sbi_console_putchar(int ch)
{
	sbi_ecall(SBI_EXT_0_1_CONSOLE_PUTCHAR, 0, ch, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_console_putchar);

/**
 * sbi_console_getchar() - Reads a byte from console device.
 *
 * Returns the value read from console.
 */
int sbi_console_getchar(void)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_0_1_CONSOLE_GETCHAR, 0, 0, 0, 0, 0, 0, 0);

	return ret.error;
}
EXPORT_SYMBOL(sbi_console_getchar);

/**
 * sbi_shutdown() - Remove all the harts from executing supervisor code.
 *
 * Return: None
 */
void sbi_shutdown(void)
{
	sbi_ecall(SBI_EXT_0_1_SHUTDOWN, 0, 0, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_set_timer);

/**
 * sbi_clear_ipi() - Clear any pending IPIs for the calling hart.
 *
 * Return: None
 */
void sbi_clear_ipi(void)
{
	sbi_ecall(SBI_EXT_0_1_CLEAR_IPI, 0, 0, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_shutdown);

/**
 * sbi_set_timer_v01() - Program the timer for next timer event.
 * @stime_value: The value after which next timer event should fire.
 *
 * Return: None
 */
static void __sbi_set_timer_v01(uint64_t stime_value)
{
#if __riscv_xlen == 32
	sbi_ecall(SBI_EXT_0_1_SET_TIMER, 0, stime_value,
		  stime_value >> 32, 0, 0, 0, 0);
#else
	sbi_ecall(SBI_EXT_0_1_SET_TIMER, 0, stime_value, 0, 0, 0, 0, 0);
#endif
}

static int __sbi_send_ipi_v01(const unsigned long *hart_mask)
{
	sbi_ecall(SBI_EXT_0_1_SEND_IPI, 0, (unsigned long)hart_mask,
		  0, 0, 0, 0, 0);
	return 0;
}

static int __sbi_rfence_v01(int fid, const unsigned long *hart_mask,
			    unsigned long start, unsigned long size,
			    unsigned long arg4, unsigned long arg5)
{
	int result = 0;

	/* v0.2 function IDs are equivalent to v0.1 extension IDs */
	switch (fid) {
	case SBI_EXT_RFENCE_REMOTE_FENCE_I:
		sbi_ecall(SBI_EXT_0_1_REMOTE_FENCE_I, 0,
			  (unsigned long)hart_mask, 0, 0, 0, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA:
		sbi_ecall(SBI_EXT_0_1_REMOTE_SFENCE_VMA, 0,
			  (unsigned long)hart_mask, start, size,
			  0, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID:
		sbi_ecall(SBI_EXT_0_1_REMOTE_SFENCE_VMA_ASID, 0,
			  (unsigned long)hart_mask, start, size,
			  arg4, 0, 0);
		break;
	default:
		pr_err("SBI call [%d]not supported in SBI v0.1\n", fid);
		result = -EINVAL;
	}

	return result;
}
#else
static void __sbi_set_timer_v01(uint64_t stime_value)
{
	pr_warn("Timer extension is not available in SBI v%lu.%lu\n",
		sbi_major_version(), sbi_minor_version());
}

static int __sbi_send_ipi_v01(const unsigned long *hart_mask)
{
	pr_warn("IPI extension is not available in SBI v%lu.%lu\n",
		sbi_major_version(), sbi_minor_version());

	return 0;
}

static int __sbi_rfence_v01(int fid, const unsigned long *hart_mask,
			    unsigned long start, unsigned long size,
			    unsigned long arg4, unsigned long arg5)
{
	pr_warn("remote fence extension is not available in SBI v%lu.%lu\n",
		sbi_major_version(), sbi_minor_version());

	return 0;
}
#endif /* CONFIG_RISCV_SBI_V01 */

/**
 * sbi_set_timer() - Program the timer for next timer event.
 * @stime_value: The value after which next timer event should fire.
 *
 * Return: None
 */
void sbi_set_timer(uint64_t stime_value)
{
	__sbi_set_timer(stime_value);
}

/**
 * sbi_send_ipi() - Send an IPI to any hart.
 * @hart_mask: A cpu mask containing all the target harts.
 *
 * Return: None
 */
void sbi_send_ipi(const unsigned long *hart_mask)
{
	__sbi_send_ipi(hart_mask);
}
EXPORT_SYMBOL(sbi_send_ipi);

/**
 * sbi_remote_fence_i() - Execute FENCE.I instruction on given remote harts.
 * @hart_mask: A cpu mask containing all the target harts.
 *
 * Return: None
 */
void sbi_remote_fence_i(const unsigned long *hart_mask)
{
	__sbi_rfence(SBI_EXT_RFENCE_REMOTE_FENCE_I,
		     hart_mask, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_remote_fence_i);

/**
 * sbi_remote_sfence_vma() - Execute SFENCE.VMA instructions on given remote
 *			     harts for the specified virtual address range.
 * @hart_mask: A cpu mask containing all the target harts.
 * @start: Start of the virtual address
 * @size: Total size of the virtual address range.
 *
 * Return: None
 */
void sbi_remote_sfence_vma(const unsigned long *hart_mask,
			   unsigned long start,
			   unsigned long size)
{
	__sbi_rfence(SBI_EXT_RFENCE_REMOTE_SFENCE_VMA,
		     hart_mask, start, size, 0, 0);
}
EXPORT_SYMBOL(sbi_remote_sfence_vma);

/**
 * sbi_remote_sfence_vma_asid() - Execute SFENCE.VMA instructions on given
 * remote harts for a virtual address range belonging to a specific ASID.
 *
 * @hart_mask: A cpu mask containing all the target harts.
 * @start: Start of the virtual address
 * @size: Total size of the virtual address range.
 * @asid: The value of address space identifier (ASID).
 *
 * Return: None
 */
void sbi_remote_sfence_vma_asid(const unsigned long *hart_mask,
				unsigned long start,
				unsigned long size,
				unsigned long asid)
{
	__sbi_rfence(SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID,
		     hart_mask, start, size, asid, 0);
}
EXPORT_SYMBOL(sbi_remote_sfence_vma_asid);

/**
 * sbi_probe_extension() - Check if an SBI extension ID is supported or not.
 * @extid: The extension ID to be probed.
 *
 * Return: Extension specific nonzero value f yes, -ENOTSUPP otherwise.
 */
int sbi_probe_extension(int extid)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_PROBE_EXT, extid,
			0, 0, 0, 0, 0);
	if (!ret.error)
		if (ret.value)
			return ret.value;

	return -ENOTSUPP;
}
EXPORT_SYMBOL(sbi_probe_extension);

static long __sbi_base_ecall(int fid)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_BASE, fid, 0, 0, 0, 0, 0, 0);
	if (!ret.error)
		return ret.value;
	else
		return sbi_err_map_linux_errno(ret.error);
}

static inline long sbi_get_spec_version(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_SPEC_VERSION);
}

static inline long sbi_get_firmware_id(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_IMP_ID);
}

static inline long sbi_get_firmware_version(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_IMP_VERSION);
}

static void sbi_power_off(void)
{
	sbi_shutdown();
}

int __init sbi_init(void)
{
	int ret;

	pm_power_off = sbi_power_off;
	ret = sbi_get_spec_version();
	if (ret > 0)
		sbi_spec_version = ret;

	pr_info("SBI specification v%lu.%lu detected\n",
		sbi_major_version(), sbi_minor_version());

	if (!sbi_spec_is_0_1()) {
		pr_info("SBI implementation ID=0x%lx Version=0x%lx\n",
			sbi_get_firmware_id(), sbi_get_firmware_version());
	}

	__sbi_set_timer = __sbi_set_timer_v01;
	__sbi_send_ipi	= __sbi_send_ipi_v01;
	__sbi_rfence	= __sbi_rfence_v01;

	return 0;
}
