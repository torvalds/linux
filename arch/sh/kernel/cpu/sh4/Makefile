#
# Makefile for the Linux/SuperH SH-4 backends.
#

obj-y	:= ex.o probe.o common.o
common-y	+= $(addprefix ../sh3/, entry.o)

obj-$(CONFIG_SH_FPU)                    += fpu.o
obj-$(CONFIG_SH_STORE_QUEUES)		+= sq.o

# CPU subtype setup
obj-$(CONFIG_CPU_SUBTYPE_SH7750)	+= setup-sh7750.o
obj-$(CONFIG_CPU_SUBTYPE_SH7751)	+= setup-sh7750.o
obj-$(CONFIG_CPU_SUBTYPE_SH7760)	+= setup-sh7760.o
obj-$(CONFIG_CPU_SUBTYPE_SH7770)	+= setup-sh7770.o
obj-$(CONFIG_CPU_SUBTYPE_SH7780)	+= setup-sh7780.o
obj-$(CONFIG_CPU_SUBTYPE_SH73180)	+= setup-sh73180.o
obj-$(CONFIG_CPU_SUBTYPE_SH7343)	+= setup-sh7343.o
obj-$(CONFIG_CPU_SUBTYPE_SH4_202)	+= setup-sh4-202.o

# Primary on-chip clocks (common)
clock-$(CONFIG_CPU_SH4)			:= clock-sh4.o
clock-$(CONFIG_CPU_SUBTYPE_SH73180)	:= clock-sh73180.o
clock-$(CONFIG_CPU_SUBTYPE_SH7770)	:= clock-sh7770.o
clock-$(CONFIG_CPU_SUBTYPE_SH7780)	:= clock-sh7780.o

# Additional clocks by subtype
clock-$(CONFIG_CPU_SUBTYPE_SH4_202)	+= clock-sh4-202.o

obj-y	+= $(clock-y)
