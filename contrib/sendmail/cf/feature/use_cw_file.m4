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
VERSIONID(`$Id: use_cw_file.m4,v 8.12 2013-11-22 20:51:11 ca Exp $')
divert(-1)

# if defined, the sendmail.cf will read the /etc/mail/local-host-names file
# to find alternate names for this host.  Typically only used when several
# hosts have been squashed into one another at high speed.

define(`USE_CW_FILE', `')

divert(0)
