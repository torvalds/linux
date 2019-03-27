PUSHDIVERT(-1)
#
# Copyright (c) 1998-2000, 2004 Proofpoint, Inc. and its suppliers.
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
_DEFIFNOT(`_DEF_LOCAL_MAILER_FLAGS', `lsDFMAw5:/|@q')
_DEFIFNOT(`LOCAL_MAILER_FLAGS', `Prmn9')
ifdef(`LOCAL_MAILER_PATH',, `define(`LOCAL_MAILER_PATH', /bin/mail)')
ifdef(`LOCAL_MAILER_ARGS',, `define(`LOCAL_MAILER_ARGS', `mail -d $u')')
ifdef(`LOCAL_MAILER_DSN_DIAGNOSTIC_CODE',, `define(`LOCAL_MAILER_DSN_DIAGNOSTIC_CODE', `X-Unix')')
_DEFIFNOT(`_DEF_LOCAL_SHELL_FLAGS', `lsDFMoq')
_DEFIFNOT(`LOCAL_SHELL_FLAGS', `eu9')
ifdef(`LOCAL_SHELL_PATH',, `define(`LOCAL_SHELL_PATH', /bin/sh)')
ifdef(`LOCAL_SHELL_ARGS',, `define(`LOCAL_SHELL_ARGS', `sh -c $u')')
ifdef(`LOCAL_SHELL_DIR',, `define(`LOCAL_SHELL_DIR', `$z:/')')
define(`LOCAL_RWR', `ifdef(`_LOCAL_LMTP_',
`S=EnvFromSMTP/HdrFromL, R=EnvToL/HdrToL',
`S=EnvFromL/HdrFromL, R=EnvToL/HdrToL')')
define(`_LOCAL_QGRP', `ifelse(defn(`LOCAL_MAILER_QGRP'),`',`', ` Q=LOCAL_MAILER_QGRP,')')dnl
define(`_PROG_QGRP', `ifelse(defn(`LOCAL_PROG_QGRP'),`',`', ` Q=LOCAL_PROG_QGRP,')')dnl
POPDIVERT

##################################################
###   Local and Program Mailer specification   ###
##################################################

VERSIONID(`$Id: local.m4,v 8.60 2013-11-22 20:51:14 ca Exp $')

#
#  Envelope sender rewriting
#
SEnvFromL
R<@>			$n			errors to mailer-daemon
R@ <@ $*>		$n			temporarily bypass Sun bogosity
R$+			$: $>AddDomain $1	add local domain if needed
ifdef(`_LOCAL_NO_MASQUERADE_', `dnl', `dnl
R$*			$: $>MasqEnv $1		do masquerading')

#
#  Envelope recipient rewriting
#
SEnvToL
R$+ < @ $* >		$: $1			strip host part
ifdef(`confUSERDB_SPEC', `dnl', `dnl
R$+ + $*		$: < $&{addr_type} > $1 + $2	mark with addr type
R<e s> $+ + $*		$: $1			remove +detail for sender
R< $* > $+		$: $2			else remove mark')

#
#  Header sender rewriting
#
SHdrFromL
R<@>			$n			errors to mailer-daemon
R@ <@ $*>		$n			temporarily bypass Sun bogosity
R$+			$: $>AddDomain $1	add local domain if needed
ifdef(`_LOCAL_NO_MASQUERADE_', `dnl', `dnl
R$*			$: $>MasqHdr $1		do masquerading')

#
#  Header recipient rewriting
#
SHdrToL
R$+			$: $>AddDomain $1	add local domain if needed
ifdef(`_ALL_MASQUERADE_', `dnl
ifdef(`_LOCAL_NO_MASQUERADE_', `dnl', `dnl
R$*			$: $>MasqHdr $1		do all-masquerading')',
`R$* < @ *LOCAL* > $*	$: $1 < @ $j . > $2')

#
#  Common code to add local domain name (only if always-add-domain)
#
SAddDomain
ifdef(`_ALWAYS_ADD_DOMAIN_', `dnl
R$* < @ $* > $* 	$@ $1 < @ $2 > $3	already fully qualified
ifelse(len(X`'_ALWAYS_ADD_DOMAIN_),`1',`
R$+			$@ $1 < @ *LOCAL* >	add local qualification',
`R$+			$@ $1 < @ _ALWAYS_ADD_DOMAIN_ >	add qualification')',
`dnl')

Mlocal,		P=LOCAL_MAILER_PATH, F=_MODMF_(CONCAT(_DEF_LOCAL_MAILER_FLAGS, LOCAL_MAILER_FLAGS), `LOCAL'), LOCAL_RWR,_OPTINS(`LOCAL_MAILER_EOL', ` E=', `, ')
		_OPTINS(`LOCAL_MAILER_MAX', `M=', `, ')_OPTINS(`LOCAL_MAILER_MAXMSGS', `m=', `, ')_OPTINS(`LOCAL_MAILER_MAXRCPTS', `r=', `, ')_OPTINS(`LOCAL_MAILER_CHARSET', `C=', `, ')T=DNS/RFC822/LOCAL_MAILER_DSN_DIAGNOSTIC_CODE,_LOCAL_QGRP
		A=LOCAL_MAILER_ARGS
Mprog,		P=LOCAL_SHELL_PATH, F=_MODMF_(CONCAT(_DEF_LOCAL_SHELL_FLAGS, LOCAL_SHELL_FLAGS), `SHELL'), S=EnvFromL/HdrFromL, R=EnvToL/HdrToL, D=LOCAL_SHELL_DIR,
		_OPTINS(`LOCAL_MAILER_MAX', `M=', `, ')T=X-Unix/X-Unix/X-Unix,_PROG_QGRP
		A=LOCAL_SHELL_ARGS
