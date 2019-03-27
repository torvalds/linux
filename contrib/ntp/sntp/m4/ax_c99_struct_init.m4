# ===========================================================================
#       http://www.gnu.org/software/autoconf-archive/ax_c99_struct_init.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_C99_STRUCT_INIT
#
# DESCRIPTION
#
#   This macro defines MISSING_C99_STRUCT_INIT if the C compiler does not
#   supports the C99 tagged structure initialization.
#
#   Given: struct foo_s {int i1; int i2; int i3;};
#   one can write:
#	#if !define(MISSING_C99_STRUCT_INIT)
#	# define FOO_INIT(a, b, c) { .i1 = a, .i2 = b, .i3 = c }
#	#else
#	# define FOO_INIT(a, b, c) { a, b, c }
#
#	static struct foo_s foo[] = {
#		FOO_INIT(1, 1, 1),
#		FOO_INIT(2, 2, 2),
#		FOO_INIT(0, 0, 0)
#	};
#
# LICENSE
#
#   Copyright (c) 2015 Network Time Foundation
#
#   Author: Harlan Stenn <stenn@nwtime.org>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 1

AC_DEFUN([AX_C99_STRUCT_INIT], [
	AC_MSG_CHECKING([whether the compiler supports C99 structure initialization])
	AC_REQUIRE([AC_PROG_CC_C99])

	AC_LANG_PUSH([C])

	dnl AC_LINK_IFELSE?
	AC_COMPILE_IFELSE(
		[AC_LANG_SOURCE([[
			struct foo_s {int i1; int i2;};
			int main() { struct foo_s foo[] = { { .i1 = 1, .i2 = 1 }, { .i1 = 2, .i2 = 2 }, { .i1 = 0, .i2 = 0 } }; }
			]])],
		AC_MSG_RESULT([yes]),
		AC_MSG_RESULT([no])
		AC_DEFINE([MISSING_C99_STRUCT_INIT], [1],
			[Define to 1 if the compiler does not support C99's structure initialization.]),
		)

	AC_LANG_POP([C])
	]);
