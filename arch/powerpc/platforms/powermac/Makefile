obj-y				+= pic.o setup.o time.o feature.o pci.o \
				   sleep.o low_i2c.o cache.o
obj-$(CONFIG_PMAC_BACKLIGHT)	+= backlight.o
obj-$(CONFIG_CPU_FREQ_PMAC)	+= cpufreq.o
obj-$(CONFIG_NVRAM)		+= nvram.o
# ppc64 pmac doesn't define CONFIG_NVRAM but needs nvram stuff
obj-$(CONFIG_PPC64)		+= nvram.o
obj-$(CONFIG_SMP)		+= smp.o
