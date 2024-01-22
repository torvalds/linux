# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_CRYPTO_AES_RISCV64) += aes-riscv64.o
aes-riscv64-y := aes-riscv64-glue.o aes-riscv64-zvkned.o \
		 aes-riscv64-zvkned-zvbb-zvkg.o aes-riscv64-zvkned-zvkb.o

obj-$(CONFIG_CRYPTO_CHACHA_RISCV64) += chacha-riscv64.o
chacha-riscv64-y := chacha-riscv64-glue.o chacha-riscv64-zvkb.o
