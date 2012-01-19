/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#ifndef __NOUVEAU_PM_H__
#define __NOUVEAU_PM_H__

/* nouveau_pm.c */
int  nouveau_pm_init(struct drm_device *dev);
void nouveau_pm_fini(struct drm_device *dev);
void nouveau_pm_resume(struct drm_device *dev);

/* nouveau_volt.c */
void nouveau_volt_init(struct drm_device *);
void nouveau_volt_fini(struct drm_device *);
int  nouveau_volt_vid_lookup(struct drm_device *, int voltage);
int  nouveau_volt_lvl_lookup(struct drm_device *, int vid);
int  nouveau_voltage_gpio_get(struct drm_device *);
int  nouveau_voltage_gpio_set(struct drm_device *, int voltage);

/* nouveau_perf.c */
void nouveau_perf_init(struct drm_device *);
void nouveau_perf_fini(struct drm_device *);

/* nouveau_mem.c */
void nouveau_mem_timing_init(struct drm_device *);
void nouveau_mem_timing_fini(struct drm_device *);

/* nv04_pm.c */
int nv04_pm_clocks_get(struct drm_device *, struct nouveau_pm_level *);
void *nv04_pm_clocks_pre(struct drm_device *, struct nouveau_pm_level *);
int nv04_pm_clocks_set(struct drm_device *, void *);

/* nv40_pm.c */
int nv40_pm_clocks_get(struct drm_device *, struct nouveau_pm_level *);
void *nv40_pm_clocks_pre(struct drm_device *, struct nouveau_pm_level *);
int nv40_pm_clocks_set(struct drm_device *, void *);
int nv40_pm_pwm_get(struct drm_device *, int, u32 *, u32 *);
int nv40_pm_pwm_set(struct drm_device *, int, u32, u32);

/* nv50_pm.c */
int nv50_pm_clocks_get(struct drm_device *, struct nouveau_pm_level *);
void *nv50_pm_clocks_pre(struct drm_device *, struct nouveau_pm_level *);
int nv50_pm_clocks_set(struct drm_device *, void *);
int nv50_pm_pwm_get(struct drm_device *, int, u32 *, u32 *);
int nv50_pm_pwm_set(struct drm_device *, int, u32, u32);

/* nva3_pm.c */
int nva3_pm_clocks_get(struct drm_device *, struct nouveau_pm_level *);
void *nva3_pm_clocks_pre(struct drm_device *, struct nouveau_pm_level *);
int nva3_pm_clocks_set(struct drm_device *, void *);

/* nvc0_pm.c */
int nvc0_pm_clocks_get(struct drm_device *, struct nouveau_pm_level *);
void *nvc0_pm_clocks_pre(struct drm_device *, struct nouveau_pm_level *);
int nvc0_pm_clocks_set(struct drm_device *, void *);

/* nouveau_temp.c */
void nouveau_temp_init(struct drm_device *dev);
void nouveau_temp_fini(struct drm_device *dev);
void nouveau_temp_safety_checks(struct drm_device *dev);
int nv40_temp_get(struct drm_device *dev);
int nv84_temp_get(struct drm_device *dev);

#endif
