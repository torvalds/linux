#!/bin/sh

MAKE=make
if command -v gmake >/dev/null 2>/dev/null; then
  MAKE=gmake
fi
$MAKE maintainer-clean >/dev/null 2>/dev/null

if [ -x "`which autoreconf 2>/dev/null`" ] ; then
   exec autoreconf -ivf
fi

LIBTOOLIZE=libtoolize
SYSNAME=`uname`
if [ "x$SYSNAME" = "xDarwin" ] ; then
  LIBTOOLIZE=glibtoolize
fi
aclocal -I m4 && \
	autoheader && \
	$LIBTOOLIZE && \
	autoconf && \
	automake --add-missing --force-missing --copy
