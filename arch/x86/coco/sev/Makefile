# SPDX-License-Identifier: GPL-2.0

obj-y += core.o noinstr.o vc-handle.o

# Clang 14 and older may fail to respect __no_sanitize_undefined when inlining
UBSAN_SANITIZE_noinstr.o	:= n

# GCC may fail to respect __no_sanitize_address or __no_kcsan when inlining
KASAN_SANITIZE_noinstr.o	:= n
KCSAN_SANITIZE_noinstr.o	:= n
