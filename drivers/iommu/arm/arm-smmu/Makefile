# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_QCOM_IOMMU) += qcom_iommu.o
obj-$(CONFIG_ARM_SMMU) += arm_smmu.o
arm_smmu-objs += arm-smmu.o arm-smmu-impl.o arm-smmu-nvidia.o
arm_smmu-$(CONFIG_ARM_SMMU_QCOM) += arm-smmu-qcom.o
arm_smmu-$(CONFIG_ARM_SMMU_QCOM_DEBUG) += arm-smmu-qcom-debug.o
