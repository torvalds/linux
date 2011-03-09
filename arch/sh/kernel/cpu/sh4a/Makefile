#
# Makefile for the Linux/SuperH SH-4 backends.
#

# CPU subtype setup
obj-$(CONFIG_CPU_SUBTYPE_SH7757)	+= setup-sh7757.o
obj-$(CONFIG_CPU_SUBTYPE_SH7763)	+= setup-sh7763.o
obj-$(CONFIG_CPU_SUBTYPE_SH7770)	+= setup-sh7770.o
obj-$(CONFIG_CPU_SUBTYPE_SH7780)	+= setup-sh7780.o
obj-$(CONFIG_CPU_SUBTYPE_SH7785)	+= setup-sh7785.o
obj-$(CONFIG_CPU_SUBTYPE_SH7786)	+= setup-sh7786.o intc-shx3.o
obj-$(CONFIG_CPU_SUBTYPE_SH7343)	+= setup-sh7343.o
obj-$(CONFIG_CPU_SUBTYPE_SH7722)	+= setup-sh7722.o
obj-$(CONFIG_CPU_SUBTYPE_SH7723)	+= setup-sh7723.o
obj-$(CONFIG_CPU_SUBTYPE_SH7724)	+= setup-sh7724.o
obj-$(CONFIG_CPU_SUBTYPE_SH7366)	+= setup-sh7366.o
obj-$(CONFIG_CPU_SUBTYPE_SHX3)		+= setup-shx3.o intc-shx3.o

# SMP setup
smp-$(CONFIG_CPU_SHX3)			:= smp-shx3.o

# Primary on-chip clocks (common)
clock-$(CONFIG_CPU_SUBTYPE_SH7757)	:= clock-sh7757.o
clock-$(CONFIG_CPU_SUBTYPE_SH7763)	:= clock-sh7763.o
clock-$(CONFIG_CPU_SUBTYPE_SH7770)	:= clock-sh7770.o
clock-$(CONFIG_CPU_SUBTYPE_SH7780)	:= clock-sh7780.o
clock-$(CONFIG_CPU_SUBTYPE_SH7785)	:= clock-sh7785.o
clock-$(CONFIG_CPU_SUBTYPE_SH7786)	:= clock-sh7786.o
clock-$(CONFIG_CPU_SUBTYPE_SH7343)	:= clock-sh7343.o
clock-$(CONFIG_CPU_SUBTYPE_SH7722)	:= clock-sh7722.o hwblk-sh7722.o
clock-$(CONFIG_CPU_SUBTYPE_SH7723)	:= clock-sh7723.o hwblk-sh7723.o
clock-$(CONFIG_CPU_SUBTYPE_SH7724)	:= clock-sh7724.o hwblk-sh7724.o
clock-$(CONFIG_CPU_SUBTYPE_SH7366)	:= clock-sh7366.o
clock-$(CONFIG_CPU_SUBTYPE_SHX3)	:= clock-shx3.o

# Pinmux setup
pinmux-$(CONFIG_CPU_SUBTYPE_SH7722)	:= pinmux-sh7722.o
pinmux-$(CONFIG_CPU_SUBTYPE_SH7723)	:= pinmux-sh7723.o
pinmux-$(CONFIG_CPU_SUBTYPE_SH7724)	:= pinmux-sh7724.o
pinmux-$(CONFIG_CPU_SUBTYPE_SH7757)	:= pinmux-sh7757.o
pinmux-$(CONFIG_CPU_SUBTYPE_SH7785)	:= pinmux-sh7785.o
pinmux-$(CONFIG_CPU_SUBTYPE_SH7786)	:= pinmux-sh7786.o
pinmux-$(CONFIG_CPU_SUBTYPE_SHX3)	:= pinmux-shx3.o

obj-y					+= $(clock-y)
obj-$(CONFIG_SMP)			+= $(smp-y)
obj-$(CONFIG_GENERIC_GPIO)		+= $(pinmux-y)
obj-$(CONFIG_PERF_EVENTS)		+= perf_event.o
obj-$(CONFIG_HAVE_HW_BREAKPOINT)	+= ubc.o
