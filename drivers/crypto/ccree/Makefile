# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2012-2019 ARM Limited (or its affiliates).

obj-$(CONFIG_CRYPTO_DEV_CCREE) := ccree.o
ccree-y := cc_driver.o cc_buffer_mgr.o cc_request_mgr.o cc_cipher.o cc_hash.o cc_aead.o cc_ivgen.o cc_sram_mgr.o
ccree-$(CONFIG_CRYPTO_FIPS) += cc_fips.o
ccree-$(CONFIG_DEBUG_FS) += cc_debugfs.o
ccree-$(CONFIG_PM) += cc_pm.o
