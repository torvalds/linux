#
# Makefile for the PCI specific kernel interface routines under Linux.
#

obj-y					+= pci.o
obj-$(CONFIG_PCI_AUTO)			+= pci-auto.o

obj-$(CONFIG_CPU_SUBTYPE_SH7751)	+= pci-sh7751.o ops-sh4.o
obj-$(CONFIG_CPU_SUBTYPE_SH7751R)	+= pci-sh7751.o ops-sh4.o
obj-$(CONFIG_CPU_SUBTYPE_SH7763)	+= pci-sh7780.o ops-sh4.o
obj-$(CONFIG_CPU_SUBTYPE_SH7780)	+= pci-sh7780.o ops-sh4.o
obj-$(CONFIG_CPU_SUBTYPE_SH7785)	+= pci-sh7780.o ops-sh4.o
obj-$(CONFIG_CPU_SH5)			+= pci-sh5.o ops-sh5.o

obj-$(CONFIG_SH_DREAMCAST)		+= ops-dreamcast.o fixups-dreamcast.o
obj-$(CONFIG_SH_SECUREEDGE5410)		+= ops-snapgear.o
obj-$(CONFIG_SH_RTS7751R2D)		+= ops-rts7751r2d.o fixups-rts7751r2d.o
obj-$(CONFIG_SH_SH03)			+= ops-sh03.o fixups-sh03.o
obj-$(CONFIG_SH_HIGHLANDER)		+= ops-r7780rp.o fixups-r7780rp.o
obj-$(CONFIG_SH_SDK7780)		+= ops-sdk7780.o fixups-sdk7780.o
obj-$(CONFIG_SH_TITAN)			+= ops-titan.o
obj-$(CONFIG_SH_LANDISK)		+= ops-landisk.o
obj-$(CONFIG_SH_LBOX_RE2)		+= ops-lboxre2.o fixups-lboxre2.o
obj-$(CONFIG_SH_7780_SOLUTION_ENGINE)	+= ops-se7780.o fixups-se7780.o
obj-$(CONFIG_SH_CAYMAN)			+= ops-cayman.o
