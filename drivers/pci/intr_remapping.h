#include <linux/intel-iommu.h>

struct ioapic_scope {
	struct intel_iommu *iommu;
	unsigned int id;
};

#define IR_X2APIC_MODE(mode) (mode ? (1 << 11) : 0)
