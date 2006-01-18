#
# Makefile for the Linux/SuperH SH-4 backends.
#

obj-y	:= ex.o probe.o

obj-$(CONFIG_SH_FPU)                    += fpu.o
obj-$(CONFIG_SH_STORE_QUEUES)		+= sq.o

# Primary on-chip clocks (common)
clock-$(CONFIG_CPU_SH4)			:= clock-sh4.o
clock-$(CONFIG_CPU_SUBTYPE_SH73180)	:= clock-sh73180.o
clock-$(CONFIG_CPU_SUBTYPE_SH7770)	:= clock-sh7770.o
clock-$(CONFIG_CPU_SUBTYPE_SH7780)	:= clock-sh7780.o

# Additional clocks by subtype
clock-$(CONFIG_CPU_SUBTYPE_SH4_202)	+= clock-sh4-202.o

obj-y	+= $(clock-y)
