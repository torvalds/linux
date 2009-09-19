#
# arch/blackfin/mach-common/Makefile
#

obj-y := \
	cache.o cache-c.o entry.o head.o \
	interrupt.o arch_checks.o ints-priority.o

obj-$(CONFIG_PM)          += pm.o dpmc_modes.o
obj-$(CONFIG_CPU_FREQ)    += cpufreq.o
obj-$(CONFIG_CPU_VOLTAGE) += dpmc.o
obj-$(CONFIG_SMP)         += smp.o
obj-$(CONFIG_BFIN_KERNEL_CLOCK) += clocks-init.o
obj-$(CONFIG_DEBUG_ICACHE_CHECK) += irqpanic.o
