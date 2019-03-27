PUSHDIVERT(-1)
#
# Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.
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

ifdef(`UUCP_MAILER_PATH',, `define(`UUCP_MAILER_PATH', /usr/bin/uux)')
ifdef(`UUCP_MAILER_ARGS',, `define(`UUCP_MAILER_ARGS', `uux - -r -a$g -gC $h!rmail ($u)')')
_DEFIFNOT(`UUCP_MAILER_FLAGS', `')
ifdef(`UUCP_MAILER_MAX',,
	`define(`UUCP_MAILER_MAX',
		`ifdef(`UUCP_MAX_SIZE', `UUCP_MAX_SIZE', 100000)')')
define(`_UUCP_QGRP', `ifelse(defn(`UUCP_MAILER_QGRP'),`',`', ` Q=UUCP_MAILER_QGRP,')')dnl
POPDIVERT
#####################################
###   UUCP Mailer specification   ###
#####################################

VERSIONID(`$Id: uucp.m4,v 8.45 2013-11-22 20:51:14 ca Exp $')

#
#  envelope and header sender rewriting
#
SFromU

# handle error address as a special case
R<@>				$n			errors to mailer-daemon

# list:; syntax should disappear
R:; <@>				$@

R$* < @ $* . > $*		$1 < @ $2 > $3		strip trailing dots
R$* < @ $=w >			$1			strip local name
R<@ $- . UUCP > : $+		$1 ! $2			convert to UUCP format
R<@ $+ > : $+			$1 ! $2			convert to UUCP format
R$* < @ $- . UUCP >		$2 ! $1			convert to UUCP format
R$* < @ $+ >			$2 ! $1			convert to UUCP format
R$&h ! $+ ! $+			$@ $1 ! $2		$h!...!user => ...!user
R$&h ! $+			$@ $&h ! $1		$h!user => $h!user
R$+				$: $U ! $1		prepend our name
R! $+				$: $k ! $1		in case $U undefined

#
#  envelope recipient rewriting
#
SEnvToU

# list:; should disappear
R:; <@>				$@

R$* < @ $* . > $*		$1 < @ $2 > $3		strip trailing dots
R$* < @ $=w >			$1			strip local name
R<@ $- . UUCP > : $+		$1 ! $2			convert to UUCP format
R<@ $+ > : $+			$1 ! $2			convert to UUCP format
R$* < @ $- . UUCP >		$2 ! $1			convert to UUCP format
R$* < @ $+ >			$2 ! $1			convert to UUCP format

#
#  header recipient rewriting
#
SHdrToU

# list:; syntax should disappear
R:; <@>				$@

R$* < @ $* . > $*		$1 < @ $2 > $3		strip trailing dots
R$* < @ $=w >			$1			strip local name
R<@ $- . UUCP > : $+		$1 ! $2			convert to UUCP format
R<@ $+ > : $+			$1 ! $2			convert to UUCP format
R$* < @ $- . UUCP >		$2 ! $1			convert to UUCP format
R$* < @ $+ >			$2 ! $1			convert to UUCP format
R$&h ! $+ ! $+			$@ $1 ! $2		$h!...!user => ...!user
R$&h ! $+			$@ $&h ! $1		$h!user => $h!user
R$+				$: $U ! $1		prepend our name
R! $+				$: $k ! $1		in case $U undefined


ifdef(`_MAILER_smtp_',
`#
#  envelope sender rewriting for uucp-dom mailer
#
SEnvFromUD

# handle error address as a special case
R<@>				$n			errors to mailer-daemon

# pass everything to standard SMTP mailer rewriting
R$*				$@ $>EnvFromSMTP $1

#
#  envelope sender rewriting for uucp-uudom mailer
#
SEnvFromUUD

# handle error address as a special case
R<@>				$n			errors to mailer-daemon

# do standard SMTP mailer rewriting
R$*				$: $>EnvFromSMTP $1

R$* < @ $* . > $*		$1 < @ $2 > $3		strip trailing dots
R<@ $- . UUCP > : $+		$@ $1 ! $2		convert to UUCP format
R<@ $+ > : $+			$@ $1 ! $2		convert to UUCP format
R$* < @ $- . UUCP >		$@ $2 ! $1		convert to UUCP format
R$* < @ $+ >			$@ $2 ! $1		convert to UUCP format',
`errprint(`*** MAILER(`smtp') must appear before MAILER(`uucp')
    if uucp-dom should be included.')
')

PUSHDIVERT(4)
# resolve locally connected UUCP links
R$* < @ $=Z . UUCP. > $*	$#uucp-uudom $@ $2 $: $1 < @ $2 .UUCP. > $3
R$* < @ $=Y . UUCP. > $*	$#uucp-new $@ $2 $: $1 < @ $2 .UUCP. > $3
R$* < @ $=U . UUCP. > $*	$#uucp-old $@ $2 $: $1 < @ $2 .UUCP. > $3
POPDIVERT

#
#  There are innumerable variations on the UUCP mailer.  It really
#  is rather absurd.
#

# old UUCP mailer (two names)
Muucp,		P=UUCP_MAILER_PATH, F=_MODMF_(CONCAT(`DFMhuUd', UUCP_MAILER_FLAGS), `UUCP'), S=FromU, R=EnvToU/HdrToU,
		M=UUCP_MAILER_MAX, _OPTINS(`UUCP_MAILER_CHARSET', `C=', `, ')T=X-UUCP/X-UUCP/X-Unix,_UUCP_QGRP
		A=UUCP_MAILER_ARGS
Muucp-old,	P=UUCP_MAILER_PATH, F=_MODMF_(CONCAT(`DFMhuUd', UUCP_MAILER_FLAGS), `UUCP'), S=FromU, R=EnvToU/HdrToU,
		M=UUCP_MAILER_MAX, _OPTINS(`UUCP_MAILER_CHARSET', `C=', `, ')T=X-UUCP/X-UUCP/X-Unix,_UUCP_QGRP
		A=UUCP_MAILER_ARGS

# smart UUCP mailer (handles multiple addresses) (two names)
Msuucp,		P=UUCP_MAILER_PATH, F=_MODMF_(CONCAT(`mDFMhuUd', UUCP_MAILER_FLAGS), `UUCP'), S=FromU, R=EnvToU/HdrToU,
		M=UUCP_MAILER_MAX, _OPTINS(`UUCP_MAILER_CHARSET', `C=', `, ')T=X-UUCP/X-UUCP/X-Unix,_UUCP_QGRP
		A=UUCP_MAILER_ARGS
Muucp-new,	P=UUCP_MAILER_PATH, F=_MODMF_(CONCAT(`mDFMhuUd', UUCP_MAILER_FLAGS), `UUCP'), S=FromU, R=EnvToU/HdrToU,
		M=UUCP_MAILER_MAX, _OPTINS(`UUCP_MAILER_CHARSET', `C=', `, ')T=X-UUCP/X-UUCP/X-Unix,_UUCP_QGRP
		A=UUCP_MAILER_ARGS

ifdef(`_MAILER_smtp_',
`# domain-ized UUCP mailer
Muucp-dom,	P=UUCP_MAILER_PATH, F=_MODMF_(CONCAT(`mDFMhud', UUCP_MAILER_FLAGS), `UUCP'), S=EnvFromUD/HdrFromSMTP, R=ifdef(`_ALL_MASQUERADE_', `EnvToSMTP/HdrFromSMTP', `EnvToSMTP'),
		M=UUCP_MAILER_MAX, _OPTINS(`UUCP_MAILER_CHARSET', `C=', `, ')T=X-UUCP/X-UUCP/X-Unix,_UUCP_QGRP
		A=UUCP_MAILER_ARGS

# domain-ized UUCP mailer with UUCP-style sender envelope
Muucp-uudom,	P=UUCP_MAILER_PATH, F=_MODMF_(CONCAT(`mDFMhud', UUCP_MAILER_FLAGS), `UUCP'), S=EnvFromUUD/HdrFromSMTP, R=ifdef(`_ALL_MASQUERADE_', `EnvToSMTP/HdrFromSMTP', `EnvToSMTP'),
		M=UUCP_MAILER_MAX, _OPTINS(`UUCP_MAILER_CHARSET', `C=', `, ')T=X-UUCP/X-UUCP/X-Unix,_UUCP_QGRP
		A=UUCP_MAILER_ARGS')


