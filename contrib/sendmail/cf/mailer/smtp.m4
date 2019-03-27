PUSHDIVERT(-1)
#
# Copyright (c) 1998-2001, 2006 Proofpoint, Inc. and its suppliers.
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
_DEFIFNOT(`_DEF_SMTP_MAILER_FLAGS', `mDFMuX')
_DEFIFNOT(`SMTP_MAILER_FLAGS',`')
_DEFIFNOT(`SMTP_MAILER_LL',`990')
_DEFIFNOT(`RELAY_MAILER_LL',`2040')
_DEFIFNOT(`RELAY_MAILER_FLAGS', `SMTP_MAILER_FLAGS')
ifdef(`SMTP_MAILER_ARGS',, `define(`SMTP_MAILER_ARGS', `TCP $h')')
ifdef(`ESMTP_MAILER_ARGS',, `define(`ESMTP_MAILER_ARGS', `TCP $h')')
ifdef(`SMTP8_MAILER_ARGS',, `define(`SMTP8_MAILER_ARGS', `TCP $h')')
ifdef(`DSMTP_MAILER_ARGS',, `define(`DSMTP_MAILER_ARGS', `TCP $h')')
ifdef(`RELAY_MAILER_ARGS',, `define(`RELAY_MAILER_ARGS', `TCP $h')')
define(`_SMTP_QGRP', `ifelse(defn(`SMTP_MAILER_QGRP'),`',`', ` Q=SMTP_MAILER_QGRP,')')dnl
define(`_ESMTP_QGRP', `ifelse(defn(`ESMTP_MAILER_QGRP'),`',`', ` Q=ESMTP_MAILER_QGRP,')')dnl
define(`_SMTP8_QGRP', `ifelse(defn(`SMTP8_MAILER_QGRP'),`',`', ` Q=SMTP8_MAILER_QGRP,')')dnl
define(`_DSMTP_QGRP', `ifelse(defn(`DSMTP_MAILER_QGRP'),`',`', ` Q=DSMTP_MAILER_QGRP,')')dnl
define(`_RELAY_QGRP', `ifelse(defn(`RELAY_MAILER_QGRP'),`',`', ` Q=RELAY_MAILER_QGRP,')')dnl
POPDIVERT
#####################################
###   SMTP Mailer specification   ###
#####################################

VERSIONID(`$Id: smtp.m4,v 8.66 2013-11-22 20:51:14 ca Exp $')

#
#  common sender and masquerading recipient rewriting
#
SMasqSMTP
R$* < @ $* > $*		$@ $1 < @ $2 > $3		already fully qualified
R$+			$@ $1 < @ *LOCAL* >		add local qualification

#
#  convert pseudo-domain addresses to real domain addresses
#
SPseudoToReal

# pass <route-addr>s through
R< @ $+ > $*		$@ < @ $1 > $2			resolve <route-addr>

# output fake domains as user%fake@relay
ifdef(`BITNET_RELAY',
`R$+ <@ $+ .BITNET. >	$: $1 % $2 .BITNET < @ $B >	user@host.BITNET
R$+.BITNET <@ $~[ $*:$+ >	$: $1 .BITNET < @ $4 >	strip mailer: part',
	`dnl')
ifdef(`_NO_UUCP_', `dnl', `
# do UUCP heuristics; note that these are shared with UUCP mailers
R$+ < @ $+ .UUCP. >	$: < $2 ! > $1			convert to UUCP form
R$+ < @ $* > $*		$@ $1 < @ $2 > $3		not UUCP form

# leave these in .UUCP form to avoid further tampering
R< $&h ! > $- ! $+	$@ $2 < @ $1 .UUCP. >
R< $&h ! > $-.$+ ! $+	$@ $3 < @ $1.$2 >
R< $&h ! > $+		$@ $1 < @ $&h .UUCP. >
R< $+ ! > $+		$: $1 ! $2 < @ $Y >		use UUCP_RELAY
R$+ < @ $~[ $* : $+ >	$@ $1 < @ $4 >			strip mailer: part
R$+ < @ >		$: $1 < @ *LOCAL* >		if no UUCP_RELAY')


#
#  envelope sender rewriting
#
SEnvFromSMTP
R$+			$: $>PseudoToReal $1		sender/recipient common
R$* :; <@>		$@				list:; special case
R$*			$: $>MasqSMTP $1		qualify unqual'ed names
R$+			$: $>MasqEnv $1			do masquerading


#
#  envelope recipient rewriting --
#  also header recipient if not masquerading recipients
#
SEnvToSMTP
R$+			$: $>PseudoToReal $1		sender/recipient common
R$+			$: $>MasqSMTP $1		qualify unqual'ed names
R$* < @ *LOCAL* > $*	$: $1 < @ $j . > $2

#
#  header sender and masquerading header recipient rewriting
#
SHdrFromSMTP
R$+			$: $>PseudoToReal $1		sender/recipient common
R:; <@>			$@				list:; special case

# do special header rewriting
R$* <@> $*		$@ $1 <@> $2			pass null host through
R< @ $* > $*		$@ < @ $1 > $2			pass route-addr through
R$*			$: $>MasqSMTP $1		qualify unqual'ed names
R$+			$: $>MasqHdr $1			do masquerading


#
#  relay mailer header masquerading recipient rewriting
#
SMasqRelay
R$+			$: $>MasqSMTP $1
R$+			$: $>MasqHdr $1

Msmtp,		P=[IPC], F=_MODMF_(CONCAT(_DEF_SMTP_MAILER_FLAGS, SMTP_MAILER_FLAGS), `SMTP'), S=EnvFromSMTP/HdrFromSMTP, R=ifdef(`_ALL_MASQUERADE_', `EnvToSMTP/HdrFromSMTP', `EnvToSMTP'), E=\r\n, L=SMTP_MAILER_LL,
		_OPTINS(`SMTP_MAILER_MAX', `M=', `, ')_OPTINS(`SMTP_MAILER_MAXMSGS', `m=', `, ')_OPTINS(`SMTP_MAILER_MAXRCPTS', `r=', `, ')_OPTINS(`SMTP_MAILER_CHARSET', `C=', `, ')T=DNS/RFC822/SMTP,_SMTP_QGRP
		A=SMTP_MAILER_ARGS
Mesmtp,		P=[IPC], F=_MODMF_(CONCAT(_DEF_SMTP_MAILER_FLAGS, `a', SMTP_MAILER_FLAGS), `ESMTP'), S=EnvFromSMTP/HdrFromSMTP, R=ifdef(`_ALL_MASQUERADE_', `EnvToSMTP/HdrFromSMTP', `EnvToSMTP'), E=\r\n, L=SMTP_MAILER_LL,
		_OPTINS(`SMTP_MAILER_MAX', `M=', `, ')_OPTINS(`SMTP_MAILER_MAXMSGS', `m=', `, ')_OPTINS(`SMTP_MAILER_MAXRCPTS', `r=', `, ')_OPTINS(`SMTP_MAILER_CHARSET', `C=', `, ')T=DNS/RFC822/SMTP,_ESMTP_QGRP
		A=ESMTP_MAILER_ARGS
Msmtp8,		P=[IPC], F=_MODMF_(CONCAT(_DEF_SMTP_MAILER_FLAGS, `8', SMTP_MAILER_FLAGS), `SMTP8'), S=EnvFromSMTP/HdrFromSMTP, R=ifdef(`_ALL_MASQUERADE_', `EnvToSMTP/HdrFromSMTP', `EnvToSMTP'), E=\r\n, L=SMTP_MAILER_LL,
		_OPTINS(`SMTP_MAILER_MAX', `M=', `, ')_OPTINS(`SMTP_MAILER_MAXMSGS', `m=', `, ')_OPTINS(`SMTP_MAILER_MAXRCPTS', `r=', `, ')_OPTINS(`SMTP_MAILER_CHARSET', `C=', `, ')T=DNS/RFC822/SMTP,_SMTP8_QGRP
		A=SMTP8_MAILER_ARGS
Mdsmtp,		P=[IPC], F=_MODMF_(CONCAT(_DEF_SMTP_MAILER_FLAGS, `a%', SMTP_MAILER_FLAGS), `DSMTP'), S=EnvFromSMTP/HdrFromSMTP, R=ifdef(`_ALL_MASQUERADE_', `EnvToSMTP/HdrFromSMTP', `EnvToSMTP'), E=\r\n, L=SMTP_MAILER_LL,
		_OPTINS(`SMTP_MAILER_MAX', `M=', `, ')_OPTINS(`SMTP_MAILER_MAXMSGS', `m=', `, ')_OPTINS(`SMTP_MAILER_MAXRCPTS', `r=', `, ')_OPTINS(`SMTP_MAILER_CHARSET', `C=', `, ')T=DNS/RFC822/SMTP,_DSMTP_QGRP
		A=DSMTP_MAILER_ARGS
Mrelay,		P=[IPC], F=_MODMF_(CONCAT(_DEF_SMTP_MAILER_FLAGS, `a8', RELAY_MAILER_FLAGS), `RELAY'), S=EnvFromSMTP/HdrFromSMTP, R=ifdef(`_ALL_MASQUERADE_', `MasqSMTP/MasqRelay', `MasqSMTP'), E=\r\n, L=RELAY_MAILER_LL,
		_OPTINS(`RELAY_MAILER_CHARSET', `C=', `, ')_OPTINS(`RELAY_MAILER_MAXMSGS', `m=', `, ')_OPTINS(`SMTP_MAILER_MAXRCPTS', `r=', `, ')T=DNS/RFC822/SMTP,_RELAY_QGRP
		A=RELAY_MAILER_ARGS
