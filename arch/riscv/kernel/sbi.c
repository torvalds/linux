// SPDX-License-Identifier: GPL-2.0-only
/*
 * SBI initialilization and all extension implementation.
 *
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/init.h>
#include <linux/pm.h>
#include <asm/sbi.h>
#include <asm/smp.h>

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

int sbi_err_map_linux_errno(int err)
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
EXPORT_SYMBOL(sbi_err_map_linux_errno);

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
EXPORT_SYMBOL(sbi_shutdown);

/**
 * sbi_clear_ipi() - Clear any pending IPIs for the calling hart.
 *
 * Return: None
 */
void sbi_clear_ipi(void)
{
	sbi_ecall(SBI_EXT_0_1_CLEAR_IPI, 0, 0, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_clear_ipi);

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

static void sbi_set_power_off(void)
{
	pm_power_off = sbi_shutdown;
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

static void sbi_set_power_off(void) {}
#endif /* CONFIG_RISCV_SBI_V01 */

static void __sbi_set_timer_v02(uint64_t stime_value)
{
#if __riscv_xlen == 32
	sbi_ecall(SBI_EXT_TIME, SBI_EXT_TIME_SET_TIMER, stime_value,
		  stime_value >> 32, 0, 0, 0, 0);
#else
	sbi_ecall(SBI_EXT_TIME, SBI_EXT_TIME_SET_TIMER, stime_value, 0,
		  0, 0, 0, 0);
#endif
}

static int __sbi_send_ipi_v02(const unsigned long *hart_mask)
{
	unsigned long hartid, hmask_val, hbase;
	struct cpumask tmask;
	struct sbiret ret = {0};
	int result;

	if (!hart_mask || !(*hart_mask)) {
		riscv_cpuid_to_hartid_mask(cpu_online_mask, &tmask);
		hart_mask = cpumask_bits(&tmask);
	}

	hmask_val = 0;
	hbase = 0;
	for_each_set_bit(hartid, hart_mask, NR_CPUS) {
		if (hmask_val && ((hbase + BITS_PER_LONG) <= hartid)) {
			ret = sbi_ecall(SBI_EXT_IPI, SBI_EXT_IPI_SEND_IPI,
					hmask_val, hbase, 0, 0, 0, 0);
			if (ret.error)
				goto ecall_failed;
			hmask_val = 0;
			hbase = 0;
		}
		if (!hmask_val)
			hbase = hartid;
		hmask_val |= 1UL << (hartid - hbase);
	}

	if (hmask_val) {
		ret = sbi_ecall(SBI_EXT_IPI, SBI_EXT_IPI_SEND_IPI,
				hmask_val, hbase, 0, 0, 0, 0);
		if (ret.error)
			goto ecall_failed;
	}

	return 0;

ecall_failed:
	result = sbi_err_map_linux_errno(ret.error);
	pr_err("%s: hbase = [%lu] hmask = [0x%lx] failed (error [%d])\n",
	       __func__, hbase, hmask_val, result);
	return result;
}

static int __sbi_rfence_v02_call(unsigned long fid, unsigned long hmask_val,
				 unsigned long hbase, unsigned long start,
				 unsigned long size, unsigned long arg4,
				 unsigned long arg5)
{
	struct sbiret ret = {0};
	int ext = SBI_EXT_RFENCE;
	int result = 0;

	switch (fid) {
	case SBI_EXT_RFENCE_REMOTE_FENCE_I:
		ret = sbi_ecall(ext, fid, hmask_val, hbase, 0, 0, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA:
		ret = sbi_ecall(ext, fid, hmask_val, hbase, start,
				size, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID:
		ret = sbi_ecall(ext, fid, hmask_val, hbase, start,
				size, arg4, 0);
		break;

	case SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA:
		ret = sbi_ecall(ext, fid, hmask_val, hbase, start,
				size, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA_VMID:
		ret = sbi_ecall(ext, fid, hmask_val, hbase, start,
				size, arg4, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA:
		ret = sbi_ecall(ext, fid, hmask_val, hbase, start,
				size, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA_ASID:
		ret = sbi_ecall(ext, fid, hmask_val, hbase, start,
				size, arg4, 0);
		break;
	default:
		pr_err("unknown function ID [%lu] for SBI extension [%d]\n",
		       fid, ext);
		result = -EINVAL;
	}

	if (ret.error) {
		result = sbi_err_map_linux_errno(ret.error);
		pr_err("%s: hbase = [%lu] hmask = [0x%lx] failed (error [%d])\n",
		       __func__, hbase, hmask_val, result);
	}

	return result;
}

static int __sbi_rfence_v02(int fid, const unsigned long *hart_mask,
			    unsigned long start, unsigned long size,
			    unsigned long arg4, unsigned long arg5)
{
	unsigned long hmask_val, hartid, hbase;
	struct cpumask tmask;
	int result;

	if (!hart_mask || !(*hart_mask)) {
		riscv_cpuid_to_hartid_mask(cpu_online_mask, &tmask);
		hart_mask = cpumask_bits(&tmask);
	}

	hmask_val = 0;
	hbase = 0;
	for_each_set_bit(hartid, hart_mask, NR_CPUS) {
		if (hmask_val && ((hbase + BITS_PER_LONG) <= hartid)) {
			result = __sbi_rfence_v02_call(fid, hmask_val, hbase,
						       start, size, arg4, arg5);
			if (result)
				return result;
			hmask_val = 0;
			hbase = 0;
		}
		if (!hmask_val)
			hbase = hartid;
		hmask_val |= 1UL << (hartid - hbase);
	}

	if (hmask_val) {
		result = __sbi_rfence_v02_call(fid, hmask_val, hbase,
					       start, size, arg4, arg5);
		if (result)
			return result;
	}

	return 0;
}

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
 * sbi_remote_hfence_gvma() - Execute HFENCE.GVMA instructions on given remote
 *			   harts for the specified guest physical address range.
 * @hart_mask: A cpu mask containing all the target harts.
 * @start: Start of the guest physical address
 * @size: Total size of the guest physical address range.
 *
 * Return: None
 */
int sbi_remote_hfence_gvma(const unsigned long *hart_mask,
			   unsigned long start,
			   unsigned long size)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA,
			    hart_mask, start, size, 0, 0);
}
EXPORT_SYMBOL_GPL(sbi_remote_hfence_gvma);

/**
 * sbi_remote_hfence_gvma_vmid() - Execute HFENCE.GVMA instructions on given
 * remote harts for a guest physical address range belonging to a specific VMID.
 *
 * @hart_mask: A cpu mask containing all the target harts.
 * @start: Start of the guest physical address
 * @size: Total size of the guest physical address range.
 * @vmid: The value of guest ID (VMID).
 *
 * Return: 0 if success, Error otherwise.
 */
int sbi_remote_hfence_gvma_vmid(const unsigned long *hart_mask,
				unsigned long start,
				unsigned long size,
				unsigned long vmid)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA_VMID,
			    hart_mask, start, size, vmid, 0);
}
EXPORT_SYMBOL(sbi_remote_hfence_gvma_vmid);

/**
 * sbi_remote_hfence_vvma() - Execute HFENCE.VVMA instructions on given remote
 *			     harts for the current guest virtual address range.
 * @hart_mask: A cpu mask containing all the target harts.
 * @start: Start of the current guest virtual address
 * @size: Total size of the current guest virtual address range.
 *
 * Return: None
 */
int sbi_remote_hfence_vvma(const unsigned long *hart_mask,
			   unsigned long start,
			   unsigned long size)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA,
			    hart_mask, start, size, 0, 0);
}
EXPORT_SYMBOL(sbi_remote_hfence_vvma);

/**
 * sbi_remote_hfence_vvma_asid() - Execute HFENCE.VVMA instructions on given
 * remote harts for current guest virtual address range belonging to a specific
 * ASID.
 *
 * @hart_mask: A cpu mask containing all the target harts.
 * @start: Start of the current guest virtual address
 * @size: Total size of the current guest virtual address range.
 * @asid: The value of address space identifier (ASID).
 *
 * Return: None
 */
int sbi_remote_hfence_vvma_asid(const unsigned long *hart_mask,
				unsigned long start,
				unsigned long size,
				unsigned long asid)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA_ASID,
			    hart_mask, start, size, asid, 0);
}
EXPORT_SYMBOL(sbi_remote_hfence_vvma_asid);

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

static void sbi_send_cpumask_ipi(const struct cpumask *target)
{
	struct cpumask hartid_mask;

	riscv_cpuid_to_hartid_mask(target, &hartid_mask);

	sbi_send_ipi(cpumask_bits(&hartid_mask));
}

static struct riscv_ipi_ops sbi_ipi_ops = {
	.ipi_inject = sbi_send_cpumask_ipi
};

int __init sbi_init(void)
{
	int ret;

	sbi_set_power_off();
	ret = sbi_get_spec_version();
	if (ret > 0)
		sbi_spec_version = ret;

	pr_info("SBI specification v%lu.%lu detected\n",
		sbi_major_version(), sbi_minor_version());

	if (!sbi_spec_is_0_1()) {
		pr_info("SBI implementation ID=0x%lx Version=0x%lx\n",
			sbi_get_firmware_id(), sbi_get_firmware_version());
		if (sbi_probe_extension(SBI_EXT_TIME) > 0) {
			__sbi_set_timer = __sbi_set_timer_v02;
			pr_info("SBI v0.2 TIME extension detected\n");
		} else {
			__sbi_set_timer = __sbi_set_timer_v01;
		}
		if (sbi_probe_extension(SBI_EXT_IPI) > 0) {
			__sbi_send_ipi	= __sbi_send_ipi_v02;
			pr_info("SBI v0.2 IPI extension detected\n");
		} else {
			__sbi_send_ipi	= __sbi_send_ipi_v01;
		}
		if (sbi_probe_extension(SBI_EXT_RFENCE) > 0) {
			__sbi_rfence	= __sbi_rfence_v02;
			pr_info("SBI v0.2 RFENCE extension detected\n");
		} else {
			__sbi_rfence	= __sbi_rfence_v01;
		}
	} else {
		__sbi_set_timer = __sbi_set_timer_v01;
		__sbi_send_ipi	= __sbi_send_ipi_v01;
		__sbi_rfence	= __sbi_rfence_v01;
	}

	riscv_set_ipi_ops(&sbi_ipi_ops);

	return 0;
}
