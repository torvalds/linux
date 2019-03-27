#!/bin/sh
#
# findssl.sh
#	Search for all instances of OpenSSL headers and libraries
#	and print their versions.
#	Intended to help diagnose OpenSSH's "OpenSSL headers do not
#	match your library" errors.
#
#	Written by Darren Tucker (dtucker at zip dot com dot au)
#	This file is placed in the public domain.
#
#	Release history:
#	2002-07-27: Initial release.
#	2002-08-04: Added public domain notice.
#	2003-06-24: Incorporated readme, set library paths. First cvs version.
#	2004-12-13: Add traps to cleanup temp files, from Amarendra Godbole.
#
# "OpenSSL headers do not match your library" are usually caused by
# OpenSSH's configure picking up an older version of OpenSSL headers
# or libraries.  You can use the following # procedure to help identify
# the cause.
#
# The  output  of  configure  will  tell you the versions of the OpenSSL
# headers and libraries that were picked up, for example:
#
# checking OpenSSL header version... 90604f (OpenSSL 0.9.6d 9 May 2002)
# checking OpenSSL library version... 90602f (OpenSSL 0.9.6b [engine] 9 Jul 2001)
# checking whether OpenSSL's headers match the library... no
# configure: error: Your OpenSSL headers do not match your library
#
# Now run findssl.sh. This should identify the headers and libraries
# present  and  their  versions.  You  should  be  able  to identify the
# libraries  and headers used and adjust your CFLAGS or remove incorrect
# versions.  The  output will show OpenSSL's internal version identifier
# and should look something like:

# $ ./findssl.sh
# Searching for OpenSSL header files.
# 0x0090604fL /usr/include/openssl/opensslv.h
# 0x0090604fL /usr/local/ssl/include/openssl/opensslv.h
#
# Searching for OpenSSL shared library files.
# 0x0090602fL /lib/libcrypto.so.0.9.6b
# 0x0090602fL /lib/libcrypto.so.2
# 0x0090581fL /usr/lib/libcrypto.so.0
# 0x0090602fL /usr/lib/libcrypto.so
# 0x0090581fL /usr/lib/libcrypto.so.0.9.5a
# 0x0090600fL /usr/lib/libcrypto.so.0.9.6
# 0x0090600fL /usr/lib/libcrypto.so.1
#
# Searching for OpenSSL static library files.
# 0x0090602fL /usr/lib/libcrypto.a
# 0x0090604fL /usr/local/ssl/lib/libcrypto.a
#
# In  this  example, I gave configure no extra flags, so it's picking up
# the  OpenSSL header from /usr/include/openssl (90604f) and the library
# from /usr/lib/ (90602f).

#
# Adjust these to suit your compiler.
# You may also need to set the *LIB*PATH environment variables if
# DEFAULT_LIBPATH is not correct for your system.
#
CC=gcc
STATIC=-static

#
# Cleanup on interrupt
#
trap 'rm -f conftest.c' INT HUP TERM

#
# Set up conftest C source
#
rm -f findssl.log
cat >conftest.c <<EOD
#include <stdio.h>
int main(){printf("0x%08xL\n", SSLeay());}
EOD

#
# Set default library paths if not already set
#
DEFAULT_LIBPATH=/usr/lib:/usr/local/lib
LIBPATH=${LIBPATH:=$DEFAULT_LIBPATH}
LD_LIBRARY_PATH=${LD_LIBRARY_PATH:=$DEFAULT_LIBPATH}
LIBRARY_PATH=${LIBRARY_PATH:=$DEFAULT_LIBPATH}
export LIBPATH LD_LIBRARY_PATH LIBRARY_PATH

# not all platforms have a 'which' command
if which ls >/dev/null 2>/dev/null; then
    : which is defined
else
    which () {
	saveIFS="$IFS"
	IFS=:
	for p in $PATH; do
	    if test -x "$p/$1" -a -f "$p/$1"; then
		IFS="$saveIFS"
		echo "$p/$1"
		return 0
	    fi
	done
	IFS="$saveIFS"
	return 1
    }
fi

#
# Search for OpenSSL headers and print versions
#
echo Searching for OpenSSL header files.
if [ -x "`which locate`" ]
then
	headers=`locate opensslv.h`
else
	headers=`find / -name opensslv.h -print 2>/dev/null`
fi

for header in $headers
do
	ver=`awk '/OPENSSL_VERSION_NUMBER/{printf \$3}' $header`
	echo "$ver $header"
done
echo

#
# Search for shared libraries.
# Relies on shared libraries looking like "libcrypto.s*"
#
echo Searching for OpenSSL shared library files.
if [ -x "`which locate`" ]
then
	libraries=`locate libcrypto.s`
else
	libraries=`find / -name 'libcrypto.s*' -print 2>/dev/null`
fi

for lib in $libraries
do
	(echo "Trying libcrypto $lib" >>findssl.log
	dir=`dirname $lib`
	LIBPATH="$dir:$LIBPATH"
	LD_LIBRARY_PATH="$dir:$LIBPATH"
	LIBRARY_PATH="$dir:$LIBPATH"
	export LIBPATH LD_LIBRARY_PATH LIBRARY_PATH
	${CC} -o conftest conftest.c $lib 2>>findssl.log
	if [ -x ./conftest ]
	then
		ver=`./conftest 2>/dev/null`
		rm -f ./conftest
		echo "$ver $lib"
	fi)
done
echo

#
# Search for static OpenSSL libraries and print versions
#
echo Searching for OpenSSL static library files.
if [ -x "`which locate`" ]
then
	libraries=`locate libcrypto.a`
else
	libraries=`find / -name libcrypto.a -print 2>/dev/null`
fi

for lib in $libraries
do
	libdir=`dirname $lib`
	echo "Trying libcrypto $lib" >>findssl.log
	${CC} ${STATIC} -o conftest conftest.c -L${libdir} -lcrypto 2>>findssl.log
	if [ -x ./conftest ]
	then
		ver=`./conftest 2>/dev/null`
		rm -f ./conftest
		echo "$ver $lib"
	fi
done

#
# Clean up
#
rm -f conftest.c
