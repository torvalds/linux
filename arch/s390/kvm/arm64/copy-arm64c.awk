#!/usr/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
#
# Extract marked sections from ARM64 C files for sharing with s390 KVM
#
# Usage: copy-arm64c.awk <input_file>
#
# Extracts all sections between start/end markers. If no markers found, signals failure.

BEGIN {
	# Constants
	start_pattern = "^#if ARM64_S390_COMMON == 1$"
	end_pattern = "^#endif /\\* ARM64_S390_COMMON == 1 \\*/$"

	# State variables
	copying = found_marker = 0
	file_header_done = 0
	depth = 0
}

!file_header_done {
	if (/^\/\*/ || /^\/\/ SPDX-License-Identifier:/) {
		print
		next
	}
	if (/[[:space:]]\*([[:space:]]|$)/) {
		print
		next
	}
	if (/\*\//) {
		print " *"
	} else {
		print "/*"
	}

	print " * This file was automatically generated from " FILENAME
	print " * Do not modify this file directly."
	print " */"
	print ""
	print "#ifndef __INCL_GEN_ARM_FILE"
	print "#error do not include this file directly."
	print "#endif /* __INCL_GEN_ARM_FILE */"
	print "#undef __INCL_GEN_ARM_FILE"
	print ""

	file_header_done = 1
}

$0 ~ start_pattern {
	if (copying) { error = 1; exit }
	depth = copying = found_marker = 1
	# Map subsequent lines back to the original ARM64 source file so that
	# compiler diagnostics point there rather than at the generated copy.
	print "#line " (NR + 1) " \"" FILENAME "\""
	next
}

copying && /^[ \t]*#[ \t]*if(def|ndef)?([ \t]|$)/ { depth++ }

$0 ~ /^[ \t]*#[ \t]*endif/ {
	depth--
	if (depth == 0) {
		if ($0 ~ end_pattern) {
			copying = 0
			next
		}
		error = 1; exit
	}
}

/ARM64_S390_COMMON/ { error = 1; exit }

copying {
	gsub(/#include <asm\//, "#include <arm64/")
	gsub(/#include <uapi\/asm\//, "#include <uapi/arm64/")
	print
	next
}

END {
	exit error || !found_marker || copying
}
