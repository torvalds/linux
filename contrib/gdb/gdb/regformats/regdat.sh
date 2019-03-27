#!/bin/sh -u

# Register protocol definitions for GDB, the GNU debugger.
# Copyright 2001, 2002 Free Software Foundation, Inc.
#
# This file is part of GDB.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

move_if_change ()
{
    file=$1
    if test -r ${file} && cmp -s "${file}" new-"${file}"
    then
	echo "${file} unchanged." 1>&2
    else
	mv new-"${file}" "${file}"
	echo "${file} updated." 1>&2
    fi
}

# Format of the input files
read="type entry"

do_read ()
{
    type=""
    entry=""
    while read line
    do
	if test "${line}" = ""
	then
	    continue
	elif test "${line}" = "#" -a "${comment}" = ""
	then
	    continue
	elif expr "${line}" : "#" > /dev/null
	then
	    comment="${comment}
${line}"
	else

	    # The semantics of IFS varies between different SH's.  Some
	    # treat ``::' as three fields while some treat it as just too.
	    # Work around this by eliminating ``::'' ....
	    line="`echo "${line}" | sed -e 's/::/: :/g' -e 's/::/: :/g'`"

	    OFS="${IFS}" ; IFS="[:]"
	    eval read ${read} <<EOF
${line}
EOF
	    IFS="${OFS}"

	    # .... and then going back through each field and strip out those
	    # that ended up with just that space character.
	    for r in ${read}
	    do
		if eval test \"\${${r}}\" = \"\ \"
		then
		    eval ${r}=""
		fi
	    done

	    break
	fi
    done
    if [ -n "${type}" ]
    then
	true
    else
	false
    fi
}

if test ! -r $1; then
  echo "$0: Could not open $1." 1>&2
  exit 1
fi

copyright ()
{
cat <<EOF
/* *INDENT-OFF* */ /* THIS FILE IS GENERATED */

/* A register protocol for GDB, the GNU debugger.
   Copyright 2001, 2002 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* This file was created with the aid of \`\`regdat.sh'' and \`\`$1''.  */

EOF
}


exec > new-$2
copyright $1
echo '#include "regdef.h"'
echo '#include "regcache.h"'
echo
offset=0
i=0
name=x
expedite=x
exec < $1
while do_read
do
  if test "${type}" = "name"; then
    name="${entry}"
    echo "struct reg regs_${name}[] = {"
    continue
  elif test "${type}" = "expedite"; then
    expedite="${entry}"
    continue
  elif test "${name}" = x; then
    echo "$0: $1 does not specify \`\`name''." 1>&2
    exit 1
  else
    echo "  { \"${entry}\", ${offset}, ${type} },"
    offset=`expr ${offset} + ${type}`
    i=`expr $i + 1`
  fi
done

echo "};"
echo
echo "const char *expedite_regs_${name}[] = { \"`echo ${expedite} | sed 's/,/", "/g'`\", 0 };"
echo

cat <<EOF
void
init_registers ()
{
    set_register_cache (regs_${name},
			sizeof (regs_${name}) / sizeof (regs_${name}[0]));
    gdbserver_expedite_regs = expedite_regs_${name};
}
EOF

# close things off
exec 1>&2
move_if_change $2
