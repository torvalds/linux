#
# arch/sh/tools/Makefile
#
# Copyright (C) 2003  Paul Mundt
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Shamelessly cloned from ARM.
#

include/asm-sh/machtypes.h: $(src)/gen-mach-types $(src)/mach-types
	@echo '  Generating $@'
	$(Q)if [ ! -d include/asm-sh ]; then mkdir -p include/asm-sh; fi
	$(Q)$(AWK) -f $^ > $@ || { rm -f $@; /bin/false; }
