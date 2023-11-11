# SPDX-License-Identifier: GPL-2.0
#
# Kernel configuration file for the PM8001 SAS/SATA 8x6G based HBA driver
#
# Copyright (C) 2008-2009  USI Co., Ltd.


obj-$(CONFIG_SCSI_PM8001) += pm80xx.o

CFLAGS_pm80xx_tracepoints.o := -I$(src)

pm80xx-y += pm8001_init.o \
		pm8001_sas.o  \
		pm8001_ctl.o  \
		pm8001_hwi.o  \
		pm80xx_hwi.o  \
		pm80xx_tracepoints.o
