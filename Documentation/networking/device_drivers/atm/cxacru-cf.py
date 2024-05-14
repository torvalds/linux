#!/usr/bin/env python
# Copyright 2009 Simon Arlott
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 59
# Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
# Usage: cxacru-cf.py < cxacru-cf.bin
# Output: values string suitable for the sysfs adsl_config attribute
#
# Warning: cxacru-cf.bin with MD5 hash cdbac2689969d5ed5d4850f117702110
# contains mis-aligned values which will stop the modem from being able
# to make a connection. If the first and last two bytes are removed then
# the values become valid, but the modulation will be forced to ANSI
# T1.413 only which may not be appropriate.
#
# The original binary format is a packed list of le32 values.

import sys
import struct

i = 0
while True:
	buf = sys.stdin.read(4)

	if len(buf) == 0:
		break
	elif len(buf) != 4:
		sys.stdout.write("\n")
		sys.stderr.write("Error: read {0} not 4 bytes\n".format(len(buf)))
		sys.exit(1)

	if i > 0:
		sys.stdout.write(" ")
	sys.stdout.write("{0:x}={1}".format(i, struct.unpack("<I", buf)[0]))
	i += 1

sys.stdout.write("\n")
