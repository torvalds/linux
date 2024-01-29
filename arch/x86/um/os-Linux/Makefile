#
# Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
# Licensed under the GPL
#

obj-y = registers.o task_size.o mcontext.o

obj-$(CONFIG_X86_32) += tls.o

USER_OBJS := $(obj-y)

include $(srctree)/arch/um/scripts/Makefile.rules
