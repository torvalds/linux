obj-$(CONFIG_PPC_PMAC)		+= pic.o setup.o time.o feature.o pci.o \
				   sleep.o low_i2c.o cache.o
obj-$(CONFIG_PMAC_BACKLIGHT)	+= backlight.o
obj-$(CONFIG_CPU_FREQ_PMAC)	+= cpufreq.o
ifeq ($(CONFIG_PPC_PMAC),y)
obj-$(CONFIG_NVRAM)		+= nvram.o
obj-$(CONFIG_SMP)		+= smp.o
endif
