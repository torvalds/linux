#
# Makefile for the Linux/SuperH SH-5 backends.
#
obj-y := entry.o probe.o switchto.o

obj-$(CONFIG_SH_FPU)		+= fpu.o
obj-$(CONFIG_KALLSYMS)		+= unwind.o

# Primary on-chip clocks (common)
clock-$(CONFIG_CPU_SH5)		:= clock-sh5.o

obj-y			+= $(clock-y)
