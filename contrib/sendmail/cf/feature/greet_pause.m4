divert(-1)
#
# Copyright (c) 2004 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
VERSIONID(`$Id: greet_pause.m4,v 1.5 2013-11-22 20:51:11 ca Exp $')
divert(-1)

ifelse(len(X`'_ARG_),`1',`ifdef(`_ACCESS_TABLE_', `',
	`errprint(`*** ERROR: FEATURE(`greet_pause') requires FEATURE(`access_db')
')')')

define(`_GREET_PAUSE_', `')

LOCAL_RULESETS
######################################################################
###  greet_pause: lookup pause time before 220 greeting
###
###	Parameters:
###		$1: {client_name}
###		$2: {client_addr}
######################################################################
SLocal_greet_pause
Sgreet_pause
R$*			$: <$1><?> $| $>"Local_greet_pause" $1
R<$*><?> $| $#$*	$#$2
R<$*><?> $| $*		$: $1
ifdef(`_ACCESS_TABLE_', `dnl
R$+ $| $+		$: $>D < $1 > <?> <! GreetPause> < $2 >
R   $| $+		$: $>A < $1 > <?> <! GreetPause> <>	empty client_name
R<?> <$+>		$: $>A < $1 > <?> <! GreetPause> <>	no: another lookup
ifelse(len(X`'_ARG_),`1',
`R<?> <$*>		$@',
`R<?> <$*>		$# _ARG_')
R<$* <TMPF>> <$*>	$@
R<$+> <$*>		$# $1',`dnl
R$*			$# _ARG_')
