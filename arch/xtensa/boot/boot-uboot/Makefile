#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

UIMAGE_LOADADDR = 0xd0001000
UIMAGE_COMPRESSION = gzip

$(obj)/../uImage: vmlinux.bin.gz FORCE
	$(call if_changed,uimage)
	$(Q)$(kecho) '  Kernel: $@ is ready'

zImage: $(obj)/../uImage
