#ifndef __NOUVEAU_SVM_H__
#define __NOUVEAU_SVM_H__
#include <nvif/os.h>
struct drm_device;
struct drm_file;
struct yesuveau_drm;

struct yesuveau_svmm;

#if IS_ENABLED(CONFIG_DRM_NOUVEAU_SVM)
void yesuveau_svm_init(struct yesuveau_drm *);
void yesuveau_svm_fini(struct yesuveau_drm *);
void yesuveau_svm_suspend(struct yesuveau_drm *);
void yesuveau_svm_resume(struct yesuveau_drm *);

int yesuveau_svmm_init(struct drm_device *, void *, struct drm_file *);
void yesuveau_svmm_fini(struct yesuveau_svmm **);
int yesuveau_svmm_join(struct yesuveau_svmm *, u64 inst);
void yesuveau_svmm_part(struct yesuveau_svmm *, u64 inst);
int yesuveau_svmm_bind(struct drm_device *, void *, struct drm_file *);
#else /* IS_ENABLED(CONFIG_DRM_NOUVEAU_SVM) */
static inline void yesuveau_svm_init(struct yesuveau_drm *drm) {}
static inline void yesuveau_svm_fini(struct yesuveau_drm *drm) {}
static inline void yesuveau_svm_suspend(struct yesuveau_drm *drm) {}
static inline void yesuveau_svm_resume(struct yesuveau_drm *drm) {}

static inline int yesuveau_svmm_init(struct drm_device *device, void *p,
				    struct drm_file *file)
{
	return -ENOSYS;
}

static inline void yesuveau_svmm_fini(struct yesuveau_svmm **svmmp) {}

static inline int yesuveau_svmm_join(struct yesuveau_svmm *svmm, u64 inst)
{
	return 0;
}

static inline void yesuveau_svmm_part(struct yesuveau_svmm *svmm, u64 inst) {}

static inline int yesuveau_svmm_bind(struct drm_device *device, void *p,
				    struct drm_file *file)
{
	return -ENOSYS;
}
#endif /* IS_ENABLED(CONFIG_DRM_NOUVEAU_SVM) */
#endif
