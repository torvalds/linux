divert(-1)
#
# Copyright (c) 2014 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(-1)
# Arguments:
# 1: Map to use
#   - empty/none: default map bcctable
#   - `access': to use access_db (with bcc: as tag)
#   - map definition
#   The map contains domain names and the RHS should be simply "ok".
#   If the access map is used, then its lookup algorithm is used.
#   Otherwise:
#    domain	ok
#   matches anything@domain
#    .domain	ok
#   matches any subdomain, e.g., l@sub.domain and l@sub.dom.domain
#   On a match, the original address will be used as bcc address unless
#   argument 3 is set.
# 2: Name of host ([mailer:]host)
# 3: Default bcc address: if set, this will be always used.
#   Only one of 2/3 can be empty.
#   Note: if Bcc address is used then only one copy will be sent!
#   (due to duplicate elimination)
# 4: Map definition for canonicalRcpt map of address rewriting to
#   apply to the added bcc envelope recipients.
#   The option -T<TMPF> is required to handle temporary map failures.
#
# The ruleset must return either
# - an e-mail address (user@dom.ain) which is then added as "bcc" recipient.
# - an empty string: do not add a "bcc" recipient, or
# - $#error: fail the SMTP transaction (e.g., temporary lookup failure)
#
# This feature sets O AddBcc=true

ifelse(lower(_ARG_),`access',`define(`_BCC_ACCESS_', `1')')
define(`_ADD_BCC_', `1')

ifdef(`_BCC_ACCESS_', `dnl
ifdef(`_ACCESS_TABLE_', `',
	`errprint(`*** ERROR: FEATURE(`bcc') requires FEATURE(`access_db')
')')')

ifdef(`_BCC_ACCESS_', `', `
LOCAL_CONFIG
Kbcctable ifelse(defn(`_ARG_'), `', DATABASE_MAP_TYPE MAIL_SETTINGS_DIR`bcctable', `_ARG_')')

LOCAL_CONFIG
O AddBcc=true
ifelse(len(X`'_ARG2_),`1', `', `
DA`'_ARG2_')

ifelse(len(X`'_ARG4_), `1', `',
`define(`_CANONIFY_BCC_', `1')dnl
define(`_NEED_SMTPOPMODES_', `1')dnl
# canonical address look up for AddBcc recipients
KcanonicalRcpt _ARG4_
')dnl

LOCAL_RULESETS
Sbcc
R< $+ >			$1
ifdef(`_BCC_ACCESS_', `dnl
R$+ @ $+		$: $1@$2 $| $>SearchList <! bcc> $| <D:$2> <>',
`R$+ @ $+		$: $1@$2 $| $>BCC $2')
R$* $| <?>		$@
R$* $| $*		$: ifelse(len(X`'_ARG3_),`1', `$1', `_ARG3_')

ifdef(`_CANONIFY_BCC_', `dnl
R$+ @ $+		$: $1@$2 $| <$(canonicalRcpt $1 @ $2 $: $)>
R$* $| <>		$@
R$* $| <$* <TMPF>>	$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."
R$* $| <$+>		$@ $2			map matched?
')


ifdef(`_BCC_ACCESS_', `', `
SBCC
R$+		$: $1 < $(bcctable $1 $: ? $) >
R$- . $+ <?>	$: $2 < $(bcctable .$2 $: ? $) >
R$- . $+ <?>	$: $>BCC $2
R$* <$*>	$: <$2>
')
