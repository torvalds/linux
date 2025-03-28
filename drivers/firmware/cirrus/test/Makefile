# SPDX-License-Identifier: GPL-2.0
#

cs_dsp_test_utils-objs :=	\
		cs_dsp_mock_mem_maps.o \
		cs_dsp_mock_bin.o \
		cs_dsp_mock_regmap.o \
		cs_dsp_mock_utils.o \
		cs_dsp_mock_wmfw.o

cs_dsp_test-objs :=	\
		cs_dsp_test_bin.o \
		cs_dsp_test_bin_error.o \
		cs_dsp_test_callbacks.o \
		cs_dsp_test_control_parse.o \
		cs_dsp_test_control_cache.o \
		cs_dsp_test_control_rw.o \
		cs_dsp_test_wmfw.o \
		cs_dsp_test_wmfw_error.o \
		cs_dsp_tests.o

obj-$(CONFIG_FW_CS_DSP_KUNIT_TEST_UTILS) += cs_dsp_test_utils.o
obj-$(CONFIG_FW_CS_DSP_KUNIT_TEST) += cs_dsp_test.o
