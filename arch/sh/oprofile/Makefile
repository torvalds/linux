obj-$(CONFIG_OPROFILE) += oprofile.o

DRIVER_OBJS = $(addprefix ../../../drivers/oprofile/, \
		oprof.o cpu_buffer.o buffer_sync.o \
		event_buffer.o oprofile_files.o \
		oprofilefs.o oprofile_stats.o \
		timer_int.o )

profdrvr-y				:= op_model_null.o

# SH7750-style performance counters exist across 7750/7750S and 7091.
profdrvr-$(CONFIG_CPU_SUBTYPE_SH7750S)	:= op_model_sh7750.o
profdrvr-$(CONFIG_CPU_SUBTYPE_SH7750)	:= op_model_sh7750.o
profdrvr-$(CONFIG_CPU_SUBTYPE_SH7091)	:= op_model_sh7750.o

oprofile-y				:= $(DRIVER_OBJS) $(profdrvr-y)

