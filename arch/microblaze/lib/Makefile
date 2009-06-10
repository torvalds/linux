#
# Makefile
#

lib-y :=  memset.o checksum.o

ifeq ($(CONFIG_OPT_LIB_ASM),y)
lib-y += fastcopy.o
else
lib-y += memcpy.o memmove.o
endif

lib-y +=  uaccess.o
