divert(-1)
#
# Copyright (c) 1998, 1999 Proofpoint, Inc. and its suppliers.
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
VERSIONID(`$Id: irix4.m4,v 8.20 2013-11-22 20:51:15 ca Exp $')
_DEFIFNOT(`LOCAL_MAILER_FLAGS', `Ehm9')dnl
ifdef(`QUEUE_DIR',, `define(`QUEUE_DIR', /usr/spool/mqueue)')dnl
define(`confEBINDIR', `/usr/lib')dnl
