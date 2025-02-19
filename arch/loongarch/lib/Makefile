# SPDX-License-Identifier: GPL-2.0
#
# Makefile for LoongArch-specific library files.
#

lib-y	+= delay.o memset.o memcpy.o memmove.o \
	   clear_user.o copy_user.o csum.o dump_tlb.o unaligned.o

obj-$(CONFIG_ARCH_SUPPORTS_INT128) += tishift.o

obj-$(CONFIG_CPU_HAS_LSX) += xor_simd.o xor_simd_glue.o

obj-$(CONFIG_FUNCTION_ERROR_INJECTION) += error-inject.o

obj-$(CONFIG_CRC32_ARCH) += crc32-loongarch.o
