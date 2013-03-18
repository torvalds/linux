#
# Makefile for the linux kernel.
#
ccflags-$(CONFIG_ARCH_MULTIPLATFORM) := -I$(srctree)/$(src)/include

obj-$(CONFIG_ARCH_MVEBU)          += addr-map.o
obj-$(CONFIG_ARCH_KIRKWOOD)       += addr-map.o
obj-$(CONFIG_ARCH_DOVE)           += addr-map.o
obj-$(CONFIG_ARCH_ORION5X)        += addr-map.o
obj-$(CONFIG_ARCH_MV78XX0)        += addr-map.o

orion-gpio-$(CONFIG_GENERIC_GPIO) += gpio.o
obj-$(CONFIG_PLAT_ORION_LEGACY)   += irq.o pcie.o time.o common.o mpp.o
obj-$(CONFIG_PLAT_ORION_LEGACY)   += $(orion-gpio-y)
