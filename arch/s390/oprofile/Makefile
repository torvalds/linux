obj-$(CONFIG_OPROFILE) += oprofile.o

DRIVER_OBJS = $(addprefix ../../../drivers/oprofile/, \
		oprof.o cpu_buffer.o buffer_sync.o \
		event_buffer.o oprofile_files.o \
		oprofilefs.o oprofile_stats.o  \
		timer_int.o )

oprofile-y :=	$(DRIVER_OBJS) init.o backtrace.o
oprofile-$(CONFIG_64BIT)	+= hwsampler.o
