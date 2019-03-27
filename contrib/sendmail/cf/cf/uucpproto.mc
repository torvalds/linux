divert(-1)
#
# Copyright (c) 1998, 1999 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
# Copyright (c) 1983 Eric P. Allman.  All rights reserved.
# Copyright (c) 1988, 1993
#	The Regents of the University of California.  All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

#
#  This is the prototype for a configuration that only supports UUCP
#  and does not have DNS support at all.
#
#  You MUST change the `OSTYPE' macro to specify the operating system
#  on which this will run; this will set the location of various
#  support files for your operating system environment.  You MAY
#  create a domain file in ../domain and reference it by adding a
#  `DOMAIN' macro after the `OSTYPE' macro.  I recommend that you
#  first copy this to another file name so that new sendmail releases
#  will not trash your changes.
#

divert(0)dnl
VERSIONID(`$Id: uucpproto.mc,v 8.16 2013-11-22 20:51:08 ca Exp $')
OSTYPE(unknown)
FEATURE(promiscuous_relay)dnl
FEATURE(accept_unresolvable_domains)dnl
MAILER(local)dnl
MAILER(uucp)dnl
