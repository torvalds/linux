# Makefile for the kernel cdrom device drivers.
#
# 30 Jan 1998, Michael Elizabeth Chastain, <mailto:mec@shout.net>
# Rewritten to use lists instead of if-statements.

# Each configuration option enables a list of files.

obj-$(CONFIG_BLK_DEV_IDECD)	+=              cdrom.o
obj-$(CONFIG_BLK_DEV_SR)	+=              cdrom.o
obj-$(CONFIG_PARIDE_PCD)	+=		cdrom.o
obj-$(CONFIG_CDROM_PKTCDVD)	+=		cdrom.o

obj-$(CONFIG_VIOCD)		+= viocd.o      cdrom.o
obj-$(CONFIG_GDROM)		+= gdrom.o      cdrom.o
