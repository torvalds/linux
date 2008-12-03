# Makefile for Sparc library files..
#

asflags-y := -ansi -DST_DIV0=0x02
ccflags-y := -Werror

lib-$(CONFIG_SPARC32) += mul.o rem.o sdiv.o udiv.o umul.o urem.o ashrdi3.o
lib-$(CONFIG_SPARC32) += memcpy.o memset.o
lib-y                 += strlen_$(BITS).o
lib-y                 += checksum_$(BITS).o
lib-$(CONFIG_SPARC32) += blockops.o
lib-y                 += memscan_$(BITS).o memcmp_$(BITS).o strncmp_$(BITS).o
lib-y                 += strncpy_from_user_$(BITS).o strlen_user_$(BITS).o
lib-$(CONFIG_SPARC32) += divdi3.o udivdi3.o
lib-$(CONFIG_SPARC32) += copy_user.o locks.o
lib-y                 += atomic_$(BITS).o
lib-$(CONFIG_SPARC32) += lshrdi3.o ashldi3.o
lib-y                 += rwsem_$(BITS).o
lib-$(CONFIG_SPARC32) += muldi3.o bitext.o cmpdi2.o

obj-y                 += iomap.o
obj-$(CONFIG_SPARC32) += atomic32.o
