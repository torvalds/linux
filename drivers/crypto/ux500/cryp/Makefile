#/*
# * Copyright (C) ST-Ericsson SA 2010
# * Author: shujuan.chen@stericsson.com for ST-Ericsson.
# * License terms: GNU General Public License (GPL) version 2  */

ifdef CONFIG_CRYPTO_DEV_UX500_DEBUG
CFLAGS_cryp_core.o := -DDEBUG -O0
CFLAGS_cryp.o := -DDEBUG -O0
CFLAGS_cryp_irq.o := -DDEBUG -O0
endif

obj-$(CONFIG_CRYPTO_DEV_UX500_CRYP) += ux500_cryp.o
ux500_cryp-objs :=  cryp.o cryp_irq.o cryp_core.o
