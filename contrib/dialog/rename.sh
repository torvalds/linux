#! /bin/sh
# $Id: rename.sh,v 1.4 2012/12/19 10:17:36 tom Exp $
##############################################################################
# Copyright (c) 2011,2012 Thomas E. Dickey                                   #
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
# install-helper for dialog's manpages, e.g., as "cdialog".
#
# $1 = input file
# $2 = output file
# $3 = actual program name that dialog is installed as
# $4 = actual name that header/library are installed as
# $5+ = install program and possible options

LANG=C;     export LANG
LC_ALL=C;   export LC_ALL
LC_CTYPE=C; export LC_CTYPE
LANGUAGE=C; export LANGUAGE

SOURCE=$1; shift
TARGET=$1; shift
BINARY=$1; shift
PACKAGE=$1; shift

CHR_LEAD=`echo "$BINARY" | sed -e 's/^\(.\).*/\1/'`
CHR_TAIL=`echo "$BINARY" | sed -e 's/^.//'`
ONE_CAPS=`echo $CHR_LEAD | tr '[a-z]' '[A-Z]'`$CHR_TAIL
ALL_CAPS=`echo "$BINARY" | tr '[a-z]' '[A-Z]'`

sed	-e "s,^\.ds p dialog\>,.ds p $BINARY," \
	-e "s,^\.ds l dialog\>,.ds l $PACKAGE," \
	-e "s,^\.ds L Dialog\>,.ds L $ONE_CAPS," \
	-e "s,^\.ds D DIALOG\>,.ds D $ALL_CAPS," \
	-e 's,^dialog \\- ,'"$PACKAGE"' \\- ,' \
	<$SOURCE >source.tmp
"$@" source.tmp $TARGET
rm -f source.tmp
