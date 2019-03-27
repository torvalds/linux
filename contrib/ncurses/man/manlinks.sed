# $Id: manlinks.sed,v 1.13 2008/01/19 23:31:17 tom Exp $
##############################################################################
# Copyright (c) 2000-2003,2008 Free Software Foundation, Inc.                #
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
# Given a manpage (nroff) as input, writes a list of the names that are
# listed in the "NAME" section, i.e., the names that we would like to use
# as aliases for the manpage -T.Dickey
#
# eliminate formatting controls that get in the way
/^'\\"/d
/\.\\"/d
/^\.br/d
/^\.sp/d
/typedef/d
s/^\.IX//
s/\\f.//g
s/[:,]/ /g
#
# eliminate unnecessary whitespace, convert multiple blanks to single space
s/^[ 	][ 	]*//
s/[ 	][ 	]*$//
s/[ 	][ 	]*/ /g
#
# convert ".SH" into a more manageable form
s/\.SH[ 	][ 	]*/.SH_(/
#
# in ".SH NAME"
# change "\-" to "-", eliminate text after "-", and split the remaining lines
# at each space, making a list of names:
/^\.SH_(NAME/,/^\.SH_(SYNOPSIS/{
s/\\-.*/ -/
/ -/{
s/ -.*//
s/ /\
/g
}
/^-/{
d
}
s/ /\
/g
}
#
# in ".SH SYNOPSIS"
# remove any line that does not contain a '(', since we only want functions.
# then strip off return-type of each function.
# finally, remove the parameter list, which begins with a '('.
/^\.SH_(SYNOPSIS/,/^\.SH_(DESCRIPTION/{
/^[^(]*$/d
# reduce
#	.B "int add_wch( const cchar_t *\fIwch\fB );"
# to
#	add_wch( const cchar_t *\fIwch\fB );"
s/^\([^ (]* [^ (]* [*]*\)//g
s/^\([^ (]* [*]*\)//g
# trim blanks in case we have
#	void (*) (FORM *) field_init(const FORM *form);
s/) (/)(/g
# reduce stuff like
#	void (*)(FORM *) field_init(const FORM *form);
# to
#	field_init(const FORM *form);
s/^\(([^)]*)\)\(([^)]*)\)*[ ]*//g
# rename marker temporarily
s/\.SH_(/.SH_/
# kill lines with ");", and trim off beginning of argument list.
s/[()].*//
# rename marker back
s/\.SH_/.SH_(/
}
#
# delete ".SH DESCRIPTION" and following lines
/^\.SH_(DESCRIPTION/,${
d
}
#
# delete any remaining directives
/^\./d
