divert(-1)
#
# Copyright (c) 1998-2000 Proofpoint, Inc. and its suppliers.
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
VERSIONID(`$Id: bestmx_is_local.m4,v 8.27 2013-11-22 20:51:11 ca Exp $')
divert(-1)

define(`_BESTMX_IS_LOCAL_', _ARG_)

LOCAL_CONFIG
# turn on bestMX lookup table
Kbestmx bestmx
ifelse(defn(`_ARG_'), `', `dnl',`
# limit bestmx to these domains
CB`'_ARG_')

LOCAL_NET_CONFIG

# If we are the best MX for a site, then we want to accept
# its mail as local.  We assume we've already weeded out mail to
# UUCP sites which are connected to us, which should also have
# listed us as their best MX.
#
# Warning: this may generate a lot of extra DNS traffic -- a
# lower cost method is to list all the expected best MX hosts
# in $=w.  This should be fine (and easier to administer) for
# low to medium traffic hosts.  If you use the limited bestmx
# by passing in a set of possible domains it will improve things.

ifelse(defn(`_ARG_'), `', `dnl
# unlimited bestmx
R$* < @ $* > $*			$: $1 < @ $2 @@ $(bestmx $2 $) > $3',
`dnl
# limit bestmx to $=B
R$* < @ $* $=B . > $*		$: $1 < @ $2 $3 . @@ $(bestmx $2 $3 . $) > $4')
R$* $=O $* < @ $* @@ $=w . > $*	$@ $>Recurse $1 $2 $3
R< @ $* @@ $=w . > : $*		$@ $>Recurse $3
dnl we cannot use _LOCAL_ here since it is defined too late
R$* < @ $* @@ $=w . > $*	$@ $>CanonLocal < $1 >
R$* < @ $* @@ $* > $*		$: $1 < @ $2 > $4
