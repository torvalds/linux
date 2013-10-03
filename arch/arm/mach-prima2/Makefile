obj-y += rstc.o
obj-y += common.o
obj-y += rtciobrg.o
obj-$(CONFIG_DEBUG_LL) += lluart.o
obj-$(CONFIG_CACHE_L2X0) += l2x0.o
obj-$(CONFIG_SUSPEND) += pm.o sleep.o
obj-$(CONFIG_SMP) += platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)  += hotplug.o

CFLAGS_hotplug.o += -march=armv7-a
