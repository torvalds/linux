#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# mkd -- a script to create the device special files for the PARIDE subsystem
#
#  block devices:  	pd (45), pcd (46), pf (47)
#  character devices:	pt (96), pg (97)
#
function mkdev {
  mknod $1 $2 $3 $4 ; chmod 0660 $1 ; chown root:disk $1
}
#
function pd {
  D=$( printf \\$( printf "x%03x" $[ $1 + 97 ] ) )
  mkdev pd$D b 45 $[ $1 * 16 ]
  for P in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
  do mkdev pd$D$P b 45 $[ $1 * 16 + $P ]
  done
}
#
cd /dev
#
for u in 0 1 2 3 ; do pd $u ; done
for u in 0 1 2 3 ; do mkdev pcd$u b 46 $u ; done 
for u in 0 1 2 3 ; do mkdev pf$u  b 47 $u ; done 
for u in 0 1 2 3 ; do mkdev pt$u  c 96 $u ; done 
for u in 0 1 2 3 ; do mkdev npt$u c 96 $[ $u + 128 ] ; done 
for u in 0 1 2 3 ; do mkdev pg$u  c 97 $u ; done
#
# end of mkd

