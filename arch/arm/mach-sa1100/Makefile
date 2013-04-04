#
# Makefile for the linux kernel.
#

# Common support
obj-y := clock.o generic.o irq.o time.o #nmi-oopser.o
obj-m :=
obj-n :=
obj-  :=

# Specific board support
obj-$(CONFIG_SA1100_ASSABET)		+= assabet.o
obj-$(CONFIG_ASSABET_NEPONSET)		+= neponset.o

obj-$(CONFIG_SA1100_BADGE4)		+= badge4.o

obj-$(CONFIG_SA1100_CERF)		+= cerf.o

obj-$(CONFIG_SA1100_COLLIE)		+= collie.o

obj-$(CONFIG_SA1100_H3100)		+= h3100.o h3xxx.o
obj-$(CONFIG_SA1100_H3600)		+= h3600.o h3xxx.o

obj-$(CONFIG_SA1100_HACKKIT)		+= hackkit.o

obj-$(CONFIG_SA1100_JORNADA720)		+= jornada720.o
obj-$(CONFIG_SA1100_JORNADA720_SSP)	+= jornada720_ssp.o

obj-$(CONFIG_SA1100_LART)		+= lart.o

obj-$(CONFIG_SA1100_NANOENGINE)		+= nanoengine.o
obj-$(CONFIG_PCI_NANOENGINE)		+= pci-nanoengine.o

obj-$(CONFIG_SA1100_PLEB)		+= pleb.o

obj-$(CONFIG_SA1100_SHANNON)		+= shannon.o

obj-$(CONFIG_SA1100_SIMPAD)		+= simpad.o

# Miscellaneous functions
obj-$(CONFIG_PM)			+= pm.o sleep.o
obj-$(CONFIG_SA1100_SSP)		+= ssp.o

