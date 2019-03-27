divert(-1)
#
# Copyright (c) 1998-2011 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
# Copyright (c) 1983 Eric P. Allman.  All rights reserved.
# Copyright (c) 1988, 1993
#	The Regents of the University of California.  All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#

divert(0)
VERSIONID(`$Id: xconnect.m4,v 1.3 2013-11-22 20:51:13 ca Exp $')
divert(-1)

ifdef(`_ACCESS_TABLE_', `dnl
LOCAL_RULESETS
#
# x_connect ruleset for looking up XConnect: tag in access DB to enable
# XCONNECT support in MTA
#
Sx_connect
dnl workspace: {client_name} $| {client_addr}
R$+ $| $+		$: $>D < $1 > <?> <! XConnect> < $2 >
dnl workspace: <result-of-lookup> <{client_addr}>
dnl OR $| $+ if client_name is empty
R   $| $+		$: $>A < $1 > <?> <! XConnect> <>	empty client_name
dnl workspace: <result-of-lookup> <{client_addr}>
R<?> <$+>		$: $>A < $1 > <?> <! XConnect> <>	no: another lookup
dnl workspace: <result-of-lookup> (<>|<{client_addr}>)
R<?> <$*>		$# no					found nothing
dnl workspace: <result-of-lookup> (<>|<{client_addr}>) | OK
R<$+> <$*>		$@ yes					found in access DB',
	`errprint(`*** ERROR: HACK(xconnect) requires FEATURE(access_db)
')')
