divert(-1)
#
# Copyright (c) 2004, 2005 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
ifdef(`_MTAMARK_R',`dnl',`dnl
VERSIONID(`$Id: mtamark.m4,v 1.3 2013-11-22 20:51:11 ca Exp $')
LOCAL_CONFIG
define(`_MTAMARK_R',`')dnl
# map for MTA mark
Kmtamark dns -R TXT -a. -T<TMP> -r`'ifdef(`MTAMARK_TO',`MTAMARK_TO',`5')
')
divert(-1)
define(`_MTAMARK_RR_', `ifelse(len(X`'_ARG3_),`1',`_perm._smtp._srv',`_ARG3_')')dnl
define(`_MTAMARK_MSG_', `ifelse(len(X`'_ARG_),`1',`"550 Rejected: " $`'&{client_addr} " not listed as MTA"',`_ARG_')')dnl
define(`_MTAMARK_MSG_TMP_', `ifelse(_ARG2_,`t',`"451 Temporary lookup failure of " _MTAMARK_RR_.$`'&{client_addr}',`_ARG2_')')dnl
divert(8)
# DNS based IP MTA list
R$*		$: $&{client_addr}
R$-.$-.$-.$-	$: <?> $(mtamark _MTAMARK_RR_.$4.$3.$2.$1.in-addr.arpa. $: OK $)
R<?>1.		$: OKSOFAR
R<?>0.		$#error $@ 5.7.1 $: _MTAMARK_MSG_
ifelse(len(X`'_ARG2_),`1',
`R<?>$+<TMP>	$: TMPOK',
`R<?>$+<TMP>	$#error $@ 4.4.3 $: _MTAMARK_MSG_TMP_')
divert(-1)
