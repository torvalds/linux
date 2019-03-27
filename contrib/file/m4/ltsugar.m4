# ltsugar.m4 -- libtool m4 base layer.                         -*-Autoconf-*-
#
# Copyright (C) 2004, 2005, 2007, 2008 Free Software Foundation, Inc.
# Written by Gary V. Vaughan, 2004
#
# This file is free software; the Free Software Foundation gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.

# serial 6 ltsugar.m4

# This is to help aclocal find these macros, as it can't see m4_define.
AC_DEFUN([LTSUGAR_VERSION], [m4_if([0.1])])


# lt_join(SEP, ARG1, [ARG2...])
# -----------------------------
# Produce ARG1SEPARG2...SEPARGn, omitting [] arguments and their
# associated separator.
# Needed until we can rely on m4_join from Autoconf 2.62, since all earlier
# versions in m4sugar had bugs.
m4_define([lt_join],
[m4_if([$#], [1], [],
       [$#], [2], [[$2]],
       [m4_if([$2], [], [], [[$2]_])$0([$1], m4_shift(m4_shift($@)))])])
m4_define([_lt_join],
[m4_if([$#$2], [2], [],
       [m4_if([$2], [], [], [[$1$2]])$0([$1], m4_shift(m4_shift($@)))])])


# lt_car(LIST)
# lt_cdr(LIST)
# ------------
# Manipulate m4 lists.
# These macros are necessary as long as will still need to support
# Autoconf-2.59 which quotes differently.
m4_define([lt_car], [[$1]])
m4_define([lt_cdr],
[m4_if([$#], 0, [m4_fatal([$0: cannot be called without arguments])],
       [$#], 1, [],
       [m4_dquote(m4_shift($@))])])
m4_define([lt_unquote], $1)


# lt_append(MACRO-NAME, STRING, [SEPARATOR])
# ------------------------------------------
# Redefine MACRO-NAME to hold its former content plus `SEPARATOR'`STRING'.
# Note that neither SEPARATOR nor STRING are expanded; they are appended
# to MACRO-NAME as is (leaving the expansion for when MACRO-NAME is invoked).
# No SEPARATOR is output if MACRO-NAME was previously undefined (different
# than defined and empty).
#
# This macro is needed until we can rely on Autoconf 2.62, since earlier
# versions of m4sugar mistakenly expanded SEPARATOR but not STRING.
m4_define([lt_append],
[m4_define([$1],
	   m4_ifdef([$1], [m4_defn([$1])[$3]])[$2])])



# lt_combine(SEP, PREFIX-LIST, INFIX, SUFFIX1, [SUFFIX2...])
# ----------------------------------------------------------
# Produce a SEP delimited list of all paired combinations of elements of
# PREFIX-LIST with SUFFIX1 through SUFFIXn.  Each element of the list
# has the form PREFIXmINFIXSUFFIXn.
# Needed until we can rely on m4_combine added in Autoconf 2.62.
m4_define([lt_combine],
[m4_if(m4_eval([$# > 3]), [1],
       [m4_pushdef([_Lt_sep], [m4_define([_Lt_sep], m4_defn([lt_car]))])]]dnl
[[m4_foreach([_Lt_prefix], [$2],
	     [m4_foreach([_Lt_suffix],
		]m4_dquote(m4_dquote(m4_shift(m4_shift(m4_shift($@)))))[,
	[_Lt_sep([$1])[]m4_defn([_Lt_prefix])[$3]m4_defn([_Lt_suffix])])])])])


# lt_if_append_uniq(MACRO-NAME, VARNAME, [SEPARATOR], [UNIQ], [NOT-UNIQ])
# -----------------------------------------------------------------------
# Iff MACRO-NAME does not yet contain VARNAME, then append it (delimited
# by SEPARATOR if supplied) and expand UNIQ, else NOT-UNIQ.
m4_define([lt_if_append_uniq],
[m4_ifdef([$1],
	  [m4_if(m4_index([$3]m4_defn([$1])[$3], [$3$2$3]), [-1],
		 [lt_append([$1], [$2], [$3])$4],
		 [$5])],
	  [lt_append([$1], [$2], [$3])$4])])


# lt_dict_add(DICT, KEY, VALUE)
# -----------------------------
m4_define([lt_dict_add],
[m4_define([$1($2)], [$3])])


# lt_dict_add_subkey(DICT, KEY, SUBKEY, VALUE)
# --------------------------------------------
m4_define([lt_dict_add_subkey],
[m4_define([$1($2:$3)], [$4])])


# lt_dict_fetch(DICT, KEY, [SUBKEY])
# ----------------------------------
m4_define([lt_dict_fetch],
[m4_ifval([$3],
	m4_ifdef([$1($2:$3)], [m4_defn([$1($2:$3)])]),
    m4_ifdef([$1($2)], [m4_defn([$1($2)])]))])


# lt_if_dict_fetch(DICT, KEY, [SUBKEY], VALUE, IF-TRUE, [IF-FALSE])
# -----------------------------------------------------------------
m4_define([lt_if_dict_fetch],
[m4_if(lt_dict_fetch([$1], [$2], [$3]), [$4],
	[$5],
    [$6])])


# lt_dict_filter(DICT, [SUBKEY], VALUE, [SEPARATOR], KEY, [...])
# --------------------------------------------------------------
m4_define([lt_dict_filter],
[m4_if([$5], [], [],
  [lt_join(m4_quote(m4_default([$4], [[, ]])),
           lt_unquote(m4_split(m4_normalize(m4_foreach(_Lt_key, lt_car([m4_shiftn(4, $@)]),
		      [lt_if_dict_fetch([$1], _Lt_key, [$2], [$3], [_Lt_key ])])))))])[]dnl
])
