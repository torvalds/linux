/*
 * MobiCore Driver Kernel Module.
 * This module is written as a Linux device driver.
 * This driver represents the command proxy on the lowest layer, from the
 * secure world to the non secure world, and vice versa.
 * This driver is located in the non secure world (Linux).
 * This driver offers IOCTL commands, for access to the secure world, and has
 * the interface from the secure world to the normal world.
 * The access to the driver is possible with a file descriptor,
 * which has to be created by the fd = open(/dev/mobicore) command.
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/suspend.h>
#include <linux/device.h>

#include "main.h"
#include "pm.h"
#include "fastcall.h"
#include "ops.h"
#include "logging.h"
#include "debug.h"

#ifdef MC_PM_RUNTIME

static struct mc_context *ctx;

static bool sleep_ready(void)
{
	if (!ctx->mcp)
		return false;

	if (!(ctx->mcp->flags.sleep_mode.ReadyToSleep & READY_TO_SLEEP))
		return false;

	return true;
}

static void mc_suspend_handler(struct work_struct *work)
{
	if (!ctx->mcp)
		return;

	ctx->mcp->flags.sleep_mode.SleepReq = REQ_TO_SLEEP;
	_nsiq();
}
DECLARE_WORK(suspend_work, mc_suspend_handler);

static inline void dump_sleep_params(struct mc_flags *flags)
{
	MCDRV_DBG(mcd, "MobiCore IDLE=%d!", flags->schedule);
	MCDRV_DBG(mcd,
		  "MobiCore Request Sleep=%d!", flags->sleep_mode.SleepReq);
	MCDRV_DBG(mcd, "MobiCore Sleep Ready=%d!",
		  flags->sleep_mode.ReadyToSleep);
}

static int mc_suspend_notifier(struct notifier_block *nb,
	unsigned long event, void *dummy)
{
	struct mc_mcp_buffer *mcp = ctx->mcp;
	/* We have noting to say if MobiCore is not initialized */
	if (!mcp)
		return 0;

#ifdef MC_MEM_TRACES
	mobicore_log_read();
#endif

	switch (event) {
	case PM_SUSPEND_PREPARE:
		/*
		 * Make sure we have finished all the work otherwise
		 * we end up in a race condition
		 */
		cancel_work_sync(&suspend_work);
		/*
		 * We can't go to sleep if MobiCore is not IDLE
		 * or not Ready to sleep
		 */
		dump_sleep_params(&mcp->flags);
		if (!sleep_ready()) {
			ctx->mcp->flags.sleep_mode.SleepReq = REQ_TO_SLEEP;
			schedule_work_on(0, &suspend_work);
			flush_work(&suspend_work);
			if (!sleep_ready()) {
				dump_sleep_params(&mcp->flags);
				ctx->mcp->flags.sleep_mode.SleepReq = 0;
				MCDRV_DBG_ERROR(mcd, "MobiCore can't SLEEP!");
				return NOTIFY_BAD;
			}
		}
		break;
	case PM_POST_SUSPEND:
		MCDRV_DBG(mcd, "Resume MobiCore system!");
		ctx->mcp->flags.sleep_mode.SleepReq = 0;
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block mc_notif_block = {
	.notifier_call = mc_suspend_notifier,
};

#ifdef MC_BL_NOTIFIER

static int bL_switcher_notifier_handler(struct notifier_block *this,
			unsigned long event, void *ptr)
{
	unsigned int mpidr, cpu, cluster;
	struct mc_mcp_buffer *mcp = ctx->mcp;

	if (!mcp)
		return 0;

	asm volatile ("mrc\tp15, 0, %0, c0, c0, 5" : "=r" (mpidr));
	cpu = mpidr & 0x3;
	cluster = (mpidr >> 8) & 0xf;
	MCDRV_DBG(mcd, "%s switching!!, cpu: %u, Out=%u\n",
		  (event == SWITCH_ENTER ? "Before" : "After"), cpu, cluster);

	if (cpu != 0)
		return 0;

	switch (event) {
	case SWITCH_ENTER:
		if (!sleep_ready()) {
			ctx->mcp->flags.sleep_mode.SleepReq = REQ_TO_SLEEP;
			_nsiq();
			/*
			 * By this time we should be ready for sleep or we are
			 * in the middle of something important
			 */
			if (!sleep_ready()) {
				dump_sleep_params(&mcp->flags);
				MCDRV_DBG(mcd,
					  "MobiCore: Don't allow switch!\n");
				ctx->mcp->flags.sleep_mode.SleepReq = 0;
				return -EPERM;
			}
		}
		break;
	case SWITCH_EXIT:
			ctx->mcp->flags.sleep_mode.SleepReq = 0;
			break;
	default:
		MCDRV_DBG(mcd, "MobiCore: Unknown switch event!\n");
	}

	return 0;
}

static struct notifier_block switcher_nb = {
	.notifier_call = bL_switcher_notifier_handler,
};
#endif

int mc_pm_initialize(struct mc_context *context)
{
	int ret = 0;

	ctx = context;

	ret = register_pm_notifier(&mc_notif_block);
	if (ret)
		MCDRV_DBG_ERROR(mcd, "device pm register failed\n");
#ifdef MC_BL_NOTIFIER
	if (register_bL_swicher_notifier(&switcher_nb))
		MCDRV_DBG_ERROR(mcd,
				"Failed to register to bL_switcher_notifier\n");
#endif

	return ret;
}

int mc_pm_free(void)
{
	int ret = unregister_pm_notifier(&mc_notif_block);
	if (ret)
		MCDRV_DBG_ERROR(mcd, "device pm unregister failed\n");
#ifdef MC_BL_NOTIFIER
	ret = unregister_bL_swicher_notifier(&switcher_nb);
	if (ret)
		MCDRV_DBG_ERROR(mcd, "device bl unregister failed\n");
#endif
	return ret;
}

#endif /* MC_PM_RUNTIME */
