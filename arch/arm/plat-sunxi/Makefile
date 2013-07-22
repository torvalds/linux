ifeq ($(CONFIG_ARCH_SUN7I),)

obj-y += sys_config.o
obj-y += core.o soc-detect.o
obj-y += script.o
obj-y += clocksrc.o
obj-y += devices.o
obj-y += dma.o
obj-$(CONFIG_CPU_FREQ) += cpu-freq/
obj-$(CONFIG_PM) += pm/

else

obj-y += dummy.o

endif
