divert(-1)
#
# Copyright (c) 2006 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

divert(0)dnl
VERSIONID(`$Id: block_bad_helo.m4,v 1.2 2013-11-22 20:51:11 ca Exp $')
divert(-1)

define(`_BLOCK_BAD_HELO_', `')dnl
RELAY_DOMAIN(`127.0.0.1')dnl
RELAY_DOMAIN(`IPv6:0:0:0:0:0:0:0:1 IPv6:::1')dnl
LOCAL_DOMAIN(`[127.0.0.1]')dnl
LOCAL_DOMAIN(`[IPv6:0:0:0:0:0:0:0:1] [IPv6:::1]')dnl
