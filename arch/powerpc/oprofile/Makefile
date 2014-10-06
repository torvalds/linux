subdir-ccflags-$(CONFIG_PPC_WERROR) := -Werror

ccflags-$(CONFIG_PPC64)	:= $(NO_MINIMAL_TOC)

obj-$(CONFIG_OPROFILE) += oprofile.o

DRIVER_OBJS := $(addprefix ../../../drivers/oprofile/, \
		oprof.o cpu_buffer.o buffer_sync.o \
		event_buffer.o oprofile_files.o \
		oprofilefs.o oprofile_stats.o \
		timer_int.o )

oprofile-y := $(DRIVER_OBJS) common.o backtrace.o
oprofile-$(CONFIG_OPROFILE_CELL) += op_model_cell.o \
		cell/spu_profiler.o cell/vma_map.o \
		cell/spu_task_sync.o
oprofile-$(CONFIG_PPC_BOOK3S_64) += op_model_power4.o op_model_pa6t.o
oprofile-$(CONFIG_FSL_EMB_PERFMON) += op_model_fsl_emb.o
oprofile-$(CONFIG_6xx) += op_model_7450.o
