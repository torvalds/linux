obj-$(CONFIG_OPROFILE) += oprofile.o

DRIVER_OBJS := $(addprefix ../../../drivers/oprofile/, \
		oprof.o cpu_buffer.o buffer_sync.o \
		event_buffer.o oprofile_files.o \
		oprofilefs.o oprofile_stats.o \
		timer_int.o )

oprofile-y := $(DRIVER_OBJS) common.o

ifeq ($(CONFIG_FSL_BOOKE),y)
	oprofile-y += op_model_fsl_booke.o
endif

