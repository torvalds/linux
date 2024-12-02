#ifndef __NOUVEAU_SVM_H__
#define __NOUVEAU_SVM_H__
#include <nvif/os.h>
#include <linux/mmu_notifier.h>
struct drm_device;
struct drm_file;
struct nouveau_drm;

struct nouveau_svmm {
	struct mmu_notifier notifier;
	struct nouveau_vmm *vmm;
	struct {
		unsigned long start;
		unsigned long limit;
	} unmanaged;

	struct mutex mutex;
};

#if IS_ENABLED(CONFIG_DRM_NOUVEAU_SVM)
void nouveau_svm_init(struct nouveau_drm *);
void nouveau_svm_fini(struct nouveau_drm *);
void nouveau_svm_suspend(struct nouveau_drm *);
void nouveau_svm_resume(struct nouveau_drm *);

int nouveau_svmm_init(struct drm_device *, void *, struct drm_file *);
void nouveau_svmm_fini(struct nouveau_svmm **);
int nouveau_svmm_join(struct nouveau_svmm *, u64 inst);
void nouveau_svmm_part(struct nouveau_svmm *, u64 inst);
int nouveau_svmm_bind(struct drm_device *, void *, struct drm_file *);

void nouveau_svmm_invalidate(struct nouveau_svmm *svmm, u64 start, u64 limit);
u64 *nouveau_pfns_alloc(unsigned long npages);
void nouveau_pfns_free(u64 *pfns);
void nouveau_pfns_map(struct nouveau_svmm *svmm, struct mm_struct *mm,
		      unsigned long addr, u64 *pfns, unsigned long npages);
#else /* IS_ENABLED(CONFIG_DRM_NOUVEAU_SVM) */
static inline void nouveau_svm_init(struct nouveau_drm *drm) {}
static inline void nouveau_svm_fini(struct nouveau_drm *drm) {}
static inline void nouveau_svm_suspend(struct nouveau_drm *drm) {}
static inline void nouveau_svm_resume(struct nouveau_drm *drm) {}

static inline int nouveau_svmm_init(struct drm_device *device, void *p,
				    struct drm_file *file)
{
	return -ENOSYS;
}

static inline void nouveau_svmm_fini(struct nouveau_svmm **svmmp) {}

static inline int nouveau_svmm_join(struct nouveau_svmm *svmm, u64 inst)
{
	return 0;
}

static inline void nouveau_svmm_part(struct nouveau_svmm *svmm, u64 inst) {}

static inline int nouveau_svmm_bind(struct drm_device *device, void *p,
				    struct drm_file *file)
{
	return -ENOSYS;
}
#endif /* IS_ENABLED(CONFIG_DRM_NOUVEAU_SVM) */
#endif
