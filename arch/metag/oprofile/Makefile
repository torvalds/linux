obj-$(CONFIG_OPROFILE)	+= oprofile.o

oprofile-core-y	+= buffer_sync.o
oprofile-core-y	+= cpu_buffer.o
oprofile-core-y	+= event_buffer.o
oprofile-core-y	+= oprof.o
oprofile-core-y	+= oprofile_files.o
oprofile-core-y	+= oprofile_stats.o
oprofile-core-y	+= oprofilefs.o
oprofile-core-y	+= timer_int.o
oprofile-core-$(CONFIG_HW_PERF_EVENTS)	+= oprofile_perf.o

oprofile-y	+= backtrace.o
oprofile-y	+= common.o
oprofile-y	+= $(addprefix ../../../drivers/oprofile/,$(oprofile-core-y))

ccflags-y	+= -Werror
