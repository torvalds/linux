#
# Makefile for the SuperH specific drivers.
#
obj-$(CONFIG_SUPERH)			+= intc/
obj-$(CONFIG_ARCH_SHMOBILE_LEGACY)	+= intc/
ifneq ($(CONFIG_COMMON_CLK),y)
obj-$(CONFIG_HAVE_CLK)			+= clk/
endif
obj-$(CONFIG_MAPLE)			+= maple/
obj-$(CONFIG_SUPERHYWAY)		+= superhyway/

obj-y					+= pm_runtime.o
