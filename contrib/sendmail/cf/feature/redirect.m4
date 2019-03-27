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

divert(0)
VERSIONID(`$Id: redirect.m4,v 8.16 2013-11-22 20:51:11 ca Exp $')
divert(-1)

LOCAL_RULE_0
# addresses sent to foo@host.REDIRECT will give a 551 error code
R$* < @ $+ .REDIRECT. >		$: $1 < @ $2 . REDIRECT . > < ${opMode} >
R$* < @ $+ .REDIRECT. > <i>	$: $1 < @ $2 . REDIRECT. >
R$* < @ $+ .REDIRECT. > < $- >	$#error $@ 5.1.1 $: "551 User has moved; please try " <$1@$2>

LOCAL_CONFIG
CPREDIRECT
