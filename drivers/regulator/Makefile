#
# Makefile for regulator drivers.
#


obj-$(CONFIG_REGULATOR) += core.o
obj-$(CONFIG_REGULATOR_FIXED_VOLTAGE) += fixed.o
obj-$(CONFIG_REGULATOR_VIRTUAL_CONSUMER) += virtual.o

ccflags-$(CONFIG_REGULATOR_DEBUG) += -DDEBUG
