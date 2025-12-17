# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_BCM2835_VCHIQ)	+= vchiq.o

vchiq-objs := \
   vchiq-interface/vchiq_core.o  \
   vchiq-interface/vchiq_arm.o \
   vchiq-interface/vchiq_bus.o \
   vchiq-interface/vchiq_debugfs.o \

ifdef CONFIG_VCHIQ_CDEV
vchiq-objs += vchiq-interface/vchiq_dev.o
endif

obj-$(CONFIG_BCM2835_VCHIQ_MMAL)	+= vchiq-mmal/
