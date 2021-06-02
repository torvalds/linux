# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_TEST_ASYNC_DRIVER_PROBE)	+= test_async_driver_probe.o

obj-$(CONFIG_DRIVER_PE_KUNIT_TEST) += property-entry-test.o
CFLAGS_REMOVE_property-entry-test.o += -fplugin-arg-structleak_plugin-byref -fplugin-arg-structleak_plugin-byref-all
