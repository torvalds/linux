#!/bin/sh

awk '
BEGIN {
	print
	print "#include <ctype.h>"
	print "#include <sys/types.h>"
	print "#include <errno.h>"
	print
	print "#define ETHER_ADDR_LEN 6"
	print
	print "int ether_aton_r(u_char *dest, size_t len, const char *str);"
	print
}
/^ether_aton_r/ {
	print prevline
	out = 1
}
{
	if (out) print
	else prevline = $0
}
/^}$/ {
	if (out) exit(0)
}' $1 >$2
