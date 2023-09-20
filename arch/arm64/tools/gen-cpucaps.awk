#!/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
# gen-cpucaps.awk: arm64 cpucaps header generator
#
# Usage: awk -f gen-cpucaps.awk cpucaps.txt

# Log an error and terminate
function fatal(msg) {
	print "Error at line " NR ": " msg > "/dev/stderr"
	exit 1
}

# skip blank lines and comment lines
/^$/ { next }
/^#/ { next }

BEGIN {
	print "#ifndef __ASM_CPUCAPS_H"
	print "#define __ASM_CPUCAPS_H"
	print ""
	print "/* Generated file - do not edit */"
	cap_num = 0
	print ""
}

/^[vA-Z0-9_]+$/ {
	printf("#define ARM64_%-40s\t%d\n", $0, cap_num++)
	next
}

END {
	printf("#define ARM64_NCAPS\t\t\t\t\t%d\n", cap_num)
	print ""
	print "#endif /* __ASM_CPUCAPS_H */"
}

# Any lines not handled by previous rules are unexpected
{
	fatal("unhandled statement")
}
