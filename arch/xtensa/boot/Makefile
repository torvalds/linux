#
# arch/xtensa/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
#


# KBUILD_CFLAGS used when building rest of boot (takes effect recursively)
KBUILD_CFLAGS	+= -fno-builtin -Iarch/$(ARCH)/boot/include
HOSTFLAGS	+= -Iarch/$(ARCH)/boot/include

BIG_ENDIAN	:= $(shell echo -e __XTENSA_EB__ | $(CC) -E - | grep -v "\#")

export ccflags-y
export BIG_ENDIAN

subdir-y	:= lib

# Subdirs for the boot loader(s)

bootdir-$(CONFIG_XTENSA_PLATFORM_ISS)	 += boot-elf
bootdir-$(CONFIG_XTENSA_PLATFORM_XT2000) += boot-redboot boot-elf boot-uboot
bootdir-$(CONFIG_XTENSA_PLATFORM_XTFPGA) += boot-redboot boot-elf boot-uboot


BUILTIN_DTB := $(patsubst "%",%,$(CONFIG_BUILTIN_DTB)).dtb.o
ifneq ($(CONFIG_BUILTIN_DTB),"")
obj-$(CONFIG_OF) += $(BUILTIN_DTB)
endif

# Rule to build device tree blobs
$(obj)/%.dtb: $(src)/dts/%.dts FORCE
	$(call if_changed_dep,dtc)

clean-files := *.dtb.S

zImage Image: $(bootdir-y)

$(bootdir-y): $(addprefix $(obj)/,$(subdir-y)) \
	      $(addprefix $(obj)/,$(host-progs))
	$(Q)$(MAKE) $(build)=$(obj)/$@ $(MAKECMDGOALS)

OBJCOPYFLAGS = --strip-all -R .comment -R .note.gnu.build-id -O binary

vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

vmlinux.bin.gz: vmlinux.bin FORCE
	$(call if_changed,gzip)

boot-elf: vmlinux.bin
boot-redboot: vmlinux.bin.gz
boot-uboot: vmlinux.bin.gz
