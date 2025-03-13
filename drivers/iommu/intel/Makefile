# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_DMAR_TABLE) += dmar.o
obj-$(CONFIG_INTEL_IOMMU) += iommu.o pasid.o nested.o cache.o prq.o
obj-$(CONFIG_DMAR_TABLE) += trace.o
obj-$(CONFIG_DMAR_PERF) += perf.o
obj-$(CONFIG_INTEL_IOMMU_DEBUGFS) += debugfs.o
obj-$(CONFIG_INTEL_IOMMU_SVM) += svm.o
ifdef CONFIG_INTEL_IOMMU
obj-$(CONFIG_IRQ_REMAP) += irq_remapping.o
endif
obj-$(CONFIG_INTEL_IOMMU_PERF_EVENTS) += perfmon.o
