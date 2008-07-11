#
# arch/blackfin/mach-common/Makefile
#

obj-y := \
	cache.o cacheinit.o entry.o \
	interrupt.o lock.o irqpanic.o arch_checks.o ints-priority.o

obj-$(CONFIG_PM)          += pm.o dpmc_modes.o
obj-$(CONFIG_CPU_FREQ)    += cpufreq.o
obj-$(CONFIG_CPU_VOLTAGE) += dpmc.o
