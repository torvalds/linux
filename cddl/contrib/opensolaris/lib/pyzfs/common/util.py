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

"""This module provides utility functions for ZFS.
zfs.util.dev -- a file object of /dev/zfs """

import gettext
import errno
import os
import solaris.misc
# Note: this module (zfs.util) should not import zfs.ioctl, because that
# would introduce a circular dependency

errno.ECANCELED = 47
errno.ENOTSUP = 48

dev = open("/dev/zfs", "w")

try:
	_ = gettext.translation("SUNW_OST_OSLIB", "/usr/lib/locale",
	    fallback=True).gettext
except:
	_ = solaris.misc.gettext

def default_repr(self):
	"""A simple __repr__ function."""
	if self.__slots__:
		str = "<" + self.__class__.__name__
		for v in self.__slots__:
			str += " %s: %r" % (v, getattr(self, v))
		return str + ">"
	else:
		return "<%s %s>" % \
		    (self.__class__.__name__, repr(self.__dict__))

class ZFSError(StandardError):
	"""This exception class represents a potentially user-visible
	ZFS error.  If uncaught, it will be printed and the process will
	exit with exit code 1.
	
	errno -- the error number (eg, from ioctl(2))."""

	__slots__ = "why", "task", "errno"
	__repr__ = default_repr

	def __init__(self, eno, task=None, why=None):
		"""Create a ZFS exception.
		eno -- the error number (errno)
		task -- a string describing the task that failed
		why -- a string describing why it failed (defaults to
		    strerror(eno))"""

		self.errno = eno
		self.task = task
		self.why = why

	def __str__(self):
		s = ""
		if self.task:
			s += self.task + ": "
		if self.why:
			s += self.why
		else:
			s += self.strerror
		return s

	__strs = {
		errno.EPERM: _("permission denied"),
		errno.ECANCELED:
		    _("delegated administration is disabled on pool"),
		errno.EINTR: _("signal received"),
		errno.EIO: _("I/O error"),
		errno.ENOENT: _("dataset does not exist"),
		errno.ENOSPC: _("out of space"),
		errno.EEXIST: _("dataset already exists"),
		errno.EBUSY: _("dataset is busy"),
		errno.EROFS:
		    _("snapshot permissions cannot be modified"),
		errno.ENAMETOOLONG: _("dataset name is too long"),
		errno.ENOTSUP: _("unsupported version"),
		errno.EAGAIN: _("pool I/O is currently suspended"),
	}

	__strs[errno.EACCES] = __strs[errno.EPERM]
	__strs[errno.ENXIO] = __strs[errno.EIO]
	__strs[errno.ENODEV] = __strs[errno.EIO]
	__strs[errno.EDQUOT] = __strs[errno.ENOSPC]

	@property
	def strerror(self):
		return ZFSError.__strs.get(self.errno, os.strerror(self.errno))

def nicenum(num):
	"""Return a nice string (eg "1.23M") for this integer."""
	index = 0;
	n = num;

	while n >= 1024:
		n /= 1024
		index += 1

	u = " KMGTPE"[index]
	if index == 0:
		return "%u" % n;
	elif n >= 100 or num & ((1024*index)-1) == 0:
		# it's an exact multiple of its index, or it wouldn't
		# fit as floating point, so print as an integer
		return "%u%c" % (n, u)
	else:
		# due to rounding, it's tricky to tell what precision to
		# use; try each precision and see which one fits
		for i in (2, 1, 0):
			s = "%.*f%c" % (i, float(num) / (1<<(10*index)), u)
			if len(s) <= 5:
				return s

def append_with_opt(option, opt, value, parser):
	"""A function for OptionParser which appends a tuple (opt, value)."""
	getattr(parser.values, option.dest).append((opt, value))

