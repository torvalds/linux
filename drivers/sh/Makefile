#
# Makefile for the SuperH specific drivers.
#
obj-y	:= intc/

obj-$(CONFIG_HAVE_CLK)		+= clk/
obj-$(CONFIG_MAPLE)		+= maple/
obj-$(CONFIG_SUPERHYWAY)	+= superhyway/
obj-$(CONFIG_GENERIC_GPIO)	+= pfc.o

#
# For the moment we only use this framework for ARM-based SH/R-Mobile
# platforms and generic SH. SH-based SH-Mobile platforms are still using
# an older framework that is pending up-porting, at which point this
# special casing can go away.
#
obj-$(CONFIG_SUPERH)$(CONFIG_ARCH_SHMOBILE)	+= pm_runtime.o
