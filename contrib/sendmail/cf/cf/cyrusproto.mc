divert(-1)
#
#	(C) Copyright 1995 by Carnegie Mellon University
#
#                      All Rights Reserved
#
# Permission to use, copy, modify, and distribute this software and its
# documentation for any purpose and without fee is hereby granted,
# provided that the above copyright notice appear in all copies and that
# both that copyright notice and this permission notice appear in
# supporting documentation, and that the name of CMU not be
# used in advertising or publicity pertaining to distribution of the
# software without specific, written prior permission.
#
# CMU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
# ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
# CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
# ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
# WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
# ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
# SOFTWARE.
#
#	Contributed to Berkeley by John Gardiner Myers <jgm+@CMU.EDU>.
#
#	This sample mc file is for a site that uses the Cyrus IMAP server
#	exclusively for local mail.
#

divert(0)dnl
VERSIONID(`$Id: cyrusproto.mc,v 8.7 1999-09-07 14:57:10 ca Exp $')
define(`confBIND_OPTS',`-DNSRCH -DEFNAMES')
define(`confLOCAL_MAILER', `cyrus')
FEATURE(`nocanonify')
FEATURE(`always_add_domain')
MAILER(`local')
MAILER(`smtp')
MAILER(`cyrus')

LOCAL_RULE_0
Rbb + $+ < @ $=w . >	$#cyrusbb $: $1
