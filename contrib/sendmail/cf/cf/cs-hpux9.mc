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
#  This is a Berkeley-specific configuration file for HP-UX 9.x.
#  It applies only to the Computer Science Division at Berkeley,
#  and should not be used elsewhere.   It is provided on the sendmail
#  distribution as a sample only.  To create your own configuration
#  file, create an appropriate domain file in ../domain, change the
#  `DOMAIN' macro below to reference that file, and copy the result
#  to a name of your own choosing.
#

divert(0)dnl
VERSIONID(`$Id: cs-hpux9.mc,v 8.15 2013-11-22 20:51:08 ca Exp $')
OSTYPE(hpux9)dnl
DOMAIN(CS.Berkeley.EDU)dnl
define(`MAIL_HUB', mailspool.CS.Berkeley.EDU)dnl
MAILER(local)dnl
MAILER(smtp)dnl
