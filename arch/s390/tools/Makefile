#
# Makefile for s390 specific build tools
#

hostprogs-y		    += gen_facilities
HOSTCFLAGS_gen_facilities.o += -Wall $(LINUXINCLUDE)

define filechk_facilities.h
	$(obj)/gen_facilities
endef

$(obj)/gen_facilities.o: $(srctree)/arch/s390/tools/gen_facilities.c

include/generated/facilities.h: $(obj)/gen_facilities FORCE
	$(call filechk,facilities.h)
