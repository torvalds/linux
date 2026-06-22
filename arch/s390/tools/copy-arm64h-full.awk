#!/usr/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
#
# Process entire ARM64 headers for sharing with s390 KVM
#
# Usage: copy-arm64h-full.awk <input_file>
#
# This processes the entire file (unlike copy-arm64h.awk which only extracts marked sections)

BEGIN {
	max_guard_line = 25
	guard_found = 0
	header_added = 0
}

# Find and process the include guard in the first few lines
NR <= max_guard_line && !guard_found && /^#ifndef [A-Z_0-9]+$/ {
	guard_name = $2
	guard_found = 1
	print "/* This header was copied from " FILENAME " */"
	print ""
	# Map subsequent lines back to the original ARM64 source file so that
	# compiler diagnostics point there rather than at the generated copy.
	print "#line " NR " \"" FILENAME "\""
	print
	header_added = 1
	next
}

# Transform include directives
/^#include <uapi\/asm\// {
	sub(/<uapi\/asm\//, "<uapi/arm64/")
	print
	next
}

/^#include <asm\// {
	sub(/<asm\//, "<arm64/")
	print
	next
}

/ARM64_S390_COMMON/ {
	exit 1
}

# Pass through all other lines
{
	print
}
