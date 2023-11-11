# SPDX-License-Identifier: GPL-2.0
#
# Makefile for drivers/scsi/arm
#

acornscsi_mod-objs	:= acornscsi.o acornscsi-io.o

obj-$(CONFIG_SCSI_ACORNSCSI_3)	+= acornscsi_mod.o queue.o msgqueue.o
obj-$(CONFIG_SCSI_ARXESCSI)	+= arxescsi.o fas216.o queue.o msgqueue.o
obj-$(CONFIG_SCSI_CUMANA_1)	+= cumana_1.o
obj-$(CONFIG_SCSI_CUMANA_2)	+= cumana_2.o fas216.o queue.o msgqueue.o
obj-$(CONFIG_SCSI_OAK1)		+= oak.o
obj-$(CONFIG_SCSI_POWERTECSCSI)	+= powertec.o fas216.o queue.o msgqueue.o
obj-$(CONFIG_SCSI_EESOXSCSI)	+= eesox.o fas216.o queue.o msgqueue.o
