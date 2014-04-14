#
# Makefile for the linux kernel.
#

obj-y				+= firmware.o

obj-$(CONFIG_ICST)		+= icst.o
obj-$(CONFIG_SA1111)		+= sa1111.o
obj-$(CONFIG_DMABOUNCE)		+= dmabounce.o
obj-$(CONFIG_SHARP_LOCOMO)	+= locomo.o
obj-$(CONFIG_SHARP_PARAM)	+= sharpsl_param.o
obj-$(CONFIG_SHARP_SCOOP)	+= scoop.o
obj-$(CONFIG_PCI_HOST_ITE8152)  += it8152.o
obj-$(CONFIG_ARM_TIMER_SP804)	+= timer-sp.o
obj-$(CONFIG_MCPM)		+= mcpm_head.o mcpm_entry.o mcpm_platsmp.o vlock.o
CFLAGS_REMOVE_mcpm_entry.o	= -pg
AFLAGS_mcpm_head.o		:= -march=armv7-a
AFLAGS_vlock.o			:= -march=armv7-a
obj-$(CONFIG_TI_PRIV_EDMA)	+= edma.o
obj-$(CONFIG_BL_SWITCHER)	+= bL_switcher.o
obj-$(CONFIG_BL_SWITCHER_DUMMY_IF) += bL_switcher_dummy_if.o
