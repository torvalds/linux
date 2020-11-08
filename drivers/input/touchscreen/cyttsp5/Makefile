# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the touchscreen drivers.
#

# Each configuration option enables a list of files.

obj-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5)	+= cyttsp5.o
cyttsp5-y := cyttsp5_core.o cyttsp5_mt_common.o
cyttsp5-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MT_A) += cyttsp5_mta.o
cyttsp5-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MT_B) += cyttsp5_mtb.o
cyttsp5-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BUTTON) += cyttsp5_btn.o
cyttsp5-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PROXIMITY) += cyttsp5_proximity.o
obj-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT) += cyttsp5_devtree.o
ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5
obj-y += cyttsp5_platform.o
endif
obj-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_I2C)	+= cyttsp5_i2c.o
obj-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_SPI)	+= cyttsp5_spi.o
obj-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEBUG_MDL)	+= cyttsp5_debug.o
obj-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_LOADER)	+= cyttsp5_loader.o
obj-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICE_ACCESS)	+= cyttsp5_device_access.o
obj-$(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_TEST_DEVICE_ACCESS_API)	+= cyttsp5_test_device_access_api.o
