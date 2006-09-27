#
# Makefile for the Linux/SuperH SH-3 backends.
#

obj-y	:= ex.o probe.o

# CPU subtype setup
obj-$(CONFIG_CPU_SUBTYPE_SH7705)	+= setup-sh7705.o
obj-$(CONFIG_CPU_SUBTYPE_SH7707)	+= setup-sh7709.o
obj-$(CONFIG_CPU_SUBTYPE_SH7708)	+= setup-sh7708.o
obj-$(CONFIG_CPU_SUBTYPE_SH7709)	+= setup-sh7709.o
obj-$(CONFIG_CPU_SUBTYPE_SH7300)	+= setup-sh7300.o

# Primary on-chip clocks (common)
clock-$(CONFIG_CPU_SH3)			:= clock-sh3.o
clock-$(CONFIG_CPU_SUBTYPE_SH7300)	:= clock-sh7300.o
clock-$(CONFIG_CPU_SUBTYPE_SH7705)	:= clock-sh7705.o
clock-$(CONFIG_CPU_SUBTYPE_SH7709)	:= clock-sh7709.o

obj-y	+= $(clock-y)

