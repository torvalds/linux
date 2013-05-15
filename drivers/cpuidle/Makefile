#
# Makefile for cpuidle.
#

obj-y += cpuidle.o driver.o governor.o sysfs.o governors/
obj-$(CONFIG_ARCH_NEEDS_CPU_IDLE_COUPLED) += coupled.o

obj-$(CONFIG_CPU_IDLE_CALXEDA) += cpuidle-calxeda.o
obj-$(CONFIG_ARCH_KIRKWOOD) += cpuidle-kirkwood.o
