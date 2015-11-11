#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

ifdef CONFIG_MMU
ifdef CONFIG_INITIALIZE_XTENSA_MMU_INSIDE_VMLINUX
UIMAGE_LOADADDR = 0x00003000
else
UIMAGE_LOADADDR = 0xd0003000
endif
else
UIMAGE_LOADADDR = $(shell printf "0x%x" $$(( ${CONFIG_DEFAULT_MEM_START} + 0x3000 )) )
endif
UIMAGE_COMPRESSION = gzip

$(obj)/../uImage: vmlinux.bin.gz FORCE
	$(call if_changed,uimage)
	$(Q)$(kecho) '  Kernel: $@ is ready'

zImage: $(obj)/../uImage
