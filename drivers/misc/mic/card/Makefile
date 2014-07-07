#
# Makefile - Intel MIC Linux driver.
# Copyright(c) 2013, Intel Corporation.
#
ccflags-y += -DINTEL_MIC_CARD

obj-$(CONFIG_INTEL_MIC_CARD) += mic_card.o
mic_card-y += mic_x100.o
mic_card-y += mic_device.o
mic_card-y += mic_debugfs.o
mic_card-y += mic_virtio.o
