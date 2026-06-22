#!/usr/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
#
# Extract marked sections from ARM64 headers for sharing with s390 KVM
#
# Usage: copy-arm64h.awk <input_file>
#
# Extracts all sections between start/end markers. If no markers found, signals for fallback.
BEGIN {
	# Constants
	start_pattern = "^#if ARM64_S390_COMMON == 1$"
	end_pattern = "^#endif \\/\\* ARM64_S390_COMMON == 1 \\*\\/$"
	max_guard_line = 25

	# State variables
	copying = found_marker = error = 0
	guard_name = ""
	file_header_done = 0
	depth = 0
}

NR <= max_guard_line && !guard_name && /^#ifndef [A-Za-z0-9_]+$/ {
	guard_name = $2
	next
}

NR > max_guard_line && !guard_name && !file_header_done {
	print "error: no include guard found in first " max_guard_line " lines" > "/dev/stderr"
	file_header_done = 1
	error = 1; exit
}

$0 ~ start_pattern {
	if (copying) { error = 1; exit }
	if (!found_marker) {
		print "/*"
		print " * This header was automatically generated from " FILENAME
		print " * Do not modify this file directly."
		print " */"
		print "#ifndef " guard_name
		print "#define " guard_name
		print ""
	}
	depth = copying= found_marker = 1
	# Map subsequent lines back to the original ARM64 source file so that
	# compiler diagnostics point there rather than at the generated copy.
	print "#line " (NR + 1) " \"" FILENAME "\""
	next
}

guard_name && !found_marker {
	next
}

$0 ~ /^[ \t]*#[ \t]*endif/ {
	depth--
	if (depth == 0) {
		if ($0 ~ end_pattern ) {
			copying = 0
			next
		}
		error = 1; exit
	}
}

/ARM64_S390_COMMON/ { error = 1; exit }

copying {
	if ($0 ~ /^[ \t]*#[ \t]*if(def|ndef)?([ \t]|$)/) depth++
	gsub(/#include <asm\//, "#include <arm64/")
	gsub(/#include <uapi\/asm\//, "#include <uapi/arm64/")
	print
	next
}

END {
	if (error) exit 1
	if (found_marker) {
		print ""
		print "#endif /* " guard_name " */"
	}
	exit !found_marker || copying
}
