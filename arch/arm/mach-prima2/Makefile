obj-y += rstc.o
obj-y += common.o
obj-y += rtciobrg.o
obj-$(CONFIG_SUSPEND) += pm.o sleep.o
obj-$(CONFIG_SMP) += platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)  += hotplug.o

CFLAGS_hotplug.o += -march=armv7-a
