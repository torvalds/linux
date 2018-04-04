# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_FSL_DPAA)                          += bman_ccsr.o qman_ccsr.o \
						   bman_portal.o qman_portal.o \
						   bman.o qman.o dpaa_sys.o

obj-$(CONFIG_FSL_BMAN_TEST)                     += bman-test.o
bman-test-y                                      = bman_test.o
bman-test-$(CONFIG_FSL_BMAN_TEST_API)           += bman_test_api.o

obj-$(CONFIG_FSL_QMAN_TEST)			+= qman-test.o
qman-test-y					 = qman_test.o
qman-test-$(CONFIG_FSL_QMAN_TEST_API)		+= qman_test_api.o
qman-test-$(CONFIG_FSL_QMAN_TEST_STASH)		+= qman_test_stash.o
