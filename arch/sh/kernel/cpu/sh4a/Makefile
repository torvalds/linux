#
# Makefile for the Linux/SuperH SH-4 backends.
#

# CPU subtype setup
obj-$(CONFIG_CPU_SUBTYPE_SH7770)	+= setup-sh7770.o
obj-$(CONFIG_CPU_SUBTYPE_SH7780)	+= setup-sh7780.o
obj-$(CONFIG_CPU_SUBTYPE_SH73180)	+= setup-sh73180.o
obj-$(CONFIG_CPU_SUBTYPE_SH7343)	+= setup-sh7343.o
obj-$(CONFIG_CPU_SUBTYPE_SH7722)	+= setup-sh7722.o

# Primary on-chip clocks (common)
clock-$(CONFIG_CPU_SUBTYPE_SH73180)	:= clock-sh73180.o
clock-$(CONFIG_CPU_SUBTYPE_SH7770)	:= clock-sh7770.o
clock-$(CONFIG_CPU_SUBTYPE_SH7780)	:= clock-sh7780.o
clock-$(CONFIG_CPU_SUBTYPE_SH7343)	:= clock-sh7343.o
clock-$(CONFIG_CPU_SUBTYPE_SH7722)	:= clock-sh7343.o

obj-y	+= $(clock-y)
