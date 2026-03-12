# SPDX-License-Identifier: GPL-2.0
#
# 32-bit vDSO images for x86.
#

# The vDSOs built in this directory
vdsos-y			:= 32

# Files to link into the vDSO:
vobjs-y			:= note.o vclock_gettime.o vgetcpu.o
vobjs-y			+= system_call.o sigreturn.o

# Compilation flags
flags-y			:= -DBUILD_VDSO32 -m32 -mregparm=0
flags-$(CONFIG_X86_64)	+= -include $(src)/fake_32bit_build.h
flags-remove-y          := -m64

# The location of this include matters!
include $(src)/../common/Makefile.include

# Linker options for the vdso
VDSO_LDFLAGS_32		:= -m elf_i386 -soname linux-gate.so.1

$(obj)/vdso32.so.dbg: $(vobjs)
