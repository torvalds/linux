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
 * The above copyright yestice and this permission yestice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __NOUVEAU_DMEM_H__
#define __NOUVEAU_DMEM_H__
#include <nvif/os.h>
struct drm_device;
struct drm_file;
struct yesuveau_drm;
struct hmm_range;

#if IS_ENABLED(CONFIG_DRM_NOUVEAU_SVM)
void yesuveau_dmem_init(struct yesuveau_drm *);
void yesuveau_dmem_fini(struct yesuveau_drm *);
void yesuveau_dmem_suspend(struct yesuveau_drm *);
void yesuveau_dmem_resume(struct yesuveau_drm *);

int yesuveau_dmem_migrate_vma(struct yesuveau_drm *drm,
			     struct vm_area_struct *vma,
			     unsigned long start,
			     unsigned long end);

void yesuveau_dmem_convert_pfn(struct yesuveau_drm *drm,
			      struct hmm_range *range);
#else /* IS_ENABLED(CONFIG_DRM_NOUVEAU_SVM) */
static inline void yesuveau_dmem_init(struct yesuveau_drm *drm) {}
static inline void yesuveau_dmem_fini(struct yesuveau_drm *drm) {}
static inline void yesuveau_dmem_suspend(struct yesuveau_drm *drm) {}
static inline void yesuveau_dmem_resume(struct yesuveau_drm *drm) {}
#endif /* IS_ENABLED(CONFIG_DRM_NOUVEAU_SVM) */
#endif
