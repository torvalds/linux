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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

"""This module implements the "zfs allow" and "zfs unallow" subcommands.
The only public interface is the zfs.allow.do_allow() function."""

import zfs.util
import zfs.dataset
import optparse
import sys
import pwd
import grp
import errno

_ = zfs.util._

class FSPerms(object):
	"""This class represents all the permissions that are set on a
	particular filesystem (not including those inherited)."""

	__slots__ = "create", "sets", "local", "descend", "ld"
	__repr__ = zfs.util.default_repr

	def __init__(self, raw):
		"""Create a FSPerms based on the dict of raw permissions
		from zfs.ioctl.get_fsacl()."""
		# set of perms
		self.create = set()

		# below are { "Ntype name": set(perms) }
		# where N is a number that we just use for sorting,
		# type is "user", "group", "everyone", or "" (for sets)
		# name is a user, group, or set name, or "" (for everyone)
		self.sets = dict()
		self.local = dict()
		self.descend = dict()
		self.ld = dict()

		# see the comment in dsl_deleg.c for the definition of whokey
		for whokey in raw.keys():
			perms = raw[whokey].keys()
			whotypechr = whokey[0].lower()
			ws = whokey[3:]
			if whotypechr == "c":
				self.create.update(perms)
			elif whotypechr == "s":
				nwho = "1" + ws
				self.sets.setdefault(nwho, set()).update(perms)
			else:
				if whotypechr == "u":
					try:
						name = pwd.getpwuid(int(ws)).pw_name
					except KeyError:
						name = ws
					nwho = "1user " + name
				elif whotypechr == "g":
					try:
						name = grp.getgrgid(int(ws)).gr_name
					except KeyError:
						name = ws
					nwho = "2group " + name
				elif whotypechr == "e":
					nwho = "3everyone"
				else:
					raise ValueError(whotypechr)

				if whokey[1] == "l":
					d = self.local
				elif whokey[1] == "d":
					d = self.descend
				else:
					raise ValueError(whokey[1])

				d.setdefault(nwho, set()).update(perms)

		# Find perms that are in both local and descend, and
		# move them to ld.
		for nwho in self.local:
			if nwho not in self.descend:
				continue
			# note: these are set operations
			self.ld[nwho] = self.local[nwho] & self.descend[nwho]
			self.local[nwho] -= self.ld[nwho]
			self.descend[nwho] -= self.ld[nwho]

	@staticmethod
	def __ldstr(d, header):
		s = ""
		for (nwho, perms) in sorted(d.items()):
			# local and descend may have entries where perms
			# is an empty set, due to consolidating all
			# permissions into ld
			if perms:
				s += "\t%s %s\n" % \
				    (nwho[1:], ",".join(sorted(perms)))
		if s:
			s = header + s
		return s

	def __str__(self):
		s = self.__ldstr(self.sets, _("Permission sets:\n"))

		if self.create:
			s += _("Create time permissions:\n")
			s += "\t%s\n" % ",".join(sorted(self.create))

		s += self.__ldstr(self.local, _("Local permissions:\n"))
		s += self.__ldstr(self.descend, _("Descendent permissions:\n"))
		s += self.__ldstr(self.ld, _("Local+Descendent permissions:\n"))
		return s.rstrip()

def args_to_perms(parser, options, who, perms):
	"""Return a dict of raw perms {"whostr" -> {"perm" -> None}}
	based on the command-line input."""

	# perms is not set if we are doing a "zfs unallow <who> <fs>" to
	# remove all of someone's permissions
	if perms:
		setperms = dict(((p, None) for p in perms if p[0] == "@"))
		baseperms = dict(((canonicalized_perm(p), None)
		    for p in perms if p[0] != "@"))
	else:
		setperms = None
		baseperms = None

	d = dict()
	
	def storeperm(typechr, inheritchr, arg):
		assert typechr in "ugecs"
		assert inheritchr in "ld-"

		def mkwhokey(t):
			return "%c%c$%s" % (t, inheritchr, arg)

		if baseperms or not perms:
			d[mkwhokey(typechr)] = baseperms
		if setperms or not perms:
			d[mkwhokey(typechr.upper())] = setperms

	def decodeid(w, toidfunc, fmt):
		try:
			return int(w)
		except ValueError:
			try:
				return toidfunc(w)[2]
			except KeyError:
				parser.error(fmt % w)

	if options.set:
		storeperm("s", "-", who)
	elif options.create:
		storeperm("c", "-", "")
	else:
		for w in who:
			if options.user:
				id = decodeid(w, pwd.getpwnam,
				    _("invalid user %s"))
				typechr = "u"
			elif options.group:
				id = decodeid(w, grp.getgrnam,
				    _("invalid group %s"))
				typechr = "g"
			elif w == "everyone":
				id = ""
				typechr = "e"
			else:
				try:
					id = pwd.getpwnam(w)[2]
					typechr = "u"
				except KeyError:
					try:
						id = grp.getgrnam(w)[2]
						typechr = "g"
					except KeyError:
						parser.error(_("invalid user/group %s") % w)
			if options.local:
				storeperm(typechr, "l", id)
			if options.descend:
				storeperm(typechr, "d", id)
	return d

perms_subcmd = dict(
    create=_("Must also have the 'mount' ability"),
    destroy=_("Must also have the 'mount' ability"),
    snapshot="",
    rollback="",
    clone=_("""Must also have the 'create' ability and 'mount'
\t\t\t\tability in the origin file system"""),
    promote=_("""Must also have the 'mount'
\t\t\t\tand 'promote' ability in the origin file system"""),
    rename=_("""Must also have the 'mount' and 'create'
\t\t\t\tability in the new parent"""),
    receive=_("Must also have the 'mount' and 'create' ability"),
    allow=_("Must also have the permission that is being\n\t\t\t\tallowed"),
    mount=_("Allows mount/umount of ZFS datasets"),
    share=_("Allows sharing file systems over NFS or SMB\n\t\t\t\tprotocols"),
    send="",
    hold=_("Allows adding a user hold to a snapshot"),
    release=_("Allows releasing a user hold which\n\t\t\t\tmight destroy the snapshot"),
    diff=_("Allows lookup of paths within a dataset,\n\t\t\t\tgiven an object number. Ordinary users need this\n\t\t\t\tin order to use zfs diff"),
    bookmark="",
)

perms_other = dict(
    userprop=_("Allows changing any user property"),
    userquota=_("Allows accessing any userquota@... property"),
    groupquota=_("Allows accessing any groupquota@... property"),
    userused=_("Allows reading any userused@... property"),
    groupused=_("Allows reading any groupused@... property"),
)

def hasset(ds, setname):
	"""Return True if the given setname (string) is defined for this
	ds (Dataset)."""
	# It would be nice to cache the result of get_fsacl().
	for raw in ds.get_fsacl().values():
		for whokey in raw.keys():
			if whokey[0].lower() == "s" and whokey[3:] == setname:
				return True
	return False

def canonicalized_perm(permname):
	"""Return the canonical name (string) for this permission (string).
	Raises ZFSError if it is not a valid permission."""
	if permname in perms_subcmd.keys() or permname in perms_other.keys():
		return permname
	try:
		return zfs.dataset.getpropobj(permname).name
	except KeyError:
		raise zfs.util.ZFSError(errno.EINVAL, permname,
		    _("invalid permission"))
		
def print_perms():
	"""Print the set of supported permissions."""
	print(_("\nThe following permissions are supported:\n"))
	fmt = "%-16s %-14s\t%s"
	print(fmt % (_("NAME"), _("TYPE"), _("NOTES")))

	for (name, note) in sorted(perms_subcmd.iteritems()):
		print(fmt % (name, _("subcommand"), note))

	for (name, note) in sorted(perms_other.iteritems()):
		print(fmt % (name, _("other"), note))

	for (name, prop) in sorted(zfs.dataset.proptable.iteritems()):
		if prop.visible and prop.delegatable():
			print(fmt % (name, _("property"), ""))

def do_allow():
	"""Implements the "zfs allow" and "zfs unallow" subcommands."""
	un = (sys.argv[1] == "unallow")

	def usage(msg=None):
		parser.print_help()
		print_perms()
		if msg:
			print
			parser.exit("zfs: error: " + msg)
		else:
			parser.exit()

	if un:
		u = _("""unallow [-rldug] <"everyone"|user|group>[,...]
	    [<perm|@setname>[,...]] <filesystem|volume>
	unallow [-rld] -e [<perm|@setname>[,...]] <filesystem|volume>
	unallow [-r] -c [<perm|@setname>[,...]] <filesystem|volume>
	unallow [-r] -s @setname [<perm|@setname>[,...]] <filesystem|volume>""")
		verb = _("remove")
		sstr = _("undefine permission set")
	else:
		u = _("""allow <filesystem|volume>
	allow [-ldug] <"everyone"|user|group>[,...] <perm|@setname>[,...]
	    <filesystem|volume>
	allow [-ld] -e <perm|@setname>[,...] <filesystem|volume>
	allow -c <perm|@setname>[,...] <filesystem|volume>
	allow -s @setname <perm|@setname>[,...] <filesystem|volume>""")
		verb = _("set")
		sstr = _("define permission set")

	parser = optparse.OptionParser(usage=u, prog="zfs")

	parser.add_option("-l", action="store_true", dest="local",
	    help=_("%s permission locally") % verb)
	parser.add_option("-d", action="store_true", dest="descend",
	    help=_("%s permission for descendents") % verb)
	parser.add_option("-u", action="store_true", dest="user",
	    help=_("%s permission for user") % verb)
	parser.add_option("-g", action="store_true", dest="group",
	    help=_("%s permission for group") % verb)
	parser.add_option("-e", action="store_true", dest="everyone",
	    help=_("%s permission for everyone") % verb)
	parser.add_option("-c", action="store_true", dest="create",
	    help=_("%s create time permissions") % verb)
	parser.add_option("-s", action="store_true", dest="set", help=sstr)
	if un:
		parser.add_option("-r", action="store_true", dest="recursive",
		    help=_("remove permissions recursively"))

	if len(sys.argv) == 3 and not un:
		# just print the permissions on this fs

		if sys.argv[2] == "-h":
			# hack to make "zfs allow -h" work
			usage()
		ds = zfs.dataset.Dataset(sys.argv[2], snaps=False)

		p = dict()
		for (fs, raw) in ds.get_fsacl().items():
			p[fs] = FSPerms(raw)

		for fs in sorted(p.keys(), reverse=True):
			s = _("---- Permissions on %s ") % fs
			print(s + "-" * (70-len(s)))
			print(p[fs])
		return
	

	(options, args) = parser.parse_args(sys.argv[2:])

	if sum((bool(options.everyone), bool(options.user),
	    bool(options.group))) > 1:
		parser.error(_("-u, -g, and -e are mutually exclusive"))

	def mungeargs(expected_len):
		if un and len(args) == expected_len-1:
			return (None, args[expected_len-2])
		elif len(args) == expected_len:
			return (args[expected_len-2].split(","),
			    args[expected_len-1])
		else:
			usage(_("wrong number of parameters"))

	if options.set:
		if options.local or options.descend or options.user or \
		    options.group or options.everyone or options.create:
			parser.error(_("invalid option combined with -s"))
		if args[0][0] != "@":
			parser.error(_("invalid set name: missing '@' prefix"))

		(perms, fsname) = mungeargs(3)
		who = args[0]
	elif options.create:
		if options.local or options.descend or options.user or \
		    options.group or options.everyone or options.set:
			parser.error(_("invalid option combined with -c"))

		(perms, fsname) = mungeargs(2)
		who = None
	elif options.everyone:
		if options.user or options.group or \
		    options.create or options.set:
			parser.error(_("invalid option combined with -e"))

		(perms, fsname) = mungeargs(2)
		who = ["everyone"]
	else:
		(perms, fsname) = mungeargs(3)
		who = args[0].split(",")

	if not options.local and not options.descend:
		options.local = True
		options.descend = True

	d = args_to_perms(parser, options, who, perms)

	ds = zfs.dataset.Dataset(fsname, snaps=False)

	if not un and perms:
		for p in perms:
			if p[0] == "@" and not hasset(ds, p):
				parser.error(_("set %s is not defined") % p)

	ds.set_fsacl(un, d)
	if un and options.recursive:
		for child in ds.descendents():
			child.set_fsacl(un, d)
