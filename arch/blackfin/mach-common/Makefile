#
# arch/blackfin/mach-common/Makefile
#

obj-y := \
	cache.o cache-c.o entry.o head.o \
	interrupt.o arch_checks.o ints-priority.o

obj-$(CONFIG_PM)          += pm.o
ifneq ($(CONFIG_BF60x),y)
obj-$(CONFIG_PM)	  += dpmc_modes.o
endif
obj-$(CONFIG_CPU_VOLTAGE) += dpmc.o
obj-$(CONFIG_SMP)         += smp.o
obj-$(CONFIG_BFIN_KERNEL_CLOCK) += clocks-init.o
