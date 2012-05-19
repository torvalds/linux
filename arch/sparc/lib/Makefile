# Makefile for Sparc library files..
#

asflags-y := -ansi -DST_DIV0=0x02
ccflags-y := -Werror

lib-$(CONFIG_SPARC32) += mul.o rem.o sdiv.o udiv.o umul.o urem.o ashrdi3.o
lib-$(CONFIG_SPARC32) += memcpy.o memset.o
lib-y                 += strlen.o
lib-y                 += checksum_$(BITS).o
lib-$(CONFIG_SPARC32) += blockops.o
lib-y                 += memscan_$(BITS).o memcmp.o strncmp_$(BITS).o
lib-y                 += strncpy_from_user_$(BITS).o strlen_user_$(BITS).o
lib-$(CONFIG_SPARC32) += divdi3.o udivdi3.o
lib-$(CONFIG_SPARC32) += copy_user.o locks.o
lib-y                 += atomic_$(BITS).o
lib-$(CONFIG_SPARC32) += lshrdi3.o ashldi3.o
lib-$(CONFIG_SPARC32) += muldi3.o bitext.o cmpdi2.o

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

lib-$(CONFIG_SPARC64) += GENmemcpy.o GENcopy_from_user.o GENcopy_to_user.o
lib-$(CONFIG_SPARC64) += GENpatch.o GENpage.o GENbzero.o

lib-$(CONFIG_SPARC64) += copy_in_user.o user_fixup.o memmove.o
lib-$(CONFIG_SPARC64) += mcount.o ipcsum.o xor.o hweight.o ffs.o

obj-y                 += iomap.o
obj-$(CONFIG_SPARC32) += atomic32.o ucmpdi2.o
obj-y                 += ksyms.o
obj-$(CONFIG_SPARC64) += PeeCeeI.o
obj-y                 += usercopy.o
