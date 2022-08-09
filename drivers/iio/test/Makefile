# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the industrial I/O unit tests.
#

# Keep in alphabetical order
obj-$(CONFIG_IIO_RESCALE_KUNIT_TEST) += iio-test-rescale.o ../afe/iio-rescale.o
obj-$(CONFIG_IIO_TEST_FORMAT) += iio-test-format.o
CFLAGS_iio-test-format.o += $(DISABLE_STRUCTLEAK_PLUGIN)
