#
# Makefile for the Linux/SuperH SH-5 backends.
#
obj-y := entry.o probe.o switchto.o

obj-$(CONFIG_SH_FPU)		+= fpu.o
obj-$(CONFIG_KALLSYMS)		+= unwind.o

# CPU subtype setup
obj-$(CONFIG_CPU_SUBTYPE_SH5_101)	+= setup-sh5-101.o
obj-$(CONFIG_CPU_SUBTYPE_SH5_103)	+= setup-sh5-101.o
