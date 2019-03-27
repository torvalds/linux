1,/START OF targmatch.h/	d
/END OF targmatch.h/,$		d
/^[ 	]*case/,/^[ 	]*esac/ d
s/^#if/KEEP #if/
s/^#endif/KEEP #endif/
s/^[ 	]*#.*$//
s/^KEEP #/#/
s/[ 	]*\\$//
t lab1
	:lab1
s/[| 	][| 	]*\([^|() 	][^|() 	]*\)[ 	]*|/{ "\1", NULL },/g
s/[| 	][| 	]*\([^|() 	][^|() 	]*\)[ 	]*)/{ "\1",/g
t lab2
s/^[ 	]*targ_defvec=\([^ 	]*\)/#if !defined (SELECT_VECS) || defined (HAVE_\1)/
t lab3
s/.*=.*//
s/;;//
b
	:lab2
H
d
	:lab3
G
s/\n/%EOL%/g
s/\(defined (HAVE_\)\([^)]*\)\(.*\)/\1\2\3\
\&\2 },\
#endif/
s/%EOL%/\
/g
p
s/.*//g
s/\n//g
h
