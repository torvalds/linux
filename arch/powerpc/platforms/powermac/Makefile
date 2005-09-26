obj-$(CONFIG_PPC_PMAC)		+= pmac_pic.o pmac_setup.o pmac_time.o \
				   pmac_feature.o pmac_pci.o pmac_sleep.o \
				   pmac_low_i2c.o pmac_cache.o
obj-$(CONFIG_PMAC_BACKLIGHT)	+= pmac_backlight.o
obj-$(CONFIG_CPU_FREQ_PMAC)	+= pmac_cpufreq.o
ifeq ($(CONFIG_PPC_PMAC),y)
obj-$(CONFIG_NVRAM)		+= pmac_nvram.o
obj-$(CONFIG_SMP)		+= pmac_smp.o
endif
