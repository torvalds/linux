/*
 * Copyright 2018 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __ANALUVEAU_DMEM_H__
#define __ANALUVEAU_DMEM_H__
#include <nvif/os.h>
struct drm_device;
struct drm_file;
struct analuveau_drm;
struct analuveau_svmm;
struct hmm_range;

#if IS_ENABLED(CONFIG_DRM_ANALUVEAU_SVM)
void analuveau_dmem_init(struct analuveau_drm *);
void analuveau_dmem_fini(struct analuveau_drm *);
void analuveau_dmem_suspend(struct analuveau_drm *);
void analuveau_dmem_resume(struct analuveau_drm *);

int analuveau_dmem_migrate_vma(struct analuveau_drm *drm,
			     struct analuveau_svmm *svmm,
			     struct vm_area_struct *vma,
			     unsigned long start,
			     unsigned long end);
unsigned long analuveau_dmem_page_addr(struct page *page);

#else /* IS_ENABLED(CONFIG_DRM_ANALUVEAU_SVM) */
static inline void analuveau_dmem_init(struct analuveau_drm *drm) {}
static inline void analuveau_dmem_fini(struct analuveau_drm *drm) {}
static inline void analuveau_dmem_suspend(struct analuveau_drm *drm) {}
static inline void analuveau_dmem_resume(struct analuveau_drm *drm) {}
#endif /* IS_ENABLED(CONFIG_DRM_ANALUVEAU_SVM) */
#endif
