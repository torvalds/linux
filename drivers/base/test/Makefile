# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_TEST_ASYNC_DRIVER_PROBE)	+= test_async_driver_probe.o

obj-$(CONFIG_DM_KUNIT_TEST)	+= root-device-test.o
obj-$(CONFIG_DM_KUNIT_TEST)	+= platform-device-test.o

obj-$(CONFIG_DRIVER_PE_KUNIT_TEST) += property-entry-test.o
CFLAGS_property-entry-test.o += $(DISABLE_STRUCTLEAK_PLUGIN)
