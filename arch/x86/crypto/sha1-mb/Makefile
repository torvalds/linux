#
# Arch-specific CryptoAPI modules.
#

OBJECT_FILES_NON_STANDARD := y

avx2_supported := $(call as-instr,vpgatherdd %ymm0$(comma)(%eax$(comma)%ymm1\
                                $(comma)4)$(comma)%ymm2,yes,no)
ifeq ($(avx2_supported),yes)
	obj-$(CONFIG_CRYPTO_SHA1_MB) += sha1-mb.o
	sha1-mb-y := sha1_mb.o sha1_mb_mgr_flush_avx2.o \
	     sha1_mb_mgr_init_avx2.o sha1_mb_mgr_submit_avx2.o sha1_x8_avx2.o
endif
