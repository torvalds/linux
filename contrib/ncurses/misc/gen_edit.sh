#!/bin/sh
##############################################################################
# Copyright (c) 2004-2011,2012 Free Software Foundation, Inc.                #
#                                                                            #
# Permission is hereby granted, free of charge, to any person obtaining a    #
# copy of this software and associated documentation files (the "Software"), #
# to deal in the Software without restriction, including without limitation  #
# the rights to use, copy, modify, merge, publish, distribute, distribute    #
# with modifications, sublicense, and/or sell copies of the Software, and to #
# permit persons to whom the Software is furnished to do so, subject to the  #
# following conditions:                                                      #
#                                                                            #
# The above copyright notice and this permission notice shall be included in #
# all copies or substantial portions of the Software.                        #
#                                                                            #
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR #
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   #
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    #
# THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER      #
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    #
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        #
# DEALINGS IN THE SOFTWARE.                                                  #
#                                                                            #
# Except as contained in this notice, the name(s) of the above copyright     #
# holders shall not be used in advertising or otherwise to promote the sale, #
# use or other dealings in this Software without prior written               #
# authorization.                                                             #
##############################################################################
#
# Author: Thomas E. Dickey
#
# $Id: gen_edit.sh,v 1.5 2012/04/01 15:04:37 tom Exp $
# Generate a sed-script for converting the terminfo.src to the form which will
# be installed.
#
# Assumes:
#	The leaf directory names (lib, tabset, terminfo)
#

linux_dft=linux2.2

: ${datadir=/usr/share}
: ${WHICH_LINUX=$linux_dft}
: ${WHICH_XTERM=xterm-new}
: ${XTERM_KBS=BS}

# If we're not installing into /usr/share/, we'll have to adjust the location
# of the tabset files in terminfo.src (which are in a parallel directory).
TABSET=${datadir}/tabset
if test "x$TABSET" != "x/usr/share/tabset" ; then
cat <<EOF
s%/usr/share/tabset%$TABSET%g
EOF
fi

if test "$WHICH_XTERM" != "xterm-new" ; then
echo "** using $WHICH_XTERM terminal description for XTerm entry" >&2
cat <<EOF
/^# This is xterm for ncurses/,/^$/{
	s/use=xterm-new,/use=$WHICH_XTERM,/
}
EOF
fi

if test "$XTERM_KBS" != "BS" ; then
echo "** using DEL for XTerm backspace-key" >&2
cat <<EOF
/^xterm+kbs|fragment for backspace key/,/^#/{
	s/kbs=^H,/kbs=^?,/
}
EOF
fi

# Work around incompatibities built into Linux console.  The 2.6 series added
# a patch to fixup the SI/SO behavior, which is closer to vt100, but the older
# kernels do not recognize those controls.  All of the kernels recognize the
# older flavor of rmacs/smacs, but beginning in the late 1990s, changes made
# as part of implementing UTF-8 prevent using those for line-drawing when the
# console is in UTF-8 mode.  Taking into account the fact that it took about
# ten years to provide (and distribute) the 2.6 series' change for SI/SO, the
# default remains "linux2.2".
case x$WHICH_LINUX in #(vi
xauto)
	system=`uname -s 2>/dev/null`
	if test "x$system" = xLinux
	then
		case x`uname -r` in
		x1.*)
			WHICH_LINUX=linux-c
			;;
		x2.[0-4]*)
			WHICH_LINUX=linux2.2
			;;
		*)
			WHICH_LINUX=linux3.0
			;;
		esac
	else
		WHICH_LINUX=$linux_dft
	fi
	;;
xlinux*)
	# allow specific setting
	;;
*)
	WHICH_LINUX=$linux_dft
	;;
esac

if test $WHICH_LINUX != $linux_dft
then
echo "** using $WHICH_LINUX terminal description for Linux console" >&2
cat <<EOF
/^# This is Linux console for ncurses/,/^$/{
	s/use=$linux_dft,/use=$WHICH_LINUX,/
}
EOF
fi
