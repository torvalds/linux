divert(-1)
#
# Copyright (c) 2000 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#

divert(0)
VERSIONID(`$Id: local_no_masquerade.m4,v 1.3 2013-11-22 20:51:11 ca Exp $')
divert(-1)

ifdef(`_MAILER_local_',
	`errprint(`*** MAILER(`local') must appear after FEATURE(`local_no_masquerade')')
')dnl
define(`_LOCAL_NO_MASQUERADE_', `1')
