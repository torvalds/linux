divert(-1)
#
# Copyright (c) 2004 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)
VERSIONID(`$Id: use_client_ptr.m4,v 1.2 2013-11-22 20:51:11 ca Exp $')
divert(-1)

# if defined, check_relay will use {client_ptr} instead of whatever
# is passed in as its first argument.

define(`_USE_CLIENT_PTR_', `1')

divert(0)
