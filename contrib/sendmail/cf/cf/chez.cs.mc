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
#  This is a Berkeley-specific configuration file for a specific
#  machine in the Computer Science Division at Berkeley, and should
#  not be used elsewhere.   It is provided on the sendmail distribution
#  as a sample only.
#
#  This file is for a home machine that wants to masquerade as an
#  on-campus machine.  Additionally, all addresses without a hostname
#  will be forwarded to that machine.
#

divert(0)dnl
VERSIONID(`$Id: chez.cs.mc,v 8.15 2013-11-22 20:51:08 ca Exp $')
OSTYPE(bsd4.4)dnl
DOMAIN(CS.Berkeley.EDU)dnl
define(`LOCAL_RELAY', vangogh.CS.Berkeley.EDU)dnl
MASQUERADE_AS(vangogh.CS.Berkeley.EDU)dnl
FEATURE(use_cw_file)dnl
MAILER(local)dnl
MAILER(smtp)dnl
