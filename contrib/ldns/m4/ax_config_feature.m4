# ===========================================================================
#     http://www.gnu.org/software/autoconf-archive/ax_config_feature.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_CONFIG_FEATURE(FEATURE-NAME, FEATURE-DESCRIPTION, DEFINE, DEFINE-DESCRIPTION, [ACTION-IF-ENABLED [, ACTION-IF-NOT-ENABLED]])
#
# DESCRIPTION
#
#   AX_CONFIG_FEATURE is a simple wrapper for AC_ARG_ENABLE, it enables the
#   feature FEATURE-NAME and AC_DEFINEs the passed DEFINE, depending on the
#   user choice. DESCRIPTION will be used for AC_DEFINEs. ACTION-IF-ENABLED
#   and ACTION-IF-NOT-ENABLED are the actions that will be run. A feature is
#   enabled by default, in order to change this behaviour use the
#   AX_CONFIG_FEATURE_DEFAULT_ENABLED and AX_CONFIG_FEATURE_DEFAULT_DISABLED
#   macros.
#
#   A simple example:
#
#     AX_CONFIG_FEATURE_DEFAULT_ENABLED
#     AX_CONFIG_FEATURE(feature_xxxxx, [turns on/off XXXXX support],
#                       HAVE_XXXXX, [Define if you want XXXXX support])
#
#     ...
#
#     AX_CONFIG_FEATURE_DEFAULT_DISABLED
#     AX_CONFIG_FEATURE(feature_yyyyy, [turns on/off YYYYY support],
#                       HAVE_YYYYY, [Define if you want YYYYY support],
#                       [enable_yyyyy="yes"], [enable_yyyyy="no"])
#     AM_CONDITIONAL(YYYYY, [test "$enable_yyyyy" = "yes"])
#
#     AX_CONFIG_FEATURE_DEFAULT_ENABLED
#     AX_CONFIG_FEATURE(...)
#
#     ...
#
#   If you have lot of features and you want a verbose dumping of each user
#   selection use AX_CONFIG_FEATURE_VERBOSE. Use AX_CONFIG_FEATURE_SILENT in
#   order to remove a previously AX_CONFIG_FEATURE_VERBOSE. By default
#   features are silent.
#
#   Use AX_CONFIG_FEATURE_ENABLE or AX_CONFIG_FEATURE_DISABLE in order to
#   enable or disable a specific feature.
#
#   Another simple example:
#
#     AS_IF([some_test_here],[AX_CONFIG_FEATURE_ENABLE(feature_xxxxx)],[])
#
#     AX_CONFIG_FEATURE(feature_xxxxx, [turns on/off XXXXX support],
#                       HAVE_XXXXX, [Define if you want XXXXX support])
#     AX_CONFIG_FEATURE(feature_yyyyy, [turns on/off YYYYY support],
#                       HAVE_YYYYY, [Define if you want YYYYY support],
#                       [enable_yyyyy="yes"], [enable_yyyyy="no"])
#
#     ...
#
#   NOTE: AX_CONFIG_FEATURE_ENABLE() must be placed first of the relative
#   AX_CONFIG_FEATURE() macro ...
#
# LICENSE
#
#   Copyright (c) 2008 Francesco Salvestrini <salvestrini@users.sourceforge.net>
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation; either version 2 of the License, or (at your
#   option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
#   Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program. If not, see <http://www.gnu.org/licenses/>.
#
#   As a special exception, the respective Autoconf Macro's copyright owner
#   gives unlimited permission to copy, distribute and modify the configure
#   scripts that are the output of Autoconf when processing the Macro. You
#   need not follow the terms of the GNU General Public License when using
#   or distributing such scripts, even though portions of the text of the
#   Macro appear in them. The GNU General Public License (GPL) does govern
#   all other use of the material that constitutes the Autoconf Macro.
#
#   This special exception to the GPL applies to versions of the Autoconf
#   Macro released by the Autoconf Archive. When you make and distribute a
#   modified version of the Autoconf Macro, you may extend this special
#   exception to the GPL to apply to your modified version as well.

#serial 10

AC_DEFUN([AX_CONFIG_FEATURE],[ dnl
m4_pushdef([FEATURE], patsubst([$1], -, _))dnl

AC_ARG_ENABLE([$1],AS_HELP_STRING([--enable-$1],[$2]),[
case "${enableval}" in
   yes)
     ax_config_feature_[]FEATURE[]="yes"
     ;;
   no)
     ax_config_feature_[]FEATURE[]="no"
     ;;
   *)
     AC_MSG_ERROR([bad value ${enableval} for feature --$1])
     ;;
esac
])

AS_IF([test "$ax_config_feature_[]FEATURE[]" = yes],[ dnl
  AC_DEFINE([$3])
  $5
  AS_IF([test "$ax_config_feature_verbose" = yes],[ dnl
    AC_MSG_NOTICE([Feature $1 is enabled])
  ])
],[ dnl
  $6
  AS_IF([test "$ax_config_feature_verbose" = yes],[ dnl
    AC_MSG_NOTICE([Feature $1 is disabled])
  ])
])

AH_TEMPLATE([$3],[$4])

m4_popdef([FEATURE])dnl
])

dnl Feature global
AC_DEFUN([AX_CONFIG_FEATURE_VERBOSE],[ dnl
  ax_config_feature_verbose=yes
])

dnl Feature global
AC_DEFUN([AX_CONFIG_FEATURE_SILENT],[ dnl
  ax_config_feature_verbose=no
])

dnl Feature specific
AC_DEFUN([AX_CONFIG_FEATURE_DEFAULT_ENABLED], [
  ax_config_feature_[]FEATURE[]_default=yes
])

dnl Feature specific
AC_DEFUN([AX_CONFIG_FEATURE_DEFAULT_DISABLED], [
  ax_config_feature_[]FEATURE[]_default=no
])

dnl Feature specific
AC_DEFUN([AX_CONFIG_FEATURE_ENABLE],[ dnl
  ax_config_feature_[]patsubst([$1], -, _)[]=yes
])

dnl Feature specific
AC_DEFUN([AX_CONFIG_FEATURE_DISABLE],[ dnl
  ax_config_feature_[]patsubst([$1], -, _)[]=no
])
