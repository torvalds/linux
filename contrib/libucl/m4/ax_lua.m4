# ===========================================================================
#          http://www.gnu.org/software/autoconf-archive/ax_lua.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_PROG_LUA[([MINIMUM-VERSION], [TOO-BIG-VERSION], [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])]
#   AX_LUA_HEADERS[([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])]
#   AX_LUA_LIBS[([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])]
#   AX_LUA_READLINE[([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])]
#
# DESCRIPTION
#
#   Detect a Lua interpreter, optionally specifying a minimum and maximum
#   version number. Set up important Lua paths, such as the directories in
#   which to install scripts and modules (shared libraries).
#
#   Also detect Lua headers and libraries. The Lua version contained in the
#   header is checked to match the Lua interpreter version exactly. When
#   searching for Lua libraries, the version number is used as a suffix.
#   This is done with the goal of supporting multiple Lua installs (5.1,
#   5.2, and 5.3 side-by-side).
#
#   A note on compatibility with previous versions: This file has been
#   mostly rewritten for serial 18. Most developers should be able to use
#   these macros without needing to modify configure.ac. Care has been taken
#   to preserve each macro's behavior, but there are some differences:
#
#   1) AX_WITH_LUA is deprecated; it now expands to the exact same thing as
#   AX_PROG_LUA with no arguments.
#
#   2) AX_LUA_HEADERS now checks that the version number defined in lua.h
#   matches the interpreter version. AX_LUA_HEADERS_VERSION is therefore
#   unnecessary, so it is deprecated and does not expand to anything.
#
#   3) The configure flag --with-lua-suffix no longer exists; the user
#   should instead specify the LUA precious variable on the command line.
#   See the AX_PROG_LUA description for details.
#
#   Please read the macro descriptions below for more information.
#
#   This file was inspired by Andrew Dalke's and James Henstridge's
#   python.m4 and Tom Payne's, Matthieu Moy's, and Reuben Thomas's ax_lua.m4
#   (serial 17). Basically, this file is a mash-up of those two files. I
#   like to think it combines the best of the two!
#
#   AX_PROG_LUA: Search for the Lua interpreter, and set up important Lua
#   paths. Adds precious variable LUA, which may contain the path of the Lua
#   interpreter. If LUA is blank, the user's path is searched for an
#   suitable interpreter.
#
#   If MINIMUM-VERSION is supplied, then only Lua interpreters with a
#   version number greater or equal to MINIMUM-VERSION will be accepted. If
#   TOO-BIG-VERSION is also supplied, then only Lua interpreters with a
#   version number greater or equal to MINIMUM-VERSION and less than
#   TOO-BIG-VERSION will be accepted.
#
#   The Lua version number, LUA_VERSION, is found from the interpreter, and
#   substituted. LUA_PLATFORM is also found, but not currently supported (no
#   standard representation).
#
#   Finally, the macro finds four paths:
#
#     luadir             Directory to install Lua scripts.
#     pkgluadir          $luadir/$PACKAGE
#     luaexecdir         Directory to install Lua modules.
#     pkgluaexecdir      $luaexecdir/$PACKAGE
#
#   These paths are found based on $prefix, $exec_prefix, Lua's
#   package.path, and package.cpath. The first path of package.path
#   beginning with $prefix is selected as luadir. The first path of
#   package.cpath beginning with $exec_prefix is used as luaexecdir. This
#   should work on all reasonable Lua installations. If a path cannot be
#   determined, a default path is used. Of course, the user can override
#   these later when invoking make.
#
#     luadir             Default: $prefix/share/lua/$LUA_VERSION
#     luaexecdir         Default: $exec_prefix/lib/lua/$LUA_VERSION
#
#   These directories can be used by Automake as install destinations. The
#   variable name minus 'dir' needs to be used as a prefix to the
#   appropriate Automake primary, e.g. lua_SCRIPS or luaexec_LIBRARIES.
#
#   If an acceptable Lua interpreter is found, then ACTION-IF-FOUND is
#   performed, otherwise ACTION-IF-NOT-FOUND is preformed. If ACTION-IF-NOT-
#   FOUND is blank, then it will default to printing an error. To prevent
#   the default behavior, give ':' as an action.
#
#   AX_LUA_HEADERS: Search for Lua headers. Requires that AX_PROG_LUA be
#   expanded before this macro. Adds precious variable LUA_INCLUDE, which
#   may contain Lua specific include flags, e.g. -I/usr/include/lua5.1. If
#   LUA_INCLUDE is blank, then this macro will attempt to find suitable
#   flags.
#
#   LUA_INCLUDE can be used by Automake to compile Lua modules or
#   executables with embedded interpreters. The *_CPPFLAGS variables should
#   be used for this purpose, e.g. myprog_CPPFLAGS = $(LUA_INCLUDE).
#
#   This macro searches for the header lua.h (and others). The search is
#   performed with a combination of CPPFLAGS, CPATH, etc, and LUA_INCLUDE.
#   If the search is unsuccessful, then some common directories are tried.
#   If the headers are then found, then LUA_INCLUDE is set accordingly.
#
#   The paths automatically searched are:
#
#     * /usr/include/luaX.Y
#     * /usr/include/lua/X.Y
#     * /usr/include/luaXY
#     * /usr/local/include/luaX.Y
#     * /usr/local/include/lua-X.Y
#     * /usr/local/include/lua/X.Y
#     * /usr/local/include/luaXY
#
#   (Where X.Y is the Lua version number, e.g. 5.1.)
#
#   The Lua version number found in the headers is always checked to match
#   the Lua interpreter's version number. Lua headers with mismatched
#   version numbers are not accepted.
#
#   If headers are found, then ACTION-IF-FOUND is performed, otherwise
#   ACTION-IF-NOT-FOUND is performed. If ACTION-IF-NOT-FOUND is blank, then
#   it will default to printing an error. To prevent the default behavior,
#   set the action to ':'.
#
#   AX_LUA_LIBS: Search for Lua libraries. Requires that AX_PROG_LUA be
#   expanded before this macro. Adds precious variable LUA_LIB, which may
#   contain Lua specific linker flags, e.g. -llua5.1. If LUA_LIB is blank,
#   then this macro will attempt to find suitable flags.
#
#   LUA_LIB can be used by Automake to link Lua modules or executables with
#   embedded interpreters. The *_LIBADD and *_LDADD variables should be used
#   for this purpose, e.g. mymod_LIBADD = $(LUA_LIB).
#
#   This macro searches for the Lua library. More technically, it searches
#   for a library containing the function lua_load. The search is performed
#   with a combination of LIBS, LIBRARY_PATH, and LUA_LIB.
#
#   If the search determines that some linker flags are missing, then those
#   flags will be added to LUA_LIB.
#
#   If libraries are found, then ACTION-IF-FOUND is performed, otherwise
#   ACTION-IF-NOT-FOUND is performed. If ACTION-IF-NOT-FOUND is blank, then
#   it will default to printing an error. To prevent the default behavior,
#   set the action to ':'.
#
#   AX_LUA_READLINE: Search for readline headers and libraries. Requires the
#   AX_LIB_READLINE macro, which is provided by ax_lib_readline.m4 from the
#   Autoconf Archive.
#
#   If a readline compatible library is found, then ACTION-IF-FOUND is
#   performed, otherwise ACTION-IF-NOT-FOUND is performed.
#
# LICENSE
#
#   Copyright (c) 2015 Reuben Thomas <rrt@sc3d.org>
#   Copyright (c) 2014 Tim Perkins <tprk77@gmail.com>
#
#   This program is free software: you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation, either version 3 of the License, or (at your
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

#serial 39

dnl =========================================================================
dnl AX_PROG_LUA([MINIMUM-VERSION], [TOO-BIG-VERSION],
dnl             [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
dnl =========================================================================
AC_DEFUN([AX_PROG_LUA],
[
  dnl Check for required tools.
  AC_REQUIRE([AC_PROG_GREP])
  AC_REQUIRE([AC_PROG_SED])

  dnl Make LUA a precious variable.
  AC_ARG_VAR([LUA], [The Lua interpreter, e.g. /usr/bin/lua5.1])

  dnl Find a Lua interpreter.
  m4_define_default([_AX_LUA_INTERPRETER_LIST],
    [lua lua5.3 lua53 lua5.2 lua52 lua5.1 lua51 lua50])

  m4_if([$1], [],
  [ dnl No version check is needed. Find any Lua interpreter.
    AS_IF([test "x$LUA" = 'x'],
      [AC_PATH_PROGS([LUA], [_AX_LUA_INTERPRETER_LIST], [:])])
    ax_display_LUA='lua'

    AS_IF([test "x$LUA" != 'x:'],
      [ dnl At least check if this is a Lua interpreter.
        AC_MSG_CHECKING([if $LUA is a Lua interpreter])
        _AX_LUA_CHK_IS_INTRP([$LUA],
          [AC_MSG_RESULT([yes])],
          [ AC_MSG_RESULT([no])
            AC_MSG_ERROR([not a Lua interpreter])
          ])
      ])
  ],
  [ dnl A version check is needed.
    AS_IF([test "x$LUA" != 'x'],
    [ dnl Check if this is a Lua interpreter.
      AC_MSG_CHECKING([if $LUA is a Lua interpreter])
      _AX_LUA_CHK_IS_INTRP([$LUA],
        [AC_MSG_RESULT([yes])],
        [ AC_MSG_RESULT([no])
          AC_MSG_ERROR([not a Lua interpreter])
        ])
      dnl Check the version.
      m4_if([$2], [],
        [_ax_check_text="whether $LUA version >= $1"],
        [_ax_check_text="whether $LUA version >= $1, < $2"])
      AC_MSG_CHECKING([$_ax_check_text])
      _AX_LUA_CHK_VER([$LUA], [$1], [$2],
        [AC_MSG_RESULT([yes])],
        [ AC_MSG_RESULT([no])
          AC_MSG_ERROR([version is out of range for specified LUA])])
      ax_display_LUA=$LUA
    ],
    [ dnl Try each interpreter until we find one that satisfies VERSION.
      m4_if([$2], [],
        [_ax_check_text="for a Lua interpreter with version >= $1"],
        [_ax_check_text="for a Lua interpreter with version >= $1, < $2"])
      AC_CACHE_CHECK([$_ax_check_text],
        [ax_cv_pathless_LUA],
        [ for ax_cv_pathless_LUA in _AX_LUA_INTERPRETER_LIST none; do
            test "x$ax_cv_pathless_LUA" = 'xnone' && break
            _AX_LUA_CHK_IS_INTRP([$ax_cv_pathless_LUA], [], [continue])
            _AX_LUA_CHK_VER([$ax_cv_pathless_LUA], [$1], [$2], [break])
          done
        ])
      dnl Set $LUA to the absolute path of $ax_cv_pathless_LUA.
      AS_IF([test "x$ax_cv_pathless_LUA" = 'xnone'],
        [LUA=':'],
        [AC_PATH_PROG([LUA], [$ax_cv_pathless_LUA])])
      ax_display_LUA=$ax_cv_pathless_LUA
    ])
  ])

  AS_IF([test "x$LUA" = 'x:'],
  [ dnl Run any user-specified action, or abort.
    m4_default([$4], [AC_MSG_ERROR([cannot find suitable Lua interpreter])])
  ],
  [ dnl Query Lua for its version number.
    AC_CACHE_CHECK([for $ax_display_LUA version],
      [ax_cv_lua_version],
      [ dnl Get the interpreter version in X.Y format. This should work for
        dnl interpreters version 5.0 and beyond.
        ax_cv_lua_version=[`$LUA -e '
          -- return a version number in X.Y format
          local _, _, ver = string.find(_VERSION, "^Lua (%d+%.%d+)")
          print(ver)'`]
      ])
    AS_IF([test "x$ax_cv_lua_version" = 'x'],
      [AC_MSG_ERROR([invalid Lua version number])])
    AC_SUBST([LUA_VERSION], [$ax_cv_lua_version])
    AC_SUBST([LUA_SHORT_VERSION], [`echo "$LUA_VERSION" | $SED 's|\.||'`])

    dnl The following check is not supported:
    dnl At times (like when building shared libraries) you may want to know
    dnl which OS platform Lua thinks this is.
    AC_CACHE_CHECK([for $ax_display_LUA platform],
      [ax_cv_lua_platform],
      [ax_cv_lua_platform=[`$LUA -e 'print("unknown")'`]])
    AC_SUBST([LUA_PLATFORM], [$ax_cv_lua_platform])

    dnl Use the values of $prefix and $exec_prefix for the corresponding
    dnl values of LUA_PREFIX and LUA_EXEC_PREFIX. These are made distinct
    dnl variables so they can be overridden if need be. However, the general
    dnl consensus is that you shouldn't need this ability.
    AC_SUBST([LUA_PREFIX], ['${prefix}'])
    AC_SUBST([LUA_EXEC_PREFIX], ['${exec_prefix}'])

    dnl Lua provides no way to query the script directory, and instead
    dnl provides LUA_PATH. However, we should be able to make a safe educated
    dnl guess. If the built-in search path contains a directory which is
    dnl prefixed by $prefix, then we can store scripts there. The first
    dnl matching path will be used.
    AC_CACHE_CHECK([for $ax_display_LUA script directory],
      [ax_cv_lua_luadir],
      [ AS_IF([test "x$prefix" = 'xNONE'],
          [ax_lua_prefix=$ac_default_prefix],
          [ax_lua_prefix=$prefix])

        dnl Initialize to the default path.
        ax_cv_lua_luadir="$LUA_PREFIX/share/lua/$LUA_VERSION"

        dnl Try to find a path with the prefix.
        _AX_LUA_FND_PRFX_PTH([$LUA], [$ax_lua_prefix], [script])
        AS_IF([test "x$ax_lua_prefixed_path" != 'x'],
        [ dnl Fix the prefix.
          _ax_strip_prefix=`echo "$ax_lua_prefix" | $SED 's|.|.|g'`
          ax_cv_lua_luadir=`echo "$ax_lua_prefixed_path" | \
            $SED "s|^$_ax_strip_prefix|$LUA_PREFIX|"`
        ])
      ])
    AC_SUBST([luadir], [$ax_cv_lua_luadir])
    AC_SUBST([pkgluadir], [\${luadir}/$PACKAGE])

    dnl Lua provides no way to query the module directory, and instead
    dnl provides LUA_PATH. However, we should be able to make a safe educated
    dnl guess. If the built-in search path contains a directory which is
    dnl prefixed by $exec_prefix, then we can store modules there. The first
    dnl matching path will be used.
    AC_CACHE_CHECK([for $ax_display_LUA module directory],
      [ax_cv_lua_luaexecdir],
      [ AS_IF([test "x$exec_prefix" = 'xNONE'],
          [ax_lua_exec_prefix=$ax_lua_prefix],
          [ax_lua_exec_prefix=$exec_prefix])

        dnl Initialize to the default path.
        ax_cv_lua_luaexecdir="$LUA_EXEC_PREFIX/lib/lua/$LUA_VERSION"

        dnl Try to find a path with the prefix.
        _AX_LUA_FND_PRFX_PTH([$LUA],
          [$ax_lua_exec_prefix], [module])
        AS_IF([test "x$ax_lua_prefixed_path" != 'x'],
        [ dnl Fix the prefix.
          _ax_strip_prefix=`echo "$ax_lua_exec_prefix" | $SED 's|.|.|g'`
          ax_cv_lua_luaexecdir=`echo "$ax_lua_prefixed_path" | \
            $SED "s|^$_ax_strip_prefix|$LUA_EXEC_PREFIX|"`
        ])
      ])
    AC_SUBST([luaexecdir], [$ax_cv_lua_luaexecdir])
    AC_SUBST([pkgluaexecdir], [\${luaexecdir}/$PACKAGE])

    dnl Run any user specified action.
    $3
  ])
])

dnl AX_WITH_LUA is now the same thing as AX_PROG_LUA.
AC_DEFUN([AX_WITH_LUA],
[
  AC_MSG_WARN([[$0 is deprecated, please use AX_PROG_LUA instead]])
  AX_PROG_LUA
])


dnl =========================================================================
dnl _AX_LUA_CHK_IS_INTRP(PROG, [ACTION-IF-TRUE], [ACTION-IF-FALSE])
dnl =========================================================================
AC_DEFUN([_AX_LUA_CHK_IS_INTRP],
[
  dnl A minimal Lua factorial to prove this is an interpreter. This should work
  dnl for Lua interpreters version 5.0 and beyond.
  _ax_lua_factorial=[`$1 2>/dev/null -e '
    -- a simple factorial
    function fact (n)
      if n == 0 then
        return 1
      else
        return n * fact(n-1)
      end
    end
    print("fact(5) is " .. fact(5))'`]
  AS_IF([test "$_ax_lua_factorial" = 'fact(5) is 120'],
    [$2], [$3])
])


dnl =========================================================================
dnl _AX_LUA_CHK_VER(PROG, MINIMUM-VERSION, [TOO-BIG-VERSION],
dnl                 [ACTION-IF-TRUE], [ACTION-IF-FALSE])
dnl =========================================================================
AC_DEFUN([_AX_LUA_CHK_VER],
[
  dnl Check that the Lua version is within the bounds. Only the major and minor
  dnl version numbers are considered. This should work for Lua interpreters
  dnl version 5.0 and beyond.
  _ax_lua_good_version=[`$1 -e '
    -- a script to compare versions
    function verstr2num(verstr)
      local _, _, majorver, minorver = string.find(verstr, "^(%d+)%.(%d+)")
      if majorver and minorver then
        return tonumber(majorver) * 100 + tonumber(minorver)
      end
    end
    local minver = verstr2num("$2")
    local _, _, trimver = string.find(_VERSION, "^Lua (.*)")
    local ver = verstr2num(trimver)
    local maxver = verstr2num("$3") or 1e9
    if minver <= ver and ver < maxver then
      print("yes")
    else
      print("no")
    end'`]
    AS_IF([test "x$_ax_lua_good_version" = "xyes"],
      [$4], [$5])
])


dnl =========================================================================
dnl _AX_LUA_FND_PRFX_PTH(PROG, PREFIX, SCRIPT-OR-MODULE-DIR)
dnl =========================================================================
AC_DEFUN([_AX_LUA_FND_PRFX_PTH],
[
  dnl Get the script or module directory by querying the Lua interpreter,
  dnl filtering on the given prefix, and selecting the shallowest path. If no
  dnl path is found matching the prefix, the result will be an empty string.
  dnl The third argument determines the type of search, it can be 'script' or
  dnl 'module'. Supplying 'script' will perform the search with package.path
  dnl and LUA_PATH, and supplying 'module' will search with package.cpath and
  dnl LUA_CPATH. This is done for compatibility with Lua 5.0.

  ax_lua_prefixed_path=[`$1 -e '
    -- get the path based on search type
    local searchtype = "$3"
    local paths = ""
    if searchtype == "script" then
      paths = (package and package.path) or LUA_PATH
    elseif searchtype == "module" then
      paths = (package and package.cpath) or LUA_CPATH
    end
    -- search for the prefix
    local prefix = "'$2'"
    local minpath = ""
    local mindepth = 1e9
    string.gsub(paths, "(@<:@^;@:>@+)",
      function (path)
        path = string.gsub(path, "%?.*$", "")
        path = string.gsub(path, "/@<:@^/@:>@*$", "")
        if string.find(path, prefix) then
          local depth = string.len(string.gsub(path, "@<:@^/@:>@", ""))
          if depth < mindepth then
            minpath = path
            mindepth = depth
          end
        end
      end)
    print(minpath)'`]
])


dnl =========================================================================
dnl AX_LUA_HEADERS([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
dnl =========================================================================
AC_DEFUN([AX_LUA_HEADERS],
[
  dnl Check for LUA_VERSION.
  AC_MSG_CHECKING([if LUA_VERSION is defined])
  AS_IF([test "x$LUA_VERSION" != 'x'],
    [AC_MSG_RESULT([yes])],
    [ AC_MSG_RESULT([no])
      AC_MSG_ERROR([cannot check Lua headers without knowing LUA_VERSION])
    ])

  dnl Make LUA_INCLUDE a precious variable.
  AC_ARG_VAR([LUA_INCLUDE], [The Lua includes, e.g. -I/usr/include/lua5.1])

  dnl Some default directories to search.
  LUA_SHORT_VERSION=`echo "$LUA_VERSION" | $SED 's|\.||'`
  m4_define_default([_AX_LUA_INCLUDE_LIST],
    [ /usr/include/lua$LUA_VERSION \
      /usr/include/lua-$LUA_VERSION \
      /usr/include/lua/$LUA_VERSION \
      /usr/include/lua$LUA_SHORT_VERSION \
      /usr/local/include/lua$LUA_VERSION \
      /usr/local/include/lua-$LUA_VERSION \
      /usr/local/include/lua/$LUA_VERSION \
      /usr/local/include/lua$LUA_SHORT_VERSION \
    ])

  dnl Try to find the headers.
  _ax_lua_saved_cppflags=$CPPFLAGS
  CPPFLAGS="$CPPFLAGS $LUA_INCLUDE"
  AC_CHECK_HEADERS([lua.h lualib.h lauxlib.h luaconf.h])
  CPPFLAGS=$_ax_lua_saved_cppflags

  dnl Try some other directories if LUA_INCLUDE was not set.
  AS_IF([test "x$LUA_INCLUDE" = 'x' &&
         test "x$ac_cv_header_lua_h" != 'xyes'],
    [ dnl Try some common include paths.
      for _ax_include_path in _AX_LUA_INCLUDE_LIST; do
        test ! -d "$_ax_include_path" && continue

        AC_MSG_CHECKING([for Lua headers in])
        AC_MSG_RESULT([$_ax_include_path])

        AS_UNSET([ac_cv_header_lua_h])
        AS_UNSET([ac_cv_header_lualib_h])
        AS_UNSET([ac_cv_header_lauxlib_h])
        AS_UNSET([ac_cv_header_luaconf_h])

        _ax_lua_saved_cppflags=$CPPFLAGS
        CPPFLAGS="$CPPFLAGS -I$_ax_include_path"
        AC_CHECK_HEADERS([lua.h lualib.h lauxlib.h luaconf.h])
        CPPFLAGS=$_ax_lua_saved_cppflags

        AS_IF([test "x$ac_cv_header_lua_h" = 'xyes'],
          [ LUA_INCLUDE="-I$_ax_include_path"
            break
          ])
      done
    ])

  AS_IF([test "x$ac_cv_header_lua_h" = 'xyes'],
    [ dnl Make a program to print LUA_VERSION defined in the header.
      dnl TODO It would be really nice if we could do this without compiling a
      dnl program, then it would work when cross compiling. But I'm not sure how
      dnl to do this reliably. For now, assume versions match when cross compiling.

      AS_IF([test "x$cross_compiling" != 'xyes'],
        [ AC_CACHE_CHECK([for Lua header version],
            [ax_cv_lua_header_version],
            [ _ax_lua_saved_cppflags=$CPPFLAGS
              CPPFLAGS="$CPPFLAGS $LUA_INCLUDE"
              AC_RUN_IFELSE(
                [ AC_LANG_SOURCE([[
#include <lua.h>
#include <stdlib.h>
#include <stdio.h>
int main(int argc, char ** argv)
{
  if(argc > 1) printf("%s", LUA_VERSION);
  exit(EXIT_SUCCESS);
}
]])
                ],
                [ ax_cv_lua_header_version=`./conftest$EXEEXT p | \
                    $SED -n "s|^Lua \(@<:@0-9@:>@\{1,\}\.@<:@0-9@:>@\{1,\}\).\{0,\}|\1|p"`
                ],
                [ax_cv_lua_header_version='unknown'])
              CPPFLAGS=$_ax_lua_saved_cppflags
            ])

          dnl Compare this to the previously found LUA_VERSION.
          AC_MSG_CHECKING([if Lua header version matches $LUA_VERSION])
          AS_IF([test "x$ax_cv_lua_header_version" = "x$LUA_VERSION"],
            [ AC_MSG_RESULT([yes])
              ax_header_version_match='yes'
            ],
            [ AC_MSG_RESULT([no])
              ax_header_version_match='no'
            ])
        ],
        [ AC_MSG_WARN([cross compiling so assuming header version number matches])
          ax_header_version_match='yes'
        ])
    ])

  dnl Was LUA_INCLUDE specified?
  AS_IF([test "x$ax_header_version_match" != 'xyes' &&
         test "x$LUA_INCLUDE" != 'x'],
    [AC_MSG_ERROR([cannot find headers for specified LUA_INCLUDE])])

  dnl Test the final result and run user code.
  AS_IF([test "x$ax_header_version_match" = 'xyes'], [$1],
    [m4_default([$2], [AC_MSG_ERROR([cannot find Lua includes])])])
])

dnl AX_LUA_HEADERS_VERSION no longer exists, use AX_LUA_HEADERS.
AC_DEFUN([AX_LUA_HEADERS_VERSION],
[
  AC_MSG_WARN([[$0 is deprecated, please use AX_LUA_HEADERS instead]])
])


dnl =========================================================================
dnl AX_LUA_LIBS([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
dnl =========================================================================
AC_DEFUN([AX_LUA_LIBS],
[
  dnl TODO Should this macro also check various -L flags?

  dnl Check for LUA_VERSION.
  AC_MSG_CHECKING([if LUA_VERSION is defined])
  AS_IF([test "x$LUA_VERSION" != 'x'],
    [AC_MSG_RESULT([yes])],
    [ AC_MSG_RESULT([no])
      AC_MSG_ERROR([cannot check Lua libs without knowing LUA_VERSION])
    ])

  dnl Make LUA_LIB a precious variable.
  AC_ARG_VAR([LUA_LIB], [The Lua library, e.g. -llua5.1])

  AS_IF([test "x$LUA_LIB" != 'x'],
  [ dnl Check that LUA_LIBS works.
    _ax_lua_saved_libs=$LIBS
    LIBS="$LUA_LIB $LIBS"
    AC_SEARCH_LIBS([lua_load], [],
      [_ax_found_lua_libs='yes'],
      [_ax_found_lua_libs='no'])
    LIBS=$_ax_lua_saved_libs

    dnl Check the result.
    AS_IF([test "x$_ax_found_lua_libs" != 'xyes'],
      [AC_MSG_ERROR([cannot find libs for specified LUA_LIB])])
  ],
  [ dnl First search for extra libs.
    _ax_lua_extra_libs=''

    _ax_lua_saved_libs=$LIBS
    LIBS="$LUA_LIB $LIBS"
    AC_SEARCH_LIBS([exp], [m])
    AC_SEARCH_LIBS([dlopen], [dl])
    LIBS=$_ax_lua_saved_libs

    AS_IF([test "x$ac_cv_search_exp" != 'xno' &&
           test "x$ac_cv_search_exp" != 'xnone required'],
      [_ax_lua_extra_libs="$_ax_lua_extra_libs $ac_cv_search_exp"])

    AS_IF([test "x$ac_cv_search_dlopen" != 'xno' &&
           test "x$ac_cv_search_dlopen" != 'xnone required'],
      [_ax_lua_extra_libs="$_ax_lua_extra_libs $ac_cv_search_dlopen"])

    dnl Try to find the Lua libs.
    _ax_lua_saved_libs=$LIBS
    LIBS="$LUA_LIB $LIBS"
    AC_SEARCH_LIBS([lua_load],
      [ lua$LUA_VERSION \
        lua$LUA_SHORT_VERSION \
        lua-$LUA_VERSION \
        lua-$LUA_SHORT_VERSION \
        lua \
      ],
      [_ax_found_lua_libs='yes'],
      [_ax_found_lua_libs='no'],
      [$_ax_lua_extra_libs])
    LIBS=$_ax_lua_saved_libs

    AS_IF([test "x$ac_cv_search_lua_load" != 'xno' &&
           test "x$ac_cv_search_lua_load" != 'xnone required'],
      [LUA_LIB="$ac_cv_search_lua_load $_ax_lua_extra_libs"])
  ])

  dnl Test the result and run user code.
  AS_IF([test "x$_ax_found_lua_libs" = 'xyes'], [$1],
    [m4_default([$2], [AC_MSG_ERROR([cannot find Lua libs])])])
])


dnl =========================================================================
dnl AX_LUA_READLINE([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
dnl =========================================================================
AC_DEFUN([AX_LUA_READLINE],
[
  AX_LIB_READLINE
  AS_IF([test "x$ac_cv_header_readline_readline_h" != 'x' &&
         test "x$ac_cv_header_readline_history_h" != 'x'],
    [ LUA_LIBS_CFLAGS="-DLUA_USE_READLINE $LUA_LIBS_CFLAGS"
      $1
    ],
    [$2])
])
