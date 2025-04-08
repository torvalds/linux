# SPDX-License-Identifier: GPL-2.0-only
lib-y			+= delay.o
lib-y			+= memcpy.o
lib-y			+= memset.o
lib-y			+= memmove.o
ifeq ($(CONFIG_KASAN_GENERIC)$(CONFIG_KASAN_SW_TAGS),)
lib-y			+= strcmp.o
lib-y			+= strlen.o
lib-y			+= strncmp.o
endif
lib-y			+= csum.o
ifeq ($(CONFIG_MMU), y)
lib-$(CONFIG_RISCV_ISA_V)	+= uaccess_vector.o
endif
lib-$(CONFIG_MMU)	+= uaccess.o
lib-$(CONFIG_64BIT)	+= tishift.o
lib-$(CONFIG_RISCV_ISA_ZICBOZ)	+= clear_page.o
obj-$(CONFIG_CRC32_ARCH)	+= crc32-riscv.o
crc32-riscv-y := crc32.o crc32_msb.o crc32_lsb.o
obj-$(CONFIG_CRC64_ARCH) += crc64-riscv.o
crc64-riscv-y := crc64.o crc64_msb.o crc64_lsb.o
obj-$(CONFIG_CRC_T10DIF_ARCH)	+= crc-t10dif-riscv.o
crc-t10dif-riscv-y := crc-t10dif.o crc16_msb.o
obj-$(CONFIG_FUNCTION_ERROR_INJECTION) += error-inject.o
lib-$(CONFIG_RISCV_ISA_V)	+= xor.o
lib-$(CONFIG_RISCV_ISA_V)	+= riscv_v_helpers.o
