# SPDX-License-Identifier: BSD-3-Clause-Clear

obj-$(CONFIG_MT7996E) += mt7996e.o

mt7996e-y := pci.o init.o dma.o eeprom.o main.o mcu.o mac.o \
	     debugfs.o mmio.o

mt7996e-$(CONFIG_MT7996_NPU) += npu.o
mt7996e-$(CONFIG_DEV_COREDUMP) += coredump.o
