obj-$(CONFIG_OPROFILE) += oprofile.o

DRIVER_OBJS := $(addprefix ../../../drivers/oprofile/, \
		oprof.o cpu_buffer.o buffer_sync.o \
		event_buffer.o oprofile_files.o \
		oprofilefs.o oprofile_stats.o \
		timer_int.o )

oprofile-y := $(DRIVER_OBJS) common.o backtrace.o
oprofile-$(CONFIG_PPC64) += op_model_rs64.o op_model_power4.o
oprofile-$(CONFIG_FSL_BOOKE) += op_model_fsl_booke.o
oprofile-$(CONFIG_PPC32) += op_model_7450.o
