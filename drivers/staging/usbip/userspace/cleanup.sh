#!/bin/sh -x


if [ -r Makefile ]; then
	make distclean
fi

FILES="configure cscope.out Makefile.in depcomp compile config.guess config.sub config.h.in~ config.log config.status ltmain.sh libtool config.h.in autom4te.cache missing aclocal.m4 install-sh cmd/Makefile.in lib/Makefile.in Makefile lib/Makefile cmd/Makefile"

rm -Rf $FILES
