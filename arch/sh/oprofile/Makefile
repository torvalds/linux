obj-$(CONFIG_OPROFILE) += oprofile.o

DRIVER_OBJS = $(addprefix ../../../drivers/oprofile/, \
		oprof.o cpu_buffer.o buffer_sync.o \
		event_buffer.o oprofile_files.o \
		oprofilefs.o oprofile_stats.o \
		timer_int.o )

oprofile-y	:= $(DRIVER_OBJS) common.o backtrace.o

oprofile-$(CONFIG_CPU_SUBTYPE_SH7750S)	+= op_model_sh7750.o
oprofile-$(CONFIG_CPU_SUBTYPE_SH7750)	+= op_model_sh7750.o
oprofile-$(CONFIG_CPU_SUBTYPE_SH7091)	+= op_model_sh7750.o
