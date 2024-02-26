# SPDX-License-Identifier: GPL-2.0

# "live" kunit tests
obj-$(CONFIG_DRM_XE_KUNIT_TEST) += \
	xe_bo_test.o \
	xe_dma_buf_test.o \
	xe_migrate_test.o \
	xe_mocs_test.o

# Normal kunit tests
obj-$(CONFIG_DRM_XE_KUNIT_TEST) += xe_test.o
xe_test-y = xe_test_mod.o \
	xe_pci_test.o \
	xe_rtp_test.o \
	xe_wa_test.o
