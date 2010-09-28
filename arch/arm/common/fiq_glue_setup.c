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
DEFINE_PER_CPU(void *, fiq_stack);
static struct fiq_glue_handler *current_handler;
static DEFINE_MUTEX(fiq_glue_lock);

static void fiq_glue_setup_helper(void *info)
{
	struct fiq_glue_handler *handler = info;
	fiq_glue_setup(handler->fiq, handler,
		__get_cpu_var(fiq_stack) + THREAD_START_SP);
}

int fiq_glue_register_handler(struct fiq_glue_handler *handler)
{
	int ret;
	int cpu;

	if (!handler || !handler->fiq)
		return -EINVAL;

	mutex_lock(&fiq_glue_lock);
	if (fiq_stack) {
		ret = -EBUSY;
		goto err_busy;
	}

	for_each_possible_cpu(cpu) {
		void *stack;
		stack = (void *)__get_free_pages(GFP_KERNEL, THREAD_SIZE_ORDER);
		if (WARN_ON(!stack)) {
			ret = -ENOMEM;
			goto err_alloc_fiq_stack;
		}
		per_cpu(fiq_stack, cpu) = stack;
	}

	ret = claim_fiq(&fiq_debbuger_fiq_handler);
	if (WARN_ON(ret))
		goto err_claim_fiq;

	current_handler = handler;
	on_each_cpu(fiq_glue_setup_helper, handler, true);
	set_fiq_handler(&fiq_glue, &fiq_glue_end - &fiq_glue);

	mutex_unlock(&fiq_glue_lock);
	return 0;

err_claim_fiq:
err_alloc_fiq_stack:
	for_each_possible_cpu(cpu) {
		__free_pages(per_cpu(fiq_stack, cpu), THREAD_SIZE_ORDER);
		per_cpu(fiq_stack, cpu) = NULL;
	}
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
		__get_cpu_var(fiq_stack) + THREAD_START_SP);
	if (current_handler->resume)
		current_handler->resume(current_handler);
}

