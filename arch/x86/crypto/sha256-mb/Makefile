#
# Arch-specific CryptoAPI modules.
#

OBJECT_FILES_NON_STANDARD := y

avx2_supported := $(call as-instr,vpgatherdd %ymm0$(comma)(%eax$(comma)%ymm1\
                                $(comma)4)$(comma)%ymm2,yes,no)
ifeq ($(avx2_supported),yes)
	obj-$(CONFIG_CRYPTO_SHA256_MB) += sha256-mb.o
	sha256-mb-y := sha256_mb.o sha256_mb_mgr_flush_avx2.o \
	     sha256_mb_mgr_init_avx2.o sha256_mb_mgr_submit_avx2.o sha256_x8_avx2.o
endif
