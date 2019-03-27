divert(-1)
#
# Copyright (c) 1998-2000 Proofpoint, Inc. and its suppliers.
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
#  This the prototype for a "null client" -- that is, a client that
#  does nothing except forward all mail to a mail hub.  IT IS NOT
#  USABLE AS IS!!!
#
#  To use this, you MUST use the nullclient feature with the name of
#  the mail hub as its argument.  You MUST also define an `OSTYPE' to
#  define the location of the queue directories and the like.
#

divert(0)dnl
VERSIONID(`$Id: clientproto.mc,v 8.17 2013-11-22 20:51:08 ca Exp $')

OSTYPE(unknown)
FEATURE(nullclient, mailhost.$m)
