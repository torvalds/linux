#!/bin/sh
# $Id: MKtermsort.sh,v 1.10 2008/07/12 20:22:54 tom Exp $
#
# MKtermsort.sh -- generate indirection vectors for the various sort methods
#
##############################################################################
# Copyright (c) 1998-2003,2008 Free Software Foundation, Inc.                #
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
# The output of this script is C source for nine arrays that list three sort
# orders for each of the three different classes of terminfo capabilities.
#
# keep the order independent of locale:
if test "${LANGUAGE+set}"    = set; then LANGUAGE=C;    export LANGUAGE;    fi
if test "${LANG+set}"        = set; then LANG=C;        export LANG;        fi
if test "${LC_ALL+set}"      = set; then LC_ALL=C;      export LC_ALL;      fi
if test "${LC_MESSAGES+set}" = set; then LC_MESSAGES=C; export LC_MESSAGES; fi
if test "${LC_CTYPE+set}"    = set; then LC_CTYPE=C;    export LC_CTYPE;    fi
if test "${LC_COLLATE+set}"  = set; then LC_COLLATE=C;  export LC_COLLATE;  fi
#
AWK=${1-awk}
DATA=${2-../include/Caps}

data=data$$
trap 'rm -f $data' 1 2 5 15
sed -e 's/[	][	]*/	/g' < $DATA >$data
DATA=$data

echo "/*";
echo " * termsort.c --- sort order arrays for use by infocmp.";
echo " *";
echo " * Note: this file is generated using MKtermsort.sh, do not edit by hand.";
echo " */";

echo "static const PredIdx bool_terminfo_sort[] = {";
$AWK <$DATA '
BEGIN           {i = 0;}
/^#/            {next;}
$3 == "bool"    {printf("%s\t%d\n", $2, i++);}
' | sort | $AWK '{print "\t", $2, ",\t/* ", $1, " */";}';
echo "};";
echo "";

echo "static const PredIdx num_terminfo_sort[] = {";
$AWK <$DATA '
BEGIN           {i = 0;}
/^#/            {next;}
$3 == "num"     {printf("%s\t%d\n", $2, i++);}
' | sort | $AWK '{print "\t", $2, ",\t/* ", $1, " */";}';
echo "};";
echo "";

echo "static const PredIdx str_terminfo_sort[] = {";
$AWK <$DATA '
BEGIN           {i = 0;}
/^#/            {next;}
$3 == "str"     {printf("%s\t%d\n", $2, i++);}
' | sort | $AWK '{print "\t", $2, ",\t/* ", $1, " */";}';
echo "};";
echo "";

echo "static const PredIdx bool_variable_sort[] = {";
$AWK <$DATA '
BEGIN           {i = 0;}
/^#/            {next;}
$3 == "bool"    {printf("%s\t%d\n", $1, i++);}
' | sort | $AWK '{print "\t", $2, ",\t/* ", $1, " */";}';
echo "};";
echo "";

echo "static const PredIdx num_variable_sort[] = {";
$AWK <$DATA '
BEGIN           {i = 0;}
/^#/            {next;}
$3 == "num"     {printf("%s\t%d\n", $1, i++);}
' | sort | $AWK '{print "\t", $2, ",\t/* ", $1, " */";}';
echo "};";
echo "";

echo "static const PredIdx str_variable_sort[] = {";
$AWK <$DATA '
BEGIN           {i = 0;}
/^#/            {next;}
$3 == "str"     {printf("%s\t%d\n", $1, i++);}
' | sort | $AWK '{print "\t", $2, ",\t/* ", $1, " */";}';
echo "};";
echo "";

echo "static const PredIdx bool_termcap_sort[] = {";
$AWK <$DATA '
BEGIN           {i = 0;}
/^#/            {next;}
$3 == "bool"    {printf("%s\t%d\n", $4, i++);}
' | sort | $AWK '{print "\t", $2, ",\t/* ", $1, " */";}';
echo "};";
echo "";

echo "static const PredIdx num_termcap_sort[] = {";
$AWK <$DATA '
BEGIN           {i = 0;}
/^#/            {next;}
$3 == "num"     {printf("%s\t%d\n", $4, i++);}
' | sort | $AWK '{print "\t", $2, ",\t/* ", $1, " */";}';
echo "};";
echo "";

echo "static const PredIdx str_termcap_sort[] = {";
$AWK <$DATA '
BEGIN           {i = 0;}
/^#/            {next;}
$3 == "str"     {printf("%s\t%d\n", $4, i++);}
' | sort | $AWK '{print "\t", $2, ",\t/* ", $1, " */";}';
echo "};";
echo "";

echo "static const bool bool_from_termcap[] = {";
$AWK <$DATA '
$3 == "bool" && substr($7, 1, 1) == "-"       {print "\tFALSE,\t/* ", $2, " */";}
$3 == "bool" && substr($7, 1, 1) == "Y"       {print "\tTRUE,\t/* ", $2, " */";}
'
echo "};";
echo "";

echo "static const bool num_from_termcap[] = {";
$AWK <$DATA '
$3 == "num" && substr($7, 1, 1) == "-"        {print "\tFALSE,\t/* ", $2, " */";}
$3 == "num" && substr($7, 1, 1) == "Y"        {print "\tTRUE,\t/* ", $2, " */";}
'
echo "};";
echo "";

echo "static const bool str_from_termcap[] = {";
$AWK <$DATA '
$3 == "str" && substr($7, 1, 1) == "-"        {print "\tFALSE,\t/* ", $2, " */";}
$3 == "str" && substr($7, 1, 1) == "Y"        {print "\tTRUE,\t/* ", $2, " */";}
'
echo "};";
echo "";

rm -f $data
