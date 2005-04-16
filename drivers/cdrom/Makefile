# Makefile for the kernel cdrom device drivers.
#
# 30 Jan 1998, Michael Elizabeth Chastain, <mailto:mec@shout.net>
# Rewritten to use lists instead of if-statements.

# Each configuration option enables a list of files.

obj-$(CONFIG_BLK_DEV_IDECD)	+=              cdrom.o
obj-$(CONFIG_BLK_DEV_SR)	+=              cdrom.o
obj-$(CONFIG_PARIDE_PCD)	+=		cdrom.o
obj-$(CONFIG_CDROM_PKTCDVD)	+=		cdrom.o

obj-$(CONFIG_AZTCD)		+= aztcd.o
obj-$(CONFIG_CDU31A)		+= cdu31a.o     cdrom.o
obj-$(CONFIG_CM206)		+= cm206.o      cdrom.o
obj-$(CONFIG_GSCD)		+= gscd.o
obj-$(CONFIG_ISP16_CDI)		+= isp16.o
obj-$(CONFIG_MCDX)		+= mcdx.o       cdrom.o
obj-$(CONFIG_OPTCD)		+= optcd.o
obj-$(CONFIG_SBPCD)		+= sbpcd.o      cdrom.o
obj-$(CONFIG_SJCD)		+= sjcd.o
obj-$(CONFIG_CDU535)		+= sonycd535.o
obj-$(CONFIG_VIOCD)		+= viocd.o      cdrom.o
