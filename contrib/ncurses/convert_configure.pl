extproc perl -S -w

# $Id: convert_configure.pl,v 1.3 2006/04/22 23:04:06 tom Exp $
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

# The converted script is written to stdout, so run this script as
#    convert_configure configure > configure.cmd
#
# When the converted script runs, it expects that /tmp dir is
# available (so we create it).
#
# run the result like this:
#    .\configure

# Some frequent manual intervention:
# a) Some makefiles hardwire SHELL = /bin/sh ==> change to: sh
# b) Some makefiles recognize that exe files terminate on .exe
#    You need to give this script -no-zexe option...

shift, $no_zexe = 1 if @ARGV and $ARGV[0] eq '-no-zexe';

mkdir '/tmp', 0777 unless -d '/tmp';

print <<EOF;
extproc sh

EOF

print <<EOF unless $no_zexe;
# Make sensible defaults:
CC="gcc -Zexe -Zmt"
export CC
CXX="gcc -Zexe -Zmt"
export CXX
#GCCOPT="$GCCOPT -Zexe"
#export GCCOPT
EOF

print <<EOF;
CONFIG_SHELL=sh
export CONFIG_SHELL

# Optimization (GNU make 3.74 cannot be loaded :-():
emxload -m 30 sh.exe ls.exe tr.exe id.exe sed.exe # make.exe 
emxload -m 30 grep.exe egrep.exe fgrep.exe cat.exe rm.exe mv.exe cp.exe
emxload -m 30 uniq.exe basename.exe sort.exe awk.exe echo.exe


EOF

$checking_path = 0;

while (<>) {
  if (/for\s+(\w+)\s+in\s*\$(PATH|ac_dummy)\s*;/) {
    $checking_path = 1;
    $varname = $1;
    $subst= <<EOS
$varname="`echo -E \\"\$$varname\\" | tr \\\\\\\\\\\\\\\\ / `"
EOS
  } 
  if (/if\s+test\s+-z\s+\"\$INSTALL\"/) {
    $checking_install = 1;
  } 
  $checking_install = $checking_path = 0 if /^\s*done\s*$/;
  # We want to create an extra line like this one:
#   ac_dir="`echo -E \"$ac_dir\" | tr \\\\\\\\ / `"
  s{^((\s*)if\s+test)\s*-f\s*(\$$varname/\S+)\s*;}
   {$2$subst$1 -f $3 -o -f $3.exe ;}
      if $checking_path;	# Checking for executables
  # change |/usr/sbin/*| to |/usr/sbin/*|?:[\\/]os2[\\/]install[\\/]*|
  # in the list of things to skip (with both cases)
  s{\Q|/usr/sbin/*|}
   {|/usr/sbin/*|?:[\\\\/]os2[\\\\/]install[\\\\/]*|?:[\\\\/]OS2[\\\\/]INSTALL[\\\\/]*|}
      if $checking_install;	# Do not accept d:/os2/install/install.exe
  s/^(host|build)=NONE$/$1=x86-emx-os2/;	# Make default host/build
  s/"\$\{IFS}:"$/"\${IFS};"/;	# Fix IFS line
  s/\bIFS=\":\"$/IFS=";"/;	# Fix another IFS line
  s/\btest\s+-s\s+conftest\b/test -f conftest/g; # Fix exe test
  # This one is needed for curses:
  s/^\s*host=`.*\$ac_config_sub \$host_alias`/$&\nif test -z "\$host"; then host=\$host_alias; fi/;
  s,/bin/sh(?![/\w]),sh,g;
  s,^(\s*/usr/sbin/sendmail\s*)\\$,$1 "`whence sendmail | tr '\\\\\\\\' / `" \\,;
  print;
}

__END__

Changes:	98/11 : support check for executables in ncurses.
		99/2  : support INSTALL, 
			new IFS=':' style
		99/11 : find sendmail
		00/01 : export CONFIG_SHELL
		00/10 : new syntax for host=`...` line
