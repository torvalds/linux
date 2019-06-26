#
# Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.

lib-y	:= strchr-700.o strcpy-700.o strlen.o memcmp.o

lib-$(CONFIG_ISA_ARCOMPACT)	+= memcpy-700.o memset.o strcmp.o
lib-$(CONFIG_ISA_ARCV2)		+= memset-archs.o strcmp-archs.o

ifdef CONFIG_ARC_USE_UNALIGNED_MEM_ACCESS
lib-$(CONFIG_ISA_ARCV2)		+=memcpy-archs-unaligned.o
else
lib-$(CONFIG_ISA_ARCV2)		+=memcpy-archs.o
endif
