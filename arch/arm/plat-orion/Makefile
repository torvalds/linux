#
# Makefile for the linux kernel.
#
ccflags-$(CONFIG_ARCH_MULTIPLATFORM) := -I$(srctree)/$(src)/include

orion-gpio-$(CONFIG_GENERIC_GPIO) += gpio.o
obj-$(CONFIG_PLAT_ORION_LEGACY)   += irq.o pcie.o time.o common.o mpp.o
obj-$(CONFIG_PLAT_ORION_LEGACY)   += $(orion-gpio-y)
