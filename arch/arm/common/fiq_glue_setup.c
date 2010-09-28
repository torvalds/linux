/*
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <asm/fiq.h>
#include <asm/fiq_glue.h>

extern unsigned char fiq_glue, fiq_glue_end;
extern void fiq_glue_setup(void *func, void *data, void *sp);

static struct fiq_handler fiq_debbuger_fiq_handler = {
	.name = "fiq_glue",
};
static __percpu void *fiq_stack;
static struct fiq_glue_handler *current_handler;
static DEFINE_MUTEX(fiq_glue_lock);

static void fiq_glue_setup_helper(void *info)
{
	struct fiq_glue_handler *handler = info;
	fiq_glue_setup(handler->fiq, handler,
		__this_cpu_ptr(fiq_stack) + THREAD_START_SP);
}

int fiq_glue_register_handler(struct fiq_glue_handler *handler)
{
	int ret;

	if (!handler || !handler->fiq)
		return -EINVAL;

	mutex_lock(&fiq_glue_lock);
	if (fiq_stack) {
		ret = -EBUSY;
		goto err_busy;
	}

	fiq_stack = __alloc_percpu(THREAD_SIZE, L1_CACHE_BYTES);
	if (WARN_ON(!fiq_stack)) {
		ret = -ENOMEM;
		goto err_alloc_fiq_stack;
	}

	ret = claim_fiq(&fiq_debbuger_fiq_handler);
	if (WARN_ON(ret))
		goto err_claim_fiq;

	current_handler = handler;
	on_each_cpu(fiq_glue_setup_helper, handler, true);
	set_fiq_handler(&fiq_glue, &fiq_glue_end - &fiq_glue);

err_claim_fiq:
	if (ret) {
		free_percpu(fiq_stack);
		fiq_stack = NULL;
	}
err_alloc_fiq_stack:
err_busy:
	mutex_unlock(&fiq_glue_lock);
	return ret;
}

/**
 * fiq_glue_resume - Restore fiqs after suspend or low power idle states
 *
 * This must be called before calling local_fiq_enable after returning from a
 * power state where the fiq mode registers were lost. If a driver provided
 * a resume hook when it registered the handler it will be called.
 */

void fiq_glue_resume(void)
{
	if (!current_handler)
		return;
	fiq_glue_setup(current_handler->fiq, current_handler,
			__this_cpu_ptr(fiq_stack) + THREAD_START_SP);
	if (current_handler->resume)
		current_handler->resume(current_handler);
}

