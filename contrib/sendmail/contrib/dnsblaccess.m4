divert(-1)
#
# Copyright (c) 2001-2002, 2005 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

dnl ##	This is a modified enhdnsbl, loosely based on the
dnl ##	original.
dnl ##
dnl ##	Use it as follows
dnl ##
dnl ##	HACK(dnsblaccess, domain, optional-message, tempfail-message, keytag)
dnl ##
dnl ##	The first argument (domain) is required.  The other arguments
dnl ##	are optional and have reasonable defaults.  The
dnl ##	optional-message is the error message given in case of a
dnl ##	match.  The default behavior for a tempfail is to accept the
dnl ##	email.  A tempfail-message value of `t' temporarily rejects
dnl ##	with a default message.  Otherwise the value should be your
dnl ##	own message.  The keytag is used to lookup the access map to
dnl ##	further refine the result.  I recommend a qualified keytag
dnl ##	(containing a ".") as less likely to accidently conflict with
dnl ##	other access tags.
dnl ##
dnl ##	This is best illustrated with an example.  Please do not use
dnl ##	the example, as it refers to a bogus lookup list.
dnl ##
dnl ##	Suppose that you use
dnl ##
dnl ##	HACK(dnsblaccess, `rbl.bogus.org',`',`t',bogus.tag)
dnl ##
dnl ##	and suppose that your access map contains the entries
dnl ##
dnl ##	bogus.tag:127.0.0.2	REJECT
dnl ##	bogus.tag:127.0.0.3	error:dialup mail from %1: listed at %2
dnl ##	bogus.tag:127.0.0.4	OK
dnl ##	bogus.tag:127		REJECT
dnl ##	bogus.tag:		OK
dnl ##
dnl ##	If an SMTP connection is received from 123.45.6.7, sendmail
dnl ##	will lookup the A record for 7.6.45.123.bogus.org.  If there
dnl ##	is a temp failure for the lookup, sendmail will generate a
dnl ##	temporary failure with a default message.  If there is no
dnl ##	A-record for this lookup, then the mail is treated as if the
dnl ##	HACK line were not present.  If the lookup returns 127.0.0.2,
dnl ##	then a default message rejects the mail.  If it returns
dnl ##	127.0.0.3, then the message
dnl ##	"dialup mail from 123.45.6.7: listed at rbl.bogus.org"
dnl ##	is used to reject the mail.  If it returns 127.0.0.4, the
dnl ##	mail is processed as if there were no HACK line.  If the
dnl ##	address returned is something else beginning with 127.*, the
dnl ##	mail is rejected with a default error message.  If the
dnl ##	address returned does not begin 127, then the mail is
dnl ##	processed as if the HACK line were not present.

divert(0)
VERSIONID(`$Id: dnsblaccess.m4,v 1.7 2013-11-22 20:51:18 ca Exp $')
ifdef(`_ACCESS_TABLE_', `dnl',
	`errprint(`*** ERROR: dnsblaccess requires FEATURE(`access_db')
')')
ifdef(`_EDNSBL_R_',`dnl',`dnl
define(`_EDNSBL_R_', `1')dnl ## prevent multiple redefines of the map.
LOCAL_CONFIG
# map for enhanced DNS based blacklist lookups
Kednsbl dns -R A -a. -T<TMP> -r`'ifdef(`EDNSBL_TO',`EDNSBL_TO',`5')
')
divert(-1)
define(`_EDNSBL_SRV_', `ifelse(len(X`'_ARG_),`1',`blackholes.mail-abuse.org',_ARG_)')dnl
define(`_EDNSBL_MSG_', `ifelse(len(X`'_ARG2_),`1',`"550 Rejected: " $`'&{client_addr} " listed at '_EDNSBL_SRV_`"',`_ARG2_')')dnl
define(`_EDNSBL_MSG_TMP_', `ifelse(_ARG3_,`t',`"451 Temporary lookup failure of " $`'&{client_addr} " at '_EDNSBL_SRV_`"',`_ARG3_')')dnl
define(`_EDNSBL_KEY_', `ifelse(len(X`'_ARG4_),`1',`dnsblaccess',_ARG4_)')dnl
divert(8)
# DNS based IP address spam list _EDNSBL_SRV_
R$*			$: $&{client_addr}
dnl IPv6?
R$-.$-.$-.$-		$: <?> $(ednsbl $4.$3.$2.$1._EDNSBL_SRV_. $: OK $) <>$1.$2.$3.$4
R<?>OK<>$*		$: OKSOFAR
R<?>$+<TMP><>$*		$: <? <TMPF>>
R<?>$* $- .<>$*		<$(access _EDNSBL_KEY_`:'$1$2 $@$3 $@`'_EDNSBL_SRV_ $: ? $)> $1 <>$3
R<?>$* <>$*		$:<$(access _EDNSBL_KEY_`:' $@$2 $@`'_EDNSBL_SRV_ $: ? $)> <>$2
ifelse(len(X`'_ARG3_),`1',
`R<$*<TMPF>>$*		$: TMPOK',
`R<$*<TMPF>>$*		$#error $@ 4.4.3 $: _EDNSBL_MSG_TMP_')
R<$={Accept}>$*		$: OKSOFAR
R<ERROR:$-.$-.$-:$+> $*	$#error $@ $1.$2.$3 $: $4
R<ERROR:$+> $*		$#error $: $1
R<DISCARD> $*		$#discard $: discard
R<$*> $*		$#error $@ 5.7.1 $: _EDNSBL_MSG_
divert(-1)
