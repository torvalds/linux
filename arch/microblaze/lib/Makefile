#
# Makefile
#

lib-y :=  memset.o

ifeq ($(CONFIG_OPT_LIB_ASM),y)
lib-y += fastcopy.o
else
lib-y += memcpy.o memmove.o
endif

lib-$(CONFIG_NO_MMU) += uaccess.o
lib-$(CONFIG_MMU) += uaccess_old.o
