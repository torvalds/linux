#
# Makefile for the powerpc trace subsystem
#

subdir-ccflags-$(CONFIG_PPC_WERROR)	:= -Werror

ifdef CONFIG_FUNCTION_TRACER
# do not trace tracer code
CFLAGS_REMOVE_ftrace.o = -mno-sched-epilog $(CC_FLAGS_FTRACE)
endif

obj32-$(CONFIG_FUNCTION_TRACER)		+= ftrace_32.o
obj64-$(CONFIG_FUNCTION_TRACER)		+= ftrace_64.o
ifdef CONFIG_MPROFILE_KERNEL
obj64-$(CONFIG_FUNCTION_TRACER)		+= ftrace_64_mprofile.o
else
obj64-$(CONFIG_FUNCTION_TRACER)		+= ftrace_64_pg.o
endif
obj-$(CONFIG_DYNAMIC_FTRACE)		+= ftrace.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER)	+= ftrace.o
obj-$(CONFIG_FTRACE_SYSCALLS)		+= ftrace.o
obj-$(CONFIG_TRACING)			+= trace_clock.o

obj-$(CONFIG_PPC64)			+= $(obj64-y)
obj-$(CONFIG_PPC32)			+= $(obj32-y)

# Disable GCOV & sanitizers in odd or sensitive code
GCOV_PROFILE_ftrace.o := n
UBSAN_SANITIZE_ftrace.o := n
