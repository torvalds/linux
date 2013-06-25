#
# Makefile for the linux kernel.
#

obj-y=core.o printk.o devices.o timer.o sunxi_dump_reg.o soc-detect.o
obj-y += clock/
obj-y += dma/
obj-y += sys_config.o script.o
obj-$(CONFIG_CPU_FREQ)			+= cpu-freq/
obj-$(CONFIG_PM) += pm/
obj-$(CONFIG_AW_TIME_DELAY)	+= delay.o
ifeq ($(CONFIG_SMP),y)
obj-y += platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)		+= hotplug.o


ifeq ($(CONFIG_LOCAL_TIMERS),y)
obj-y += localtimer.o
endif


endif


