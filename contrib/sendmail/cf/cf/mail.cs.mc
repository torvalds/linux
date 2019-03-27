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
#  This file is for the primary CS Division mail server.
#

divert(0)dnl
VERSIONID(`$Id: mail.cs.mc,v 8.19 2013-11-22 20:51:08 ca Exp $')
OSTYPE(ultrix4)dnl
DOMAIN(Berkeley.EDU)dnl
MASQUERADE_AS(CS.Berkeley.EDU)dnl
MAILER(local)dnl
MAILER(smtp)dnl
define(`confUSERDB_SPEC', ``/usr/local/lib/users.cs.db,/usr/local/lib/users.eecs.db'')dnl

LOCAL_CONFIG
DDBerkeley.EDU

# hosts for which we accept and forward mail (must be in .Berkeley.EDU)
CF CS
FF/etc/sendmail.cw

LOCAL_RULE_0
R< @ $=F . $D . > : $*		$@ $>7 $2		@here:... -> ...
R$* $=O $* < @ $=F . $D . >	$@ $>7 $1 $2 $3		...@here -> ...

R$* < @ $=F . $D . >		$#local $: $1		use UDB
