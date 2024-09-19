/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tegra host1x Interrupt Management
 *
 * Copyright (c) 2010-2021, NVIDIA Corporation.
 */

#ifndef __HOST1X_INTR_H
#define __HOST1X_INTR_H

struct host1x;
struct host1x_syncpt_fence;

struct host1x_intr_irq_data {
	struct host1x *host;
	u32 offset;
};

/* Initialize host1x sync point interrupt */
int host1x_intr_init(struct host1x *host);

/* Deinitialize host1x sync point interrupt */
void host1x_intr_deinit(struct host1x *host);

/* Enable host1x sync point interrupt */
void host1x_intr_start(struct host1x *host);

/* Disable host1x sync point interrupt */
void host1x_intr_stop(struct host1x *host);

void host1x_intr_handle_interrupt(struct host1x *host, unsigned int id);

void host1x_intr_add_fence_locked(struct host1x *host, struct host1x_syncpt_fence *fence);

bool host1x_intr_remove_fence(struct host1x *host, struct host1x_syncpt_fence *fence);

#endif
