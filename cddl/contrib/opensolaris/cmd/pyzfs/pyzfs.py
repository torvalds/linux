#! /usr/bin/python2.4 -S
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

# Note, we want SIGINT (control-c) to exit the process quietly, to mimic
# the standard behavior of C programs.  The best we can do with pure
# Python is to run with -S (to disable "import site"), and start our
# program with a "try" statement.  Hopefully nobody hits ^C before our
# try statement is executed.

try:
	import site
	import gettext
	import zfs.util
	import zfs.ioctl
	import sys
	import errno

	"""This is the main script for doing zfs subcommands.  It doesn't know
	what subcommands there are, it just looks for a module zfs.<subcommand>
	that implements that subcommand."""

	_ = gettext.translation("SUNW_OST_OSCMD", "/usr/lib/locale",
	    fallback=True).gettext

	if len(sys.argv) < 2:
		sys.exit(_("missing subcommand argument"))

	zfs.ioctl.set_cmdstr(" ".join(["zfs"] + sys.argv[1:]))

	try:
		# import zfs.<subcommand>
		# subfunc =  zfs.<subcommand>.do_<subcommand>

		subcmd = sys.argv[1]
		__import__("zfs." + subcmd)
		submod = getattr(zfs, subcmd)
		subfunc = getattr(submod, "do_" + subcmd)
	except (ImportError, AttributeError):
		sys.exit(_("invalid subcommand"))

	try:
		subfunc()
	except zfs.util.ZFSError, e:
		print(e)
		sys.exit(1)

except IOError, e:
	import errno
	import sys

	if e.errno == errno.EPIPE:
		sys.exit(1)
	raise
except KeyboardInterrupt:
	import sys

	sys.exit(1)
