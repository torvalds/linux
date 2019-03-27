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

#
#  This machine has been decommissioned at Berkeley, and hence should
#  not be considered to be tested.  This file is provided as an example
#  only, of how you might set up a fairly complex configuration.
#  Ucbvax was our main relay (both SMTP and UUCP) for many years.
#  At this point I recommend using `FEATURE(mailertable)' instead of
#  `SITECONFIG' for routing of UUCP within your domain.
#

divert(0)dnl
VERSIONID(`$Id: ucbvax.mc,v 8.15 2013-11-22 20:51:08 ca Exp $')
OSTYPE(bsd4.3)
DOMAIN(CS.Berkeley.EDU)
MASQUERADE_AS(CS.Berkeley.EDU)
MAILER(local)
MAILER(smtp)
MAILER(uucp)
undefine(`UUCP_RELAY')dnl

LOCAL_CONFIG
DDBerkeley.EDU

# names for which we act as a local forwarding agent
CF CS
FF/etc/sendmail.cw

# local UUCP connections, and our local uucp name
SITECONFIG(uucp.ucbvax, ucbvax, U)

# remote UUCP connections, and the machine they are on
SITECONFIG(uucp.ucbarpa, ucbarpa.Berkeley.EDU, W)

SITECONFIG(uucp.cogsci, cogsci.Berkeley.EDU, X)

LOCAL_RULE_3
# map old UUCP names into Internet names
UUCPSMTP(bellcore,	bellcore.com)
UUCPSMTP(decvax,	decvax.dec.com)
UUCPSMTP(decwrl,	decwrl.dec.com)
UUCPSMTP(hplabs,	hplabs.hp.com)
UUCPSMTP(lbl-csam,	lbl-csam.arpa)
UUCPSMTP(pur-ee,	ecn.purdue.edu)
UUCPSMTP(purdue,	purdue.edu)
UUCPSMTP(research,	research.att.com)
UUCPSMTP(sdcarl,	sdcarl.ucsd.edu)
UUCPSMTP(sdcsvax,	sdcsvax.ucsd.edu)
UUCPSMTP(ssyx,		ssyx.ucsc.edu)
UUCPSMTP(sun,		sun.com)
UUCPSMTP(ucdavis,	ucdavis.ucdavis.edu)
UUCPSMTP(ucivax,	ics.uci.edu)
UUCPSMTP(ucla-cs,	cs.ucla.edu)
UUCPSMTP(ucla-se,	seas.ucla.edu)
UUCPSMTP(ucsbcsl,	ucsbcsl.ucsb.edu)
UUCPSMTP(ucscc,		c.ucsc.edu)
UUCPSMTP(ucsd,		ucsd.edu)
UUCPSMTP(ucsfcgl,	cgl.ucsf.edu)
UUCPSMTP(unmvax,	unmvax.cs.unm.edu)
UUCPSMTP(uwvax,		spool.cs.wisc.edu)

LOCAL_RULE_0

# make sure we handle the local domain as absolute
R$* <  @ $* $D > $*		$: $1 < @ $2 $D . > $3

# handle names we forward for as though they were local, so we will use UDB
R< @ $=F . $D . > : $*		$@ $>7 $2		@here:... -> ...
R< @ $D . > : $*		$@ $>7 $1		@here:... -> ...
R$* $=O $* < @ $=F . $D . >	$@ $>7 $1 $2 $3		...@here -> ...
R$* $=O $* < @ $D . >		$@ $>7 $1 $2 $3		...@here -> ...

R$* < @ $=F . $D . >		$#local $: $1		use UDB

# handle local UUCP connections in the Berkeley.EDU domain
R$+<@cnmat.$D . >		$#uucp$@cnmat$:$1
R$+<@cnmat.CS.$D . >		$#uucp$@cnmat$:$1
R$+<@craig.$D . >		$#uucp$@craig$:$1
R$+<@craig.CS.$D . >		$#uucp$@craig$:$1
