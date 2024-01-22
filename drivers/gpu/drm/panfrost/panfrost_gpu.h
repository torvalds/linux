/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Collabora ltd. */

#ifndef __PANFROST_GPU_H__
#define __PANFROST_GPU_H__

struct panfrost_device;

int panfrost_gpu_init(struct panfrost_device *pfdev);
void panfrost_gpu_fini(struct panfrost_device *pfdev);

u32 panfrost_gpu_get_latest_flush_id(struct panfrost_device *pfdev);

int panfrost_gpu_soft_reset(struct panfrost_device *pfdev);
void panfrost_gpu_power_on(struct panfrost_device *pfdev);
void panfrost_gpu_power_off(struct panfrost_device *pfdev);
void panfrost_gpu_suspend_irq(struct panfrost_device *pfdev);

void panfrost_cycle_counter_get(struct panfrost_device *pfdev);
void panfrost_cycle_counter_put(struct panfrost_device *pfdev);
unsigned long long panfrost_cycle_counter_read(struct panfrost_device *pfdev);

void panfrost_gpu_amlogic_quirk(struct panfrost_device *pfdev);

#endif
