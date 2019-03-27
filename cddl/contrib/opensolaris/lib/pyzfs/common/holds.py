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

"""This module implements the "zfs holds" subcommand.
The only public interface is the zfs.holds.do_holds() function."""

import optparse
import sys
import errno
import time
import zfs.util
import zfs.dataset
import zfs.table

_ = zfs.util._

def do_holds():
	"""Implements the "zfs holds" subcommand."""
	def usage(msg=None):
		parser.print_help()
		if msg:
			print
			parser.exit("zfs: error: " + msg)
		else:
			parser.exit()

	u = _("""holds [-r] <snapshot> ...""")

	parser = optparse.OptionParser(usage=u, prog="zfs")

	parser.add_option("-r", action="store_true", dest="recursive",
	    help=_("list holds recursively"))

	(options, args) = parser.parse_args(sys.argv[2:])

	if len(args) < 1:
		usage(_("missing snapshot argument"))

	fields = ("name", "tag", "timestamp")
	rjustfields = ()
	printing = False 
	gotone = False
	t = zfs.table.Table(fields, rjustfields) 
	for ds in zfs.dataset.snapshots_fromcmdline(args, options.recursive):
		gotone = True
		for tag, tm in ds.get_holds().iteritems():
			val = {"name": ds.name, "tag": tag,
			    "timestamp": time.ctime(tm)}
			t.addline(ds.name, val)
			printing = True
	if printing:
		t.printme()
	elif not gotone:
		raise zfs.util.ZFSError(errno.ENOENT, _("no matching datasets"))
