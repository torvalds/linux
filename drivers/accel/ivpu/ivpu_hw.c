// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 - 2024 Intel Corporation
 */

#include "ivpu_drv.h"
#include "ivpu_hw.h"
#include "ivpu_hw_btrs.h"
#include "ivpu_hw_ip.h"

#include <linux/dmi.h>
#include <linux/fault-inject.h>
#include <linux/pm_runtime.h>

#ifdef CONFIG_FAULT_INJECTION
DECLARE_FAULT_ATTR(ivpu_hw_failure);

static char *ivpu_fail_hw;
module_param_named_unsafe(fail_hw, ivpu_fail_hw, charp, 0444);
MODULE_PARM_DESC(fail_hw, "<interval>,<probability>,<space>,<times>");
#endif

static char *platform_to_str(u32 platform)
{
	switch (platform) {
	case IVPU_PLATFORM_SILICON:
		return "SILICON";
	case IVPU_PLATFORM_SIMICS:
		return "SIMICS";
	case IVPU_PLATFORM_FPGA:
		return "FPGA";
	case IVPU_PLATFORM_HSLE:
		return "HSLE";
	default:
		return "Invalid platform";
	}
}

static void platform_init(struct ivpu_device *vdev)
{
	int platform = ivpu_hw_btrs_platform_read(vdev);

	ivpu_dbg(vdev, MISC, "Platform type: %s (%d)\n", platform_to_str(platform), platform);

	switch (platform) {
	case IVPU_PLATFORM_SILICON:
	case IVPU_PLATFORM_SIMICS:
	case IVPU_PLATFORM_FPGA:
	case IVPU_PLATFORM_HSLE:
		vdev->platform = platform;
		break;

	default:
		ivpu_err(vdev, "Invalid platform type: %d\n", platform);
		break;
	}
}

static void wa_init(struct ivpu_device *vdev)
{
	vdev->wa.punit_disabled = false;
	vdev->wa.clear_runtime_mem = false;

	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		vdev->wa.interrupt_clear_with_0 = ivpu_hw_btrs_irqs_clear_with_0_mtl(vdev);

	if (ivpu_device_id(vdev) == PCI_DEVICE_ID_LNL &&
	    ivpu_revision(vdev) < IVPU_HW_IP_REV_LNL_B0)
		vdev->wa.disable_clock_relinquish = true;

	if (ivpu_test_mode & IVPU_TEST_MODE_CLK_RELINQ_ENABLE)
		vdev->wa.disable_clock_relinquish = false;

	if (ivpu_test_mode & IVPU_TEST_MODE_CLK_RELINQ_DISABLE)
		vdev->wa.disable_clock_relinquish = true;

	if (ivpu_hw_ip_gen(vdev) == IVPU_HW_IP_37XX)
		vdev->wa.wp0_during_power_up = true;

	if (ivpu_test_mode & IVPU_TEST_MODE_D0I2_DISABLE)
		vdev->wa.disable_d0i2 = true;

	IVPU_PRINT_WA(punit_disabled);
	IVPU_PRINT_WA(clear_runtime_mem);
	IVPU_PRINT_WA(interrupt_clear_with_0);
	IVPU_PRINT_WA(disable_clock_relinquish);
	IVPU_PRINT_WA(wp0_during_power_up);
	IVPU_PRINT_WA(disable_d0i2);
}

static void timeouts_init(struct ivpu_device *vdev)
{
	if (ivpu_test_mode & IVPU_TEST_MODE_DISABLE_TIMEOUTS) {
		vdev->timeout.boot = -1;
		vdev->timeout.jsm = -1;
		vdev->timeout.tdr = -1;
		vdev->timeout.autosuspend = -1;
		vdev->timeout.d0i3_entry_msg = -1;
	} else if (ivpu_is_fpga(vdev)) {
		vdev->timeout.boot = 50;
		vdev->timeout.jsm = 15000;
		vdev->timeout.tdr = 30000;
		vdev->timeout.autosuspend = -1;
		vdev->timeout.d0i3_entry_msg = 500;
		vdev->timeout.state_dump_msg = 10000;
	} else if (ivpu_is_simics(vdev)) {
		vdev->timeout.boot = 50;
		vdev->timeout.jsm = 500;
		vdev->timeout.tdr = 10000;
		vdev->timeout.autosuspend = 100;
		vdev->timeout.d0i3_entry_msg = 100;
		vdev->timeout.state_dump_msg = 10;
	} else {
		vdev->timeout.boot = 1000;
		vdev->timeout.jsm = 500;
		vdev->timeout.tdr = 2000;
		if (ivpu_hw_ip_gen(vdev) == IVPU_HW_IP_37XX)
			vdev->timeout.autosuspend = 10;
		else
			vdev->timeout.autosuspend = 100;
		vdev->timeout.d0i3_entry_msg = 5;
		vdev->timeout.state_dump_msg = 100;
	}
}

static void priority_bands_init(struct ivpu_device *vdev)
{
	/* Idle */
	vdev->hw->hws.grace_period[VPU_JOB_SCHEDULING_PRIORITY_BAND_IDLE] = 0;
	vdev->hw->hws.process_grace_period[VPU_JOB_SCHEDULING_PRIORITY_BAND_IDLE] = 50000;
	vdev->hw->hws.process_quantum[VPU_JOB_SCHEDULING_PRIORITY_BAND_IDLE] = 160000;
	/* Normal */
	vdev->hw->hws.grace_period[VPU_JOB_SCHEDULING_PRIORITY_BAND_NORMAL] = 50000;
	vdev->hw->hws.process_grace_period[VPU_JOB_SCHEDULING_PRIORITY_BAND_NORMAL] = 50000;
	vdev->hw->hws.process_quantum[VPU_JOB_SCHEDULING_PRIORITY_BAND_NORMAL] = 300000;
	/* Focus */
	vdev->hw->hws.grace_period[VPU_JOB_SCHEDULING_PRIORITY_BAND_FOCUS] = 50000;
	vdev->hw->hws.process_grace_period[VPU_JOB_SCHEDULING_PRIORITY_BAND_FOCUS] = 50000;
	vdev->hw->hws.process_quantum[VPU_JOB_SCHEDULING_PRIORITY_BAND_FOCUS] = 200000;
	/* Realtime */
	vdev->hw->hws.grace_period[VPU_JOB_SCHEDULING_PRIORITY_BAND_REALTIME] = 0;
	vdev->hw->hws.process_grace_period[VPU_JOB_SCHEDULING_PRIORITY_BAND_REALTIME] = 50000;
	vdev->hw->hws.process_quantum[VPU_JOB_SCHEDULING_PRIORITY_BAND_REALTIME] = 200000;
}

static void memory_ranges_init(struct ivpu_device *vdev)
{
	if (ivpu_hw_ip_gen(vdev) == IVPU_HW_IP_37XX) {
		ivpu_hw_range_init(&vdev->hw->ranges.global, 0x80000000, SZ_512M);
		ivpu_hw_range_init(&vdev->hw->ranges.user,   0x88000000, 511 * SZ_1M);
		ivpu_hw_range_init(&vdev->hw->ranges.shave, 0x180000000, SZ_2G);
		ivpu_hw_range_init(&vdev->hw->ranges.dma,   0x200000000, SZ_128G);
	} else {
		ivpu_hw_range_init(&vdev->hw->ranges.global, 0x80000000, SZ_512M);
		ivpu_hw_range_init(&vdev->hw->ranges.shave,  0x80000000, SZ_2G);
		ivpu_hw_range_init(&vdev->hw->ranges.user,  0x100000000, SZ_256G);
		vdev->hw->ranges.dma = vdev->hw->ranges.user;
	}
}

static int wp_enable(struct ivpu_device *vdev)
{
	return ivpu_hw_btrs_wp_drive(vdev, true);
}

static int wp_disable(struct ivpu_device *vdev)
{
	return ivpu_hw_btrs_wp_drive(vdev, false);
}

int ivpu_hw_power_up(struct ivpu_device *vdev)
{
	int ret;

	if (IVPU_WA(wp0_during_power_up)) {
		/* WP requests may fail when powering down, so issue WP 0 here */
		ret = wp_disable(vdev);
		if (ret)
			ivpu_warn(vdev, "Failed to disable workpoint: %d\n", ret);
	}

	ret = ivpu_hw_btrs_d0i3_disable(vdev);
	if (ret)
		ivpu_warn(vdev, "Failed to disable D0I3: %d\n", ret);

	ret = wp_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to enable workpoint: %d\n", ret);
		return ret;
	}

	if (ivpu_hw_btrs_gen(vdev) >= IVPU_HW_BTRS_LNL) {
		if (IVPU_WA(disable_clock_relinquish))
			ivpu_hw_btrs_clock_relinquish_disable_lnl(vdev);
		ivpu_hw_btrs_profiling_freq_reg_set_lnl(vdev);
		ivpu_hw_btrs_ats_print_lnl(vdev);
	}

	ret = ivpu_hw_ip_host_ss_configure(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to configure host SS: %d\n", ret);
		return ret;
	}

	ivpu_hw_ip_idle_gen_disable(vdev);

	ret = ivpu_hw_btrs_wait_for_clock_res_own_ack(vdev);
	if (ret) {
		ivpu_err(vdev, "Timed out waiting for clock resource own ACK\n");
		return ret;
	}

	ret = ivpu_hw_ip_pwr_domain_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to enable power domain: %d\n", ret);
		return ret;
	}

	ret = ivpu_hw_ip_host_ss_axi_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to enable AXI: %d\n", ret);
		return ret;
	}

	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_LNL)
		ivpu_hw_btrs_set_port_arbitration_weights_lnl(vdev);

	ret = ivpu_hw_ip_top_noc_enable(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to enable TOP NOC: %d\n", ret);

	return ret;
}

static void save_d0i3_entry_timestamp(struct ivpu_device *vdev)
{
	vdev->hw->d0i3_entry_host_ts = ktime_get_boottime();
	vdev->hw->d0i3_entry_vpu_ts = ivpu_hw_ip_read_perf_timer_counter(vdev);
}

int ivpu_hw_reset(struct ivpu_device *vdev)
{
	int ret = 0;

	if (ivpu_hw_btrs_ip_reset(vdev)) {
		ivpu_err(vdev, "Failed to reset NPU IP\n");
		ret = -EIO;
	}

	if (wp_disable(vdev)) {
		ivpu_err(vdev, "Failed to disable workpoint\n");
		ret = -EIO;
	}

	return ret;
}

int ivpu_hw_power_down(struct ivpu_device *vdev)
{
	int ret = 0;

	save_d0i3_entry_timestamp(vdev);

	if (!ivpu_hw_is_idle(vdev))
		ivpu_warn(vdev, "NPU not idle during power down\n");

	if (ivpu_hw_reset(vdev)) {
		ivpu_err(vdev, "Failed to reset NPU\n");
		ret = -EIO;
	}

	if (ivpu_hw_btrs_d0i3_enable(vdev)) {
		ivpu_err(vdev, "Failed to enter D0I3\n");
		ret = -EIO;
	}

	return ret;
}

int ivpu_hw_init(struct ivpu_device *vdev)
{
	ivpu_hw_btrs_info_init(vdev);
	ivpu_hw_btrs_freq_ratios_init(vdev);
	priority_bands_init(vdev);
	memory_ranges_init(vdev);
	platform_init(vdev);
	wa_init(vdev);
	timeouts_init(vdev);
	atomic_set(&vdev->hw->firewall_irq_counter, 0);

#ifdef CONFIG_FAULT_INJECTION
	if (ivpu_fail_hw)
		setup_fault_attr(&ivpu_hw_failure, ivpu_fail_hw);
#endif

	return 0;
}

int ivpu_hw_boot_fw(struct ivpu_device *vdev)
{
	int ret;

	ivpu_hw_ip_snoop_disable(vdev);
	ivpu_hw_ip_tbu_mmu_enable(vdev);
	ret = ivpu_hw_ip_soc_cpu_boot(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to boot SOC CPU: %d\n", ret);

	return ret;
}

void ivpu_hw_profiling_freq_drive(struct ivpu_device *vdev, bool enable)
{
	if (ivpu_hw_ip_gen(vdev) == IVPU_HW_IP_37XX) {
		vdev->hw->pll.profiling_freq = PLL_PROFILING_FREQ_DEFAULT;
		return;
	}

	if (enable)
		vdev->hw->pll.profiling_freq = PLL_PROFILING_FREQ_HIGH;
	else
		vdev->hw->pll.profiling_freq = PLL_PROFILING_FREQ_DEFAULT;
}

void ivpu_irq_handlers_init(struct ivpu_device *vdev)
{
	if (ivpu_hw_ip_gen(vdev) == IVPU_HW_IP_37XX)
		vdev->hw->irq.ip_irq_handler = ivpu_hw_ip_irq_handler_37xx;
	else
		vdev->hw->irq.ip_irq_handler = ivpu_hw_ip_irq_handler_40xx;

	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		vdev->hw->irq.btrs_irq_handler = ivpu_hw_btrs_irq_handler_mtl;
	else
		vdev->hw->irq.btrs_irq_handler = ivpu_hw_btrs_irq_handler_lnl;
}

void ivpu_hw_irq_enable(struct ivpu_device *vdev)
{
	ivpu_hw_ip_irq_enable(vdev);
	ivpu_hw_btrs_irq_enable(vdev);
}

void ivpu_hw_irq_disable(struct ivpu_device *vdev)
{
	ivpu_hw_btrs_irq_disable(vdev);
	ivpu_hw_ip_irq_disable(vdev);
}

irqreturn_t ivpu_hw_irq_handler(int irq, void *ptr)
{
	struct ivpu_device *vdev = ptr;
	bool ip_handled, btrs_handled;

	ivpu_hw_btrs_global_int_disable(vdev);

	btrs_handled = ivpu_hw_btrs_irq_handler(vdev, irq);
	if (!ivpu_hw_is_idle((vdev)) || !btrs_handled)
		ip_handled = ivpu_hw_ip_irq_handler(vdev, irq);
	else
		ip_handled = false;

	/* Re-enable global interrupts to re-trigger MSI for pending interrupts */
	ivpu_hw_btrs_global_int_enable(vdev);

	if (!ip_handled && !btrs_handled)
		return IRQ_NONE;

	pm_runtime_mark_last_busy(vdev->drm.dev);
	return IRQ_HANDLED;
}
