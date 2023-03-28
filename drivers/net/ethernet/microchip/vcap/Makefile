# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Microchip VCAP API
#

obj-$(CONFIG_VCAP) += vcap.o
obj-$(CONFIG_VCAP_KUNIT_TEST) +=  vcap_model_kunit.o
vcap-$(CONFIG_DEBUG_FS) += vcap_api_debugfs.o

vcap-y += vcap_api.o vcap_tc.o
