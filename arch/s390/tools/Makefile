# SPDX-License-Identifier: GPL-2.0
#
# Makefile for s390 specific build tools
#

hostprogs-y		    += gen_facilities
hostprogs-y		    += gen_opcode_table

HOSTCFLAGS_gen_facilities.o += -Wall $(LINUXINCLUDE)
HOSTCFLAGS_gen_opcode_table.o += -Wall $(LINUXINCLUDE)

define filechk_facilities.h
	$(obj)/gen_facilities
endef

define filechk_dis.h
	( $(obj)/gen_opcode_table < $(srctree)/arch/$(ARCH)/tools/opcodes.txt )
endef

include/generated/facilities.h: $(obj)/gen_facilities FORCE
	$(call filechk,facilities.h)

include/generated/dis.h: $(obj)/gen_opcode_table FORCE
	$(call filechk,dis.h,__FUN)
