divert(-1)
#
# Copyright (c) 1998, 1999 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
# Copyright (c) 1996 Eric P. Allman.  All rights reserved.
# Copyright (c) 1988, 1993
#	The Regents of the University of California.  All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
VERSIONID(`$Id: sinix.m4,v 8.14 2013-11-22 20:51:15 ca Exp $')
ifdef(`QUEUE_DIR',, `define(`QUEUE_DIR', /var/spool/mqueue)')dnl
define(`LOCAL_MAILER_PATH', `/bin/mail.local')dnl
ifdef(`STATUS_FILE',, `define(`STATUS_FILE', `/var/sendmail.st')')dnl
define(`confEBINDIR', `/usr/ucblib')dnl
