EXTRA_CFLAGS := -Werror -Wno-sign-compare

obj-$(CONFIG_OPROFILE) += oprofile.o

DRIVER_OBJS = $(addprefix ../../../drivers/oprofile/, \
		oprof.o cpu_buffer.o buffer_sync.o \
		event_buffer.o oprofile_files.o \
		oprofilefs.o oprofile_stats.o \
		timer_int.o )

oprofile-y				:= $(DRIVER_OBJS) common.o
oprofile-$(CONFIG_ALPHA_GENERIC)	+= op_model_ev4.o \
					   op_model_ev5.o \
					   op_model_ev6.o \
					   op_model_ev67.o
oprofile-$(CONFIG_ALPHA_EV4)		+= op_model_ev4.o
oprofile-$(CONFIG_ALPHA_EV5)		+= op_model_ev5.o
oprofile-$(CONFIG_ALPHA_EV6)		+= op_model_ev6.o \
					   op_model_ev67.o
