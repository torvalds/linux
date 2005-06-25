#
# arch/xtensa/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
#


CFLAGS 		+= -fno-builtin -Iarch/$(ARCH)/boot/include
HOSTFLAGS	+= -Iarch/$(ARCH)/boot/include

BIG_ENDIAN	:= $(shell echo -e "\#ifdef __XTENSA_EL__\nint little;\n\#else\nint big;\n\#endif" | $(CC) -E -|grep -c big)


export CFLAGS
export AFLAGS
export BIG_ENDIAN

# Subdirs for the boot loader(s)

bootdir-$(CONFIG_XTENSA_PLATFORM_ISS)	 += boot-elf
bootdir-$(CONFIG_XTENSA_PLATFORM_XT2000) += boot-redboot boot-elf

subdir-y	:= lib/

subdir-y	+= boot-elf/ boot-redboot/

zImage zImage.initrd Image Image.initrd: $(bootdir-y)

$(bootdir-y): $(addprefix $(obj)/,$(subdir-y)) \
	      $(addprefix $(obj)/,$(host-progs))
	$(Q)$(MAKE) $(build)=$(obj)/$@ $(MAKECMDGOALS)



