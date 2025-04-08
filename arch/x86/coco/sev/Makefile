# SPDX-License-Identifier: GPL-2.0

obj-y += core.o

# jump tables are emitted using absolute references in non-PIC code
# so they cannot be used in the early SEV startup code
CFLAGS_core.o += -fno-jump-tables

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_core.o = -pg
endif

KASAN_SANITIZE_core.o	:= n
KMSAN_SANITIZE_core.o	:= n
KCOV_INSTRUMENT_core.o	:= n

# With some compiler versions the generated code results in boot hangs, caused
# by several compilation units. To be safe, disable all instrumentation.
KCSAN_SANITIZE		:= n

# Clang 14 and older may fail to respect __no_sanitize_undefined when inlining
UBSAN_SANITIZE		:= n
