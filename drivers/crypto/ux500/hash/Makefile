#
# Copyright (C) ST-Ericsson SA 2010
# Author: Shujuan Chen (shujuan.chen@stericsson.com)
# License terms: GNU General Public License (GPL) version 2
#
ifdef CONFIG_CRYPTO_DEV_UX500_DEBUG
CFLAGS_hash_core.o := -DDEBUG
endif

obj-$(CONFIG_CRYPTO_DEV_UX500_HASH) += ux500_hash.o
ux500_hash-objs :=  hash_core.o
