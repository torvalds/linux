# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

obj-y				+= core.o core_$(BITS).o ranges.o

obj-$(CONFIG_PPC32)		+= relocate_32.o

obj-$(CONFIG_KEXEC_FILE)	+= file_load.o file_load_$(BITS).o elf_$(BITS).o
obj-$(CONFIG_VMCORE_INFO)	+= vmcore_info.o
obj-$(CONFIG_CRASH_DUMP)	+= crash.o

# Disable GCOV, KCOV & sanitizers in odd or sensitive code
GCOV_PROFILE_core_$(BITS).o := n
KCOV_INSTRUMENT_core_$(BITS).o := n
UBSAN_SANITIZE_core_$(BITS).o := n
KASAN_SANITIZE_core.o := n
KASAN_SANITIZE_core_$(BITS) := n
