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

"""Implements the Dataset class, providing methods for manipulating ZFS
datasets.  Also implements the Property class, which describes ZFS
properties."""

import zfs.ioctl
import zfs.util
import errno

_ = zfs.util._

class Property(object):
	"""This class represents a ZFS property.  It contains
	information about the property -- if it's readonly, a number vs
	string vs index, etc.  Only native properties are represented by
	this class -- not user properties (eg "user:prop") or userspace
	properties (eg "userquota@joe")."""

	__slots__ = "name", "number", "type", "default", "attr", "validtypes", \
	    "values", "colname", "rightalign", "visible", "indextable"
	__repr__ = zfs.util.default_repr

	def __init__(self, t):
		"""t is the tuple of information about this property
		from zfs.ioctl.get_proptable, which should match the
		members of zprop_desc_t (see zfs_prop.h)."""

		self.name = t[0]
		self.number = t[1]
		self.type = t[2]
		if self.type == "string":
			self.default = t[3]
		else:
			self.default = t[4]
		self.attr = t[5]
		self.validtypes = t[6]
		self.values = t[7]
		self.colname = t[8]
		self.rightalign = t[9]
		self.visible = t[10]
		self.indextable = t[11]

	def delegatable(self):
		"""Return True if this property can be delegated with
		"zfs allow"."""
		return self.attr != "readonly"

proptable = dict()
for name, t in zfs.ioctl.get_proptable().iteritems():
	proptable[name] = Property(t)
del name, t

def getpropobj(name):
	"""Return the Property object that is identified by the given
	name string.  It can be the full name, or the column name."""
	try:
		return proptable[name]
	except KeyError:
		for p in proptable.itervalues():
			if p.colname and p.colname.lower() == name:
				return p
		raise

class Dataset(object):
	"""Represents a ZFS dataset (filesystem, snapshot, zvol, clone, etc).

	Generally, this class provides interfaces to the C functions in
	zfs.ioctl which actually interface with the kernel to manipulate
	datasets.
	
	Unless otherwise noted, any method can raise a ZFSError to
	indicate failure."""

	__slots__ = "name", "__props"
	__repr__ = zfs.util.default_repr

	def __init__(self, name, props=None,
	    types=("filesystem", "volume"), snaps=True):
		"""Open the named dataset, checking that it exists and
		is of the specified type.
		
		name is the string name of this dataset.

		props is the property settings dict from zfs.ioctl.next_dataset.

		types is an iterable of strings specifying which types
		of datasets are permitted.  Accepted strings are
		"filesystem" and "volume".  Defaults to accepting all
		types.

		snaps is a boolean specifying if snapshots are acceptable.

		Raises a ZFSError if the dataset can't be accessed (eg
		doesn't exist) or is not of the specified type.
		"""

		self.name = name

		e = zfs.util.ZFSError(errno.EINVAL,
		    _("cannot open %s") % name,
		    _("operation not applicable to datasets of this type"))
		if "@" in name and not snaps:
			raise e
		if not props:
			props = zfs.ioctl.dataset_props(name)
		self.__props = props
		if "volume" not in types and self.getprop("type") == 3:
			raise e
		if "filesystem" not in types and self.getprop("type") == 2:
			raise e

	def getprop(self, propname):
		"""Return the value of the given property for this dataset.

		Currently only works for native properties (those with a
		Property object.)
		
		Raises KeyError if propname does not specify a native property.
		Does not raise ZFSError.
		"""

		p = getpropobj(propname)
		try:
			return self.__props[p.name]["value"]
		except KeyError:
			return p.default

	def parent(self):
		"""Return a Dataset representing the parent of this one."""
		return Dataset(self.name[:self.name.rindex("/")])

	def descendents(self):
		"""A generator function which iterates over all
		descendent Datasets (not including snapshots."""

		cookie = 0
		while True:
			# next_dataset raises StopIteration when done
			(name, cookie, props) = \
			    zfs.ioctl.next_dataset(self.name, False, cookie)
			ds = Dataset(name, props)
			yield ds
			for child in ds.descendents():
				yield child
	
	def userspace(self, prop):
		"""A generator function which iterates over a
		userspace-type property.

		prop specifies which property ("userused@",
		"userquota@", "groupused@", or "groupquota@").

		returns 3-tuple of domain (string), rid (int), and space (int).
		"""

		d = zfs.ioctl.userspace_many(self.name, prop)
		for ((domain, rid), space) in d.iteritems():
			yield (domain, rid, space)

	def userspace_upgrade(self):
		"""Initialize the accounting information for
		userused@... and groupused@... properties."""
		return zfs.ioctl.userspace_upgrade(self.name)
	
	def set_fsacl(self, un, d):
		"""Add to the "zfs allow"-ed permissions on this Dataset.

		un is True if the specified permissions should be removed.

		d is a dict specifying which permissions to add/remove:
		{ "whostr" -> None # remove all perms for this entity
		  "whostr" -> { "perm" -> None} # add/remove these perms
		} """
		return zfs.ioctl.set_fsacl(self.name, un, d)

	def get_fsacl(self):
		"""Get the "zfs allow"-ed permissions on the Dataset.

		Return a dict("whostr": { "perm" -> None })."""

		return zfs.ioctl.get_fsacl(self.name)

	def get_holds(self):
		"""Get the user holds on this Dataset.

		Return a dict("tag": timestamp)."""

		return zfs.ioctl.get_holds(self.name)

def snapshots_fromcmdline(dsnames, recursive):
	for dsname in dsnames:
		if not "@" in dsname:
			raise zfs.util.ZFSError(errno.EINVAL,
			    _("cannot open %s") % dsname,
			    _("operation only applies to snapshots"))
		try:
			ds = Dataset(dsname)
			yield ds
		except zfs.util.ZFSError, e:
			if not recursive or e.errno != errno.ENOENT:
				raise
		if recursive:
			(base, snapname) = dsname.split('@')
			parent = Dataset(base)
			for child in parent.descendents():
				try:
					yield Dataset(child.name + "@" +
					    snapname)
				except zfs.util.ZFSError, e:
					if e.errno != errno.ENOENT:
						raise
