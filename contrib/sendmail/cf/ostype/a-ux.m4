divert(-1)
#
# Copyright (c) 1998, 1999, 2001 Proofpoint, Inc. and its suppliers.
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

divert(0)
VERSIONID(`$Id: a-ux.m4,v 8.3 2013-11-22 20:51:15 ca Exp $')
ifdef(`QUEUE_DIR',, `define(`QUEUE_DIR', /usr/spool/mqueue)')dnl
ifdef(`UUCP_MAILER_PATH',, `define(`UUCP_MAILER_PATH', /usr/bin/uux)')dnl
_DEFIFNOT(`LOCAL_MAILER_FLAGS', `mn9')dnl
ifdef(`LOCAL_MAILER_ARGS',, `define(`LOCAL_MAILER_ARGS', `mail -d -r $f $u')')dnl
define(`confEBINDIR', `/usr/lib')dnl
