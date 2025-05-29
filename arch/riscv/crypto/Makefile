# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_CRYPTO_AES_RISCV64) += aes-riscv64.o
aes-riscv64-y := aes-riscv64-glue.o aes-riscv64-zvkned.o \
		 aes-riscv64-zvkned-zvbb-zvkg.o aes-riscv64-zvkned-zvkb.o

obj-$(CONFIG_CRYPTO_GHASH_RISCV64) += ghash-riscv64.o
ghash-riscv64-y := ghash-riscv64-glue.o ghash-riscv64-zvkg.o

obj-$(CONFIG_CRYPTO_SHA512_RISCV64) += sha512-riscv64.o
sha512-riscv64-y := sha512-riscv64-glue.o sha512-riscv64-zvknhb-zvkb.o

obj-$(CONFIG_CRYPTO_SM3_RISCV64) += sm3-riscv64.o
sm3-riscv64-y := sm3-riscv64-glue.o sm3-riscv64-zvksh-zvkb.o

obj-$(CONFIG_CRYPTO_SM4_RISCV64) += sm4-riscv64.o
sm4-riscv64-y := sm4-riscv64-glue.o sm4-riscv64-zvksed-zvkb.o
