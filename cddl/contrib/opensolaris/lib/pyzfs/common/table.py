#! /usr/bin/python2.6
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
#

import zfs.util

class Table:
	__slots__ = "fields", "rjustfields", "maxfieldlen", "lines"
	__repr__ = zfs.util.default_repr

	def __init__(self, fields, rjustfields=()):
		# XXX maybe have a defaults, too?
		self.fields = fields
		self.rjustfields = rjustfields
		self.maxfieldlen = dict.fromkeys(fields, 0)
		self.lines = list()
	
	def __updatemax(self, k, v):
		self.maxfieldlen[k] = max(self.maxfieldlen.get(k, None), v)

	def addline(self, sortkey, values):
		"""values is a dict from field name to value"""

		va = list()
		for f in self.fields:
			v = str(values[f])
			va.append(v)
			self.__updatemax(f, len(v))
		self.lines.append((sortkey, va))

	def printme(self, headers=True):
		if headers:
			d = dict([(f, f.upper()) for f in self.fields])
			self.addline(None, d)

		self.lines.sort()
		for (k, va) in self.lines:
			line = str()
			for i in range(len(self.fields)):
				if not headers:
					line += va[i]
					line += "\t"
				else:
					if self.fields[i] in self.rjustfields:
						fmt = "%*s  "
					else:
						fmt = "%-*s  "
					mfl = self.maxfieldlen[self.fields[i]]
					line += fmt % (mfl, va[i])
			print(line)
