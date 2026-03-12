# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_ACPI_APEI)		+= apei.o
obj-$(CONFIG_ACPI_APEI_GHES)	+= ghes.o
# clang versions prior to 18 may blow out the stack with KASAN
ifeq ($(CONFIG_COMPILE_TEST)_$(CONFIG_CC_IS_CLANG)_$(call clang-min-version, 180000),y_y_)
KASAN_SANITIZE_ghes.o := n
endif
obj-$(CONFIG_ACPI_APEI_PCIEAER)	+= ghes_helpers.o
obj-$(CONFIG_ACPI_APEI_EINJ)	+= einj.o
einj-y				:= einj-core.o
einj-$(CONFIG_ACPI_APEI_EINJ_CXL) += einj-cxl.o
obj-$(CONFIG_ACPI_APEI_ERST_DEBUG) += erst-dbg.o

apei-y := apei-base.o hest.o erst.o bert.o
