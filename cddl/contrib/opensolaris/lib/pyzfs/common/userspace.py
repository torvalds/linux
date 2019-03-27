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

"""This module implements the "zfs userspace" and "zfs groupspace" subcommands.
The only public interface is the zfs.userspace.do_userspace() function."""

import optparse
import sys
import pwd
import grp
import errno
import solaris.misc
import zfs.util
import zfs.ioctl
import zfs.dataset
import zfs.table

_ = zfs.util._

# map from property name prefix -> (field name, isgroup)
props = {
    "userused@": ("used", False),
    "userquota@": ("quota", False),
    "groupused@": ("used", True),
    "groupquota@": ("quota", True),
}

def skiptype(options, prop):
	"""Return True if this property (eg "userquota@") should be skipped."""
	(field, isgroup) = props[prop]
	if field not in options.fields:
		return True
	if isgroup and "posixgroup" not in options.types and \
	    "smbgroup" not in options.types:
		return True
	if not isgroup and "posixuser" not in options.types and \
	    "smbuser" not in options.types:
		return True
	return False

def new_entry(options, isgroup, domain, rid):
	"""Return a dict("field": value) for this domain (string) + rid (int)"""

	if domain:
		idstr = "%s-%u" % (domain, rid)
	else:
		idstr = "%u" % rid

	(typename, mapfunc) = {
	    (1, 1): ("SMB Group",   lambda id: solaris.misc.sid_to_name(id, 0)),
	    (1, 0): ("POSIX Group", lambda id: grp.getgrgid(int(id)).gr_name),
	    (0, 1): ("SMB User",    lambda id: solaris.misc.sid_to_name(id, 1)),
	    (0, 0): ("POSIX User",  lambda id: pwd.getpwuid(int(id)).pw_name)
	}[isgroup, bool(domain)]

	if typename.lower().replace(" ", "") not in options.types:
		return None

	v = dict()
	v["type"] = typename

	# python's getpwuid/getgrgid is confused by ephemeral uids
	if not options.noname and rid < 1<<31:
		try:
			v["name"] = mapfunc(idstr)
		except KeyError:
			pass

	if "name" not in v:
		v["name"] = idstr
		if not domain:
			# it's just a number, so pad it with spaces so
			# that it will sort numerically
			v["name.sort"] = "%20d" % rid
	# fill in default values
	v["used"] = "0"
	v["used.sort"] = 0
	v["quota"] = "none"
	v["quota.sort"] = 0
	return v

def process_one_raw(acct, options, prop, elem):
	"""Update the acct dict to incorporate the
	information from this elem from Dataset.userspace(prop)."""

	(domain, rid, value) = elem
	(field, isgroup) = props[prop]

	if options.translate and domain:
		try:
			rid = solaris.misc.sid_to_id("%s-%u" % (domain, rid),
			    not isgroup)
			domain = None
		except KeyError:
			pass;
	key = (isgroup, domain, rid)
		
	try:
		v = acct[key]
	except KeyError:
		v = new_entry(options, isgroup, domain, rid)
		if not v:
			return
		acct[key] = v

	# Add our value to an existing value, which may be present if
	# options.translate is set.
	value = v[field + ".sort"] = value + v[field + ".sort"]

	if options.parsable:
		v[field] = str(value)
	else:
		v[field] = zfs.util.nicenum(value)

def do_userspace():
	"""Implements the "zfs userspace" and "zfs groupspace" subcommands."""

	def usage(msg=None):
		parser.print_help()
		if msg:
			print
			parser.exit("zfs: error: " + msg)
		else:
			parser.exit()

	if sys.argv[1] == "userspace":
		defaulttypes = "posixuser,smbuser"
	else:
		defaulttypes = "posixgroup,smbgroup"

	fields = ("type", "name", "used", "quota")
	rjustfields = ("used", "quota")
	types = ("all", "posixuser", "smbuser", "posixgroup", "smbgroup")

	u = _("%s [-niHp] [-o field[,...]] [-sS field] ... \n") % sys.argv[1]
	u += _("    [-t type[,...]] <filesystem|snapshot>")
	parser = optparse.OptionParser(usage=u, prog="zfs")

	parser.add_option("-n", action="store_true", dest="noname",
	    help=_("Print numeric ID instead of user/group name"))
	parser.add_option("-i", action="store_true", dest="translate",
	    help=_("translate SID to posix (possibly ephemeral) ID"))
	parser.add_option("-H", action="store_true", dest="noheaders",
	    help=_("no headers, tab delimited output"))
	parser.add_option("-p", action="store_true", dest="parsable",
	    help=_("exact (parsable) numeric output"))
	parser.add_option("-o", dest="fields", metavar="field[,...]",
	    default="type,name,used,quota",
	    help=_("print only these fields (eg type,name,used,quota)"))
	parser.add_option("-s", dest="sortfields", metavar="field",
	    type="choice", choices=fields, default=list(),
	    action="callback", callback=zfs.util.append_with_opt,
	    help=_("sort field"))
	parser.add_option("-S", dest="sortfields", metavar="field",
	    type="choice", choices=fields, #-s sets the default
	    action="callback", callback=zfs.util.append_with_opt,
	    help=_("reverse sort field"))
	parser.add_option("-t", dest="types", metavar="type[,...]",
	    default=defaulttypes,
	    help=_("print only these types (eg posixuser,smbuser,posixgroup,smbgroup,all)"))

	(options, args) = parser.parse_args(sys.argv[2:])
	if len(args) != 1:
		usage(_("wrong number of arguments"))
	dsname = args[0]

	options.fields = options.fields.split(",")
	for f in options.fields:
		if f not in fields:
			usage(_("invalid field %s") % f)

	options.types = options.types.split(",")
	for t in options.types:
		if t not in types:
			usage(_("invalid type %s") % t)

	if not options.sortfields:
		options.sortfields = [("-s", "type"), ("-s", "name")]

	if "all" in options.types:
		options.types = types[1:]

	ds = zfs.dataset.Dataset(dsname, types=("filesystem"))

	if ds.getprop("jailed") and solaris.misc.isglobalzone():
		options.noname = True

	if not ds.getprop("useraccounting"):
		print(_("Initializing accounting information on old filesystem, please wait..."))
		ds.userspace_upgrade()

	# gather and process accounting information
	# Due to -i, we need to keep a dict, so we can potentially add
	# together the posix ID and SID's usage.  Grr.
	acct = dict()
	for prop in props.keys():
		if skiptype(options, prop):
			continue;
		for elem in ds.userspace(prop):
			process_one_raw(acct, options, prop, elem)

	def cmpkey(val):
		l = list()
		for (opt, field) in options.sortfields:
			try:
				n = val[field + ".sort"]
			except KeyError:
				n = val[field]
			if opt == "-S":
				# reverse sorting
				try:
					n = -n
				except TypeError:
					# it's a string; decompose it
					# into an array of integers,
					# each one the negative of that
					# character
					n = [-ord(c) for c in n]
			l.append(n)
		return l

	t = zfs.table.Table(options.fields, rjustfields)
	for val in acct.itervalues():
		t.addline(cmpkey(val), val)
	t.printme(not options.noheaders)
