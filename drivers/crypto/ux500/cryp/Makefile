# SPDX-License-Identifier: GPL-2.0-only
#/*
# * Copyright (C) ST-Ericsson SA 2010
# * Author: shujuan.chen@stericsson.com for ST-Ericsson.
# */

ccflags-$(CONFIG_CRYPTO_DEV_UX500_DEBUG) += -DDEBUG

obj-$(CONFIG_CRYPTO_DEV_UX500_CRYP) += ux500_cryp.o
ux500_cryp-objs :=  cryp.o cryp_irq.o cryp_core.o
