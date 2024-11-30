// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtual PTP 1588 clock for use with LM-safe VMclock device.
 *
 * Copyright © 2024 Amazon.com, Inc. or its affiliates.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <uapi/linux/vmclock-abi.h>

#include <linux/ptp_clock_kernel.h>

#ifdef CONFIG_X86
#include <asm/pvclock.h>
#include <asm/kvmclock.h>
#endif

#ifdef CONFIG_KVM_GUEST
#define SUPPORT_KVMCLOCK
#endif

static DEFINE_IDA(vmclock_ida);

ACPI_MODULE_NAME("vmclock");

struct vmclock_state {
	struct resource res;
	struct vmclock_abi *clk;
	struct miscdevice miscdev;
	struct ptp_clock_info ptp_clock_info;
	struct ptp_clock *ptp_clock;
	enum clocksource_ids cs_id, sys_cs_id;
	int index;
	char *name;
};

#define VMCLOCK_MAX_WAIT ms_to_ktime(100)

/* Require at least the flags field to be present. All else can be optional. */
#define VMCLOCK_MIN_SIZE offsetof(struct vmclock_abi, pad)

#define VMCLOCK_FIELD_PRESENT(_c, _f)			  \
	(le32_to_cpu((_c)->size) >= (offsetof(struct vmclock_abi, _f) +	\
				     sizeof((_c)->_f)))

/*
 * Multiply a 64-bit count by a 64-bit tick 'period' in units of seconds >> 64
 * and add the fractional second part of the reference time.
 *
 * The result is a 128-bit value, the top 64 bits of which are seconds, and
 * the low 64 bits are (seconds >> 64).
 */
static uint64_t mul_u64_u64_shr_add_u64(uint64_t *res_hi, uint64_t delta,
					uint64_t period, uint8_t shift,
					uint64_t frac_sec)
{
	unsigned __int128 res = (unsigned __int128)delta * period;

	res >>= shift;
	res += frac_sec;
	*res_hi = res >> 64;
	return (uint64_t)res;
}

static bool tai_adjust(struct vmclock_abi *clk, uint64_t *sec)
{
	if (likely(clk->time_type == VMCLOCK_TIME_UTC))
		return true;

	if (clk->time_type == VMCLOCK_TIME_TAI &&
	    (le64_to_cpu(clk->flags) & VMCLOCK_FLAG_TAI_OFFSET_VALID)) {
		if (sec)
			*sec += (int16_t)le16_to_cpu(clk->tai_offset_sec);
		return true;
	}
	return false;
}

static int vmclock_get_crosststamp(struct vmclock_state *st,
				   struct ptp_system_timestamp *sts,
				   struct system_counterval_t *system_counter,
				   struct timespec64 *tspec)
{
	ktime_t deadline = ktime_add(ktime_get(), VMCLOCK_MAX_WAIT);
	struct system_time_snapshot systime_snapshot;
	uint64_t cycle, delta, seq, frac_sec;

#ifdef CONFIG_X86
	/*
	 * We'd expect the hypervisor to know this and to report the clock
	 * status as VMCLOCK_STATUS_UNRELIABLE. But be paranoid.
	 */
	if (check_tsc_unstable())
		return -EINVAL;
#endif

	while (1) {
		seq = le32_to_cpu(st->clk->seq_count) & ~1ULL;

		/*
		 * This pairs with a write barrier in the hypervisor
		 * which populates this structure.
		 */
		virt_rmb();

		if (st->clk->clock_status == VMCLOCK_STATUS_UNRELIABLE)
			return -EINVAL;

		/*
		 * When invoked for gettimex64(), fill in the pre/post system
		 * times. The simple case is when system time is based on the
		 * same counter as st->cs_id, in which case all three times
		 * will be derived from the *same* counter value.
		 *
		 * If the system isn't using the same counter, then the value
		 * from ktime_get_snapshot() will still be used as pre_ts, and
		 * ptp_read_system_postts() is called to populate postts after
		 * calling get_cycles().
		 *
		 * The conversion to timespec64 happens further down, outside
		 * the seq_count loop.
		 */
		if (sts) {
			ktime_get_snapshot(&systime_snapshot);
			if (systime_snapshot.cs_id == st->cs_id) {
				cycle = systime_snapshot.cycles;
			} else {
				cycle = get_cycles();
				ptp_read_system_postts(sts);
			}
		} else {
			cycle = get_cycles();
		}

		delta = cycle - le64_to_cpu(st->clk->counter_value);

		frac_sec = mul_u64_u64_shr_add_u64(&tspec->tv_sec, delta,
						   le64_to_cpu(st->clk->counter_period_frac_sec),
						   st->clk->counter_period_shift,
						   le64_to_cpu(st->clk->time_frac_sec));
		tspec->tv_nsec = mul_u64_u64_shr(frac_sec, NSEC_PER_SEC, 64);
		tspec->tv_sec += le64_to_cpu(st->clk->time_sec);

		if (!tai_adjust(st->clk, &tspec->tv_sec))
			return -EINVAL;

		/*
		 * This pairs with a write barrier in the hypervisor
		 * which populates this structure.
		 */
		virt_rmb();
		if (seq == le32_to_cpu(st->clk->seq_count))
			break;

		if (ktime_after(ktime_get(), deadline))
			return -ETIMEDOUT;
	}

	if (system_counter) {
		system_counter->cycles = cycle;
		system_counter->cs_id = st->cs_id;
	}

	if (sts) {
		sts->pre_ts = ktime_to_timespec64(systime_snapshot.real);
		if (systime_snapshot.cs_id == st->cs_id)
			sts->post_ts = sts->pre_ts;
	}

	return 0;
}

#ifdef SUPPORT_KVMCLOCK
/*
 * In the case where the system is using the KVM clock for timekeeping, convert
 * the TSC value into a KVM clock time in order to return a paired reading that
 * get_device_system_crosststamp() can cope with.
 */
static int vmclock_get_crosststamp_kvmclock(struct vmclock_state *st,
					    struct ptp_system_timestamp *sts,
					    struct system_counterval_t *system_counter,
					    struct timespec64 *tspec)
{
	struct pvclock_vcpu_time_info *pvti = this_cpu_pvti();
	unsigned int pvti_ver;
	int ret;

	preempt_disable_notrace();

	do {
		pvti_ver = pvclock_read_begin(pvti);

		ret = vmclock_get_crosststamp(st, sts, system_counter, tspec);
		if (ret)
			break;

		system_counter->cycles = __pvclock_read_cycles(pvti,
							       system_counter->cycles);
		system_counter->cs_id = CSID_X86_KVM_CLK;

		/*
		 * This retry should never really happen; if the TSC is
		 * stable and reliable enough across vCPUS that it is sane
		 * for the hypervisor to expose a VMCLOCK device which uses
		 * it as the reference counter, then the KVM clock sohuld be
		 * in 'master clock mode' and basically never changed. But
		 * the KVM clock is a fickle and often broken thing, so do
		 * it "properly" just in case.
		 */
	} while (pvclock_read_retry(pvti, pvti_ver));

	preempt_enable_notrace();

	return ret;
}
#endif

static int ptp_vmclock_get_time_fn(ktime_t *device_time,
				   struct system_counterval_t *system_counter,
				   void *ctx)
{
	struct vmclock_state *st = ctx;
	struct timespec64 tspec;
	int ret;

#ifdef SUPPORT_KVMCLOCK
	if (READ_ONCE(st->sys_cs_id) == CSID_X86_KVM_CLK)
		ret = vmclock_get_crosststamp_kvmclock(st, NULL, system_counter,
						       &tspec);
	else
#endif
		ret = vmclock_get_crosststamp(st, NULL, system_counter, &tspec);

	if (!ret)
		*device_time = timespec64_to_ktime(tspec);

	return ret;
}

static int ptp_vmclock_getcrosststamp(struct ptp_clock_info *ptp,
				      struct system_device_crosststamp *xtstamp)
{
	struct vmclock_state *st = container_of(ptp, struct vmclock_state,
						ptp_clock_info);
	int ret = get_device_system_crosststamp(ptp_vmclock_get_time_fn, st,
						NULL, xtstamp);
#ifdef SUPPORT_KVMCLOCK
	/*
	 * On x86, the KVM clock may be used for the system time. We can
	 * actually convert a TSC reading to that, and return a paired
	 * timestamp that get_device_system_crosststamp() *can* handle.
	 */
	if (ret == -ENODEV) {
		struct system_time_snapshot systime_snapshot;

		ktime_get_snapshot(&systime_snapshot);

		if (systime_snapshot.cs_id == CSID_X86_TSC ||
		    systime_snapshot.cs_id == CSID_X86_KVM_CLK) {
			WRITE_ONCE(st->sys_cs_id, systime_snapshot.cs_id);
			ret = get_device_system_crosststamp(ptp_vmclock_get_time_fn,
							    st, NULL, xtstamp);
		}
	}
#endif
	return ret;
}

/*
 * PTP clock operations
 */

static int ptp_vmclock_adjfine(struct ptp_clock_info *ptp, long delta)
{
	return -EOPNOTSUPP;
}

static int ptp_vmclock_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	return -EOPNOTSUPP;
}

static int ptp_vmclock_settime(struct ptp_clock_info *ptp,
			   const struct timespec64 *ts)
{
	return -EOPNOTSUPP;
}

static int ptp_vmclock_gettimex(struct ptp_clock_info *ptp, struct timespec64 *ts,
				struct ptp_system_timestamp *sts)
{
	struct vmclock_state *st = container_of(ptp, struct vmclock_state,
						ptp_clock_info);

	return vmclock_get_crosststamp(st, sts, NULL, ts);
}

static int ptp_vmclock_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static const struct ptp_clock_info ptp_vmclock_info = {
	.owner		= THIS_MODULE,
	.max_adj	= 0,
	.n_ext_ts	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfine	= ptp_vmclock_adjfine,
	.adjtime	= ptp_vmclock_adjtime,
	.gettimex64	= ptp_vmclock_gettimex,
	.settime64	= ptp_vmclock_settime,
	.enable		= ptp_vmclock_enable,
	.getcrosststamp = ptp_vmclock_getcrosststamp,
};

static struct ptp_clock *vmclock_ptp_register(struct device *dev,
					      struct vmclock_state *st)
{
	enum clocksource_ids cs_id;

	if (IS_ENABLED(CONFIG_ARM64) &&
	    st->clk->counter_id == VMCLOCK_COUNTER_ARM_VCNT) {
		/* Can we check it's the virtual counter? */
		cs_id = CSID_ARM_ARCH_COUNTER;
	} else if (IS_ENABLED(CONFIG_X86) &&
		   st->clk->counter_id == VMCLOCK_COUNTER_X86_TSC) {
		cs_id = CSID_X86_TSC;
	} else {
		return NULL;
	}

	/* Only UTC, or TAI with offset */
	if (!tai_adjust(st->clk, NULL)) {
		dev_info(dev, "vmclock does not provide unambiguous UTC\n");
		return NULL;
	}

	st->sys_cs_id = cs_id;
	st->cs_id = cs_id;
	st->ptp_clock_info = ptp_vmclock_info;
	strscpy(st->ptp_clock_info.name, st->name);

	return ptp_clock_register(&st->ptp_clock_info, dev);
}

static int vmclock_miscdev_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct vmclock_state *st = container_of(fp->private_data,
						struct vmclock_state, miscdev);

	if ((vma->vm_flags & (VM_READ|VM_WRITE)) != VM_READ)
		return -EROFS;

	if (vma->vm_end - vma->vm_start != PAGE_SIZE || vma->vm_pgoff)
		return -EINVAL;

	if (io_remap_pfn_range(vma, vma->vm_start,
			       st->res.start >> PAGE_SHIFT, PAGE_SIZE,
			       vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static ssize_t vmclock_miscdev_read(struct file *fp, char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct vmclock_state *st = container_of(fp->private_data,
						struct vmclock_state, miscdev);
	ktime_t deadline = ktime_add(ktime_get(), VMCLOCK_MAX_WAIT);
	size_t max_count;
	uint32_t seq;

	if (*ppos >= PAGE_SIZE)
		return 0;

	max_count = PAGE_SIZE - *ppos;
	if (count > max_count)
		count = max_count;

	while (1) {
		seq = le32_to_cpu(st->clk->seq_count) & ~1U;
		/* Pairs with hypervisor wmb */
		virt_rmb();

		if (copy_to_user(buf, ((char *)st->clk) + *ppos, count))
			return -EFAULT;

		/* Pairs with hypervisor wmb */
		virt_rmb();
		if (seq == le32_to_cpu(st->clk->seq_count))
			break;

		if (ktime_after(ktime_get(), deadline))
			return -ETIMEDOUT;
	}

	*ppos += count;
	return count;
}

static const struct file_operations vmclock_miscdev_fops = {
	.mmap = vmclock_miscdev_mmap,
	.read = vmclock_miscdev_read,
};

/* module operations */

static void vmclock_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vmclock_state *st = dev_get_drvdata(dev);

	if (st->ptp_clock)
		ptp_clock_unregister(st->ptp_clock);

	if (st->miscdev.minor != MISC_DYNAMIC_MINOR)
		misc_deregister(&st->miscdev);
}

static acpi_status vmclock_acpi_resources(struct acpi_resource *ares, void *data)
{
	struct vmclock_state *st = data;
	struct resource_win win;
	struct resource *res = &win.res;

	if (ares->type == ACPI_RESOURCE_TYPE_END_TAG)
		return AE_OK;

	/* There can be only one */
	if (resource_type(&st->res) == IORESOURCE_MEM)
		return AE_ERROR;

	if (acpi_dev_resource_memory(ares, res) ||
	    acpi_dev_resource_address_space(ares, &win)) {

		if (resource_type(res) != IORESOURCE_MEM ||
		    resource_size(res) < sizeof(st->clk))
			return AE_ERROR;

		st->res = *res;
		return AE_OK;
	}

	return AE_ERROR;
}

static int vmclock_probe_acpi(struct device *dev, struct vmclock_state *st)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	acpi_status status;

	/*
	 * This should never happen as this function is only called when
	 * has_acpi_companion(dev) is true, but the logic is sufficiently
	 * complex that Coverity can't see the tautology.
	 */
	if (!adev)
		return -ENODEV;

	status = acpi_walk_resources(adev->handle, METHOD_NAME__CRS,
				     vmclock_acpi_resources, st);
	if (ACPI_FAILURE(status) || resource_type(&st->res) != IORESOURCE_MEM) {
		dev_err(dev, "failed to get resources\n");
		return -ENODEV;
	}

	return 0;
}

static void vmclock_put_idx(void *data)
{
	struct vmclock_state *st = data;

	ida_free(&vmclock_ida, st->index);
}

static int vmclock_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vmclock_state *st;
	int ret;

	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	if (has_acpi_companion(dev))
		ret = vmclock_probe_acpi(dev, st);
	else
		ret = -EINVAL; /* Only ACPI for now */

	if (ret) {
		dev_info(dev, "Failed to obtain physical address: %d\n", ret);
		goto out;
	}

	if (resource_size(&st->res) < VMCLOCK_MIN_SIZE) {
		dev_info(dev, "Region too small (0x%llx)\n",
			 resource_size(&st->res));
		ret = -EINVAL;
		goto out;
	}
	st->clk = devm_memremap(dev, st->res.start, resource_size(&st->res),
				MEMREMAP_WB | MEMREMAP_DEC);
	if (IS_ERR(st->clk)) {
		ret = PTR_ERR(st->clk);
		dev_info(dev, "failed to map shared memory\n");
		st->clk = NULL;
		goto out;
	}

	if (le32_to_cpu(st->clk->magic) != VMCLOCK_MAGIC ||
	    le32_to_cpu(st->clk->size) > resource_size(&st->res) ||
	    le16_to_cpu(st->clk->version) != 1) {
		dev_info(dev, "vmclock magic fields invalid\n");
		ret = -EINVAL;
		goto out;
	}

	ret = ida_alloc(&vmclock_ida, GFP_KERNEL);
	if (ret < 0)
		goto out;

	st->index = ret;
	ret = devm_add_action_or_reset(&pdev->dev, vmclock_put_idx, st);
	if (ret)
		goto out;

	st->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "vmclock%d", st->index);
	if (!st->name) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * If the structure is big enough, it can be mapped to userspace.
	 * Theoretically a guest OS even using larger pages could still
	 * use 4KiB PTEs to map smaller MMIO regions like this, but let's
	 * cross that bridge if/when we come to it.
	 */
	if (le32_to_cpu(st->clk->size) >= PAGE_SIZE) {
		st->miscdev.minor = MISC_DYNAMIC_MINOR;
		st->miscdev.fops = &vmclock_miscdev_fops;
		st->miscdev.name = st->name;

		ret = misc_register(&st->miscdev);
		if (ret)
			goto out;
	}

	/* If there is valid clock information, register a PTP clock */
	if (VMCLOCK_FIELD_PRESENT(st->clk, time_frac_sec)) {
		/* Can return a silent NULL, or an error. */
		st->ptp_clock = vmclock_ptp_register(dev, st);
		if (IS_ERR(st->ptp_clock)) {
			ret = PTR_ERR(st->ptp_clock);
			st->ptp_clock = NULL;
			vmclock_remove(pdev);
			goto out;
		}
	}

	if (!st->miscdev.minor && !st->ptp_clock) {
		/* Neither miscdev nor PTP registered */
		dev_info(dev, "vmclock: Neither miscdev nor PTP available; not registering\n");
		ret = -ENODEV;
		goto out;
	}

	dev_info(dev, "%s: registered %s%s%s\n", st->name,
		 st->miscdev.minor ? "miscdev" : "",
		 (st->miscdev.minor && st->ptp_clock) ? ", " : "",
		 st->ptp_clock ? "PTP" : "");

	dev_set_drvdata(dev, st);

 out:
	return ret;
}

static const struct acpi_device_id vmclock_acpi_ids[] = {
	{ "AMZNC10C", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, vmclock_acpi_ids);

static struct platform_driver vmclock_platform_driver = {
	.probe		= vmclock_probe,
	.remove_new	= vmclock_remove,
	.driver	= {
		.name	= "vmclock",
		.acpi_match_table = vmclock_acpi_ids,
	},
};

module_platform_driver(vmclock_platform_driver)

MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("PTP clock using VMCLOCK");
MODULE_LICENSE("GPL");
