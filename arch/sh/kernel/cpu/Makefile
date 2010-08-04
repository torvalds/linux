#
# Makefile for the Linux/SuperH CPU-specifc backends.
#

obj-$(CONFIG_CPU_SH2)		= sh2/
obj-$(CONFIG_CPU_SH2A)		= sh2a/
obj-$(CONFIG_CPU_SH3)		= sh3/
obj-$(CONFIG_CPU_SH4)		= sh4/
obj-$(CONFIG_CPU_SH5)		= sh5/

# Special cases for family ancestry.

obj-$(CONFIG_CPU_SH4A)		+= sh4a/
obj-$(CONFIG_ARCH_SHMOBILE)	+= shmobile/

# Common interfaces.

obj-$(CONFIG_SH_ADC)		+= adc.o
obj-$(CONFIG_SH_CLK_CPG_LEGACY)	+= clock-cpg.o
obj-$(CONFIG_SH_FPU)		+= fpu.o
obj-$(CONFIG_SH_FPU_EMU)	+= fpu.o

obj-y	+= irq/ init.o clock.o hwblk.o
