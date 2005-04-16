#
# Makefile for the Acorn block device drivers.
#

fd1772_mod-objs	:= fd1772.o fd1772dma.o
mfmhd_mod-objs	:= mfmhd.o mfm.o

obj-$(CONFIG_BLK_DEV_FD1772)	+= fd1772_mod.o
obj-$(CONFIG_BLK_DEV_MFM)	+= mfmhd_mod.o
