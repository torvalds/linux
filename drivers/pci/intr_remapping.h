#include "intel-iommu.h"

struct ioapic_scope {
	struct intel_iommu *iommu;
	unsigned int id;
};
