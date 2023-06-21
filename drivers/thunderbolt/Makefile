# SPDX-License-Identifier: GPL-2.0-only
obj-${CONFIG_USB4} := thunderbolt.o
thunderbolt-objs := nhi.o nhi_ops.o ctl.o tb.o switch.o cap.o path.o tunnel.o eeprom.o
thunderbolt-objs += domain.o dma_port.o icm.o property.o xdomain.o lc.o tmu.o usb4.o
thunderbolt-objs += usb4_port.o nvm.o retimer.o quirks.o clx.o

thunderbolt-${CONFIG_ACPI} += acpi.o
thunderbolt-$(CONFIG_DEBUG_FS) += debugfs.o
thunderbolt-${CONFIG_USB4_KUNIT_TEST} += test.o
CFLAGS_test.o += $(DISABLE_STRUCTLEAK_PLUGIN)

thunderbolt_dma_test-${CONFIG_USB4_DMA_TEST} += dma_test.o
obj-$(CONFIG_USB4_DMA_TEST) += thunderbolt_dma_test.o
