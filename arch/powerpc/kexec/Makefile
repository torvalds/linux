# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

obj-y				+= core.o crash.o core_$(BITS).o

obj-$(CONFIG_PPC32)		+= relocate_32.o

obj-$(CONFIG_KEXEC_FILE)	+= file_load.o ranges.o file_load_$(BITS).o elf_$(BITS).o

# Disable GCOV, KCOV & sanitizers in odd or sensitive code
GCOV_PROFILE_core_$(BITS).o := n
KCOV_INSTRUMENT_core_$(BITS).o := n
UBSAN_SANITIZE_core_$(BITS).o := n
