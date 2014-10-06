#
# Makefile
#

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_ashldi3.o = -pg
CFLAGS_REMOVE_ashrdi3.o = -pg
CFLAGS_REMOVE_lshrdi3.o = -pg
endif

lib-y :=  memset.o

ifeq ($(CONFIG_OPT_LIB_ASM),y)
lib-y += fastcopy.o
else
lib-y += memcpy.o memmove.o
endif

lib-y += uaccess_old.o

# libgcc-style stuff needed in the kernel
obj-y += ashldi3.o ashrdi3.o cmpdi2.o divsi3.o lshrdi3.o modsi3.o
obj-y += muldi3.o mulsi3.o ucmpdi2.o udivsi3.o umodsi3.o
