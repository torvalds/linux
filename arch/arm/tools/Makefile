#
# linux/arch/arm/tools/Makefile
#
# Copyright (C) 2001 Russell King
#

include/asm-arm/mach-types.h: $(src)/gen-mach-types $(src)/mach-types
	@echo '  Generating $@'
	$(Q)$(AWK) -f $^ > $@ || { rm -f $@; /bin/false; }
