divert(-1)
#
# Copyright (c) 1998-2000, 2002 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
VERSIONID(`$Id: local_lmtp.m4,v 8.18 2013-11-22 20:51:11 ca Exp $')
divert(-1)

ifdef(`_MAILER_local_',
	`errprint(`*** FEATURE(local_lmtp) must occur before MAILER(local)
')')dnl

define(`LOCAL_MAILER_PATH',
	ifelse(defn(`_ARG_'), `',
		ifdef(`confEBINDIR', confEBINDIR, `/usr/libexec')`/mail.local',
		_ARG_))
define(`LOCAL_MAILER_FLAGS', `PSXmnz9')
define(`LOCAL_MAILER_ARGS',
	ifelse(len(X`'_ARG2_), `1', `mail.local -l', _ARG2_))
define(`LOCAL_MAILER_DSN_DIAGNOSTIC_CODE', `SMTP')
define(`_LOCAL_LMTP_', `1')
