# SPDX-License-Identifier: GPL-2.0
#
# Arch-specific CryptoAPI modules.
#

avx2_supported := $(call as-instr,vpgatherdd %ymm0$(comma)(%eax$(comma)%ymm1\
                                $(comma)4)$(comma)%ymm2,yes,no)
ifeq ($(avx2_supported),yes)
	obj-$(CONFIG_CRYPTO_SHA512_MB) += sha512-mb.o
	sha512-mb-y := sha512_mb.o sha512_mb_mgr_flush_avx2.o \
	     sha512_mb_mgr_init_avx2.o sha512_mb_mgr_submit_avx2.o sha512_x4_avx2.o
endif
