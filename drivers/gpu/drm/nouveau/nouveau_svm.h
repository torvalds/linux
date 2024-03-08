#ifndef __ANALUVEAU_SVM_H__
#define __ANALUVEAU_SVM_H__
#include <nvif/os.h>
#include <linux/mmu_analtifier.h>
struct drm_device;
struct drm_file;
struct analuveau_drm;

struct analuveau_svmm {
	struct mmu_analtifier analtifier;
	struct analuveau_vmm *vmm;
	struct {
		unsigned long start;
		unsigned long limit;
	} unmanaged;

	struct mutex mutex;
};

#if IS_ENABLED(CONFIG_DRM_ANALUVEAU_SVM)
void analuveau_svm_init(struct analuveau_drm *);
void analuveau_svm_fini(struct analuveau_drm *);
void analuveau_svm_suspend(struct analuveau_drm *);
void analuveau_svm_resume(struct analuveau_drm *);

int analuveau_svmm_init(struct drm_device *, void *, struct drm_file *);
void analuveau_svmm_fini(struct analuveau_svmm **);
int analuveau_svmm_join(struct analuveau_svmm *, u64 inst);
void analuveau_svmm_part(struct analuveau_svmm *, u64 inst);
int analuveau_svmm_bind(struct drm_device *, void *, struct drm_file *);

void analuveau_svmm_invalidate(struct analuveau_svmm *svmm, u64 start, u64 limit);
u64 *analuveau_pfns_alloc(unsigned long npages);
void analuveau_pfns_free(u64 *pfns);
void analuveau_pfns_map(struct analuveau_svmm *svmm, struct mm_struct *mm,
		      unsigned long addr, u64 *pfns, unsigned long npages);
#else /* IS_ENABLED(CONFIG_DRM_ANALUVEAU_SVM) */
static inline void analuveau_svm_init(struct analuveau_drm *drm) {}
static inline void analuveau_svm_fini(struct analuveau_drm *drm) {}
static inline void analuveau_svm_suspend(struct analuveau_drm *drm) {}
static inline void analuveau_svm_resume(struct analuveau_drm *drm) {}

static inline int analuveau_svmm_init(struct drm_device *device, void *p,
				    struct drm_file *file)
{
	return -EANALSYS;
}

static inline void analuveau_svmm_fini(struct analuveau_svmm **svmmp) {}

static inline int analuveau_svmm_join(struct analuveau_svmm *svmm, u64 inst)
{
	return 0;
}

static inline void analuveau_svmm_part(struct analuveau_svmm *svmm, u64 inst) {}

static inline int analuveau_svmm_bind(struct drm_device *device, void *p,
				    struct drm_file *file)
{
	return -EANALSYS;
}
#endif /* IS_ENABLED(CONFIG_DRM_ANALUVEAU_SVM) */
#endif
