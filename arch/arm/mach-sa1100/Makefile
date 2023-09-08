# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

# Common support
obj-y := clock.o generic.o #nmi-oopser.o

# Specific board support
obj-$(CONFIG_SA1100_ASSABET)		+= assabet.o
obj-$(CONFIG_ASSABET_NEPONSET)		+= neponset.o
obj-$(CONFIG_SA1100_COLLIE)		+= collie.o
obj-$(CONFIG_SA1100_H3600)		+= h3600.o h3xxx.o
obj-$(CONFIG_SA1100_JORNADA720)		+= jornada720.o
obj-$(CONFIG_SA1100_JORNADA720_SSP)	+= jornada720_ssp.o

# Miscellaneous functions
obj-$(CONFIG_PM)			+= pm.o sleep.o
obj-$(CONFIG_SA1100_SSP)		+= ssp.o

