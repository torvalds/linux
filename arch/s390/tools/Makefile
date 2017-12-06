# SPDX-License-Identifier: GPL-2.0
#
# Makefile for s390 specific build tools
#

hostprogs-y		    += gen_facilities
HOSTCFLAGS_gen_facilities.o += -Wall $(LINUXINCLUDE)

define filechk_facilities.h
	$(obj)/gen_facilities
endef

include/generated/facilities.h: $(obj)/gen_facilities FORCE
	$(call filechk,facilities.h)
