#
# arch/x86/kernel/acpi/realmode/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

always		:= wakeup.o

wakeup-y	+= wakeup_asm.o wakemain.o video-mode.o
wakeup-y	+= copy.o bioscall.o regs.o

# The link order of the video-*.o modules can matter.  In particular,
# video-vga.o *must* be listed first, followed by video-vesa.o.
# Hardware-specific drivers should follow in the order they should be
# probed, and video-bios.o should typically be last.
wakeup-y	+= video-vga.o
wakeup-y	+= video-vesa.o
wakeup-y	+= video-bios.o

targets		+= $(wakeup-y)

WAKEUP_OBJS = $(addprefix $(obj)/,$(wakeup-y))

LDFLAGS_wakeup.o := -m elf_i386 -r
$(obj)/wakeup.o: $(WAKEUP_OBJS) FORCE
	$(call if_changed,ld)

bootsrc := $(src)/../../../boot

ccflags-y += -D_WAKEUP -I$(srctree)/$(bootsrc)
asflags-y += -D_WAKEUP -I$(srctree)/$(bootsrc)
