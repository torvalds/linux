/*
 * MobiCore Driver Kernel Module.
 *
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

#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>

#include "main.h"
#include "fastcall.h"
#include "ops.h"
#include "mem.h"
#include "pm.h"
#include "debug.h"

/* MobiCore context data */
static struct mc_context *ctx;

static inline long smc(union fc_generic *fc)
{
	/*
	 * If we request sleep yields must be filtered out as they
	 * make no sense
	 */
	if (ctx->mcp)
		if (ctx->mcp->flags.sleep_mode.SleepReq) {
			if (fc->as_in.cmd == MC_SMC_N_YIELD)
				return MC_FC_RET_ERR_INVALID;
		}
	return _smc(fc);
}

#ifdef MC_FASTCALL_WORKER_THREAD

static struct task_struct *fastcall_thread;
static DEFINE_KTHREAD_WORKER(fastcall_worker);

struct fastcall_work {
	struct kthread_work work;
	void *data;
};

static void fastcall_work_func(struct kthread_work *work)
{
	struct fastcall_work *fc_work =
		container_of(work, struct fastcall_work, work);
	smc(fc_work->data);
}

void mc_fastcall(void *data)
{
	struct fastcall_work fc_work = {
		KTHREAD_WORK_INIT(fc_work.work, fastcall_work_func),
		.data = data,
	};

	queue_kthread_work(&fastcall_worker, &fc_work.work);
	flush_kthread_work(&fc_work.work);
}

int mc_fastcall_init(struct mc_context *context)
{
	int ret = 0;

	ctx = context;

	fastcall_thread = kthread_create(kthread_worker_fn, &fastcall_worker,
					 "mc_fastcall");
	if (IS_ERR(fastcall_thread)) {
		ret = PTR_ERR(fastcall_thread);
		fastcall_thread = NULL;
		MCDRV_DBG_ERROR(mcd, "cannot create fastcall wq (%d)\n", ret);
		return ret;
	}

	/* this thread MUST run on CPU 0 */
	kthread_bind(fastcall_thread, 0);
	wake_up_process(fastcall_thread);

	return 0;
}

void mc_fastcall_destroy(void)
{
	if (!IS_ERR_OR_NULL(fastcall_thread)) {
		kthread_stop(fastcall_thread);
		fastcall_thread = NULL;
	}
}
#else

struct fastcall_work_struct {
	struct work_struct work;
	void *data;
};

static void fastcall_work_func(struct work_struct *work)
{
	struct fastcall_work_struct *fc_work =
		container_of(work, struct fastcall_work_struct, work);
	smc(fc_work->data);
}

void mc_fastcall(void *data)
{
	struct fastcall_work_struct work = {
		.data = data,
	};
	INIT_WORK(&work.work, fastcall_work_func);
	schedule_work_on(0, &work.work);

	flush_work(&work.work);
}

int mc_fastcall_init(struct mc_context *context)
{
	ctx = context;
	return 0;
};

void mc_fastcall_destroy(void) {};
#endif

int mc_info(uint32_t ext_info_id, uint32_t *state, uint32_t *ext_info)
{
	int ret = 0;
	union mc_fc_info fc_info;

	MCDRV_DBG_VERBOSE(mcd, "enter\n");

	memset(&fc_info, 0, sizeof(fc_info));
	fc_info.as_in.cmd = MC_FC_INFO;
	fc_info.as_in.ext_info_id = ext_info_id;

	MCDRV_DBG(mcd, "fc_info <- cmd=0x%08x, ext_info_id=0x%08x\n",
		  fc_info.as_in.cmd, fc_info.as_in.ext_info_id);

	mc_fastcall(&(fc_info.as_generic));

	MCDRV_DBG(mcd,
		  "fc_info -> r=0x%08x ret=0x%08x state=0x%08x ext_info=0x%08x",
		  fc_info.as_out.resp,
		  fc_info.as_out.ret,
		  fc_info.as_out.state,
		  fc_info.as_out.ext_info);

	ret = convert_fc_ret(fc_info.as_out.ret);

	*state  = fc_info.as_out.state;
	*ext_info = fc_info.as_out.ext_info;

	MCDRV_DBG_VERBOSE(mcd, "exit with %d/0x%08X\n", ret, ret);

	return ret;
}

/* Yield to MobiCore */
int mc_yield(void)
{
	int ret = 0;
	union fc_generic yield;

	MCDRV_DBG_VERBOSE(mcd, "enter\n");

	memset(&yield, 0, sizeof(yield));
	yield.as_in.cmd = MC_SMC_N_YIELD;
	mc_fastcall(&yield);
	ret = convert_fc_ret(yield.as_out.ret);

	return ret;
}

/* call common notify */
int mc_nsiq(void)
{
	int ret = 0;
	union fc_generic nsiq;
	MCDRV_DBG_VERBOSE(mcd, "enter\n");

	memset(&nsiq, 0, sizeof(nsiq));
	nsiq.as_in.cmd = MC_SMC_N_SIQ;
	mc_fastcall(&nsiq);
	ret = convert_fc_ret(nsiq.as_out.ret);

	return ret;
}

/* call common notify */
int _nsiq(void)
{
	int ret = 0;
	union fc_generic nsiq;
	MCDRV_DBG_VERBOSE(mcd, "enter\n");

	memset(&nsiq, 0, sizeof(nsiq));
	nsiq.as_in.cmd = MC_SMC_N_SIQ;
	_smc(&nsiq);
	ret = convert_fc_ret(nsiq.as_out.ret);

	return ret;
}

/* Call the INIT fastcall to setup MobiCore initialization */
int mc_init(uint32_t base, uint32_t nq_offset, uint32_t nq_length,
	uint32_t mcp_offset, uint32_t mcp_length)
{
	int ret = 0;
	union mc_fc_init fc_init;

	MCDRV_DBG_VERBOSE(mcd, "enter\n");

	memset(&fc_init, 0, sizeof(fc_init));

	fc_init.as_in.cmd = MC_FC_INIT;
	/* base address of mci buffer 4KB aligned */
	fc_init.as_in.base = base;
	/* notification buffer start/length [16:16] [start, length] */
	fc_init.as_in.nq_info = (nq_offset << 16) | (nq_length & 0xFFFF);
	/* mcp buffer start/length [16:16] [start, length] */
	fc_init.as_in.mcp_info = (mcp_offset << 16) | (mcp_length & 0xFFFF);

	/*
	 * Set KMOD notification queue to start of MCI
	 * mciInfo was already set up in mmap
	 */
	MCDRV_DBG(mcd,
		  "cmd=0x%08x, base=0x%08x,nq_info=0x%08x, mcp_info=0x%08x\n",
		  fc_init.as_in.cmd, fc_init.as_in.base, fc_init.as_in.nq_info,
		  fc_init.as_in.mcp_info);

	mc_fastcall(&fc_init.as_generic);

	MCDRV_DBG(mcd, "out cmd=0x%08x, ret=0x%08x\n", fc_init.as_out.resp,
		  fc_init.as_out.ret);

	ret = convert_fc_ret(fc_init.as_out.ret);

	MCDRV_DBG_VERBOSE(mcd, "exit with %d/0x%08X\n", ret, ret);

	return ret;
}

/* Return MobiCore driver version */
uint32_t mc_get_version(void)
{
	MCDRV_DBG(mcd, "MobiCore driver version is %i.%i\n",
		  MCDRVMODULEAPI_VERSION_MAJOR,
		  MCDRVMODULEAPI_VERSION_MINOR);

	return MC_VERSION(MCDRVMODULEAPI_VERSION_MAJOR,
					MCDRVMODULEAPI_VERSION_MINOR);
}
