#
# arch/x86/realmode/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
#

always := realmode.bin

realmode-y			+= header.o
realmode-y			+= trampoline_$(BITS).o
realmode-y			+= stack.o
realmode-$(CONFIG_X86_32)	+= reboot_32.o
realmode-$(CONFIG_ACPI_SLEEP)	+= $(wakeup-objs)

wakeup-objs	:= wakeup_asm.o wakemain.o video-mode.o
wakeup-objs	+= copy.o bioscall.o regs.o
# The link order of the video-*.o modules can matter.  In particular,
# video-vga.o *must* be listed first, followed by video-vesa.o.
# Hardware-specific drivers should follow in the order they should be
# probed, and video-bios.o should typically be last.
wakeup-objs	+= video-vga.o
wakeup-objs	+= video-vesa.o
wakeup-objs	+= video-bios.o

targets	+= $(realmode-y)

REALMODE_OBJS = $(addprefix $(obj)/,$(realmode-y))

sed-pasyms := -n -r -e 's/^([0-9a-fA-F]+) [ABCDGRSTVW] (.+)$$/pa_\2 = \2;/p'

quiet_cmd_pasyms = PASYMS  $@
      cmd_pasyms = $(NM) $(filter-out FORCE,$^) | \
		   sed $(sed-pasyms) | sort | uniq > $@

$(obj)/pasyms.h: $(REALMODE_OBJS) FORCE
	$(call if_changed,pasyms)

$(obj)/realmode.lds: $(obj)/pasyms.h

LDFLAGS_realmode.elf := --emit-relocs -T
CPPFLAGS_realmode.lds += -P -C -I$(obj)

$(obj)/realmode.elf: $(obj)/realmode.lds $(REALMODE_OBJS) FORCE
	$(call if_changed,ld)

OBJCOPYFLAGS_realmode.bin := -O binary

$(obj)/realmode.bin: $(obj)/realmode.elf
	$(call if_changed,objcopy)

quiet_cmd_relocs = RELOCS  $@
      cmd_relocs = arch/x86/tools/relocs --realmode $< > $@
$(obj)/realmode.relocs: $(obj)/realmode.elf FORCE
	$(call if_changed,relocs)

# ---------------------------------------------------------------------------

# How to compile the 16-bit code.  Note we always compile for -march=i386,
# that way we can complain to the user if the CPU is insufficient.
KBUILD_CFLAGS	:= $(LINUXINCLUDE) -m32 -g -Os -D_SETUP -D__KERNEL__ -D_WAKEUP \
		   -I$(srctree)/arch/x86/boot \
		   -DDISABLE_BRANCH_PROFILING \
		   -Wall -Wstrict-prototypes \
		   -march=i386 -mregparm=3 \
		   -include $(srctree)/$(src)/../../boot/code16gcc.h \
		   -fno-strict-aliasing -fomit-frame-pointer \
		   $(call cc-option, -ffreestanding) \
		   $(call cc-option, -fno-toplevel-reorder,\
			$(call cc-option, -fno-unit-at-a-time)) \
		   $(call cc-option, -fno-stack-protector) \
		   $(call cc-option, -mpreferred-stack-boundary=2)
KBUILD_AFLAGS	:= $(KBUILD_CFLAGS) -D__ASSEMBLY__
GCOV_PROFILE := n
