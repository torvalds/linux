# SPDX-License-Identifier: GPL-2.0

obj-y += core.o sev-nmi.o vc-handle.o

# Clang 14 and older may fail to respect __no_sanitize_undefined when inlining
UBSAN_SANITIZE_sev-nmi.o	:= n

# GCC may fail to respect __no_sanitize_address or __no_kcsan when inlining
KASAN_SANITIZE_sev-nmi.o	:= n
KCSAN_SANITIZE_sev-nmi.o	:= n
