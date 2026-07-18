# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
iwlmld-tests-y += module.o hcmd.o utils.o link.o rx.o agg.o link-selection.o
iwlmld-tests-y += chan_load_thresh.o

ccflags-y += -I$(src)/../
obj-$(CONFIG_IWLWIFI_KUNIT_TESTS) += iwlmld-tests.o
