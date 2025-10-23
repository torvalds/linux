# SPDX-License-Identifier: GPL-2.0

iommu_pt_fmt-$(CONFIG_IOMMU_PT_AMDV1) += amdv1
iommu_pt_fmt-$(CONFIG_IOMMUFD_TEST) += mock

iommu_pt_fmt-$(CONFIG_IOMMU_PT_VTDSS) += vtdss

iommu_pt_fmt-$(CONFIG_IOMMU_PT_X86_64) += x86_64

IOMMU_PT_KUNIT_TEST :=
define create_format
obj-$(2) += iommu_$(1).o
iommu_pt_kunit_test-y += kunit_iommu_$(1).o
CFLAGS_kunit_iommu_$(1).o += -DGENERIC_PT_KUNIT=1
IOMMU_PT_KUNIT_TEST := iommu_pt_kunit_test.o

endef

$(eval $(foreach fmt,$(iommu_pt_fmt-y),$(call create_format,$(fmt),y)))
$(eval $(foreach fmt,$(iommu_pt_fmt-m),$(call create_format,$(fmt),m)))

# The kunit objects are constructed by compiling the main source
# with -DGENERIC_PT_KUNIT
$(obj)/kunit_iommu_%.o: $(src)/iommu_%.c FORCE
	$(call rule_mkdir)
	$(call if_changed_dep,cc_o_c)

obj-$(CONFIG_IOMMU_PT_KUNIT_TEST) += $(IOMMU_PT_KUNIT_TEST)
