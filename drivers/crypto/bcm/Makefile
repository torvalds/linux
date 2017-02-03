# File: drivers/crypto/bcm/Makefile
#
# Makefile for crypto acceleration files for Broadcom SPU driver
#
# Uncomment to enable debug tracing in the SPU driver.
# CFLAGS_util.o := -DDEBUG
# CFLAGS_cipher.o := -DDEBUG
# CFLAGS_spu.o := -DDEBUG
# CFLAGS_spu2.o := -DDEBUG

obj-$(CONFIG_CRYPTO_DEV_BCM_SPU) := bcm_crypto_spu.o

bcm_crypto_spu-objs :=  util.o spu.o spu2.o cipher.o

ccflags-y += -I. -DBCMDRIVER
