# File: drivers/crypto/bcm/Makefile
#
# Makefile for crypto acceleration files for Broadcom SPU driver
#
# Uncomment to enable de tracing in the SPU driver.
# CFLAGS_util.o := -DDE
# CFLAGS_cipher.o := -DDE
# CFLAGS_spu.o := -DDE
# CFLAGS_spu2.o := -DDE

obj-$(CONFIG_CRYPTO_DEV_BCM_SPU) := bcm_crypto_spu.o

bcm_crypto_spu-objs :=  util.o spu.o spu2.o cipher.o
