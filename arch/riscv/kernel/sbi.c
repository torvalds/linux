// SPDX-License-Identifier: GPL-2.0-only
/*
 * SBI initialilization and all extension implementation.
 *
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/bits.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/reboot.h>
#include <asm/sbi.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>

/* default SBI version is 0.1 */
unsigned long sbi_spec_version __ro_after_init = SBI_SPEC_VERSION_DEFAULT;
EXPORT_SYMBOL(sbi_spec_version);

static void (*__sbi_set_timer)(uint64_t stime) __ro_after_init;
static void (*__sbi_send_ipi)(unsigned int cpu) __ro_after_init;
static int (*__sbi_rfence)(int fid, const struct cpumask *cpu_mask,
			   unsigned long start, unsigned long size,
			   unsigned long arg4, unsigned long arg5) __ro_after_init;

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
static unsigned long __sbi_v01_cpumask_to_hartmask(const struct cpumask *cpu_mask)
{
	unsigned long cpuid, hartid;
	unsigned long hmask = 0;

	/*
	 * There is no maximum hartid concept in RISC-V and NR_CPUS must not be
	 * associated with hartid. As SBI v0.1 is only kept for backward compatibility
	 * and will be removed in the future, there is no point in supporting hartid
	 * greater than BITS_PER_LONG (32 for RV32 and 64 for RV64). Ideally, SBI v0.2
	 * should be used for platforms with hartid greater than BITS_PER_LONG.
	 */
	for_each_cpu(cpuid, cpu_mask) {
		hartid = cpuid_to_hartid_map(cpuid);
		if (hartid >= BITS_PER_LONG) {
			pr_warn("Unable to send any request to hartid > BITS_PER_LONG for SBI v0.1\n");
			break;
		}
		hmask |= BIT(hartid);
	}

	return hmask;
}

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
 * __sbi_set_timer_v01() - Program the timer for next timer event.
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

static void __sbi_send_ipi_v01(unsigned int cpu)
{
	unsigned long hart_mask =
		__sbi_v01_cpumask_to_hartmask(cpumask_of(cpu));
	sbi_ecall(SBI_EXT_0_1_SEND_IPI, 0, (unsigned long)(&hart_mask),
		  0, 0, 0, 0, 0);
}

static int __sbi_rfence_v01(int fid, const struct cpumask *cpu_mask,
			    unsigned long start, unsigned long size,
			    unsigned long arg4, unsigned long arg5)
{
	int result = 0;
	unsigned long hart_mask;

	if (!cpu_mask || cpumask_empty(cpu_mask))
		cpu_mask = cpu_online_mask;
	hart_mask = __sbi_v01_cpumask_to_hartmask(cpu_mask);

	/* v0.2 function IDs are equivalent to v0.1 extension IDs */
	switch (fid) {
	case SBI_EXT_RFENCE_REMOTE_FENCE_I:
		sbi_ecall(SBI_EXT_0_1_REMOTE_FENCE_I, 0,
			  (unsigned long)&hart_mask, 0, 0, 0, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA:
		sbi_ecall(SBI_EXT_0_1_REMOTE_SFENCE_VMA, 0,
			  (unsigned long)&hart_mask, start, size,
			  0, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID:
		sbi_ecall(SBI_EXT_0_1_REMOTE_SFENCE_VMA_ASID, 0,
			  (unsigned long)&hart_mask, start, size,
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

static void __sbi_send_ipi_v01(unsigned int cpu)
{
	pr_warn("IPI extension is not available in SBI v%lu.%lu\n",
		sbi_major_version(), sbi_minor_version());
}

static int __sbi_rfence_v01(int fid, const struct cpumask *cpu_mask,
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

static void __sbi_send_ipi_v02(unsigned int cpu)
{
	int result;
	struct sbiret ret = {0};

	ret = sbi_ecall(SBI_EXT_IPI, SBI_EXT_IPI_SEND_IPI,
			1UL, cpuid_to_hartid_map(cpu), 0, 0, 0, 0);
	if (ret.error) {
		result = sbi_err_map_linux_errno(ret.error);
		pr_err("%s: hbase = [%lu] failed (error [%d])\n",
			__func__, cpuid_to_hartid_map(cpu), result);
	}
}

static int __sbi_rfence_v02_call(unsigned long fid, unsigned long hmask,
				 unsigned long hbase, unsigned long start,
				 unsigned long size, unsigned long arg4,
				 unsigned long arg5)
{
	struct sbiret ret = {0};
	int ext = SBI_EXT_RFENCE;
	int result = 0;

	switch (fid) {
	case SBI_EXT_RFENCE_REMOTE_FENCE_I:
		ret = sbi_ecall(ext, fid, hmask, hbase, 0, 0, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, arg4, 0);
		break;

	case SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA_VMID:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, arg4, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
				size, 0, 0);
		break;
	case SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA_ASID:
		ret = sbi_ecall(ext, fid, hmask, hbase, start,
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
		       __func__, hbase, hmask, result);
	}

	return result;
}

static int __sbi_rfence_v02(int fid, const struct cpumask *cpu_mask,
			    unsigned long start, unsigned long size,
			    unsigned long arg4, unsigned long arg5)
{
	unsigned long hartid, cpuid, hmask = 0, hbase = 0, htop = 0;
	int result;

	if (!cpu_mask || cpumask_empty(cpu_mask))
		cpu_mask = cpu_online_mask;

	for_each_cpu(cpuid, cpu_mask) {
		hartid = cpuid_to_hartid_map(cpuid);
		if (hmask) {
			if (hartid + BITS_PER_LONG <= htop ||
			    hbase + BITS_PER_LONG <= hartid) {
				result = __sbi_rfence_v02_call(fid, hmask,
						hbase, start, size, arg4, arg5);
				if (result)
					return result;
				hmask = 0;
			} else if (hartid < hbase) {
				/* shift the mask to fit lower hartid */
				hmask <<= hbase - hartid;
				hbase = hartid;
			}
		}
		if (!hmask) {
			hbase = hartid;
			htop = hartid;
		} else if (hartid > htop) {
			htop = hartid;
		}
		hmask |= BIT(hartid - hbase);
	}

	if (hmask) {
		result = __sbi_rfence_v02_call(fid, hmask, hbase,
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
 * Return: None.
 */
void sbi_set_timer(uint64_t stime_value)
{
	__sbi_set_timer(stime_value);
}

/**
 * sbi_send_ipi() - Send an IPI to any hart.
 * @cpu: Logical id of the target CPU.
 */
void sbi_send_ipi(unsigned int cpu)
{
	__sbi_send_ipi(cpu);
}
EXPORT_SYMBOL(sbi_send_ipi);

/**
 * sbi_remote_fence_i() - Execute FENCE.I instruction on given remote harts.
 * @cpu_mask: A cpu mask containing all the target harts.
 *
 * Return: 0 on success, appropriate linux error code otherwise.
 */
int sbi_remote_fence_i(const struct cpumask *cpu_mask)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_FENCE_I,
			    cpu_mask, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_remote_fence_i);

/**
 * sbi_remote_sfence_vma_asid() - Execute SFENCE.VMA instructions on given
 * remote harts for a virtual address range belonging to a specific ASID or not.
 *
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the virtual address
 * @size: Total size of the virtual address range.
 * @asid: The value of address space identifier (ASID), or FLUSH_TLB_NO_ASID
 * for flushing all address spaces.
 *
 * Return: 0 on success, appropriate linux error code otherwise.
 */
int sbi_remote_sfence_vma_asid(const struct cpumask *cpu_mask,
				unsigned long start,
				unsigned long size,
				unsigned long asid)
{
	if (asid == FLUSH_TLB_NO_ASID)
		return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_SFENCE_VMA,
				    cpu_mask, start, size, 0, 0);
	else
		return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID,
				    cpu_mask, start, size, asid, 0);
}
EXPORT_SYMBOL(sbi_remote_sfence_vma_asid);

/**
 * sbi_remote_hfence_gvma() - Execute HFENCE.GVMA instructions on given remote
 *			   harts for the specified guest physical address range.
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the guest physical address
 * @size: Total size of the guest physical address range.
 *
 * Return: None
 */
int sbi_remote_hfence_gvma(const struct cpumask *cpu_mask,
			   unsigned long start,
			   unsigned long size)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA,
			    cpu_mask, start, size, 0, 0);
}
EXPORT_SYMBOL_GPL(sbi_remote_hfence_gvma);

/**
 * sbi_remote_hfence_gvma_vmid() - Execute HFENCE.GVMA instructions on given
 * remote harts for a guest physical address range belonging to a specific VMID.
 *
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the guest physical address
 * @size: Total size of the guest physical address range.
 * @vmid: The value of guest ID (VMID).
 *
 * Return: 0 if success, Error otherwise.
 */
int sbi_remote_hfence_gvma_vmid(const struct cpumask *cpu_mask,
				unsigned long start,
				unsigned long size,
				unsigned long vmid)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA_VMID,
			    cpu_mask, start, size, vmid, 0);
}
EXPORT_SYMBOL(sbi_remote_hfence_gvma_vmid);

/**
 * sbi_remote_hfence_vvma() - Execute HFENCE.VVMA instructions on given remote
 *			     harts for the current guest virtual address range.
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the current guest virtual address
 * @size: Total size of the current guest virtual address range.
 *
 * Return: None
 */
int sbi_remote_hfence_vvma(const struct cpumask *cpu_mask,
			   unsigned long start,
			   unsigned long size)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA,
			    cpu_mask, start, size, 0, 0);
}
EXPORT_SYMBOL(sbi_remote_hfence_vvma);

/**
 * sbi_remote_hfence_vvma_asid() - Execute HFENCE.VVMA instructions on given
 * remote harts for current guest virtual address range belonging to a specific
 * ASID.
 *
 * @cpu_mask: A cpu mask containing all the target harts.
 * @start: Start of the current guest virtual address
 * @size: Total size of the current guest virtual address range.
 * @asid: The value of address space identifier (ASID).
 *
 * Return: None
 */
int sbi_remote_hfence_vvma_asid(const struct cpumask *cpu_mask,
				unsigned long start,
				unsigned long size,
				unsigned long asid)
{
	return __sbi_rfence(SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA_ASID,
			    cpu_mask, start, size, asid, 0);
}
EXPORT_SYMBOL(sbi_remote_hfence_vvma_asid);

static void sbi_srst_reset(unsigned long type, unsigned long reason)
{
	sbi_ecall(SBI_EXT_SRST, SBI_EXT_SRST_RESET, type, reason,
		  0, 0, 0, 0);
	pr_warn("%s: type=0x%lx reason=0x%lx failed\n",
		__func__, type, reason);
}

static int sbi_srst_reboot(struct notifier_block *this,
			   unsigned long mode, void *cmd)
{
	sbi_srst_reset((mode == REBOOT_WARM || mode == REBOOT_SOFT) ?
		       SBI_SRST_RESET_TYPE_WARM_REBOOT :
		       SBI_SRST_RESET_TYPE_COLD_REBOOT,
		       SBI_SRST_RESET_REASON_NONE);
	return NOTIFY_DONE;
}

static struct notifier_block sbi_srst_reboot_nb;

static void sbi_srst_power_off(void)
{
	sbi_srst_reset(SBI_SRST_RESET_TYPE_SHUTDOWN,
		       SBI_SRST_RESET_REASON_NONE);
}

/**
 * sbi_probe_extension() - Check if an SBI extension ID is supported or not.
 * @extid: The extension ID to be probed.
 *
 * Return: 1 or an extension specific nonzero value if yes, 0 otherwise.
 */
long sbi_probe_extension(int extid)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_PROBE_EXT, extid,
			0, 0, 0, 0, 0);
	if (!ret.error)
		return ret.value;

	return 0;
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

long sbi_get_mvendorid(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_MVENDORID);
}
EXPORT_SYMBOL_GPL(sbi_get_mvendorid);

long sbi_get_marchid(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_MARCHID);
}
EXPORT_SYMBOL_GPL(sbi_get_marchid);

long sbi_get_mimpid(void)
{
	return __sbi_base_ecall(SBI_EXT_BASE_GET_MIMPID);
}
EXPORT_SYMBOL_GPL(sbi_get_mimpid);

void __init sbi_init(void)
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
		if (sbi_probe_extension(SBI_EXT_TIME)) {
			__sbi_set_timer = __sbi_set_timer_v02;
			pr_info("SBI TIME extension detected\n");
		} else {
			__sbi_set_timer = __sbi_set_timer_v01;
		}
		if (sbi_probe_extension(SBI_EXT_IPI)) {
			__sbi_send_ipi	= __sbi_send_ipi_v02;
			pr_info("SBI IPI extension detected\n");
		} else {
			__sbi_send_ipi	= __sbi_send_ipi_v01;
		}
		if (sbi_probe_extension(SBI_EXT_RFENCE)) {
			__sbi_rfence	= __sbi_rfence_v02;
			pr_info("SBI RFENCE extension detected\n");
		} else {
			__sbi_rfence	= __sbi_rfence_v01;
		}
		if ((sbi_spec_version >= sbi_mk_version(0, 3)) &&
		    sbi_probe_extension(SBI_EXT_SRST)) {
			pr_info("SBI SRST extension detected\n");
			pm_power_off = sbi_srst_power_off;
			sbi_srst_reboot_nb.notifier_call = sbi_srst_reboot;
			sbi_srst_reboot_nb.priority = 192;
			register_restart_handler(&sbi_srst_reboot_nb);
		}
	} else {
		__sbi_set_timer = __sbi_set_timer_v01;
		__sbi_send_ipi	= __sbi_send_ipi_v01;
		__sbi_rfence	= __sbi_rfence_v01;
	}
}
