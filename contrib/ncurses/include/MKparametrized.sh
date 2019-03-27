#!/bin/sh
##############################################################################
# Copyright (c) 1998-2000,2006 Free Software Foundation, Inc.                #
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
# $Id: MKparametrized.sh,v 1.6 2006/04/22 21:36:16 tom Exp $
#
# MKparametrized.sh -- generate indirection vectors for various sort methods
#
# The output of this script is C source for an array specifying whether
# termcap strings should undergo parameter and padding translation.
#
CAPS="${1-Caps}"
cat <<EOF
/*
 * parametrized.h --- is a termcap capability parametrized?
 *
 * Note: this file is generated using MKparametrized.sh, do not edit by hand.
 * A value of -1 in the table means suppress both pad and % translations.
 * A value of 0 in the table means do pad but not % translations.
 * A value of 1 in the table means do both pad and % translations.
 */

static short const parametrized[] = {
EOF

# We detect whether % translations should be done by looking for #[0-9] in the
# description field.  We presently suppress padding translation only for the
# XENIX acs_* capabilities.  Maybe someday we'll dedicate a flag field for
# this, that would be cleaner....

${AWK-awk} <$CAPS '
$3 != "str"	{next;}
$1 ~ /^acs_/	{print "-1,\t/* ", $2, " */"; count++; next;}
$0 ~ /#[0-9]/	{print "1,\t/* ", $2, " */"; count++; next;}
		{print "0,\t/* ", $2, " */"; count++;}
END		{printf("} /* %d entries */;\n\n", count);}
'

