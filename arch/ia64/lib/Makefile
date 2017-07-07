#
# Makefile for ia64-specific library routines..
#

lib-y := io.o __divsi3.o __udivsi3.o __modsi3.o __umodsi3.o		\
	__divdi3.o __udivdi3.o __moddi3.o __umoddi3.o			\
	checksum.o clear_page.o csum_partial_copy.o			\
	clear_user.o strncpy_from_user.o strnlen_user.o			\
	flush.o ip_fast_csum.o do_csum.o				\
	memset.o strlen.o xor.o

lib-$(CONFIG_ITANIUM)	+= copy_page.o copy_user.o memcpy.o
lib-$(CONFIG_MCKINLEY)	+= copy_page_mck.o memcpy_mck.o
lib-$(CONFIG_PERFMON)	+= carta_random.o

AFLAGS___divdi3.o	=
AFLAGS___udivdi3.o	= -DUNSIGNED
AFLAGS___moddi3.o	= 	     -DMODULO
AFLAGS___umoddi3.o	= -DUNSIGNED -DMODULO

AFLAGS___divsi3.o	=
AFLAGS___udivsi3.o	= -DUNSIGNED
AFLAGS___modsi3.o	=	     -DMODULO
AFLAGS___umodsi3.o	= -DUNSIGNED -DMODULO

$(obj)/__divdi3.o: $(src)/idiv64.S FORCE
	$(call if_changed_rule,as_o_S)

$(obj)/__udivdi3.o: $(src)/idiv64.S FORCE
	$(call if_changed_rule,as_o_S)

$(obj)/__moddi3.o: $(src)/idiv64.S FORCE
	$(call if_changed_rule,as_o_S)

$(obj)/__umoddi3.o: $(src)/idiv64.S FORCE
	$(call if_changed_rule,as_o_S)

$(obj)/__divsi3.o: $(src)/idiv32.S FORCE
	$(call if_changed_rule,as_o_S)

$(obj)/__udivsi3.o: $(src)/idiv32.S FORCE
	$(call if_changed_rule,as_o_S)

$(obj)/__modsi3.o: $(src)/idiv32.S FORCE
	$(call if_changed_rule,as_o_S)

$(obj)/__umodsi3.o: $(src)/idiv32.S FORCE
	$(call if_changed_rule,as_o_S)
