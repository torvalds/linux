#!/bin/sh

#
#	This script will determine if the system is a System V or BSD based
#	UNIX system and create a makefile for ee appropriate for the system.
#
# $Header: /home/hugh/sources/old_ae/RCS/create.make,v 1.13 2002/09/23 04:18:13 hugh Exp $
#

#set -x

name_string="`uname`"

# test for existence of termcap (exists on both BSD and SysV systems)

if [ -f /etc/termcap -o -f /usr/share/lib/termcap -o -f /usr/share/misc/termcap ]
then
	if [ -f /usr/share/lib/termcap ]
	then
		termcap_exists="-DTERMCAP=\"\\\"/usr/share/lib/termcap\\\"\""
	elif [ -f /usr/share/misc/termcap ]
	then
		termcap_exists="-DTERMCAP=\"\\\"/usr/share/misc/termcap\\\"\""
	elif [ -f /etc/termcap ]
	then
		termcap_exists="-DTERMCAP=\"\\\"/etc/termcap\\\"\""
	fi
else
	termcap_exists=""
fi

# test for terminfo directory (exists on SysV systems)

if [ -d /usr/lib/terminfo -o -d /usr/share/lib/terminfo -o -d /usr/share/terminfo ]
then
	terminfo_exists=""
else
	terminfo_exists="-DCAP"
fi

# test for existence of termio header (on SysV systems)

if [ -f /usr/include/termio.h ]
then
	termio="-DSYS5"
else
	termio=""
fi

# test for sgtty header (on BSD systems)

if [ -f /usr/include/sgtty.h ]
then
	sgtty="TRUE"
else
	sgtty=""
fi

# look for select call in headers, make sure headers exist

HEADER_FILES=""

if [ -f /usr/include/sys/time.h ]
then
	HEADER_FILES="/usr/include/sys/time.h "
fi

if [ -f /usr/include/sys/types.h ]
then
	HEADER_FILES="$HEADER_FILES /usr/include/sys/types.h"
fi

# check for unistd.h

if [ -f /usr/include/unistd.h ]
then
	HAS_UNISTD=-DHAS_UNISTD
	HEADER_FILES="$HEADER_FILES /usr/include/unistd.h"
else
	HAS_UNISTD=""
fi

if [ -n "$HEADER_FILES" ]
then
	string="`grep select $HEADER_FILES`"
	if [ -n "$string" ]
	then
		BSD_SELECT="-DBSD_SELECT"
	else
		BSD_SELECT=""
	fi
fi

# check for existence of select.h (on AIX)

if [ -f /usr/include/sys/select.h ]
then
	select_hdr="-DSLCT_HDR"
else
	select_hdr=""
fi

# check for stdlib.h

if [ -f /usr/include/stdlib.h ]
then
	HAS_STDLIB=-DHAS_STDLIB
else
	HAS_STDLIB=""
fi

# check for stdarg.h

if [ -f /usr/include/stdarg.h ]
then
	HAS_STDARG=-DHAS_STDARG
else
	HAS_STDARG=""
fi

# check for ctype.h

if [ -f /usr/include/ctype.h ]
then
	HAS_CTYPE=-DHAS_CTYPE
else
	HAS_CTYPE=""
fi

# check for sys/ioctl.h

if [ -f /usr/include/sys/ioctl.h ]
then
	HAS_SYS_IOCTL=-DHAS_SYS_IOCTL
else
	HAS_SYS_IOCTL=""
fi

# check for sys/wait.h

if [ -f /usr/include/sys/wait.h ]
then
        HAS_SYS_WAIT=-DHAS_SYS_WAIT
else
        HAS_SYS_WAIT=""
fi

# check for localization headers

if [ -f /usr/include/locale.h -a -f /usr/include/nl_types.h ]
then
	catgets=""
else
	catgets="-DNO_CATGETS"
fi

# make decisions about use of new_curse.c (use of new_curse is recommended 
# rather than local curses)

if [ -n "$terminfo_exists" -a -z "$termcap_exists" ]
then
	echo "Neither terminfo or termcap are on this system!  "
	if [ -f /usr/include/curses.h ]
	then
		echo "Relying on local curses implementation."
	else
		cat <<-EOF
		Don't know where to find curses, you'll need to modify 
		source code to be able to build!
		
		Modify the file make.default and build ee by typing:
		
		make -f make.default
		
		EOF

		exit 1
	fi
	
	TARGET="curses"
	curses=""
else
	curses="-DNCURSE"
	TARGET="ee"
fi

if [ -z "$termio" -a -z "$sgtty" ]
then
	echo "Neither termio.h or sgtty.h are on this system!  "
	if [ -f /usr/include/curses.h ]
	then
		echo "Relying on local curses implementation."
	else
		cat <<-EOF
		Don't know where to find curses, you'll need to modify 
		source code to be able to build!
		
		Modify the file make.default and build ee by typing:
		
		make -f make.default
		
		EOF

		exit 1
	fi
	
	TARGET="curses"
	curses=""
fi

# check if this is a SunOS system

if [ -d /usr/5include ]
then
	five_include="-I/usr/5include"
else
	five_include=""
fi

if [ -d /usr/5lib ]
then
	five_lib="-L/usr/5lib"
else
	five_lib=""
fi


if [ "$name_string" = "Darwin" ]
then
	if [ -n "$CFLAGS" ]
	then
		other_cflags="${CFLAGS} -DNO_CATGETS"
	else
		other_cflags="-DNO_CATGETS"
	fi
else

	if [ -n "$CFLAGS" ]
	then
		if [ -z "`echo $CFLAGS | grep '[-]g'`" ]
		then
			other_cflags="${CFLAGS} -s"
		else
			other_cflags="${CFLAGS}"
		fi
	else
		other_cflags="-s"
	fi
fi

# time to write the makefile

echo "Generating make.local"

if [ -f make.local ]
then
	mv make.local make.lcl.old
fi

echo "DEFINES =	$termio $terminfo_exists $BSD_SELECT $catgets $select $curses " > make.local
echo "" >> make.local
echo "CFLAGS =	$HAS_UNISTD $HAS_STDARG $HAS_STDLIB $HAS_CTYPE $HAS_SYS_IOCTL $HAS_SYS_WAIT $five_lib $five_include $select_hdr $other_cflags $termcap_exists" >> make.local
echo "" >> make.local
echo "" >> make.local
echo "all :	$TARGET" >> make.local

cat  >> make.local << EOF

curses :	ee.c
	cc ee.c -o ee \$(CFLAGS) -lcurses 

ee :	ee.o new_curse.o
	cc -o ee ee.o new_curse.o \$(CFLAGS) 

ee.o :	ee.c new_curse.h
	cc -c ee.c \$(DEFINES) \$(CFLAGS) 

new_curse.o :	new_curse.c new_curse.h
	cc new_curse.c -c \$(DEFINES) \$(CFLAGS)

EOF

if [ -f make.lcl.old ]
then
	diffs="`cmp make.lcl.old make.local`"
	if [ -n "${diffs}" ]
	then
		rm -f ee.o new_curse.o ee 
	fi
	rm -f make.lcl.old
fi

