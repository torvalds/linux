# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the powerpc trace subsystem
#

ifdef CONFIG_FUNCTION_TRACER
# do not trace tracer code
CFLAGS_REMOVE_ftrace.o = $(CC_FLAGS_FTRACE)
endif

obj32-$(CONFIG_FUNCTION_TRACER)		+= ftrace_mprofile.o
ifdef CONFIG_MPROFILE_KERNEL
obj64-$(CONFIG_FUNCTION_TRACER)		+= ftrace_mprofile.o
else
obj64-$(CONFIG_FUNCTION_TRACER)		+= ftrace_64_pg.o
endif
obj-$(CONFIG_FUNCTION_TRACER)		+= ftrace_low.o ftrace.o
obj-$(CONFIG_TRACING)			+= trace_clock.o

obj-$(CONFIG_PPC64)			+= $(obj64-y)
obj-$(CONFIG_PPC32)			+= $(obj32-y)

# Disable GCOV, KCOV & sanitizers in odd or sensitive code
GCOV_PROFILE_ftrace.o := n
KCOV_INSTRUMENT_ftrace.o := n
KCSAN_SANITIZE_ftrace.o := n
UBSAN_SANITIZE_ftrace.o := n
