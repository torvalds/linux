divert(-1)
#
# Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
VERSIONID(`$Id: queuegroup.m4,v 1.5 2013-11-22 20:51:11 ca Exp $')
divert(-1)

ifdef(`_ACCESS_TABLE_', `',
	`errprint(`*** ERROR: FEATURE(`queuegroup') requires FEATURE(`access_db')
')')

LOCAL_RULESETS
Squeuegroup
R< $+ >		$1
R $+ @ $+	$: $>SearchList <! qgrp> $| <F:$1@$2> <D:$2> <>
ifelse(len(X`'_ARG_),`1',
`R<?>		$@',
`R<?>		$# _ARG_')
R<$+>		$# $1
