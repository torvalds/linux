obj-$(CONFIG_OPROFILE) += oprofile.o

DRIVER_OBJS = $(addprefix ../../../drivers/oprofile/, \
		oprof.o cpu_buffer.o buffer_sync.o \
		event_buffer.o oprofile_files.o \
		oprofilefs.o oprofile_stats.o \
		timer_int.o )

oprofile-y				:= $(DRIVER_OBJS) common.o backtrace.o
oprofile-$(CONFIG_CPU_XSCALE)		+= op_model_xscale.o
oprofile-$(CONFIG_OPROFILE_ARM11_CORE)	+= op_model_arm11_core.o
oprofile-$(CONFIG_OPROFILE_ARMV6)	+= op_model_v6.o
oprofile-$(CONFIG_OPROFILE_MPCORE)	+= op_model_mpcore.o
oprofile-$(CONFIG_OPROFILE_ARMV7)	+= op_model_v7.o
