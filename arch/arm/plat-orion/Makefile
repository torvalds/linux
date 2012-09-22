#
# Makefile for the linux kernel.
#

obj-y                             += addr-map.o

orion-gpio-$(CONFIG_GENERIC_GPIO) += gpio.o
obj-$(CONFIG_PLAT_ORION_LEGACY)   += irq.o pcie.o time.o common.o mpp.o
obj-$(CONFIG_PLAT_ORION_LEGACY)   += $(orion-gpio-y)
