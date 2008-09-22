#ifndef ___ASM_SPARC_IOMMU_H
#define ___ASM_SPARC_IOMMU_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/iommu_64.h>
#else
#include <asm/iommu_32.h>
#endif
#endif
