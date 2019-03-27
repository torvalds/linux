#!/bin/sh

#   Copyright 2003  Free Software Foundation, Inc.

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

# Please email any bugs, comments, and/or additions to this file to:
# bug-gdb@prep.ai.mit.edu

#
# gcore.sh
# Script to generate a core file of a running program.
# It starts up gdb, attaches to the given PID and invokes the gcore command.
#

if [ "$#" -eq "0" ]
then
    echo "usage:  gcore [-o filename] pid"
    exit 2
fi

# Need to check for -o option, but set default basename to "core".
name=core

if [ "$1" = "-o" ]
then
    if [ "$#" -lt "3" ]
    then
	# Not enough arguments.
	echo "usage:  gcore [-o filename] pid"
	exit 2
    fi
    name=$2

    # Shift over to start of pid list
    shift; shift
fi

# Initialise return code.
rc=0

# Loop through pids
for pid in $*
do
	# Write gdb script for pid $pid.  

	# Avoid need for temporary files by using funky "here
	# document" feature of sh.

	/usr/bin/gdb > /dev/null << EOF
	attach $pid
	gcore $name.$pid
	detach
	quit
EOF

	if [ -r $name.$pid ] ; then 
	    rc=0
	else
	    echo gcore: failed to create $name.$pid
	    rc=1
	    break
	fi


done

exit $rc

