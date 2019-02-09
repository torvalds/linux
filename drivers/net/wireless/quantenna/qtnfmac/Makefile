# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2015-2016 Quantenna Communications, Inc.
# All rights reserved.
#

ccflags-y += \
	-Idrivers/net/wireless/quantenna/qtnfmac

obj-$(CONFIG_QTNFMAC) += qtnfmac.o
qtnfmac-objs += \
	core.o \
	commands.o \
	trans.o \
	cfg80211.o \
	event.o \
	util.o \
	qlink_util.o

#

obj-$(CONFIG_QTNFMAC_PEARL_PCIE) += qtnfmac_pearl_pcie.o

qtnfmac_pearl_pcie-objs += \
	shm_ipc.o \
	pearl/pcie.o

qtnfmac_pearl_pcie-$(CONFIG_DEBUG_FS) += debug.o
