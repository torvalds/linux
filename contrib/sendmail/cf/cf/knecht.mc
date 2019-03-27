divert(-1)
#
# Copyright (c) 1998-2001, 2004, 2005 Proofpoint, Inc. and its suppliers.
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
#  This is specific to Eric's home machine.
#
#	Run daemon with -bd -q5m
#

divert(0)
VERSIONID(`$Id: knecht.mc,v 8.63 2013-11-22 20:51:08 ca Exp $')
OSTYPE(bsd4.4)
DOMAIN(generic)

define(`ALIAS_FILE', ``/etc/mail/aliases, /etc/mail/lists/sendmail.org/aliases, /var/listmanager/aliases'')
define(`confFORWARD_PATH', `$z/.forward.$w:$z/.forward+$h:$z/.forward')
define(`confDEF_USER_ID', `mailnull')
define(`confHOST_STATUS_DIRECTORY', `.hoststat')
define(`confTO_ICONNECT', `10s')
define(`confTO_QUEUEWARN', `8h')
define(`confMIN_QUEUE_AGE', `27m')
define(`confTRUSTED_USER', `smtrust')
define(`confTRUSTED_USERS', ``www listmgr'')
define(`confPRIVACY_FLAGS', ``authwarnings,noexpn,novrfy'')

define(`CERT_DIR', `MAIL_SETTINGS_DIR`'certs')
define(`confCACERT_PATH', `CERT_DIR')
define(`confCACERT', `CERT_DIR/CAcert.pem')
define(`confSERVER_CERT', `CERT_DIR/MYcert.pem')
define(`confSERVER_KEY', `CERT_DIR/MYkey.pem')
define(`confCLIENT_CERT', `CERT_DIR/MYcert.pem')
define(`confCLIENT_KEY', `CERT_DIR/MYkey.pem')

define(`CYRUS_MAILER_PATH', `/usr/local/cyrus/bin/deliver')
define(`CYRUS_MAILER_FLAGS', `fAh5@/:|')

FEATURE(`access_db')
FEATURE(`blacklist_recipients')
FEATURE(`local_lmtp')
FEATURE(`virtusertable')
FEATURE(`mailertable')

FEATURE(`nocanonify', `canonify_hosts')
CANONIFY_DOMAIN(`sendmail.org')
CANONIFY_DOMAIN_FILE(`/etc/mail/canonify-domains')

dnl #  at most 10 queue runners
define(`confMAX_QUEUE_CHILDREN', `20')

define(`confMAX_RUNNERS_PER_QUEUE', `5')

dnl #  run at most 10 concurrent processes for initial submission
define(`confFAST_SPLIT', `10')

dnl #  10 runners, split into at most 15 recipients per envelope
QUEUE_GROUP(`mqueue', `P=/var/spool/mqueue, R=5, r=15, F=f')

dnl # enable spam assassin
INPUT_MAIL_FILTER(`spamassassin', `S=local:/var/run/spamass-milter.sock, F=, T=C:15m;S:4m;R:4m;E:10m')

dnl # enable DomainKeys and DKIM
INPUT_MAIL_FILTER(`dkim-filter', `S=unix:/var/run/smtrust/dkim.sock, F=T, T=R:2m')
dnl INPUT_MAIL_FILTER(`dk-filter', `S=unix:/var/run/smtrust/dk.sock, F=T, T=R:2m')

define(`confMILTER_MACROS_CONNECT', `j, {daemon_name}')
define(`confMILTER_MACROS_ENVFROM', `i, {auth_type}')

dnl # enable some DNSBLs
dnl FEATURE(`dnsbl', `dnsbl.sorbs.net', `"550 Mail from " $`'&{client_addr} " refused - see http://www.dnsbl.sorbs.net/"')
FEATURE(`dnsbl', `sbl-xbl.spamhaus.org', `"550 Mail from " $`'&{client_addr} " refused - see http://www.spamhaus.org/sbl/"')
FEATURE(`dnsbl', `list.dsbl.org', `"550 Mail from " $`'&{client_addr} " refused - see http://dsbl.org/"')
FEATURE(`dnsbl', `bl.spamcop.net', `"450 Mail from " $`'&{client_addr} " refused - see http://spamcop.net/bl.shtml"')


MAILER(`local')
MAILER(`smtp')
MAILER(`cyrus')

LOCAL_RULE_0
Rcyrus.$+ + $+ < @ $=w . >	$#cyrus $@ $2 $: $1
Rcyrus.$+ < @ $=w . >		$#cyrus $: $1

LOCAL_CONFIG
#
#  Regular expression to reject:
#    * numeric-only localparts from aol.com and msn.com
#    * localparts starting with a digit from juno.com
#
Kcheckaddress regex -a@MATCH
   ^([0-9]+<@(aol|msn)\.com|[0-9][^<]*<@juno\.com)\.?>

######################################################################
#
#  Names that won't be allowed in a To: line (local-part and domains)
#
C{RejectToLocalparts}	friend you
C{RejectToDomains}	public.com

LOCAL_RULESETS
HTo: $>CheckTo

SCheckTo
R$={RejectToLocalparts}@$*	$#error $: "553 Header error"
R$*@$={RejectToDomains}		$#error $: "553 Header error"

######################################################################
HMessage-Id: $>CheckMessageId

SCheckMessageId
# Record the presence of the header
R$*			$: $(storage {MessageIdCheck} $@ OK $) $1

# validate syntax
R< $+ @ $+ >			$@ OK
R$*				$#error $: "554 Header error"


######################################################################
HReceived: $>CheckReceived

SCheckReceived
# Record the presence of any Received header
R$*			$: $(storage {ReceivedCheck} $@ OK $) $1

# check syntax
R$* ......................................................... $*
				$#error $: "554 Header error"

######################################################################
#
#  Reject advertising subjects
#

Kadvsubj regex -b -a@MATCH ±?°í
HSubject: $>+CheckSubject
SCheckSubject
R$*			$: $(advsubj $&{currHeader} $: OK $)
ROK			$@ OK
R$*			$#error $@ 5.7.0 $: 550 5.7.0 spam rejected.

######################################################################
#
# Reject certain senders
#	Regex match to catch things in quotes
#
HFrom: $>+CheckFrom
KCheckFrom regex -a@MATCH
	[^a-z]?(Net-Pa)[^a-z]

SCheckFrom
R$*				$: $( CheckFrom $1 $)
R@MATCH				$#error $: "553 Header error"

LOCAL_RULESETS
SLocal_check_mail
# check address against various regex checks
R$*				$: $>Parse0 $>3 $1
R$+				$: $(checkaddress $1 $)
R@MATCH				$#error $: "553 Header error"

#
#  Following code from Anthony Howe <achowe@snert.com>.  The check
#  for the Outlook Express marker may hit some legal messages, but
#  the Content-Disposition is clearly illegal.
#

#########################################################################
#
# w32.sircam.worm@mm
#
# There are serveral patterns that appear common ONLY to SirCam worm and
# not to Outlook Express, which claims to have sent the worm.  There are
# four headers that always appear together and in this order:
#
#  X-MIMEOLE: Produced By Microsoft MimeOLE V5.50.4133.2400
#  X-Mailer: Microsoft Outlook Express 5.50.4133.2400
#  Content-Type: multipart/mixed; boundary="----27AA9124_Outlook_Express_message_boundary"
#  Content-Disposition: Multipart message
#
# Empirical study of the worm message headers vs. true Outlook Express
# (5.50.4133.2400 & 5.50.4522.1200) messages with multipart/mixed attachments
# shows Outlook Express does:
#
#  a) NOT supply a Content-Disposition header for multipart/mixed messages.
#  b) NOT specify the header X-MimeOLE header name in all-caps
#  c) NOT specify boundary tag with the expression "_Outlook_Express_message_boundary"
#
# The solution below catches any one of this three issues. This is not an ideal
# solution, but a temporary measure. A correct solution would be to check for
# the presence of ALL three header attributes. Also the solution is incomplete
# since Outlook Express 5.0 and 4.0 were not compared.
#
# NOTE regex keys are first dequoted and spaces removed before matching.
# This caused me no end of grief.
#
#########################################################################

LOCAL_RULESETS

KSirCamWormMarker regex -f -aSUSPECT multipart/mixed;boundary=----.+_Outlook_Express_message_boundary
HContent-Type:		$>CheckContentType

######################################################################
SCheckContentType
R$+			$: $(SirCamWormMarker $1 $)
RSUSPECT		$#error $: "553 Possible virus, see http://www.symantec.com/avcenter/venc/data/w32.sircam.worm@mm.html"

HContent-Disposition:	$>CheckContentDisposition

######################################################################
SCheckContentDisposition
R$-			$@ OK
R$- ; $+		$@ OK
R$*			$#error $: "553 Illegal Content-Disposition"


#
#  Sobig.F
#

LOCAL_CONFIG
Kstorage macro

LOCAL_RULESETS
######################################################################
### check for the existance of the X-MailScanner Header
HX-MailScanner:		$>+CheckXMSc
D{SobigFPat}Found to be clean
D{SobigFMsg}This message may contain the Sobig.F virus.

SCheckXMSc
### if it exists, and the defined value is set, record the presence
R${SobigFPat} $*	$: $(storage {SobigFCheck} $@ SobigF $) $1
R$*			$@ OK

######################################################################
Scheck_eoh
# Check if a Message-Id was found
R$*			$: < $&{MessageIdCheck} >

# If Message-Id was found clear the X-MailScanner store and return with OK
R< $+ >			$@ OK $>ClearStorage

# Are we the first Hop?
R$*			$: < $&{ReceivedCheck} >
R< $+ >			$@ OK $>ClearStorage

# no Message-Id->check X-Mailscanner presence, too
R$*			$: < $&{SobigFCheck} >

# clear store
R$*			$: $>ClearStorage $1
# no msgid, first hop and Header found? -> reject the message
R < SobigF >		$#error $: 553 ${SobigFMsg}

# No Header! Fine, take the message
R$*			$@ OK

######################################################################
SClearStorage
R$*			$: $(storage {SobigFCheck} $) $1
R$*			$: $(storage {ReceivedCheck} $) $1
R$*			$: $(storage {MessageIdCheck} $) $1
R$*			$@ $1
