#!/bin/sh
#
# MKlib_gen.sh -- generate sources from curses.h macro definitions
#
# ($Id: MKlib_gen.sh,v 1.50 2015/08/07 00:48:24 tom Exp $)
#
##############################################################################
# Copyright (c) 1998-2014,2015 Free Software Foundation, Inc.                #
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
# The XSI Curses standard requires all curses entry points to exist as
# functions, even though many definitions would normally be shadowed
# by macros.  Rather than hand-hack all that code, we actually
# generate functions from the macros.
#
# This script accepts a file of prototypes on standard input.  It discards
# any that don't have a `generated' comment attached. It then parses each
# prototype (relying on the fact that none of the macros take function
# pointer or array arguments) and generates C source from it.
#
# Here is what the pipeline stages are doing:
#
# 1. sed: extract prototypes of generated functions
# 2. sed: decorate prototypes with generated arguments a1. a2,...z
# 3. awk: generate the calls with args matching the formals
# 4. sed: prefix function names in prototypes so the preprocessor won't expand
#         them.
# 5. cpp: macro-expand the file so the macro calls turn into C calls
# 6. awk: strip the expansion junk off the front and add the new header
# 7. sed: squeeze spaces, strip off gen_ prefix.
#

# keep the editing independent of locale:
if test "${LANGUAGE+set}"    = set; then LANGUAGE=C;    export LANGUAGE;    fi
if test "${LANG+set}"        = set; then LANG=C;        export LANG;        fi
if test "${LC_ALL+set}"      = set; then LC_ALL=C;      export LC_ALL;      fi
if test "${LC_MESSAGES+set}" = set; then LC_MESSAGES=C; export LC_MESSAGES; fi
if test "${LC_CTYPE+set}"    = set; then LC_CTYPE=C;    export LC_CTYPE;    fi
if test "${LC_COLLATE+set}"  = set; then LC_COLLATE=C;  export LC_COLLATE;  fi

preprocessor="$1 -DNCURSES_INTERNALS -I../include"
AWK="$2"
USE="$3"

# A patch discussed here:
#	https://gcc.gnu.org/ml/gcc-patches/2014-06/msg02185.html
# introduces spurious #line markers into the preprocessor output.  The result
# appears in gcc 5.0 and (with modification) in 5.1, making it necessary to
# determine if we are using gcc, and if so, what version because the proposed
# solution uses a nonstandard option.
PRG=`echo "$1" | $AWK '{ sub(/^[[:space:]]*/,""); sub(/[[:space:]].*$/, ""); print; }' || exit 0`
FSF=`"$PRG" --version 2>/dev/null || exit 0 | fgrep "Free Software Foundation" | head -n 1`
ALL=`"$PRG" -dumpversion 2>/dev/null || exit 0`
ONE=`echo "$ALL" | sed -e 's/\..*$//'`
if test -n "$FSF" && test -n "$ALL" && test -n "$ONE" ; then
	if test $ONE -ge 5 ; then
		echo ".. adding -P option to work around $PRG $ALL" >&2
		preprocessor="$preprocessor -P"
	fi
fi

PID=$$
ED1=sed1_${PID}.sed
ED2=sed2_${PID}.sed
ED3=sed3_${PID}.sed
ED4=sed4_${PID}.sed
AW1=awk1_${PID}.awk
AW2=awk2_${PID}.awk
TMP=gen__${PID}.c
trap "rm -f $ED1 $ED2 $ED3 $ED4 $AW1 $AW2 $TMP" 0 1 2 5 15

ALL=$USE
if test "$USE" = implemented ; then
	CALL="call_"
	cat >$ED1 <<EOF1
/^extern.*implemented/{
	h
	s/NCURSES_SP_NAME(\([^)]*\))/NCURSES_SP_NAME___\1/
	h
	s/^.*implemented:\([^ 	*]*\).*/P_POUNDCif_USE_\1_SUPPORT/p
	g
	s/^extern \([^;]*\);.*/\1/p
	g
	s/^.*implemented:\([^ 	*]*\).*/P_POUNDCendif/p
}
/^extern.*generated/{
	h
	s/^.*generated:\([^ 	*]*\).*/P_POUNDCif_USE_\1_SUPPORT/p
	g
	s/^extern \([^;]*\);.*/\1/p
	g
	s/^.*generated:\([^ 	*]*\).*/P_POUNDCendif/p
}
EOF1
else
	CALL=""
	cat >$ED1 <<EOF1
/^extern.*${ALL}/{
	h
	s/^.*${ALL}:\([^ 	*]*\).*/P_POUNDCif_USE_\1_SUPPORT/p
	g
	s/^extern \([^;]*\);.*/\1/p
	g
	s/^.*${ALL}:\([^ 	*]*\).*/P_POUNDCendif/p
}
EOF1
fi

cat >$ED2 <<EOF2
/^P_/b nc
/(void)/b nc
	s/,/ a1% /
	s/,/ a2% /
	s/,/ a3% /
	s/,/ a4% /
	s/,/ a5% /
	s/,/ a6% /
	s/,/ a7% /
	s/,/ a8% /
	s/,/ a9% /
	s/,/ a10% /
	s/,/ a11% /
	s/,/ a12% /
	s/,/ a13% /
	s/,/ a14% /
	s/,/ a15% /
	s/*/ * /g
	s/%/ , /g
	s/)/ z)/
	s/\.\.\. z)/...)/
:nc
	s/(/ ( /
	s/)/ )/
EOF2

cat >$ED3 <<EOF3
/^P_/{
	s/^P_POUNDCif_/#if /
	s/^P_POUNDCendif/#endif/
	s/^P_//
	b done
}
	s/		*/ /g
	s/  */ /g
	s/ ,/,/g
	s/( /(/g
	s/ )/)/g
	s/ gen_/ /
	s/^[ 	]*@[ 	]*@[ 	]*/	/
:done
EOF3

if test "$USE" = generated ; then
cat >$ED4 <<EOF
	s/^\(.*\) \(.*\) (\(.*\))\$/NCURSES_EXPORT(\1) (\2) (\3)/
EOF
else
cat >$ED4 <<EOF
/^\(.*\) \(.*\) (\(.*\))\$/ {
	h
	s/^\(.*\) \(.*\) (\(.*\))\$/extern \1 call_\2 (\3);/
	p
	g
	s/^\(.*\) \(.*\) (\(.*\))\$/\1 call_\2 (\3)/
	}
s/\([^_]\)NCURSES_SP_NAME___\([a-zA-Z][a-zA-Z_]*\)/\1NCURSES_SP_NAME(\2)/g
EOF
fi

cat >$AW1 <<\EOF1
BEGIN	{
		skip=0;
	}
/^P_POUNDCif/ {
		print "\n"
		print $0
		skip=0;
}
/^P_POUNDCendif/ {
		print $0
		skip=1;
}
$0 !~ /^P_/ {
	if (skip)
		print "\n"
	skip=1;

	first=$1
	for (i = 1; i <= NF; i++) {
		if ( $i != "NCURSES_CONST" ) {
			first = i;
			break;
		}
	}
	second = first + 1;
	returnCast = "";
	if ( $first == "chtype" ) {
		returnType = "Chtype";
	} else if ( $first == "SCREEN" ) {
		returnType = "SP";
	} else if ( $first == "WINDOW" ) {
		returnType = "Win";
	} else if ( $first == "attr_t" || $second == "attrset" || $second == "standout" || $second == "standend" || $second == "wattrset" || $second == "wstandout" || $second == "wstandend" ) {
		returnType = "IntAttr";
		returnCast = "(attr_t)";
	} else if ( $first == "bool" || $first == "NCURSES_BOOL" ) {
		returnType = "Bool";
	} else if ( $second == "*" ) {
		returnType = "Ptr";
	} else {
		returnType = "Code";
	}
	myfunc = second;
	for (i = second; i <= NF; i++) {
		if ($i != "*") {
			myfunc = i;
			break;
		}
	}
	print $0;
	print "{";
	argcount = 1;
	check = NF - 1;
	if ($check == "void")
		argcount = 0;
	if (argcount != 0) {
		for (i = 1; i <= NF; i++)
			if ($i == ",")
				argcount++;
	}

	# suppress trace-code for functions that we cannot do properly here,
	# since they return data.
	dotrace = 1;
	if ($myfunc ~ /innstr/)
		dotrace = 0;
	if ($myfunc ~ /innwstr/)
		dotrace = 0;

	# workaround functions that we do not parse properly
	if ($myfunc ~ /ripoffline/) {
		dotrace = 0;
		argcount = 2;
		if ($myfunc ~ /NCURSES_SP_NAME/) {
			argcount = 3;
		}
	}
	if ($myfunc ~ /wunctrl/) {
		dotrace = 0;
	}

	call = "@@T((T_CALLED(\""
	args = ""
	comma = ""
	num = 0;
	pointer = 0;
	va_list = 0;
	varargs = 0;
	argtype = ""
	for (i = myfunc; i <= NF; i++) {
		ch = $i;
		if ( ch == "*" ) {
			pointer = 1;
		} else if ( ch == "va_list" ) {
			va_list = 1;
		} else if ( ch == "..." ) {
			varargs = 1;
		} else if ( ch == "char" ) {
			argtype = "char";
		} else if ( ch == "int" ) {
			argtype = "int";
		} else if ( ch == "short" ) {
			argtype = "short";
		} else if ( ch == "chtype" ) {
			argtype = "chtype";
		} else if ( ch == "attr_t" || ch == "NCURSES_ATTR_T" ) {
			argtype = "attr";
		}

		if ( ch == "," || ch == ")" ) {
			argcast = "";
			if (va_list) {
				call = call "%s"
			} else if (varargs) {
				call = call "%s"
			} else if (pointer) {
				if ( argtype == "char" ) {
					call = call "%s"
					comma = comma "_nc_visbuf2(" num ","
					pointer = 0;
				} else {
					call = call "%p"
					comma = comma "(const void *)"
				}
			} else if (argcount != 0) {
				if ( argtype == "int" || argtype == "short" ) {
					call = call "%d"
					argtype = ""
				} else if ( argtype != "" ) {
					call = call "%s"
					comma = comma "_trace" argtype "2(" num ","
					if (argtype == "attr") {
						argcast = "(chtype)";
					}
				} else {
					call = call "%#lx"
					comma = comma "(long)"
				}
			}
			if (ch == ",") {
				args = args comma "a" ++num;
			} else if ( argcount != 0 ) {
				if ( va_list ) {
					args = args comma "\"va_list\""
				} else if ( varargs ) {
					args = args comma "\"...\""
				} else {
					args = args comma argcast "z"
				}
			}
			call = call ch
			if (pointer == 0 && argcount != 0 && argtype != "" )
				args = args ")"
			if (args != "")
				comma = ", "
			pointer = 0;
			argtype = ""
		}
		if ( i == myfunc || ch == "(" )
			call = call ch
	}
	call = call "\")"
	if (args != "")
		call = call ", " args
	call = call ")); "

	if (dotrace)
		printf "%s", call

	if (match($0, "^void")) {
		call = ""
	} else if (dotrace) {
		call = sprintf("return%s( ", returnType);
		if (returnCast != "") {
			call = call returnCast;
		}
	} else {
		call = "@@return ";
	}

	call = call $myfunc "(";
	for (i = 1; i < argcount; i++) {
		if (i != 1)
			call = call ", ";
		call = call "a" i;
	}
	if ( argcount != 0 && $check != "..." ) {
		if (argcount != 1)
			call = call ", ";
		call = call "z";
	}
	if (!match($0, "^void"))
		call = call ") ";
	if (dotrace) {
		call = call ")";
	}
	print call ";"

	if (match($0, "^void"))
		print "@@returnVoid;"
	print "}";
}
EOF1

cat >$AW2 <<EOF1
BEGIN		{
		print "/*"
		print " * DO NOT EDIT THIS FILE BY HAND!"
		printf " * It is generated by $0 %s.\n", "$USE"
		if ( "$USE" == "generated" ) {
			print " *"
			print " * This is a file of trivial functions generated from macro"
			print " * definitions in curses.h to satisfy the XSI Curses requirement"
			print " * that every macro also exist as a callable function."
			print " *"
			print " * It will never be linked unless you call one of the entry"
			print " * points with its normal macro definition disabled.  In that"
			print " * case, if you have no shared libraries, it will indirectly"
			print " * pull most of the rest of the library into your link image."
		}
		print " */"
		print "#define NCURSES_ATTR_T int"
		print "#include <ncurses_cfg.h>"
		print ""
		print "#undef NCURSES_NOMACROS	/* _this_ file uses macros */"
		print ""
		print "#include <curses.priv.h>"
		print ""
		print "#undef vw_scanw"
		print "#undef vwscanw"
		print ""
		print "#undef vw_printw"
		print "#undef vwprintw"
		}
/^DECLARATIONS/	{start = 1; next;}
		{
		if (start) {
			if ( "$USE" == "generated" ) {
				print \$0;
			} else if ( \$0 ~ /^[{}]?\$/ ) {
				print \$0;
			} else if ( \$0 ~ /;/ ) {
				print \$0;
			} else {
				calls[start] = \$0;
				print \$0;
				start++;
			}
		}
		}
END		{
		if ( "$USE" != "generated" ) {
			print "int main(void)"
			print "{"
			for (n = 1; n < start; ++n) {
				value = calls[n];
				if ( value !~ /P_POUNDC/ ) {
					gsub(/[[:blank:]]+/," ",value);
					sub(/^[[:alnum:]_]+ /,"",value);
					sub(/^\* /,"",value);
					gsub(/[[:alnum:]_]+ \* /,"",value);
					gsub(/ (const) /," ",value);
					gsub(/ (int|short|attr_t|chtype|wchar_t|NCURSES_BOOL|NCURSES_OUTC|NCURSES_OUTC_sp|va_list) /," ",value);
					gsub(/ void /,"",value);
					sub(/^/,"call_",value);
					gsub(/ (a[[:digit:]]|z) /, " 0 ", value);
					gsub(/ int[[:blank:]]*[(][^)]+[)][(][^)]+[)]/, "0", value);
					printf "\t%s;\n", value;
				} else {
					print value;
				}
			}
			print "	return 0;"
			print "}"
		}
		}
EOF1

cat >$TMP <<EOF
#include <ncurses_cfg.h>
#undef NCURSES_NOMACROS
#include <curses.h>
#include <term.h>
#include <unctrl.h>

DECLARATIONS

EOF

sed -n -f $ED1 \
| sed -e 's/NCURSES_EXPORT(\(.*\)) \(.*\) (\(.*\))/\1 \2(\3)/' \
| sed -f $ED2 \
| $AWK -f $AW1 using=$USE \
| sed \
	-e 's/ [ ]*$//g' \
	-e 's/^\([a-zA-Z_][a-zA-Z_]*[ *]*\)/\1 gen_/' \
	-e 's/gen_$//' \
	-e 's/  / /g' >>$TMP

$preprocessor $TMP 2>/dev/null \
| sed \
	-e 's/  / /g' \
	-e 's/^ //' \
	-e 's/_Bool/NCURSES_BOOL/g' \
| $AWK -f $AW2 \
| sed -f $ED3 \
| sed \
	-e 's/^.*T_CALLED.*returnCode( \([a-z].*) \));/	return \1;/' \
	-e 's/^.*T_CALLED.*returnCode( \((wmove.*) \));/	return \1;/' \
	-e 's/gen_//' \
	-e 's/^[ 	]*#/#/' \
	-e '/#ident/d' \
	-e '/#line/d' \
| sed -f $ED4
