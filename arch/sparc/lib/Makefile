# SPDX-License-Identifier: GPL-2.0
# Makefile for Sparc library files..
#

asflags-y := -ansi -DST_DIV0=0x02

lib-$(CONFIG_SPARC32) += ashrdi3.o
lib-$(CONFIG_SPARC32) += memcpy.o memset.o
lib-y                 += strlen.o
lib-y                 += checksum_$(BITS).o
lib-$(CONFIG_SPARC32) += blockops.o
lib-y                 += memscan_$(BITS).o memcmp.o strncmp_$(BITS).o
lib-$(CONFIG_SPARC32) += divdi3.o udivdi3.o
lib-$(CONFIG_SPARC32) += copy_user.o locks.o
lib-$(CONFIG_SPARC64) += atomic_64.o
lib-$(CONFIG_SPARC32) += lshrdi3.o ashldi3.o
lib-$(CONFIG_SPARC32) += muldi3.o bitext.o
lib-$(CONFIG_SPARC64) += multi3.o
lib-$(CONFIG_SPARC64) += fls.o
lib-$(CONFIG_SPARC64) += fls64.o
lib-$(CONFIG_SPARC64) += NG4fls.o

lib-$(CONFIG_SPARC64) += copy_page.o clear_page.o bzero.o
lib-$(CONFIG_SPARC64) += csum_copy.o csum_copy_from_user.o csum_copy_to_user.o
lib-$(CONFIG_SPARC64) += VISsave.o
lib-$(CONFIG_SPARC64) += bitops.o

lib-$(CONFIG_SPARC64) += U1memcpy.o U1copy_from_user.o U1copy_to_user.o

lib-$(CONFIG_SPARC64) += U3memcpy.o U3copy_from_user.o U3copy_to_user.o
lib-$(CONFIG_SPARC64) += U3patch.o

lib-$(CONFIG_SPARC64) += NGmemcpy.o NGcopy_from_user.o NGcopy_to_user.o
lib-$(CONFIG_SPARC64) += NGpatch.o NGpage.o NGbzero.o

lib-$(CONFIG_SPARC64) += NG2memcpy.o NG2copy_from_user.o NG2copy_to_user.o
lib-$(CONFIG_SPARC64) +=  NG2patch.o

lib-$(CONFIG_SPARC64) += NG4memcpy.o NG4copy_from_user.o NG4copy_to_user.o
lib-$(CONFIG_SPARC64) +=  NG4patch.o NG4copy_page.o NG4clear_page.o NG4memset.o

lib-$(CONFIG_SPARC64) += Memcpy_utils.o

lib-$(CONFIG_SPARC64) += M7memcpy.o M7copy_from_user.o M7copy_to_user.o
lib-$(CONFIG_SPARC64) += M7patch.o M7memset.o

lib-$(CONFIG_SPARC64) += GENmemcpy.o GENcopy_from_user.o GENcopy_to_user.o
lib-$(CONFIG_SPARC64) += GENpatch.o GENpage.o GENbzero.o

lib-$(CONFIG_SPARC64) += copy_in_user.o memmove.o
lib-$(CONFIG_SPARC64) += mcount.o ipcsum.o xor.o hweight.o ffs.o

obj-$(CONFIG_SPARC64) += iomap.o
obj-$(CONFIG_SPARC32) += atomic32.o
obj-$(CONFIG_SPARC64) += PeeCeeI.o
obj-$(CONFIG_CRC32_ARCH) += crc32-sparc.o
crc32-sparc-y := crc32_glue.o crc32c_asm.o
