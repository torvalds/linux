#
# Makefile for the Linux/SuperH SH-2A backends.
#

obj-y	:= common.o probe.o opcode_helper.o

common-y	+= $(addprefix ../sh2/, ex.o entry.o)

obj-$(CONFIG_SH_FPU)	+= fpu.o

obj-$(CONFIG_CPU_SUBTYPE_SH7206)	+= setup-sh7206.o clock-sh7206.o
obj-$(CONFIG_CPU_SUBTYPE_SH7203)	+= setup-sh7203.o clock-sh7203.o
obj-$(CONFIG_CPU_SUBTYPE_SH7263)	+= setup-sh7203.o clock-sh7203.o
obj-$(CONFIG_CPU_SUBTYPE_MXG)		+= setup-mxg.o clock-sh7206.o
