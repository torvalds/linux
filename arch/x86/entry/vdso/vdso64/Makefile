# SPDX-License-Identifier: GPL-2.0
#
# 64-bit vDSO images for x86.
#

# The vDSOs built in this directory
vdsos-y				:= 64
vdsos-$(CONFIG_X86_X32_ABI)	+= x32

# Files to link into the vDSO:
vobjs-y				:= note.o vclock_gettime.o vgetcpu.o
vobjs-y				+= vgetrandom.o vgetrandom-chacha.o
vobjs-$(CONFIG_X86_SGX)		+= vsgx.o

# Compilation flags
flags-y				:= -DBUILD_VDSO64 -m64 -mcmodel=small

# The location of this include matters!
include $(src)/../common/Makefile.include

#
# X32 processes use x32 vDSO to access 64bit kernel data.
#
# Build x32 vDSO image:
# 1. Compile x32 vDSO as 64bit.
# 2. Convert object files to x32.
# 3. Build x32 VDSO image with x32 objects, which contains 64bit codes
# so that it can reach 64bit address space with 64bit pointers.
#

# Convert 64bit object file to x32 for x32 vDSO.
quiet_cmd_x32 = X32     $@
      cmd_x32 = $(OBJCOPY) -O elf32-x86-64 $< $@

$(obj)/%-x32.o: $(obj)/%.o FORCE
	$(call if_changed,x32)

vobjsx32 = $(patsubst %.o,%-x32.o,$(vobjs))
targets += $(patsubst %.o,%-x32.o,$(vobjs-y))

# Linker options for the vdso
VDSO_LDFLAGS_64  := -m elf_x86_64 -soname linux-vdso.so.1 -z max-page-size=4096
VDSO_LDFLAGS_x32 := $(subst elf_x86_64,elf32_x86_64,$(VDSO_LDFLAGS_64))

$(obj)/vdso64.so.dbg:	$(vobjs)
$(obj)/vdsox32.so.dbg:	$(vobjsx32)
