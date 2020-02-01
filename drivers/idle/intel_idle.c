// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_idle.c - native hardware idle loop for modern Intel processors
 *
 * Copyright (c) 2013, Intel Corporation.
 * Len Brown <len.brown@intel.com>
 */

/*
 * intel_idle is a cpuidle driver that loads on specific Intel processors
 * in lieu of the legacy ACPI processor_idle driver.  The intent is to
 * make Linux more efficient on these processors, as intel_idle knows
 * more than ACPI, as well as make Linux more immune to ACPI BIOS bugs.
 */

/*
 * Design Assumptions
 *
 * All CPUs have same idle states as boot CPU
 *
 * Chipset BM_STS (bus master status) bit is a NOP
 *	for preventing entry into deep C-stats
 */

/*
 * Known limitations
 *
 * The driver currently initializes for_each_online_cpu() upon modprobe.
 * It it unaware of subsequent processors hot-added to the system.
 * This means that if you boot with maxcpus=n and later online
 * processors above n, those processors will use C1 only.
 *
 * ACPI has a .suspend hack to turn off deep c-statees during suspend
 * to avoid complications with the lapic timer workaround.
 * Have not seen issues with suspend, but may need same workaround here.
 *
 */

/* un-comment DEBUG to enable pr_debug() statements */
#define DEBUG

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/cpuidle.h>
#include <linux/tick.h>
#include <trace/events/power.h>
#include <linux/sched.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/moduleparam.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/mwait.h>
#include <asm/msr.h>

#define INTEL_IDLE_VERSION "0.4.1"

static struct cpuidle_driver intel_idle_driver = {
	.name = "intel_idle",
	.owner = THIS_MODULE,
};
/* intel_idle.max_cstate=0 disables driver */
static int max_cstate = CPUIDLE_STATE_MAX - 1;

static unsigned int mwait_substates;

#define LAPIC_TIMER_ALWAYS_RELIABLE 0xFFFFFFFF
/* Reliable LAPIC Timer States, bit 1 for C1 etc.  */
static unsigned int lapic_timer_reliable_states = (1 << 1);	 /* Default to only C1 */

struct idle_cpu {
	struct cpuidle_state *state_table;

	/*
	 * Hardware C-state auto-demotion may not always be optimal.
	 * Indicate which enable bits to clear here.
	 */
	unsigned long auto_demotion_disable_flags;
	bool byt_auto_demotion_disable_flag;
	bool disable_promotion_to_c1e;
};

static const struct idle_cpu *icpu;
static struct cpuidle_device __percpu *intel_idle_cpuidle_devices;
static int intel_idle(struct cpuidle_device *dev,
			struct cpuidle_driver *drv, int index);
static void intel_idle_s2idle(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv, int index);
static struct cpuidle_state *cpuidle_state_table;

/*
 * Set this flag for states where the HW flushes the TLB for us
 * and so we don't need cross-calls to keep it consistent.
 * If this flag is set, SW flushes the TLB, so even if the
 * HW doesn't do the flushing, this flag is safe to use.
 */
#define CPUIDLE_FLAG_TLB_FLUSHED	0x10000

/*
 * MWAIT takes an 8-bit "hint" in EAX "suggesting"
 * the C-state (top nibble) and sub-state (bottom nibble)
 * 0x00 means "MWAIT(C1)", 0x10 means "MWAIT(C2)" etc.
 *
 * We store the hint at the top of our "flags" for each state.
 */
#define flg2MWAIT(flags) (((flags) >> 24) & 0xFF)
#define MWAIT2flg(eax) ((eax & 0xFF) << 24)

/*
 * States are indexed by the cstate number,
 * which is also the index into the MWAIT hint array.
 * Thus C0 is a dummy.
 */
static struct cpuidle_state nehalem_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 3,
		.target_residency = 6,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 20,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C3",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 20,
		.target_residency = 80,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 200,
		.target_residency = 800,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state snb_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 2,
		.target_residency = 2,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 20,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C3",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 80,
		.target_residency = 211,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 104,
		.target_residency = 345,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7",
		.desc = "MWAIT 0x30",
		.flags = MWAIT2flg(0x30) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 109,
		.target_residency = 345,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state byt_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 1,
		.target_residency = 1,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6N",
		.desc = "MWAIT 0x58",
		.flags = MWAIT2flg(0x58) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 300,
		.target_residency = 275,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6S",
		.desc = "MWAIT 0x52",
		.flags = MWAIT2flg(0x52) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 500,
		.target_residency = 560,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7",
		.desc = "MWAIT 0x60",
		.flags = MWAIT2flg(0x60) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 1200,
		.target_residency = 4000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7S",
		.desc = "MWAIT 0x64",
		.flags = MWAIT2flg(0x64) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 10000,
		.target_residency = 20000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state cht_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 1,
		.target_residency = 1,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6N",
		.desc = "MWAIT 0x58",
		.flags = MWAIT2flg(0x58) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 80,
		.target_residency = 275,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6S",
		.desc = "MWAIT 0x52",
		.flags = MWAIT2flg(0x52) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 200,
		.target_residency = 560,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7",
		.desc = "MWAIT 0x60",
		.flags = MWAIT2flg(0x60) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 1200,
		.target_residency = 4000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7S",
		.desc = "MWAIT 0x64",
		.flags = MWAIT2flg(0x64) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 10000,
		.target_residency = 20000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state ivb_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 1,
		.target_residency = 1,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 20,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C3",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 59,
		.target_residency = 156,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 80,
		.target_residency = 300,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7",
		.desc = "MWAIT 0x30",
		.flags = MWAIT2flg(0x30) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 87,
		.target_residency = 300,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state ivt_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 1,
		.target_residency = 1,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 80,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C3",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 59,
		.target_residency = 156,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 82,
		.target_residency = 300,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state ivt_cstates_4s[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 1,
		.target_residency = 1,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 250,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C3",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 59,
		.target_residency = 300,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 84,
		.target_residency = 400,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state ivt_cstates_8s[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 1,
		.target_residency = 1,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 500,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C3",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 59,
		.target_residency = 600,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 88,
		.target_residency = 700,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state hsw_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 2,
		.target_residency = 2,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 20,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C3",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 33,
		.target_residency = 100,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 133,
		.target_residency = 400,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7s",
		.desc = "MWAIT 0x32",
		.flags = MWAIT2flg(0x32) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 166,
		.target_residency = 500,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C8",
		.desc = "MWAIT 0x40",
		.flags = MWAIT2flg(0x40) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 300,
		.target_residency = 900,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C9",
		.desc = "MWAIT 0x50",
		.flags = MWAIT2flg(0x50) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 600,
		.target_residency = 1800,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C10",
		.desc = "MWAIT 0x60",
		.flags = MWAIT2flg(0x60) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 2600,
		.target_residency = 7700,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};
static struct cpuidle_state bdw_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 2,
		.target_residency = 2,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 20,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C3",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 40,
		.target_residency = 100,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 133,
		.target_residency = 400,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7s",
		.desc = "MWAIT 0x32",
		.flags = MWAIT2flg(0x32) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 166,
		.target_residency = 500,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C8",
		.desc = "MWAIT 0x40",
		.flags = MWAIT2flg(0x40) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 300,
		.target_residency = 900,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C9",
		.desc = "MWAIT 0x50",
		.flags = MWAIT2flg(0x50) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 600,
		.target_residency = 1800,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C10",
		.desc = "MWAIT 0x60",
		.flags = MWAIT2flg(0x60) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 2600,
		.target_residency = 7700,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state skl_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 2,
		.target_residency = 2,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 20,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C3",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 70,
		.target_residency = 100,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 85,
		.target_residency = 200,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7s",
		.desc = "MWAIT 0x33",
		.flags = MWAIT2flg(0x33) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 124,
		.target_residency = 800,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C8",
		.desc = "MWAIT 0x40",
		.flags = MWAIT2flg(0x40) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 200,
		.target_residency = 800,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C9",
		.desc = "MWAIT 0x50",
		.flags = MWAIT2flg(0x50) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 480,
		.target_residency = 5000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C10",
		.desc = "MWAIT 0x60",
		.flags = MWAIT2flg(0x60) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 890,
		.target_residency = 5000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state skx_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 2,
		.target_residency = 2,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 20,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 133,
		.target_residency = 600,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state atom_cstates[] = {
	{
		.name = "C1E",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 10,
		.target_residency = 20,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C2",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10),
		.exit_latency = 20,
		.target_residency = 80,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C4",
		.desc = "MWAIT 0x30",
		.flags = MWAIT2flg(0x30) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 100,
		.target_residency = 400,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x52",
		.flags = MWAIT2flg(0x52) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 140,
		.target_residency = 560,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};
static struct cpuidle_state tangier_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 1,
		.target_residency = 4,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C4",
		.desc = "MWAIT 0x30",
		.flags = MWAIT2flg(0x30) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 100,
		.target_residency = 400,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x52",
		.flags = MWAIT2flg(0x52) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 140,
		.target_residency = 560,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7",
		.desc = "MWAIT 0x60",
		.flags = MWAIT2flg(0x60) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 1200,
		.target_residency = 4000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C9",
		.desc = "MWAIT 0x64",
		.flags = MWAIT2flg(0x64) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 10000,
		.target_residency = 20000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};
static struct cpuidle_state avn_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 2,
		.target_residency = 2,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x51",
		.flags = MWAIT2flg(0x51) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 15,
		.target_residency = 45,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};
static struct cpuidle_state knl_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 1,
		.target_residency = 2,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle },
	{
		.name = "C6",
		.desc = "MWAIT 0x10",
		.flags = MWAIT2flg(0x10) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 120,
		.target_residency = 500,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle },
	{
		.enter = NULL }
};

static struct cpuidle_state bxt_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 2,
		.target_residency = 2,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 20,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 133,
		.target_residency = 133,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C7s",
		.desc = "MWAIT 0x31",
		.flags = MWAIT2flg(0x31) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 155,
		.target_residency = 155,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C8",
		.desc = "MWAIT 0x40",
		.flags = MWAIT2flg(0x40) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 1000,
		.target_residency = 1000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C9",
		.desc = "MWAIT 0x50",
		.flags = MWAIT2flg(0x50) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 2000,
		.target_residency = 2000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C10",
		.desc = "MWAIT 0x60",
		.flags = MWAIT2flg(0x60) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 10000,
		.target_residency = 10000,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

static struct cpuidle_state dnv_cstates[] = {
	{
		.name = "C1",
		.desc = "MWAIT 0x00",
		.flags = MWAIT2flg(0x00),
		.exit_latency = 2,
		.target_residency = 2,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C1E",
		.desc = "MWAIT 0x01",
		.flags = MWAIT2flg(0x01),
		.exit_latency = 10,
		.target_residency = 20,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.name = "C6",
		.desc = "MWAIT 0x20",
		.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,
		.exit_latency = 50,
		.target_residency = 500,
		.enter = &intel_idle,
		.enter_s2idle = intel_idle_s2idle, },
	{
		.enter = NULL }
};

/**
 * intel_idle
 * @dev: cpuidle_device
 * @drv: cpuidle driver
 * @index: index of cpuidle state
 *
 * Must be called under local_irq_disable().
 */
static __cpuidle int intel_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int index)
{
	unsigned long ecx = 1; /* break on interrupt flag */
	struct cpuidle_state *state = &drv->states[index];
	unsigned long eax = flg2MWAIT(state->flags);
	unsigned int cstate;
	bool uninitialized_var(tick);
	int cpu = smp_processor_id();

	/*
	 * leave_mm() to avoid costly and often unnecessary wakeups
	 * for flushing the user TLB's associated with the active mm.
	 */
	if (state->flags & CPUIDLE_FLAG_TLB_FLUSHED)
		leave_mm(cpu);

	if (!static_cpu_has(X86_FEATURE_ARAT)) {
		cstate = (((eax) >> MWAIT_SUBSTATE_SIZE) &
				MWAIT_CSTATE_MASK) + 1;
		tick = false;
		if (!(lapic_timer_reliable_states & (1 << (cstate)))) {
			tick = true;
			tick_broadcast_enter();
		}
	}

	mwait_idle_with_hints(eax, ecx);

	if (!static_cpu_has(X86_FEATURE_ARAT) && tick)
		tick_broadcast_exit();

	return index;
}

/**
 * intel_idle_s2idle - simplified "enter" callback routine for suspend-to-idle
 * @dev: cpuidle_device
 * @drv: cpuidle driver
 * @index: state index
 */
static void intel_idle_s2idle(struct cpuidle_device *dev,
			     struct cpuidle_driver *drv, int index)
{
	unsigned long ecx = 1; /* break on interrupt flag */
	unsigned long eax = flg2MWAIT(drv->states[index].flags);

	mwait_idle_with_hints(eax, ecx);
}

static void __setup_broadcast_timer(bool on)
{
	if (on)
		tick_broadcast_enable();
	else
		tick_broadcast_disable();
}

static void auto_demotion_disable(void)
{
	unsigned long long msr_bits;

	rdmsrl(MSR_PKG_CST_CONFIG_CONTROL, msr_bits);
	msr_bits &= ~(icpu->auto_demotion_disable_flags);
	wrmsrl(MSR_PKG_CST_CONFIG_CONTROL, msr_bits);
}
static void c1e_promotion_disable(void)
{
	unsigned long long msr_bits;

	rdmsrl(MSR_IA32_POWER_CTL, msr_bits);
	msr_bits &= ~0x2;
	wrmsrl(MSR_IA32_POWER_CTL, msr_bits);
}

static const struct idle_cpu idle_cpu_nehalem = {
	.state_table = nehalem_cstates,
	.auto_demotion_disable_flags = NHM_C1_AUTO_DEMOTE | NHM_C3_AUTO_DEMOTE,
	.disable_promotion_to_c1e = true,
};

static const struct idle_cpu idle_cpu_atom = {
	.state_table = atom_cstates,
};

static const struct idle_cpu idle_cpu_tangier = {
	.state_table = tangier_cstates,
};

static const struct idle_cpu idle_cpu_lincroft = {
	.state_table = atom_cstates,
	.auto_demotion_disable_flags = ATM_LNC_C6_AUTO_DEMOTE,
};

static const struct idle_cpu idle_cpu_snb = {
	.state_table = snb_cstates,
	.disable_promotion_to_c1e = true,
};

static const struct idle_cpu idle_cpu_byt = {
	.state_table = byt_cstates,
	.disable_promotion_to_c1e = true,
	.byt_auto_demotion_disable_flag = true,
};

static const struct idle_cpu idle_cpu_cht = {
	.state_table = cht_cstates,
	.disable_promotion_to_c1e = true,
	.byt_auto_demotion_disable_flag = true,
};

static const struct idle_cpu idle_cpu_ivb = {
	.state_table = ivb_cstates,
	.disable_promotion_to_c1e = true,
};

static const struct idle_cpu idle_cpu_ivt = {
	.state_table = ivt_cstates,
	.disable_promotion_to_c1e = true,
};

static const struct idle_cpu idle_cpu_hsw = {
	.state_table = hsw_cstates,
	.disable_promotion_to_c1e = true,
};

static const struct idle_cpu idle_cpu_bdw = {
	.state_table = bdw_cstates,
	.disable_promotion_to_c1e = true,
};

static const struct idle_cpu idle_cpu_skl = {
	.state_table = skl_cstates,
	.disable_promotion_to_c1e = true,
};

static const struct idle_cpu idle_cpu_skx = {
	.state_table = skx_cstates,
	.disable_promotion_to_c1e = true,
};

static const struct idle_cpu idle_cpu_avn = {
	.state_table = avn_cstates,
	.disable_promotion_to_c1e = true,
};

static const struct idle_cpu idle_cpu_knl = {
	.state_table = knl_cstates,
};

static const struct idle_cpu idle_cpu_bxt = {
	.state_table = bxt_cstates,
	.disable_promotion_to_c1e = true,
};

static const struct idle_cpu idle_cpu_dnv = {
	.state_table = dnv_cstates,
	.disable_promotion_to_c1e = true,
};

static const struct x86_cpu_id intel_idle_ids[] __initconst = {
	INTEL_CPU_FAM6(NEHALEM_EP,		idle_cpu_nehalem),
	INTEL_CPU_FAM6(NEHALEM,			idle_cpu_nehalem),
	INTEL_CPU_FAM6(NEHALEM_G,		idle_cpu_nehalem),
	INTEL_CPU_FAM6(WESTMERE,		idle_cpu_nehalem),
	INTEL_CPU_FAM6(WESTMERE_EP,		idle_cpu_nehalem),
	INTEL_CPU_FAM6(NEHALEM_EX,		idle_cpu_nehalem),
	INTEL_CPU_FAM6(ATOM_BONNELL,		idle_cpu_atom),
	INTEL_CPU_FAM6(ATOM_BONNELL_MID,	idle_cpu_lincroft),
	INTEL_CPU_FAM6(WESTMERE_EX,		idle_cpu_nehalem),
	INTEL_CPU_FAM6(SANDYBRIDGE,		idle_cpu_snb),
	INTEL_CPU_FAM6(SANDYBRIDGE_X,		idle_cpu_snb),
	INTEL_CPU_FAM6(ATOM_SALTWELL,		idle_cpu_atom),
	INTEL_CPU_FAM6(ATOM_SILVERMONT,		idle_cpu_byt),
	INTEL_CPU_FAM6(ATOM_SILVERMONT_MID,	idle_cpu_tangier),
	INTEL_CPU_FAM6(ATOM_AIRMONT,		idle_cpu_cht),
	INTEL_CPU_FAM6(IVYBRIDGE,		idle_cpu_ivb),
	INTEL_CPU_FAM6(IVYBRIDGE_X,		idle_cpu_ivt),
	INTEL_CPU_FAM6(HASWELL,			idle_cpu_hsw),
	INTEL_CPU_FAM6(HASWELL_X,		idle_cpu_hsw),
	INTEL_CPU_FAM6(HASWELL_L,		idle_cpu_hsw),
	INTEL_CPU_FAM6(HASWELL_G,		idle_cpu_hsw),
	INTEL_CPU_FAM6(ATOM_SILVERMONT_D,	idle_cpu_avn),
	INTEL_CPU_FAM6(BROADWELL,		idle_cpu_bdw),
	INTEL_CPU_FAM6(BROADWELL_G,		idle_cpu_bdw),
	INTEL_CPU_FAM6(BROADWELL_X,		idle_cpu_bdw),
	INTEL_CPU_FAM6(BROADWELL_D,		idle_cpu_bdw),
	INTEL_CPU_FAM6(SKYLAKE_L,		idle_cpu_skl),
	INTEL_CPU_FAM6(SKYLAKE,			idle_cpu_skl),
	INTEL_CPU_FAM6(KABYLAKE_L,		idle_cpu_skl),
	INTEL_CPU_FAM6(KABYLAKE,		idle_cpu_skl),
	INTEL_CPU_FAM6(SKYLAKE_X,		idle_cpu_skx),
	INTEL_CPU_FAM6(XEON_PHI_KNL,		idle_cpu_knl),
	INTEL_CPU_FAM6(XEON_PHI_KNM,		idle_cpu_knl),
	INTEL_CPU_FAM6(ATOM_GOLDMONT,		idle_cpu_bxt),
	INTEL_CPU_FAM6(ATOM_GOLDMONT_PLUS,	idle_cpu_bxt),
	INTEL_CPU_FAM6(ATOM_GOLDMONT_D,		idle_cpu_dnv),
	INTEL_CPU_FAM6(ATOM_TREMONT_D,		idle_cpu_dnv),
	{}
};

/*
 * intel_idle_probe()
 */
static int __init intel_idle_probe(void)
{
	unsigned int eax, ebx, ecx;
	const struct x86_cpu_id *id;

	if (max_cstate == 0) {
		pr_debug("disabled\n");
		return -EPERM;
	}

	id = x86_match_cpu(intel_idle_ids);
	if (!id) {
		if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
		    boot_cpu_data.x86 == 6)
			pr_debug("does not run on family %d model %d\n",
				 boot_cpu_data.x86, boot_cpu_data.x86_model);
		return -ENODEV;
	}

	if (!boot_cpu_has(X86_FEATURE_MWAIT)) {
		pr_debug("Please enable MWAIT in BIOS SETUP\n");
		return -ENODEV;
	}

	if (boot_cpu_data.cpuid_level < CPUID_MWAIT_LEAF)
		return -ENODEV;

	cpuid(CPUID_MWAIT_LEAF, &eax, &ebx, &ecx, &mwait_substates);

	if (!(ecx & CPUID5_ECX_EXTENSIONS_SUPPORTED) ||
	    !(ecx & CPUID5_ECX_INTERRUPT_BREAK) ||
	    !mwait_substates)
			return -ENODEV;

	pr_debug("MWAIT substates: 0x%x\n", mwait_substates);

	icpu = (const struct idle_cpu *)id->driver_data;
	cpuidle_state_table = icpu->state_table;

	pr_debug("v" INTEL_IDLE_VERSION " model 0x%X\n",
		 boot_cpu_data.x86_model);

	return 0;
}

/*
 * intel_idle_cpuidle_devices_uninit()
 * Unregisters the cpuidle devices.
 */
static void intel_idle_cpuidle_devices_uninit(void)
{
	int i;
	struct cpuidle_device *dev;

	for_each_online_cpu(i) {
		dev = per_cpu_ptr(intel_idle_cpuidle_devices, i);
		cpuidle_unregister_device(dev);
	}
}

/*
 * ivt_idle_state_table_update(void)
 *
 * Tune IVT multi-socket targets
 * Assumption: num_sockets == (max_package_num + 1)
 */
static void ivt_idle_state_table_update(void)
{
	/* IVT uses a different table for 1-2, 3-4, and > 4 sockets */
	int cpu, package_num, num_sockets = 1;

	for_each_online_cpu(cpu) {
		package_num = topology_physical_package_id(cpu);
		if (package_num + 1 > num_sockets) {
			num_sockets = package_num + 1;

			if (num_sockets > 4) {
				cpuidle_state_table = ivt_cstates_8s;
				return;
			}
		}
	}

	if (num_sockets > 2)
		cpuidle_state_table = ivt_cstates_4s;

	/* else, 1 and 2 socket systems use default ivt_cstates */
}

/*
 * Translate IRTL (Interrupt Response Time Limit) MSR to usec
 */

static unsigned int irtl_ns_units[] = {
	1, 32, 1024, 32768, 1048576, 33554432, 0, 0 };

static unsigned long long irtl_2_usec(unsigned long long irtl)
{
	unsigned long long ns;

	if (!irtl)
		return 0;

	ns = irtl_ns_units[(irtl >> 10) & 0x7];

	return div64_u64((irtl & 0x3FF) * ns, 1000);
}
/*
 * bxt_idle_state_table_update(void)
 *
 * On BXT, we trust the IRTL to show the definitive maximum latency
 * We use the same value for target_residency.
 */
static void bxt_idle_state_table_update(void)
{
	unsigned long long msr;
	unsigned int usec;

	rdmsrl(MSR_PKGC6_IRTL, msr);
	usec = irtl_2_usec(msr);
	if (usec) {
		bxt_cstates[2].exit_latency = usec;
		bxt_cstates[2].target_residency = usec;
	}

	rdmsrl(MSR_PKGC7_IRTL, msr);
	usec = irtl_2_usec(msr);
	if (usec) {
		bxt_cstates[3].exit_latency = usec;
		bxt_cstates[3].target_residency = usec;
	}

	rdmsrl(MSR_PKGC8_IRTL, msr);
	usec = irtl_2_usec(msr);
	if (usec) {
		bxt_cstates[4].exit_latency = usec;
		bxt_cstates[4].target_residency = usec;
	}

	rdmsrl(MSR_PKGC9_IRTL, msr);
	usec = irtl_2_usec(msr);
	if (usec) {
		bxt_cstates[5].exit_latency = usec;
		bxt_cstates[5].target_residency = usec;
	}

	rdmsrl(MSR_PKGC10_IRTL, msr);
	usec = irtl_2_usec(msr);
	if (usec) {
		bxt_cstates[6].exit_latency = usec;
		bxt_cstates[6].target_residency = usec;
	}

}
/*
 * sklh_idle_state_table_update(void)
 *
 * On SKL-H (model 0x5e) disable C8 and C9 if:
 * C10 is enabled and SGX disabled
 */
static void sklh_idle_state_table_update(void)
{
	unsigned long long msr;
	unsigned int eax, ebx, ecx, edx;


	/* if PC10 disabled via cmdline intel_idle.max_cstate=7 or shallower */
	if (max_cstate <= 7)
		return;

	/* if PC10 not present in CPUID.MWAIT.EDX */
	if ((mwait_substates & (0xF << 28)) == 0)
		return;

	rdmsrl(MSR_PKG_CST_CONFIG_CONTROL, msr);

	/* PC10 is not enabled in PKG C-state limit */
	if ((msr & 0xF) != 8)
		return;

	ecx = 0;
	cpuid(7, &eax, &ebx, &ecx, &edx);

	/* if SGX is present */
	if (ebx & (1 << 2)) {

		rdmsrl(MSR_IA32_FEATURE_CONTROL, msr);

		/* if SGX is enabled */
		if (msr & (1 << 18))
			return;
	}

	skl_cstates[5].flags |= CPUIDLE_FLAG_UNUSABLE;	/* C8-SKL */
	skl_cstates[6].flags |= CPUIDLE_FLAG_UNUSABLE;	/* C9-SKL */
}
/*
 * intel_idle_state_table_update()
 *
 * Update the default state_table for this CPU-id
 */

static void intel_idle_state_table_update(void)
{
	switch (boot_cpu_data.x86_model) {

	case INTEL_FAM6_IVYBRIDGE_X:
		ivt_idle_state_table_update();
		break;
	case INTEL_FAM6_ATOM_GOLDMONT:
	case INTEL_FAM6_ATOM_GOLDMONT_PLUS:
		bxt_idle_state_table_update();
		break;
	case INTEL_FAM6_SKYLAKE:
		sklh_idle_state_table_update();
		break;
	}
}

/*
 * intel_idle_cpuidle_driver_init()
 * allocate, initialize cpuidle_states
 */
static void __init intel_idle_cpuidle_driver_init(void)
{
	int cstate;
	struct cpuidle_driver *drv = &intel_idle_driver;

	intel_idle_state_table_update();

	cpuidle_poll_state_init(drv);
	drv->state_count = 1;

	for (cstate = 0; cstate < CPUIDLE_STATE_MAX; ++cstate) {
		int num_substates, mwait_hint, mwait_cstate;

		if ((cpuidle_state_table[cstate].enter == NULL) &&
		    (cpuidle_state_table[cstate].enter_s2idle == NULL))
			break;

		if (cstate + 1 > max_cstate) {
			pr_info("max_cstate %d reached\n", max_cstate);
			break;
		}

		mwait_hint = flg2MWAIT(cpuidle_state_table[cstate].flags);
		mwait_cstate = MWAIT_HINT2CSTATE(mwait_hint);

		/* number of sub-states for this state in CPUID.MWAIT */
		num_substates = (mwait_substates >> ((mwait_cstate + 1) * 4))
					& MWAIT_SUBSTATE_MASK;

		/* if NO sub-states for this state in CPUID, skip it */
		if (num_substates == 0)
			continue;

		/* if state marked as disabled, skip it */
		if (cpuidle_state_table[cstate].flags & CPUIDLE_FLAG_UNUSABLE) {
			pr_debug("state %s is disabled\n",
				 cpuidle_state_table[cstate].name);
			continue;
		}


		if (((mwait_cstate + 1) > 2) &&
			!boot_cpu_has(X86_FEATURE_NONSTOP_TSC))
			mark_tsc_unstable("TSC halts in idle"
					" states deeper than C2");

		drv->states[drv->state_count] =	/* structure copy */
			cpuidle_state_table[cstate];

		drv->state_count += 1;
	}

	if (icpu->byt_auto_demotion_disable_flag) {
		wrmsrl(MSR_CC6_DEMOTION_POLICY_CONFIG, 0);
		wrmsrl(MSR_MC6_DEMOTION_POLICY_CONFIG, 0);
	}
}


/*
 * intel_idle_cpu_init()
 * allocate, initialize, register cpuidle_devices
 * @cpu: cpu/core to initialize
 */
static int intel_idle_cpu_init(unsigned int cpu)
{
	struct cpuidle_device *dev;

	dev = per_cpu_ptr(intel_idle_cpuidle_devices, cpu);
	dev->cpu = cpu;

	if (cpuidle_register_device(dev)) {
		pr_debug("cpuidle_register_device %d failed!\n", cpu);
		return -EIO;
	}

	if (icpu->auto_demotion_disable_flags)
		auto_demotion_disable();

	if (icpu->disable_promotion_to_c1e)
		c1e_promotion_disable();

	return 0;
}

static int intel_idle_cpu_online(unsigned int cpu)
{
	struct cpuidle_device *dev;

	if (lapic_timer_reliable_states != LAPIC_TIMER_ALWAYS_RELIABLE)
		__setup_broadcast_timer(true);

	/*
	 * Some systems can hotplug a cpu at runtime after
	 * the kernel has booted, we have to initialize the
	 * driver in this case
	 */
	dev = per_cpu_ptr(intel_idle_cpuidle_devices, cpu);
	if (!dev->registered)
		return intel_idle_cpu_init(cpu);

	return 0;
}

static int __init intel_idle_init(void)
{
	int retval;

	/* Do not load intel_idle at all for now if idle= is passed */
	if (boot_option_idle_override != IDLE_NO_OVERRIDE)
		return -ENODEV;

	retval = intel_idle_probe();
	if (retval)
		return retval;

	intel_idle_cpuidle_devices = alloc_percpu(struct cpuidle_device);
	if (intel_idle_cpuidle_devices == NULL)
		return -ENOMEM;

	intel_idle_cpuidle_driver_init();
	retval = cpuidle_register_driver(&intel_idle_driver);
	if (retval) {
		struct cpuidle_driver *drv = cpuidle_get_driver();
		printk(KERN_DEBUG pr_fmt("intel_idle yielding to %s\n"),
		       drv ? drv->name : "none");
		goto init_driver_fail;
	}

	if (boot_cpu_has(X86_FEATURE_ARAT))	/* Always Reliable APIC Timer */
		lapic_timer_reliable_states = LAPIC_TIMER_ALWAYS_RELIABLE;

	retval = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "idle/intel:online",
				   intel_idle_cpu_online, NULL);
	if (retval < 0)
		goto hp_setup_fail;

	pr_debug("lapic_timer_reliable_states 0x%x\n",
		 lapic_timer_reliable_states);

	return 0;

hp_setup_fail:
	intel_idle_cpuidle_devices_uninit();
	cpuidle_unregister_driver(&intel_idle_driver);
init_driver_fail:
	free_percpu(intel_idle_cpuidle_devices);
	return retval;

}
device_initcall(intel_idle_init);

/*
 * We are not really modular, but we used to support that.  Meaning we also
 * support "intel_idle.max_cstate=..." at boot and also a read-only export of
 * it at /sys/module/intel_idle/parameters/max_cstate -- so using module_param
 * is the easiest way (currently) to continue doing that.
 */
module_param(max_cstate, int, 0444);
