# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the powerpc trace subsystem
#

ifdef CONFIG_FUNCTION_TRACER
# do not trace tracer code
CFLAGS_REMOVE_ftrace.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_ftrace_64_pg.o = $(CC_FLAGS_FTRACE)
endif

ifdef CONFIG_FUNCTION_TRACER
obj32-y					+= ftrace.o ftrace_entry.o
ifeq ($(CONFIG_MPROFILE_KERNEL)$(CONFIG_ARCH_USING_PATCHABLE_FUNCTION_ENTRY),)
obj64-y					+= ftrace_64_pg.o ftrace_64_pg_entry.o
else
obj64-y					+= ftrace.o ftrace_entry.o
endif
endif

obj-$(CONFIG_TRACING)			+= trace_clock.o

obj-$(CONFIG_PPC64)			+= $(obj64-y)
obj-$(CONFIG_PPC32)			+= $(obj32-y)

# Disable GCOV, KCOV & sanitizers in odd or sensitive code
GCOV_PROFILE_ftrace.o := n
KCOV_INSTRUMENT_ftrace.o := n
KCSAN_SANITIZE_ftrace.o := n
UBSAN_SANITIZE_ftrace.o := n
GCOV_PROFILE_ftrace_64_pg.o := n
KCOV_INSTRUMENT_ftrace_64_pg.o := n
KCSAN_SANITIZE_ftrace_64_pg.o := n
UBSAN_SANITIZE_ftrace_64_pg.o := n
