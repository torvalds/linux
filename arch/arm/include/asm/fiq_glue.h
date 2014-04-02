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

#ifndef __ASM_FIQ_GLUE_H
#define __ASM_FIQ_GLUE_H

struct fiq_glue_handler {
	void (*fiq)(struct fiq_glue_handler *h, void *regs, void *svc_sp);
	void (*resume)(struct fiq_glue_handler *h);
};
typedef void (*fiq_return_handler_t)(void);

int fiq_glue_register_handler(struct fiq_glue_handler *handler);
int fiq_glue_set_return_handler(fiq_return_handler_t fiq_return);
int fiq_glue_clear_return_handler(fiq_return_handler_t fiq_return);

#ifdef CONFIG_FIQ_GLUE
void fiq_glue_resume(void);
#else
static inline void fiq_glue_resume(void) {}
#endif

#endif
