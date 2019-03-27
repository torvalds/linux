# $Id$

BEGIN {
	print "#include <config.h>"
	print "#include <stdio.h>"
	print "#ifdef HAVE_SYS_TYPES_H"
	print "#include <sys/types.h>"
	print "#endif"
	print "#ifdef HAVE_SYS_SOCKET_H"
	print "#include <sys/socket.h>"
	print "#endif"
	print ""
	print "int main(int argc, char **argv)"
	print "{"
	    print "puts(\"/* This is an OS dependent, generated file */\");"
	print "puts(\"\\n\");"
	print "puts(\"#ifndef __ROKEN_H__\");"
	print "puts(\"#define __ROKEN_H__\");"
	print "puts(\"\");"
}

$1 == "#ifdef" || $1 == "#ifndef" || $1 == "#if" || $1 == "#else" || $1 == "#elif" || $1 == "#endif" {
	print $0;
	next
}

{
	s = ""
	for(i = 1; i <= length; i++){
		x = substr($0, i, 1)
		if(x == "\"" || x == "\\")
			s = s "\\";
		s = s x;
	}
	print "puts(\"" s "\");"
}

END {
	print "puts(\"\");"
	print "puts(\"#endif /* __ROKEN_H__ */\");"
	print "return 0;"
	print "}"
}
