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

arch/sh/include/asm/machtypes.h: $(src)/gen-mach-types $(src)/mach-types
	@echo '  Generating $@'
	$(Q)if [ ! -d arch/sh/include/asm ]; then mkdir -p arch/sh/include/asm; fi
	$(Q)$(AWK) -f $^ > $@ || { rm -f $@; /bin/false; }
