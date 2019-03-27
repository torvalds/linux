divert(-1)
#
# Copyright (c) 1998-2010 Proofpoint, Inc. and its suppliers.
#	All rights reserved.
# Copyright (c) 1983, 1995 Eric P. Allman.  All rights reserved.
# Copyright (c) 1988, 1993
#	The Regents of the University of California.  All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#
divert(0)

VERSIONID(`$Id: proto.m4,v 8.762 2013-11-22 20:51:13 ca Exp $')

# level CF_LEVEL config file format
V`'CF_LEVEL`'ifdef(`NO_VENDOR',`', `/ifdef(`VENDOR_NAME', `VENDOR_NAME', `Berkeley')')
divert(-1)

dnl if MAILER(`local') not defined: do it ourself; be nice
dnl maybe we should issue a warning?
ifdef(`_MAILER_local_',`', `MAILER(local)')

# do some sanity checking
ifdef(`__OSTYPE__',,
	`errprint(`*** ERROR: No system type defined (use OSTYPE macro)
')')

# pick our default mailers
ifdef(`confSMTP_MAILER',, `define(`confSMTP_MAILER', `esmtp')')
ifdef(`confLOCAL_MAILER',, `define(`confLOCAL_MAILER', `local')')
ifdef(`confRELAY_MAILER',,
	`define(`confRELAY_MAILER',
		`ifdef(`_MAILER_smtp_', `relay',
			`ifdef(`_MAILER_uucp', `uucp-new', `unknown')')')')
ifdef(`confUUCP_MAILER',, `define(`confUUCP_MAILER', `uucp-old')')
define(`_SMTP_', `confSMTP_MAILER')dnl		for readability only
define(`_LOCAL_', `confLOCAL_MAILER')dnl	for readability only
define(`_RELAY_', `confRELAY_MAILER')dnl	for readability only
define(`_UUCP_', `confUUCP_MAILER')dnl		for readability only

# back compatibility with old config files
ifdef(`confDEF_GROUP_ID',
`errprint(`*** confDEF_GROUP_ID is obsolete.
    Use confDEF_USER_ID with a colon in the value instead.
')')
ifdef(`confREAD_TIMEOUT',
`errprint(`*** confREAD_TIMEOUT is obsolete.
    Use individual confTO_<timeout> parameters instead.
')')
ifdef(`confMESSAGE_TIMEOUT',
	`define(`_ARG_', index(confMESSAGE_TIMEOUT, /))
	 ifelse(_ARG_, -1,
		`define(`confTO_QUEUERETURN', confMESSAGE_TIMEOUT)',
		`define(`confTO_QUEUERETURN',
			substr(confMESSAGE_TIMEOUT, 0, _ARG_))
		 define(`confTO_QUEUEWARN',
			substr(confMESSAGE_TIMEOUT, eval(_ARG_+1)))')')
ifdef(`confMIN_FREE_BLOCKS', `ifelse(index(confMIN_FREE_BLOCKS, /), -1,,
`errprint(`*** compound confMIN_FREE_BLOCKS is obsolete.
    Use confMAX_MESSAGE_SIZE for the second part of the value.
')')')


# Sanity check on ldap_routing feature
# If the user doesn't specify a new map, they better have given as a
# default LDAP specification which has the LDAP base (and most likely the host)
ifdef(`confLDAP_DEFAULT_SPEC',, `ifdef(`_LDAP_ROUTING_WARN_', `errprint(`
WARNING: Using default FEATURE(ldap_routing) map definition(s)
without setting confLDAP_DEFAULT_SPEC option.
')')')dnl

# clean option definitions below....
define(`_OPTION', `ifdef(`$2', `O $1`'ifelse(defn(`$2'), `',, `=$2')', `#O $1`'ifelse(`$3', `',,`=$3')')')dnl

dnl required to "rename" the check_* rulesets...
define(`_U_',ifdef(`_DELAY_CHECKS_',`',`_'))
dnl default relaying denied message
ifdef(`confRELAY_MSG', `', `define(`confRELAY_MSG',
ifdef(`_USE_AUTH_', `"550 Relaying denied. Proper authentication required."', `"550 Relaying denied"'))')
ifdef(`confRCPTREJ_MSG', `', `define(`confRCPTREJ_MSG', `"550 Mailbox disabled for this recipient"')')
define(`_CODE553', `553')
divert(0)dnl

# override file safeties - setting this option compromises system security,
# addressing the actual file configuration problem is preferred
# need to set this before any file actions are encountered in the cf file
_OPTION(DontBlameSendmail, `confDONT_BLAME_SENDMAIL', `safe')

# default LDAP map specification
# need to set this now before any LDAP maps are defined
_OPTION(LDAPDefaultSpec, `confLDAP_DEFAULT_SPEC', `-h localhost')

##################
#   local info   #
##################

# my LDAP cluster
# need to set this before any LDAP lookups are done (including classes)
ifdef(`confLDAP_CLUSTER', `D{sendmailMTACluster}`'confLDAP_CLUSTER', `#D{sendmailMTACluster}$m')

Cwlocalhost
ifdef(`USE_CW_FILE',
`# file containing names of hosts for which we receive email
Fw`'confCW_FILE',
	`dnl')

# my official domain name
# ... `define' this only if sendmail cannot automatically determine your domain
ifdef(`confDOMAIN_NAME', `Dj`'confDOMAIN_NAME', `#Dj$w.Foo.COM')

# host/domain names ending with a token in class P are canonical
CP.

ifdef(`UUCP_RELAY',
`# UUCP relay host
DY`'UUCP_RELAY
CPUUCP

')dnl
ifdef(`BITNET_RELAY',
`#  BITNET relay host
DB`'BITNET_RELAY
CPBITNET

')dnl
ifdef(`DECNET_RELAY',
`define(`_USE_DECNET_SYNTAX_', 1)dnl
# DECnet relay host
DC`'DECNET_RELAY
CPDECNET

')dnl
ifdef(`FAX_RELAY',
`# FAX relay host
DF`'FAX_RELAY
CPFAX

')dnl
# "Smart" relay host (may be null)
DS`'ifdef(`SMART_HOST', `SMART_HOST')

ifdef(`LUSER_RELAY', `dnl
# place to which unknown users should be forwarded
Kuser user -m -a<>
DL`'LUSER_RELAY',
`dnl')

# operators that cannot be in local usernames (i.e., network indicators)
CO @ ifdef(`_NO_PERCENTHACK_', `', `%') ifdef(`_NO_UUCP_', `', `!')

# a class with just dot (for identifying canonical names)
C..

# a class with just a left bracket (for identifying domain literals)
C[[

ifdef(`_ACCESS_TABLE_', `dnl
# access_db acceptance class
C{Accept}OK RELAY
ifdef(`_DELAY_COMPAT_8_10_',`dnl
ifdef(`_BLACKLIST_RCPT_',`dnl
# possible access_db RHS for spam friends/haters
C{SpamTag}SPAMFRIEND SPAMHATER')')',
`dnl')

dnl mark for "domain is ok" (resolved or accepted anyway)
define(`_RES_OK_', `OKR')dnl
ifdef(`_ACCEPT_UNRESOLVABLE_DOMAINS_',`dnl',`dnl
# Resolve map (to check if a host exists in check_mail)
Kresolve host -a<_RES_OK_> -T<TEMP>')
C{ResOk}_RES_OK_

ifdef(`_NEED_MACRO_MAP_', `dnl
ifdef(`_MACRO_MAP_', `', `# macro storage map
define(`_MACRO_MAP_', `1')dnl
Kmacro macro')', `dnl')

ifdef(`confCR_FILE', `dnl
# Hosts for which relaying is permitted ($=R)
FR`'confCR_FILE',
`dnl')

define(`TLS_SRV_TAG', `"TLS_Srv"')dnl
define(`TLS_CLT_TAG', `"TLS_Clt"')dnl
define(`TLS_RCPT_TAG', `"TLS_Rcpt"')dnl
define(`TLS_TRY_TAG', `"Try_TLS"')dnl
define(`SRV_FEAT_TAG', `"Srv_Features"')dnl
dnl this may be useful in other contexts too
ifdef(`_ARITH_MAP_', `', `# arithmetic map
define(`_ARITH_MAP_', `1')dnl
Karith arith')
ifdef(`_ACCESS_TABLE_', `dnl
ifdef(`_MACRO_MAP_', `', `# macro storage map
define(`_MACRO_MAP_', `1')dnl
Kmacro macro')
# possible values for TLS_connection in access map
C{Tls}VERIFY ENCR', `dnl')
ifdef(`_CERT_REGEX_ISSUER_', `dnl
# extract relevant part from cert issuer
KCERTIssuer regex _CERT_REGEX_ISSUER_', `dnl')
ifdef(`_CERT_REGEX_SUBJECT_', `dnl
# extract relevant part from cert subject
KCERTSubject regex _CERT_REGEX_SUBJECT_', `dnl')

ifdef(`LOCAL_RELAY', `dnl
# who I send unqualified names to if `FEATURE(stickyhost)' is used
# (null means deliver locally)
DR`'LOCAL_RELAY')

ifdef(`MAIL_HUB', `dnl
# who gets all local email traffic
# ($R has precedence for unqualified names if `FEATURE(stickyhost)' is used)
DH`'MAIL_HUB')

# dequoting map
Kdequote dequote`'ifdef(`confDEQUOTE_OPTS', ` confDEQUOTE_OPTS', `')

divert(0)dnl	# end of nullclient diversion
# class E: names that should be exposed as from this host, even if we masquerade
# class L: names that should be delivered locally, even if we have a relay
# class M: domains that should be converted to $M
# class N: domains that should not be converted to $M
#CL root
undivert(5)dnl
ifdef(`_VIRTHOSTS_', `CR$={VirtHost}', `dnl')

ifdef(`MASQUERADE_NAME', `dnl
# who I masquerade as (null for no masquerading) (see also $=M)
DM`'MASQUERADE_NAME')

# my name for error messages
ifdef(`confMAILER_NAME', `Dn`'confMAILER_NAME', `#DnMAILER-DAEMON')

undivert(6)dnl LOCAL_CONFIG
include(_CF_DIR_`m4/version.m4')

###############
#   Options   #
###############
ifdef(`confAUTO_REBUILD',
`errprint(WARNING: `confAUTO_REBUILD' is no longer valid.
	There was a potential for a denial of service attack if this is set.
)')dnl

# strip message body to 7 bits on input?
_OPTION(SevenBitInput, `confSEVEN_BIT_INPUT', `False')

# 8-bit data handling
_OPTION(EightBitMode, `confEIGHT_BIT_HANDLING', `pass8')

# wait for alias file rebuild (default units: minutes)
_OPTION(AliasWait, `confALIAS_WAIT', `5m')

# location of alias file
_OPTION(AliasFile, `ALIAS_FILE', `MAIL_SETTINGS_DIR`'aliases')

# minimum number of free blocks on filesystem
_OPTION(MinFreeBlocks, `confMIN_FREE_BLOCKS', `100')

# maximum message size
_OPTION(MaxMessageSize, `confMAX_MESSAGE_SIZE', `0')

# substitution for space (blank) characters
_OPTION(BlankSub, `confBLANK_SUB', `_')

# avoid connecting to "expensive" mailers on initial submission?
_OPTION(HoldExpensive, `confCON_EXPENSIVE', `False')

# checkpoint queue runs after every N successful deliveries
_OPTION(CheckpointInterval, `confCHECKPOINT_INTERVAL', `10')

# default delivery mode
_OPTION(DeliveryMode, `confDELIVERY_MODE', `background')

# error message header/file
_OPTION(ErrorHeader, `confERROR_MESSAGE', `MAIL_SETTINGS_DIR`'error-header')

# error mode
_OPTION(ErrorMode, `confERROR_MODE', `print')

# save Unix-style "From_" lines at top of header?
_OPTION(SaveFromLine, `confSAVE_FROM_LINES', `False')

# queue file mode (qf files)
_OPTION(QueueFileMode, `confQUEUE_FILE_MODE', `0600')

# temporary file mode
_OPTION(TempFileMode, `confTEMP_FILE_MODE', `0600')

# match recipients against GECOS field?
_OPTION(MatchGECOS, `confMATCH_GECOS', `False')

# maximum hop count
_OPTION(MaxHopCount, `confMAX_HOP', `25')

# location of help file
O HelpFile=ifdef(`HELP_FILE', HELP_FILE, `MAIL_SETTINGS_DIR`'helpfile')

# ignore dots as terminators in incoming messages?
_OPTION(IgnoreDots, `confIGNORE_DOTS', `False')

# name resolver options
_OPTION(ResolverOptions, `confBIND_OPTS', `+AAONLY')

# deliver MIME-encapsulated error messages?
_OPTION(SendMimeErrors, `confMIME_FORMAT_ERRORS', `True')

# Forward file search path
_OPTION(ForwardPath, `confFORWARD_PATH', `/var/forward/$u:$z/.forward.$w:$z/.forward')

# open connection cache size
_OPTION(ConnectionCacheSize, `confMCI_CACHE_SIZE', `2')

# open connection cache timeout
_OPTION(ConnectionCacheTimeout, `confMCI_CACHE_TIMEOUT', `5m')

# persistent host status directory
_OPTION(HostStatusDirectory, `confHOST_STATUS_DIRECTORY', `.hoststat')

# single thread deliveries (requires HostStatusDirectory)?
_OPTION(SingleThreadDelivery, `confSINGLE_THREAD_DELIVERY', `False')

# use Errors-To: header?
_OPTION(UseErrorsTo, `confUSE_ERRORS_TO', `False')

# use compressed IPv6 address format?
_OPTION(UseCompressedIPv6Addresses, `confUSE_COMPRESSED_IPV6_ADDRESSES', `')

# log level
_OPTION(LogLevel, `confLOG_LEVEL', `10')

# send to me too, even in an alias expansion?
_OPTION(MeToo, `confME_TOO', `True')

# verify RHS in newaliases?
_OPTION(CheckAliases, `confCHECK_ALIASES', `False')

# default messages to old style headers if no special punctuation?
_OPTION(OldStyleHeaders, `confOLD_STYLE_HEADERS', `False')

# SMTP daemon options
ifelse(defn(`confDAEMON_OPTIONS'), `', `dnl',
`errprint(WARNING: `confDAEMON_OPTIONS' is no longer valid.
	Use `DAEMON_OPTIONS()'; see cf/README.
)'dnl
`DAEMON_OPTIONS(`confDAEMON_OPTIONS')')
ifelse(defn(`_DPO_'), `',
`ifdef(`_NETINET6_', `O DaemonPortOptions=Name=MTA-v4, Family=inet
O DaemonPortOptions=Name=MTA-v6, Family=inet6',`O DaemonPortOptions=Name=MTA')', `_DPO_')
ifdef(`_NO_MSA_', `dnl', `O DaemonPortOptions=Port=587, Name=MSA, M=E')

# SMTP client options
ifelse(defn(`confCLIENT_OPTIONS'), `', `dnl',
`errprint(WARNING: `confCLIENT_OPTIONS' is no longer valid.  See cf/README for more information.
)'dnl
`CLIENT_OPTIONS(`confCLIENT_OPTIONS')')
ifelse(defn(`_CPO_'), `',
`#O ClientPortOptions=Family=inet, Address=0.0.0.0', `_CPO_')

# Modifiers to `define' {daemon_flags} for direct submissions
_OPTION(DirectSubmissionModifiers, `confDIRECT_SUBMISSION_MODIFIERS', `')

# Use as mail submission program? See sendmail/SECURITY
_OPTION(UseMSP, `confUSE_MSP', `')

# privacy flags
_OPTION(PrivacyOptions, `confPRIVACY_FLAGS', `authwarnings')

# who (if anyone) should get extra copies of error messages
_OPTION(PostmasterCopy, `confCOPY_ERRORS_TO', `Postmaster')

# slope of queue-only function
_OPTION(QueueFactor, `confQUEUE_FACTOR', `600000')

# limit on number of concurrent queue runners
_OPTION(MaxQueueChildren, `confMAX_QUEUE_CHILDREN', `')

# maximum number of queue-runners per queue-grouping with multiple queues
_OPTION(MaxRunnersPerQueue, `confMAX_RUNNERS_PER_QUEUE', `1')

# priority of queue runners (nice(3))
_OPTION(NiceQueueRun, `confNICE_QUEUE_RUN', `')

# shall we sort the queue by hostname first?
_OPTION(QueueSortOrder, `confQUEUE_SORT_ORDER', `priority')

# minimum time in queue before retry
_OPTION(MinQueueAge, `confMIN_QUEUE_AGE', `30m')

# maximum time in queue before retry (if > 0; only for exponential delay)
_OPTION(MaxQueueAge, `confMAX_QUEUE_AGE', `')

# how many jobs can you process in the queue?
_OPTION(MaxQueueRunSize, `confMAX_QUEUE_RUN_SIZE', `0')

# perform initial split of envelope without checking MX records
_OPTION(FastSplit, `confFAST_SPLIT', `1')

# queue directory
O QueueDirectory=ifdef(`QUEUE_DIR', QUEUE_DIR, `/var/spool/mqueue')

# key for shared memory; 0 to turn off, -1 to auto-select
_OPTION(SharedMemoryKey, `confSHARED_MEMORY_KEY', `0')

# file to store auto-selected key for shared memory (SharedMemoryKey = -1)
_OPTION(SharedMemoryKeyFile, `confSHARED_MEMORY_KEY_FILE', `')

# timeouts (many of these)
_OPTION(Timeout.initial, `confTO_INITIAL', `5m')
_OPTION(Timeout.connect, `confTO_CONNECT', `5m')
_OPTION(Timeout.aconnect, `confTO_ACONNECT', `0s')
_OPTION(Timeout.iconnect, `confTO_ICONNECT', `5m')
_OPTION(Timeout.helo, `confTO_HELO', `5m')
_OPTION(Timeout.mail, `confTO_MAIL', `10m')
_OPTION(Timeout.rcpt, `confTO_RCPT', `1h')
_OPTION(Timeout.datainit, `confTO_DATAINIT', `5m')
_OPTION(Timeout.datablock, `confTO_DATABLOCK', `1h')
_OPTION(Timeout.datafinal, `confTO_DATAFINAL', `1h')
_OPTION(Timeout.rset, `confTO_RSET', `5m')
_OPTION(Timeout.quit, `confTO_QUIT', `2m')
_OPTION(Timeout.misc, `confTO_MISC', `2m')
_OPTION(Timeout.command, `confTO_COMMAND', `1h')
_OPTION(Timeout.ident, `confTO_IDENT', `5s')
_OPTION(Timeout.fileopen, `confTO_FILEOPEN', `60s')
_OPTION(Timeout.control, `confTO_CONTROL', `2m')
_OPTION(Timeout.queuereturn, `confTO_QUEUERETURN', `5d')
_OPTION(Timeout.queuereturn.normal, `confTO_QUEUERETURN_NORMAL', `5d')
_OPTION(Timeout.queuereturn.urgent, `confTO_QUEUERETURN_URGENT', `2d')
_OPTION(Timeout.queuereturn.non-urgent, `confTO_QUEUERETURN_NONURGENT', `7d')
_OPTION(Timeout.queuereturn.dsn, `confTO_QUEUERETURN_DSN', `5d')
_OPTION(Timeout.queuewarn, `confTO_QUEUEWARN', `4h')
_OPTION(Timeout.queuewarn.normal, `confTO_QUEUEWARN_NORMAL', `4h')
_OPTION(Timeout.queuewarn.urgent, `confTO_QUEUEWARN_URGENT', `1h')
_OPTION(Timeout.queuewarn.non-urgent, `confTO_QUEUEWARN_NONURGENT', `12h')
_OPTION(Timeout.queuewarn.dsn, `confTO_QUEUEWARN_DSN', `4h')
_OPTION(Timeout.hoststatus, `confTO_HOSTSTATUS', `30m')
_OPTION(Timeout.resolver.retrans, `confTO_RESOLVER_RETRANS', `5s')
_OPTION(Timeout.resolver.retrans.first, `confTO_RESOLVER_RETRANS_FIRST', `5s')
_OPTION(Timeout.resolver.retrans.normal, `confTO_RESOLVER_RETRANS_NORMAL', `5s')
_OPTION(Timeout.resolver.retry, `confTO_RESOLVER_RETRY', `4')
_OPTION(Timeout.resolver.retry.first, `confTO_RESOLVER_RETRY_FIRST', `4')
_OPTION(Timeout.resolver.retry.normal, `confTO_RESOLVER_RETRY_NORMAL', `4')
_OPTION(Timeout.lhlo, `confTO_LHLO', `2m')
_OPTION(Timeout.auth, `confTO_AUTH', `10m')
_OPTION(Timeout.starttls, `confTO_STARTTLS', `1h')

# time for DeliverBy; extension disabled if less than 0
_OPTION(DeliverByMin, `confDELIVER_BY_MIN', `0')

# should we not prune routes in route-addr syntax addresses?
_OPTION(DontPruneRoutes, `confDONT_PRUNE_ROUTES', `False')

# queue up everything before forking?
_OPTION(SuperSafe, `confSAFE_QUEUE', `True')

# status file
_OPTION(StatusFile, `STATUS_FILE')

# time zone handling:
#  if undefined, use system default
#  if defined but null, use TZ envariable passed in
#  if defined and non-null, use that info
ifelse(confTIME_ZONE, `USE_SYSTEM', `#O TimeZoneSpec=',
	confTIME_ZONE, `USE_TZ', `O TimeZoneSpec=',
	`O TimeZoneSpec=confTIME_ZONE')

# default UID (can be username or userid:groupid)
_OPTION(DefaultUser, `confDEF_USER_ID', `mailnull')

# list of locations of user database file (null means no lookup)
_OPTION(UserDatabaseSpec, `confUSERDB_SPEC', `MAIL_SETTINGS_DIR`'userdb')

# fallback MX host
_OPTION(FallbackMXhost, `confFALLBACK_MX', `fall.back.host.net')

# fallback smart host
_OPTION(FallbackSmartHost, `confFALLBACK_SMARTHOST', `fall.back.host.net')

# if we are the best MX host for a site, try it directly instead of config err
_OPTION(TryNullMXList, `confTRY_NULL_MX_LIST', `False')

# load average at which we just queue messages
_OPTION(QueueLA, `confQUEUE_LA', `8')

# load average at which we refuse connections
_OPTION(RefuseLA, `confREFUSE_LA', `12')

# log interval when refusing connections for this long
_OPTION(RejectLogInterval, `confREJECT_LOG_INTERVAL', `3h')

# load average at which we delay connections; 0 means no limit
_OPTION(DelayLA, `confDELAY_LA', `0')

# maximum number of children we allow at one time
_OPTION(MaxDaemonChildren, `confMAX_DAEMON_CHILDREN', `0')

# maximum number of new connections per second
_OPTION(ConnectionRateThrottle, `confCONNECTION_RATE_THROTTLE', `0')

# Width of the window 
_OPTION(ConnectionRateWindowSize, `confCONNECTION_RATE_WINDOW_SIZE', `60s')

# work recipient factor
_OPTION(RecipientFactor, `confWORK_RECIPIENT_FACTOR', `30000')

# deliver each queued job in a separate process?
_OPTION(ForkEachJob, `confSEPARATE_PROC', `False')

# work class factor
_OPTION(ClassFactor, `confWORK_CLASS_FACTOR', `1800')

# work time factor
_OPTION(RetryFactor, `confWORK_TIME_FACTOR', `90000')

# default character set
_OPTION(DefaultCharSet, `confDEF_CHAR_SET', `unknown-8bit')

# service switch file (name hardwired on Solaris, Ultrix, OSF/1, others)
_OPTION(ServiceSwitchFile, `confSERVICE_SWITCH_FILE', `MAIL_SETTINGS_DIR`'service.switch')

# hosts file (normally /etc/hosts)
_OPTION(HostsFile, `confHOSTS_FILE', `/etc/hosts')

# dialup line delay on connection failure
_OPTION(DialDelay, `confDIAL_DELAY', `0s')

# action to take if there are no recipients in the message
_OPTION(NoRecipientAction, `confNO_RCPT_ACTION', `none')

# chrooted environment for writing to files
_OPTION(SafeFileEnvironment, `confSAFE_FILE_ENV', `')

# are colons OK in addresses?
_OPTION(ColonOkInAddr, `confCOLON_OK_IN_ADDR', `True')

# shall I avoid expanding CNAMEs (violates protocols)?
_OPTION(DontExpandCnames, `confDONT_EXPAND_CNAMES', `False')

# SMTP initial login message (old $e macro)
_OPTION(SmtpGreetingMessage, `confSMTP_LOGIN_MSG', `$j Sendmail $v ready at $b')

# UNIX initial From header format (old $l macro)
_OPTION(UnixFromLine, `confFROM_LINE', `From $g $d')

# From: lines that have embedded newlines are unwrapped onto one line
_OPTION(SingleLineFromHeader, `confSINGLE_LINE_FROM_HEADER', `False')

# Allow HELO SMTP command that does not `include' a host name
_OPTION(AllowBogusHELO, `confALLOW_BOGUS_HELO', `False')

# Characters to be quoted in a full name phrase (@,;:\()[] are automatic)
_OPTION(MustQuoteChars, `confMUST_QUOTE_CHARS', `.')

# delimiter (operator) characters (old $o macro)
_OPTION(OperatorChars, `confOPERATORS', `.:@[]')

# shall I avoid calling initgroups(3) because of high NIS costs?
_OPTION(DontInitGroups, `confDONT_INIT_GROUPS', `False')

# are group-writable `:include:' and .forward files (un)trustworthy?
# True (the default) means they are not trustworthy.
_OPTION(UnsafeGroupWrites, `confUNSAFE_GROUP_WRITES', `True')
ifdef(`confUNSAFE_GROUP_WRITES',
`errprint(`WARNING: confUNSAFE_GROUP_WRITES is deprecated; use confDONT_BLAME_SENDMAIL.
')')

# where do errors that occur when sending errors get sent?
_OPTION(DoubleBounceAddress, `confDOUBLE_BOUNCE_ADDRESS', `postmaster')

# issue temporary errors (4xy) instead of permanent errors (5xy)?
_OPTION(SoftBounce, `confSOFT_BOUNCE', `False')

# where to save bounces if all else fails
_OPTION(DeadLetterDrop, `confDEAD_LETTER_DROP', `/var/tmp/dead.letter')

# what user id do we assume for the majority of the processing?
_OPTION(RunAsUser, `confRUN_AS_USER', `sendmail')

# maximum number of recipients per SMTP envelope
_OPTION(MaxRecipientsPerMessage, `confMAX_RCPTS_PER_MESSAGE', `0')

# limit the rate recipients per SMTP envelope are accepted
# once the threshold number of recipients have been rejected
_OPTION(BadRcptThrottle, `confBAD_RCPT_THROTTLE', `0')


# shall we get local names from our installed interfaces?
_OPTION(DontProbeInterfaces, `confDONT_PROBE_INTERFACES', `False')

# Return-Receipt-To: header implies DSN request
_OPTION(RrtImpliesDsn, `confRRT_IMPLIES_DSN', `False')

# override connection address (for testing)
_OPTION(ConnectOnlyTo, `confCONNECT_ONLY_TO', `0.0.0.0')

# Trusted user for file ownership and starting the daemon
_OPTION(TrustedUser, `confTRUSTED_USER', `root')

# Control socket for daemon management
_OPTION(ControlSocketName, `confCONTROL_SOCKET_NAME', `/var/spool/mqueue/.control')

# Maximum MIME header length to protect MUAs
_OPTION(MaxMimeHeaderLength, `confMAX_MIME_HEADER_LENGTH', `0/0')

# Maximum length of the sum of all headers
_OPTION(MaxHeadersLength, `confMAX_HEADERS_LENGTH', `32768')

# Maximum depth of alias recursion
_OPTION(MaxAliasRecursion, `confMAX_ALIAS_RECURSION', `10')

# location of pid file
_OPTION(PidFile, `confPID_FILE', `/var/run/sendmail.pid')

# Prefix string for the process title shown on 'ps' listings
_OPTION(ProcessTitlePrefix, `confPROCESS_TITLE_PREFIX', `prefix')

# Data file (df) memory-buffer file maximum size
_OPTION(DataFileBufferSize, `confDF_BUFFER_SIZE', `4096')

# Transcript file (xf) memory-buffer file maximum size
_OPTION(XscriptFileBufferSize, `confXF_BUFFER_SIZE', `4096')

# lookup type to find information about local mailboxes
_OPTION(MailboxDatabase, `confMAILBOX_DATABASE', `pw')

# override compile time flag REQUIRES_DIR_FSYNC
_OPTION(RequiresDirfsync, `confREQUIRES_DIR_FSYNC', `true')

# list of authentication mechanisms
_OPTION(AuthMechanisms, `confAUTH_MECHANISMS', `EXTERNAL GSSAPI KERBEROS_V4 DIGEST-MD5 CRAM-MD5')

# Authentication realm
_OPTION(AuthRealm, `confAUTH_REALM', `')

# default authentication information for outgoing connections
_OPTION(DefaultAuthInfo, `confDEF_AUTH_INFO', `MAIL_SETTINGS_DIR`'default-auth-info')

# SMTP AUTH flags
_OPTION(AuthOptions, `confAUTH_OPTIONS', `')

# SMTP AUTH maximum encryption strength
_OPTION(AuthMaxBits, `confAUTH_MAX_BITS', `')

# SMTP STARTTLS server options
_OPTION(TLSSrvOptions, `confTLS_SRV_OPTIONS', `')

# SSL cipherlist
_OPTION(CipherList, `confCIPHER_LIST', `')
# server side SSL options
_OPTION(ServerSSLOptions, `confSERVER_SSL_OPTIONS', `')
# client side SSL options
_OPTION(ClientSSLOptions, `confCLIENT_SSL_OPTIONS', `')

# Input mail filters
_OPTION(InputMailFilters, `confINPUT_MAIL_FILTERS', `')

ifelse(len(X`'_MAIL_FILTERS_DEF), `1', `dnl', `dnl
# Milter options
_OPTION(Milter.LogLevel, `confMILTER_LOG_LEVEL', `')
_OPTION(Milter.macros.connect, `confMILTER_MACROS_CONNECT', `')
_OPTION(Milter.macros.helo, `confMILTER_MACROS_HELO', `')
_OPTION(Milter.macros.envfrom, `confMILTER_MACROS_ENVFROM', `')
_OPTION(Milter.macros.envrcpt, `confMILTER_MACROS_ENVRCPT', `')
_OPTION(Milter.macros.eom, `confMILTER_MACROS_EOM', `')
_OPTION(Milter.macros.eoh, `confMILTER_MACROS_EOH', `')
_OPTION(Milter.macros.data, `confMILTER_MACROS_DATA', `')')

# CA directory
_OPTION(CACertPath, `confCACERT_PATH', `')
# CA file
_OPTION(CACertFile, `confCACERT', `')
# Server Cert
_OPTION(ServerCertFile, `confSERVER_CERT', `')
# Server private key
_OPTION(ServerKeyFile, `confSERVER_KEY', `')
# Client Cert
_OPTION(ClientCertFile, `confCLIENT_CERT', `')
# Client private key
_OPTION(ClientKeyFile, `confCLIENT_KEY', `')
# File containing certificate revocation lists 
_OPTION(CRLFile, `confCRL', `')
# DHParameters (only required if DSA/DH is used)
_OPTION(DHParameters, `confDH_PARAMETERS', `')
# Random data source (required for systems without /dev/urandom under OpenSSL)
_OPTION(RandFile, `confRAND_FILE', `')
# fingerprint algorithm (digest) to use for the presented cert
_OPTION(CertFingerprintAlgorithm, `confCERT_FINGERPRINT_ALGORITHM', `')

# Maximum number of "useless" commands before slowing down
_OPTION(MaxNOOPCommands, `confMAX_NOOP_COMMANDS', `20')

# Name to use for EHLO (defaults to $j)
_OPTION(HeloName, `confHELO_NAME')

ifdef(`_NEED_SMTPOPMODES_', `dnl
# SMTP operation modes
C{SMTPOpModes} s d D')

############################
`# QUEUE GROUP DEFINITIONS  #'
############################
_QUEUE_GROUP_

###########################
#   Message precedences   #
###########################

Pfirst-class=0
Pspecial-delivery=100
Plist=-30
Pbulk=-60
Pjunk=-100

#####################
#   Trusted users   #
#####################

# this is equivalent to setting class "t"
ifdef(`_USE_CT_FILE_', `', `#')Ft`'ifdef(`confCT_FILE', confCT_FILE, `MAIL_SETTINGS_DIR`'trusted-users')
Troot
Tdaemon
ifdef(`_NO_UUCP_', `dnl', `Tuucp')
ifdef(`confTRUSTED_USERS', `T`'confTRUSTED_USERS', `dnl')

#########################
#   Format of headers   #
#########################

ifdef(`confFROM_HEADER',, `define(`confFROM_HEADER', `$?x$x <$g>$|$g$.')')dnl
ifdef(`confMESSAGEID_HEADER',, `define(`confMESSAGEID_HEADER', `<$t.$i@$j>')')dnl
H?P?Return-Path: <$g>
HReceived: confRECEIVED_HEADER
H?D?Resent-Date: $a
H?D?Date: $a
H?F?Resent-From: confFROM_HEADER
H?F?From: confFROM_HEADER
H?x?Full-Name: $x
# HPosted-Date: $a
# H?l?Received-Date: $b
H?M?Resent-Message-Id: confMESSAGEID_HEADER
H?M?Message-Id: confMESSAGEID_HEADER

#
######################################################################
######################################################################
#####
#####			REWRITING RULES
#####
######################################################################
######################################################################

############################################
###  Ruleset 3 -- Name Canonicalization  ###
############################################
Scanonify=3

# handle null input (translate to <@> special case)
R$@			$@ <@>

# strip group: syntax (not inside angle brackets!) and trailing semicolon
R$*			$: $1 <@>			mark addresses
R$* < $* > $* <@>	$: $1 < $2 > $3			unmark <addr>
R@ $* <@>		$: @ $1				unmark @host:...
R$* [ IPv6 : $+ ] <@>	$: $1 [ IPv6 : $2 ]		unmark IPv6 addr
R$* :: $* <@>		$: $1 :: $2			unmark node::addr
R:`include': $* <@>	$: :`include': $1			unmark :`include':...
R$* : $* [ $* ]		$: $1 : $2 [ $3 ] <@>		remark if leading colon
R$* : $* <@>		$: $2				strip colon if marked
R$* <@>			$: $1				unmark
R$* ;			   $1				strip trailing semi
R$* < $+ :; > $*	$@ $2 :; <@>			catch <list:;>
R$* < $* ; >		   $1 < $2 >			bogus bracketed semi

# null input now results from list:; syntax
R$@			$@ :; <@>

# strip angle brackets -- note RFC733 heuristic to get innermost item
R$*			$: < $1 >			housekeeping <>
R$+ < $* >		   < $2 >			strip excess on left
R< $* > $+		   < $1 >			strip excess on right
R<>			$@ < @ >			MAIL FROM:<> case
R< $+ >			$: $1				remove housekeeping <>

ifdef(`_USE_DEPRECATED_ROUTE_ADDR_',`dnl
# make sure <@a,@b,@c:user@d> syntax is easy to parse -- undone later
R@ $+ , $+		@ $1 : $2			change all "," to ":"

# localize and dispose of route-based addresses
dnl XXX: IPv6 colon conflict
ifdef(`NO_NETINET6', `dnl',
`R@ [$+] : $+		$@ $>Canonify2 < @ [$1] > : $2	handle <route-addr>')
R@ $+ : $+		$@ $>Canonify2 < @$1 > : $2	handle <route-addr>
dnl',`dnl
# strip route address <@a,@b,@c:user@d> -> <user@d>
R@ $+ , $+		$2
ifdef(`NO_NETINET6', `dnl',
`R@ [ $* ] : $+		$2')
R@ $+ : $+		$2
dnl')

# find focus for list syntax
R $+ : $* ; @ $+	$@ $>Canonify2 $1 : $2 ; < @ $3 >	list syntax
R $+ : $* ;		$@ $1 : $2;			list syntax

# find focus for @ syntax addresses
R$+ @ $+		$: $1 < @ $2 >			focus on domain
R$+ < $+ @ $+ >		$1 $2 < @ $3 >			move gaze right
R$+ < @ $+ >		$@ $>Canonify2 $1 < @ $2 >	already canonical

dnl This is flagged as an error in S0; no need to silently fix it here.
dnl # do some sanity checking
dnl R$* < @ $~[ $* : $* > $*	$1 < @ $2 $3 > $4	nix colons in addrs

ifdef(`_NO_UUCP_', `dnl',
`# convert old-style addresses to a domain-based address
R$- ! $+		$@ $>Canonify2 $2 < @ $1 .UUCP >	resolve uucp names
R$+ . $- ! $+		$@ $>Canonify2 $3 < @ $1 . $2 >		domain uucps
R$+ ! $+		$@ $>Canonify2 $2 < @ $1 .UUCP >	uucp subdomains
')
ifdef(`_USE_DECNET_SYNTAX_',
`# convert node::user addresses into a domain-based address
R$- :: $+		$@ $>Canonify2 $2 < @ $1 .DECNET >	resolve DECnet names
R$- . $- :: $+		$@ $>Canonify2 $3 < @ $1.$2 .DECNET >	numeric DECnet addr
',
	`dnl')
ifdef(`_NO_PERCENTHACK_', `dnl',
`# if we have % signs, take the rightmost one
R$* % $*		$1 @ $2				First make them all @s.
R$* @ $* @ $*		$1 % $2 @ $3			Undo all but the last.
')
R$* @ $*		$@ $>Canonify2 $1 < @ $2 >	Insert < > and finish

# else we must be a local name
R$*			$@ $>Canonify2 $1


################################################
###  Ruleset 96 -- bottom half of ruleset 3  ###
################################################

SCanonify2=96

# handle special cases for local names
R$* < @ localhost > $*		$: $1 < @ $j . > $2		no domain at all
R$* < @ localhost . $m > $*	$: $1 < @ $j . > $2		local domain
ifdef(`_NO_UUCP_', `dnl',
`R$* < @ localhost . UUCP > $*	$: $1 < @ $j . > $2		.UUCP domain')

# check for IPv4/IPv6 domain literal
R$* < @ [ $+ ] > $*		$: $1 < @@ [ $2 ] > $3		mark [addr]
R$* < @@ $=w > $*		$: $1 < @ $j . > $3		self-literal
R$* < @@ $+ > $*		$@ $1 < @ $2 > $3		canon IP addr

ifdef(`_DOMAIN_TABLE_', `dnl
# look up domains in the domain table
R$* < @ $+ > $* 		$: $1 < @ $(domaintable $2 $) > $3', `dnl')

undivert(2)dnl LOCAL_RULE_3

ifdef(`_BITDOMAIN_TABLE_', `dnl
# handle BITNET mapping
R$* < @ $+ .BITNET > $*		$: $1 < @ $(bitdomain $2 $: $2.BITNET $) > $3', `dnl')

ifdef(`_UUDOMAIN_TABLE_', `dnl
# handle UUCP mapping
R$* < @ $+ .UUCP > $*		$: $1 < @ $(uudomain $2 $: $2.UUCP $) > $3', `dnl')

ifdef(`_NO_UUCP_', `dnl',
`ifdef(`UUCP_RELAY',
`# pass UUCP addresses straight through
R$* < @ $+ . UUCP > $*		$@ $1 < @ $2 . UUCP . > $3',
`# if really UUCP, handle it immediately
ifdef(`_CLASS_U_',
`R$* < @ $=U . UUCP > $*	$@ $1 < @ $2 . UUCP . > $3', `dnl')
ifdef(`_CLASS_V_',
`R$* < @ $=V . UUCP > $*	$@ $1 < @ $2 . UUCP . > $3', `dnl')
ifdef(`_CLASS_W_',
`R$* < @ $=W . UUCP > $*	$@ $1 < @ $2 . UUCP . > $3', `dnl')
ifdef(`_CLASS_X_',
`R$* < @ $=X . UUCP > $*	$@ $1 < @ $2 . UUCP . > $3', `dnl')
ifdef(`_CLASS_Y_',
`R$* < @ $=Y . UUCP > $*	$@ $1 < @ $2 . UUCP . > $3', `dnl')

ifdef(`_NO_CANONIFY_', `dnl', `dnl
# try UUCP traffic as a local address
R$* < @ $+ . UUCP > $*		$: $1 < @ $[ $2 $] . UUCP . > $3
R$* < @ $+ . . UUCP . > $*	$@ $1 < @ $2 . > $3')
')')
# hostnames ending in class P are always canonical
R$* < @ $* $=P > $*		$: $1 < @ $2 $3 . > $4
dnl apply the next rule only for hostnames not in class P
dnl this even works for phrases in class P since . is in class P
dnl which daemon flags are set?
R$* < @ $* $~P > $*		$: $&{daemon_flags} $| $1 < @ $2 $3 > $4
dnl the other rules in this section only apply if the hostname
dnl does not end in class P hence no further checks are done here
dnl if this ever changes make sure the lookups are "protected" again!
ifdef(`_NO_CANONIFY_', `dnl
dnl do not canonify unless:
dnl domain ends in class {Canonify} (this does not work if the intersection
dnl	with class P is non-empty)
dnl or {daemon_flags} has c set
# pass to name server to make hostname canonical if in class {Canonify}
R$* $| $* < @ $* $={Canonify} > $*	$: $2 < @ $[ $3 $4 $] > $5
# pass to name server to make hostname canonical if requested
R$* c $* $| $* < @ $* > $*	$: $3 < @ $[ $4 $] > $5
dnl trailing dot? -> do not apply _CANONIFY_HOSTS_
R$* $| $* < @ $+ . > $*		$: $2 < @ $3 . > $4
# add a trailing dot to qualified hostnames so other rules will work
R$* $| $* < @ $+.$+ > $*	$: $2 < @ $3.$4 . > $5
ifdef(`_CANONIFY_HOSTS_', `dnl
dnl this should only apply to unqualified hostnames
dnl but if a valid character inside an unqualified hostname is an OperatorChar
dnl then $- does not work.
# lookup unqualified hostnames
R$* $| $* < @ $* > $*		$: $2 < @ $[ $3 $] > $4', `dnl')', `dnl
dnl _NO_CANONIFY_ is not set: canonify unless:
dnl {daemon_flags} contains CC (do not canonify)
dnl but add a trailing dot to qualified hostnames so other rules will work
dnl should we do this for every hostname: even unqualified?
R$* CC $* $| $* < @ $+.$+ > $*	$: $3 < @ $4.$5 . > $6
R$* CC $* $| $*			$: $3
ifdef(`_FFR_NOCANONIFY_HEADERS', `dnl
# do not canonify header addresses
R$* $| $* < @ $* $~P > $*	$: $&{addr_type} $| $2 < @ $3 $4 > $5
R$* h $* $| $* < @ $+.$+ > $*	$: $3 < @ $4.$5 . > $6
R$* h $* $| $*			$: $3', `dnl')
# pass to name server to make hostname canonical
R$* $| $* < @ $* > $*		$: $2 < @ $[ $3 $] > $4')
dnl remove {daemon_flags} for other cases
R$* $| $*			$: $2

# local host aliases and pseudo-domains are always canonical
R$* < @ $=w > $*		$: $1 < @ $2 . > $3
ifdef(`_MASQUERADE_ENTIRE_DOMAIN_',
`R$* < @ $* $=M > $*		$: $1 < @ $2 $3 . > $4',
`R$* < @ $=M > $*		$: $1 < @ $2 . > $3')
ifdef(`_VIRTUSER_TABLE_', `dnl
dnl virtual hosts are also canonical
ifdef(`_VIRTUSER_ENTIRE_DOMAIN_',
`R$* < @ $* $={VirtHost} > $* 	$: $1 < @ $2 $3 . > $4',
`R$* < @ $={VirtHost} > $* 	$: $1 < @ $2 . > $3')',
`dnl')
ifdef(`_GENERICS_TABLE_', `dnl
dnl hosts for genericstable are also canonical
ifdef(`_GENERICS_ENTIRE_DOMAIN_',
`R$* < @ $* $=G > $* 	$: $1 < @ $2 $3 . > $4',
`R$* < @ $=G > $* 	$: $1 < @ $2 . > $3')',
`dnl')
dnl remove superfluous dots (maybe repeatedly) which may have been added
dnl by one of the rules before
R$* < @ $* . . > $*		$1 < @ $2 . > $3


##################################################
###  Ruleset 4 -- Final Output Post-rewriting  ###
##################################################
Sfinal=4

R$+ :; <@>		$@ $1 :				handle <list:;>
R$* <@>			$@				handle <> and list:;

# strip trailing dot off possibly canonical name
R$* < @ $+ . > $*	$1 < @ $2 > $3

# eliminate internal code
R$* < @ *LOCAL* > $*	$1 < @ $j > $2

# externalize local domain info
R$* < $+ > $*		$1 $2 $3			defocus
R@ $+ : @ $+ : $+	@ $1 , @ $2 : $3		<route-addr> canonical
R@ $*			$@ @ $1				... and exit

ifdef(`_NO_UUCP_', `dnl',
`# UUCP must always be presented in old form
R$+ @ $- . UUCP		$2!$1				u@h.UUCP => h!u')

ifdef(`_USE_DECNET_SYNTAX_',
`# put DECnet back in :: form
R$+ @ $+ . DECNET	$2 :: $1			u@h.DECNET => h::u',
	`dnl')
# delete duplicate local names
R$+ % $=w @ $=w		$1 @ $2				u%host@host => u@host



##############################################################
###   Ruleset 97 -- recanonicalize and call ruleset zero   ###
###		   (used for recursive calls)		   ###
##############################################################

SRecurse=97
R$*			$: $>canonify $1
R$*			$@ $>parse $1


######################################
###   Ruleset 0 -- Parse Address   ###
######################################

Sparse=0

R$*			$: $>Parse0 $1		initial parsing
R<@>			$#_LOCAL_ $: <@>		special case error msgs
R$*			$: $>ParseLocal $1	handle local hacks
R$*			$: $>Parse1 $1		final parsing

#
#  Parse0 -- do initial syntax checking and eliminate local addresses.
#	This should either return with the (possibly modified) input
#	or return with a #error mailer.  It should not return with a
#	#mailer other than the #error mailer.
#

SParse0
R<@>			$@ <@>			special case error msgs
R$* : $* ; <@>		$#error $@ 5.1.3 $: "_CODE553 List:; syntax illegal for recipient addresses"
R@ <@ $* >		< @ $1 >		catch "@@host" bogosity
R<@ $+>			$#error $@ 5.1.3 $: "_CODE553 User address required"
R$+ <@>			$#error $@ 5.1.3 $: "_CODE553 Hostname required"
R$*			$: <> $1
dnl allow tricks like [host1]:[host2]
R<> $* < @ [ $* ] : $+ > $*	$1 < @ [ $2 ] : $3 > $4
R<> $* < @ [ $* ] , $+ > $*	$1 < @ [ $2 ] , $3 > $4
dnl but no a@[b]c
R<> $* < @ [ $* ] $+ > $*	$#error $@ 5.1.2 $: "_CODE553 Invalid address"
R<> $* < @ [ $+ ] > $*		$1 < @ [ $2 ] > $3
R<> $* <$* : $* > $*	$#error $@ 5.1.3 $: "_CODE553 Colon illegal in host name part"
R<> $*			$1
R$* < @ . $* > $*	$#error $@ 5.1.2 $: "_CODE553 Invalid host name"
R$* < @ $* .. $* > $*	$#error $@ 5.1.2 $: "_CODE553 Invalid host name"
dnl no a@b@
R$* < @ $* @ > $*	$#error $@ 5.1.2 $: "_CODE553 Invalid route address"
dnl no a@b@c
R$* @ $* < @ $* > $*	$#error $@ 5.1.3 $: "_CODE553 Invalid route address"
dnl comma only allowed before @; this check is not complete
R$* , $~O $*		$#error $@ 5.1.3 $: "_CODE553 Invalid route address"

ifdef(`_STRICT_RFC821_', `# more RFC 821 checks
R$* . < @ $* > $*	$#error $@ 5.1.2 $: "_CODE553 Local part must not end with a dot"
R. $* < @ $* > $*	$#error $@ 5.1.2 $: "_CODE553 Local part must not begin with a dot"
dnl', `dnl')

# now delete the local info -- note $=O to find characters that cause forwarding
R$* < @ > $*		$@ $>Parse0 $>canonify $1	user@ => user
R< @ $=w . > : $*	$@ $>Parse0 $>canonify $2	@here:... -> ...
R$- < @ $=w . >		$: $(dequote $1 $) < @ $2 . >	dequote "foo"@here
R< @ $+ >		$#error $@ 5.1.3 $: "_CODE553 User address required"
R$* $=O $* < @ $=w . >	$@ $>Parse0 $>canonify $1 $2 $3	...@here -> ...
R$- 			$: $(dequote $1 $) < @ *LOCAL* >	dequote "foo"
R< @ *LOCAL* >		$#error $@ 5.1.3 $: "_CODE553 User address required"
R$* $=O $* < @ *LOCAL* >
			$@ $>Parse0 $>canonify $1 $2 $3	...@*LOCAL* -> ...
R$* < @ *LOCAL* >	$: $1

ifdef(`_ADD_BCC_', `dnl
R$+			$: $>ParseBcc $1', `dnl')
ifdef(`_PREFIX_MOD_', `dnl
dnl do this only for addr_type=e r?
R _PREFIX_MOD_ $+	$: $1 $(macro {rcpt_flags} $@ _PREFIX_FLAGS_ $)
')dnl

#
#  Parse1 -- the bottom half of ruleset 0.
#

SParse1
ifdef(`_LDAP_ROUTING_', `dnl
# handle LDAP routing for hosts in $={LDAPRoute}
R$+ < @ $={LDAPRoute} . >	$: $>LDAPExpand <$1 < @ $2 . >> <$1 @ $2> <>
R$+ < @ $={LDAPRouteEquiv} . >	$: $>LDAPExpand <$1 < @ $2 . >> <$1 @ $M> <>',
`dnl')

ifdef(`_MAILER_smtp_',
`# handle numeric address spec
dnl there is no check whether this is really an IP number
R$* < @ [ $+ ] > $*	$: $>ParseLocal $1 < @ [ $2 ] > $3	numeric internet spec
R$* < @ [ $+ ] > $*	$: $1 < @ [ $2 ] : $S > $3	Add smart host to path
R$* < @ [ $+ ] : > $*		$#_SMTP_ $@ [$2] $: $1 < @ [$2] > $3	no smarthost: send
R$* < @ [ $+ ] : $- : $*> $*	$#$3 $@ $4 $: $1 < @ [$2] > $5	smarthost with mailer
R$* < @ [ $+ ] : $+ > $*	$#_SMTP_ $@ $3 $: $1 < @ [$2] > $4	smarthost without mailer',
	`dnl')

ifdef(`_VIRTUSER_TABLE_', `dnl
# handle virtual users
ifdef(`_VIRTUSER_STOP_ONE_LEVEL_RECURSION_',`dnl
dnl this is not a documented option
dnl it stops looping in virtusertable mapping if input and output
dnl are identical, i.e., if address A is mapped to A.
dnl it does not deal with multi-level recursion
# handle full domains in RHS of virtusertable
R$+ < @ $+ >			$: $(macro {RecipientAddress} $) $1 < @ $2 >
R$+ < @ $+ > 			$: <?> $1 < @ $2 > $| $>final $1 < @ $2 >
R<?> $+ $| $+			$: $1 $(macro {RecipientAddress} $@ $2 $)
R<?> $+ $| $*			$: $1',
`dnl')
R$+			$: <!> $1		Mark for lookup
dnl input: <!> local<@domain>
ifdef(`_VIRTUSER_ENTIRE_DOMAIN_',
`R<!> $+ < @ $* $={VirtHost} . > 	$: < $(virtuser $1 @ $2 $3 $@ $1 $: @ $) > $1 < @ $2 $3 . >',
`R<!> $+ < @ $={VirtHost} . > 	$: < $(virtuser $1 @ $2 $@ $1 $: @ $) > $1 < @ $2 . >')
dnl input: <result-of-lookup | @> local<@domain> | <!> local<@domain>
R<!> $+ < @ $=w . > 	$: < $(virtuser $1 @ $2 $@ $1 $: @ $) > $1 < @ $2 . >
dnl if <@> local<@domain>: no match but try lookup
dnl user+detail: try user++@domain if detail not empty
R<@> $+ + $+ < @ $* . >
			$: < $(virtuser $1 + + @ $3 $@ $1 $@ $2 $@ +$2 $: @ $) > $1 + $2 < @ $3 . >
dnl user+detail: try user+*@domain
R<@> $+ + $* < @ $* . >
			$: < $(virtuser $1 + * @ $3 $@ $1 $@ $2 $@ +$2 $: @ $) > $1 + $2 < @ $3 . >
dnl user+detail: try user@domain
R<@> $+ + $* < @ $* . >
			$: < $(virtuser $1 @ $3 $@ $1 $@ $2 $@ +$2 $: @ $) > $1 + $2 < @ $3 . >
dnl try default entry: @domain
dnl ++@domain
R<@> $+ + $+ < @ $+ . >	$: < $(virtuser + + @ $3 $@ $1 $@ $2 $@ +$2 $: @ $) > $1 + $2 < @ $3 . >
dnl +*@domain
R<@> $+ + $* < @ $+ . >	$: < $(virtuser + * @ $3 $@ $1 $@ $2 $@ +$2 $: @ $) > $1 + $2 < @ $3 . >
dnl @domain if +detail exists
dnl if no match, change marker to prevent a second @domain lookup
R<@> $+ + $* < @ $+ . >	$: < $(virtuser @ $3 $@ $1 $@ $2 $@ +$2 $: ! $) > $1 + $2 < @ $3 . >
dnl without +detail
R<@> $+ < @ $+ . >	$: < $(virtuser @ $2 $@ $1 $: @ $) > $1 < @ $2 . >
dnl no match
R<@> $+			$: $1
dnl remove mark
R<!> $+			$: $1
R< error : $-.$-.$- : $+ > $* 	$#error $@ $1.$2.$3 $: $4
R< error : $- $+ > $* 	$#error $@ $(dequote $1 $) $: $2
ifdef(`_VIRTUSER_STOP_ONE_LEVEL_RECURSION_',`dnl
# check virtuser input address against output address, if same, skip recursion
R< $+ > $+ < @ $+ >				$: < $1 > $2 < @ $3 > $| $1
# it is the same: stop now
R< $+ > $+ < @ $+ > $| $&{RecipientAddress}	$: $>ParseLocal $>Parse0 $>canonify $1
R< $+ > $+ < @ $+ > $| $* 			$: < $1 > $2 < @ $3 >
dnl', `dnl')
dnl this is not a documented option
dnl it performs no looping at all for virtusertable
ifdef(`_NO_VIRTUSER_RECURSION_',
`R< $+ > $+ < @ $+ >	$: $>ParseLocal $>Parse0 $>canonify $1',
`R< $+ > $+ < @ $+ >	$: $>Recurse $1')
dnl', `dnl')

# short circuit local delivery so forwarded email works
ifdef(`_MAILER_usenet_', `dnl
R$+ . USENET < @ $=w . >	$#usenet $@ usenet $: $1	handle usenet specially', `dnl')


ifdef(`_STICKY_LOCAL_DOMAIN_',
`R$+ < @ $=w . >		$: < $H > $1 < @ $2 . >		first try hub
R< $+ > $+ < $+ >	$>MailerToTriple < $1 > $2 < $3 >	yep ....
dnl $H empty (but @$=w.)
R< > $+ + $* < $+ >	$#_LOCAL_ $: $1 + $2		plussed name?
R< > $+ < $+ >		$#_LOCAL_ $: @ $1			nope, local address',
`R$=L < @ $=w . >	$#_LOCAL_ $: @ $1			special local names
R$+ < @ $=w . >		$#_LOCAL_ $: $1			regular local name')

ifdef(`_MAILER_TABLE_', `dnl
# not local -- try mailer table lookup
R$* <@ $+ > $*		$: < $2 > $1 < @ $2 > $3	extract host name
R< $+ . > $*		$: < $1 > $2			strip trailing dot
R< $+ > $*		$: < $(mailertable $1 $) > $2	lookup
dnl it is $~[ instead of $- to avoid matches on IPv6 addresses
R< $~[ : $* > $* 	$>MailerToTriple < $1 : $2 > $3		check -- resolved?
R< $+ > $*		$: $>Mailertable <$1> $2		try domain',
`dnl')
undivert(4)dnl UUCP rules from `MAILER(uucp)'

ifdef(`_NO_UUCP_', `dnl',
`# resolve remotely connected UUCP links (if any)
ifdef(`_CLASS_V_',
`R$* < @ $=V . UUCP . > $*		$: $>MailerToTriple < $V > $1 <@$2.UUCP.> $3',
	`dnl')
ifdef(`_CLASS_W_',
`R$* < @ $=W . UUCP . > $*		$: $>MailerToTriple < $W > $1 <@$2.UUCP.> $3',
	`dnl')
ifdef(`_CLASS_X_',
`R$* < @ $=X . UUCP . > $*		$: $>MailerToTriple < $X > $1 <@$2.UUCP.> $3',
	`dnl')')

# resolve fake top level domains by forwarding to other hosts
ifdef(`BITNET_RELAY',
`R$*<@$+.BITNET.>$*	$: $>MailerToTriple < $B > $1 <@$2.BITNET.> $3	user@host.BITNET',
	`dnl')
ifdef(`DECNET_RELAY',
`R$*<@$+.DECNET.>$*	$: $>MailerToTriple < $C > $1 <@$2.DECNET.> $3	user@host.DECNET',
	`dnl')
ifdef(`_MAILER_pop_',
`R$+ < @ POP. >		$#pop $: $1			user@POP',
	`dnl')
ifdef(`_MAILER_fax_',
`R$+ < @ $+ .FAX. >	$#fax $@ $2 $: $1		user@host.FAX',
`ifdef(`FAX_RELAY',
`R$*<@$+.FAX.>$*		$: $>MailerToTriple < $F > $1 <@$2.FAX.> $3	user@host.FAX',
	`dnl')')

ifdef(`UUCP_RELAY',
`# forward non-local UUCP traffic to our UUCP relay
R$*<@$*.UUCP.>$*		$: $>MailerToTriple < $Y > $1 <@$2.UUCP.> $3	uucp mail',
`ifdef(`_MAILER_uucp_',
`# forward other UUCP traffic straight to UUCP
R$* < @ $+ .UUCP. > $*		$#_UUCP_ $@ $2 $: $1 < @ $2 .UUCP. > $3	user@host.UUCP',
	`dnl')')
ifdef(`_MAILER_usenet_', `
# addresses sent to net.group.USENET will get forwarded to a newsgroup
R$+ . USENET		$#usenet $@ usenet $: $1',
	`dnl')

ifdef(`_LOCAL_RULES_',
`# figure out what should stay in our local mail system
undivert(1)', `dnl')

# pass names that still have a host to a smarthost (if defined)
R$* < @ $* > $*		$: $>MailerToTriple < $S > $1 < @ $2 > $3	glue on smarthost name

# deal with other remote names
ifdef(`_MAILER_smtp_',
`R$* < @$* > $*		$#_SMTP_ $@ $2 $: $1 < @ $2 > $3	user@host.domain',
`R$* < @$* > $*		$#error $@ 5.1.2 $: "_CODE553 Unrecognized host name " $2')

# handle locally delivered names
R$=L			$#_LOCAL_ $: @ $1		special local names
R$+			$#_LOCAL_ $: $1			regular local names

ifdef(`_ADD_BCC_', `dnl
SParseBcc
R$+			$: $&{addr_type} $| $&A $| $1
Re b $| $+ $| $+	$>MailerToTriple < $1 > $2	copy?
R$* $| $* $| $+		$@ $3				no copy
')

###########################################################################
###   Ruleset 5 -- special rewriting after aliases have been expanded   ###
###########################################################################

SLocal_localaddr
Slocaladdr=5
R$+			$: $1 $| $>"Local_localaddr" $1
R$+ $| $#ok		$@ $1			no change
R$+ $| $#$*		$#$2
R$+ $| $*		$: $1

ifdef(`_PRESERVE_LUSER_HOST_', `dnl
# Preserve rcpt_host in {Host}
R$+			$: $1 $| $&h $| $&{Host}	check h and {Host}
R$+ $| $|		$: $(macro {Host} $@ $) $1	no h or {Host}
R$+ $| $| $+		$: $1			h not set, {Host} set
R$+ $| +$* $| $*	$: $1			h is +detail, {Host} set
R$+ $| $* @ $+ $| $*	$: $(macro {Host} $@ @$3 $) $1	set {Host} to host in h
R$+ $| $+ $| $*		$: $(macro {Host} $@ @$2 $) $1	set {Host} to h
')dnl

ifdef(`_FFR_5_', `dnl
# Preserve host in a macro
R$+			$: $(macro {LocalAddrHost} $) $1
R$+ @ $+		$: $(macro {LocalAddrHost} $@ @ $2 $) $1')

ifdef(`_PRESERVE_LOCAL_PLUS_DETAIL_', `', `dnl
# deal with plussed users so aliases work nicely
R$+ + *			$#_LOCAL_ $@ $&h $: $1`'ifdef(`_FFR_5_', ` $&{LocalAddrHost}')
R$+ + $*		$#_LOCAL_ $@ + $2 $: $1 + *`'ifdef(`_FFR_5_', ` $&{LocalAddrHost}')
')
# prepend an empty "forward host" on the front
R$+			$: <> $1

ifdef(`LUSER_RELAY', `dnl
# send unrecognized local users to a relay host
ifdef(`_PRESERVE_LOCAL_PLUS_DETAIL_', `dnl
R< > $+ + $*		$: < ? $L > <+ $2> $(user $1 $)	look up user+
R< > $+			$: < ? $L > < > $(user $1 $)	look up user
R< ? $* > < $* > $+ <>	$: < > $3 $2			found; strip $L
R< ? $* > < $* > $+	$: < $1 > $3 $2			not found', `
R< > $+ 		$: < $L > $(user $1 $)		look up user
R< $* > $+ <>		$: < > $2			found; strip $L')
ifdef(`_PRESERVE_LUSER_HOST_', `dnl
R< $+ > $+		$: < $1 > $2 $&{Host}')
dnl')

ifdef(`MAIL_HUB', `dnl
R< > $+			$: < $H > $1			try hub', `dnl')
ifdef(`LOCAL_RELAY', `dnl
R< > $+			$: < $R > $1			try relay', `dnl')
ifdef(`_PRESERVE_LOCAL_PLUS_DETAIL_', `dnl
R< > $+			$@ $1', `dnl
R< > $+			$: < > < $1 <> $&h >		nope, restore +detail
ifdef(`_PRESERVE_LUSER_HOST_', `dnl
R< > < $+ @ $+ <> + $* >	$: < > < $1 + $3 @ $2 >	check whether +detail')
R< > < $+ <> + $* >	$: < > < $1 + $2 >		check whether +detail
R< > < $+ <> $* >	$: < > < $1 >			else discard
R< > < $+ + $* > $*	   < > < $1 > + $2 $3		find the user part
R< > < $+ > + $*	$#_LOCAL_ $@ $2 $: @ $1`'ifdef(`_FFR_5_', ` $&{LocalAddrHost}')		strip the extra +
R< > < $+ >		$@ $1				no +detail
R$+			$: $1 <> $&h			add +detail back in
ifdef(`_PRESERVE_LUSER_HOST_', `dnl
R$+ @ $+ <> + $*	$: $1 + $3 @ $2			check whether +detail')
R$+ <> + $*		$: $1 + $2			check whether +detail
R$+ <> $*		$: $1				else discard')
R< local : $* > $*	$: $>MailerToTriple < local : $1 > $2	no host extension
R< error : $* > $*	$: $>MailerToTriple < error : $1 > $2	no host extension
ifdef(`_PRESERVE_LUSER_HOST_', `dnl
dnl it is $~[ instead of $- to avoid matches on IPv6 addresses
R< $~[ : $+ > $+ @ $+	$: $>MailerToTriple < $1 : $2 > $3 < @ $4 >')
R< $~[ : $+ > $+	$: $>MailerToTriple < $1 : $2 > $3 < @ $2 >
ifdef(`_PRESERVE_LUSER_HOST_', `dnl
R< $+ > $+ @ $+		$@ $>MailerToTriple < $1 > $2 < @ $3 >')
R< $+ > $+		$@ $>MailerToTriple < $1 > $2 < @ $1 >

ifdef(`_MAILER_TABLE_', `dnl
ifdef(`_LDAP_ROUTING_', `dnl
###################################################################
###  Ruleset LDAPMailertable -- mailertable lookup for LDAP     ###
dnl input: <Domain> FullAddress
###################################################################

SLDAPMailertable
R< $+ > $*		$: < $(mailertable $1 $) > $2		lookup
R< $~[ : $* > $*	$>MailerToTriple < $1 : $2 > $3		check resolved?
R< $+ > $*		$: < $1 > $>Mailertable <$1> $2		try domain
R< $+ > $#$*		$#$2					found
R< $+ > $*		$#_RELAY_ $@ $1 $: $2			not found, direct relay',
`dnl')

###################################################################
###  Ruleset 90 -- try domain part of mailertable entry 	###
dnl input: LeftPartOfDomain <RightPartOfDomain> FullAddress
###################################################################

SMailertable=90
dnl shift and check
dnl %2 is not documented in cf/README
R$* <$- . $+ > $*	$: $1$2 < $(mailertable .$3 $@ $1$2 $@ $2 $) > $4
dnl it is $~[ instead of $- to avoid matches on IPv6 addresses
R$* <$~[ : $* > $*	$>MailerToTriple < $2 : $3 > $4		check -- resolved?
R$* < . $+ > $* 	$@ $>Mailertable $1 . <$2> $3		no -- strip & try again
dnl is $2 always empty?
R$* < $* > $*		$: < $(mailertable . $@ $1$2 $) > $3	try "."
R< $~[ : $* > $*	$>MailerToTriple < $1 : $2 > $3		"." found?
dnl return full address
R< $* > $*		$@ $2				no mailertable match',
`dnl')

###################################################################
###  Ruleset 95 -- canonify mailer:[user@]host syntax to triple	###
dnl input: in general: <[mailer:]host> lp<@domain>rest
dnl	<> address				-> address
dnl	<error:d.s.n:text>			-> error
dnl	<error:keyword:text>			-> error
dnl	<error:text>				-> error
dnl	<mailer:user@host> lp<@domain>rest	-> mailer host user
dnl	<mailer:host> address			-> mailer host address
dnl	<localdomain> address			-> address
dnl	<host> address				-> relay host address
###################################################################

SMailerToTriple=95
R< > $*				$@ $1			strip off null relay
R< error : $-.$-.$- : $+ > $* 	$#error $@ $1.$2.$3 $: $4
R< error : $- : $+ > $*		$#error $@ $(dequote $1 $) $: $2
R< error : $+ > $*		$#error $: $1
R< local : $* > $*		$>CanonLocal < $1 > $2
dnl it is $~[ instead of $- to avoid matches on IPv6 addresses
R< $~[ : $+ @ $+ > $*<$*>$*	$# $1 $@ $3 $: $2<@$3>	use literal user
R< $~[ : $+ > $*		$# $1 $@ $2 $: $3	try qualified mailer
R< $=w > $*			$@ $2			delete local host
R< $+ > $*			$#_RELAY_ $@ $1 $: $2	use unqualified mailer

###################################################################
###  Ruleset CanonLocal -- canonify local: syntax		###
dnl input: <user> address
dnl <x> <@host> : rest			-> Recurse rest
dnl <x> p1 $=O p2 <@host>		-> Recurse p1 $=O p2
dnl <> user <@host> rest		-> local user@host user
dnl <> user				-> local user user
dnl <user@host> lp <@domain> rest	-> <user> lp <@host> [cont]
dnl <user> lp <@host> rest		-> local lp@host user
dnl <user> lp				-> local lp user
###################################################################

SCanonLocal
# strip local host from routed addresses
R< $* > < @ $+ > : $+		$@ $>Recurse $3
R< $* > $+ $=O $+ < @ $+ >	$@ $>Recurse $2 $3 $4

# strip trailing dot from any host name that may appear
R< $* > $* < @ $* . >		$: < $1 > $2 < @ $3 >

# handle local: syntax -- use old user, either with or without host
R< > $* < @ $* > $*		$#_LOCAL_ $@ $1@$2 $: $1
R< > $+				$#_LOCAL_ $@ $1    $: $1

# handle local:user@host syntax -- ignore host part
R< $+ @ $+ > $* < @ $* >	$: < $1 > $3 < @ $4 >

# handle local:user syntax
R< $+ > $* <@ $* > $*		$#_LOCAL_ $@ $2@$3 $: $1
R< $+ > $* 			$#_LOCAL_ $@ $2    $: $1

###################################################################
###  Ruleset 93 -- convert header names to masqueraded form	###
###################################################################

SMasqHdr=93

ifdef(`_GENERICS_TABLE_', `dnl
# handle generics database
ifdef(`_GENERICS_ENTIRE_DOMAIN_',
dnl if generics should be applied add a @ as mark
`R$+ < @ $* $=G . >	$: < $1@$2$3 > $1 < @ $2$3 . > @	mark',
`R$+ < @ $=G . >	$: < $1@$2 > $1 < @ $2 . > @	mark')
R$+ < @ *LOCAL* >	$: < $1@$j > $1 < @ *LOCAL* > @	mark
dnl workspace: either user<@domain> or <user@domain> user <@domain> @
dnl ignore the first case for now
dnl if it has the mark lookup full address
dnl broken: %1 is full address not just detail
R< $+ > $+ < $* > @	$: < $(generics $1 $: @ $1 $) > $2 < $3 >
dnl workspace: ... or <match|@user@domain> user <@domain>
dnl no match, try user+detail@domain
R<@$+ + $* @ $+> $+ < @ $+ >
		$: < $(generics $1+*@$3 $@ $2 $:@$1 + $2@$3 $) >  $4 < @ $5 >
R<@$+ + $* @ $+> $+ < @ $+ >
		$: < $(generics $1@$3 $: $) > $4 < @ $5 >
dnl no match, remove mark
R<@$+ > $+ < @ $+ >	$: < > $2 < @ $3 >
dnl no match, try @domain for exceptions
R< > $+ < @ $+ . >	$: < $(generics @$2 $@ $1 $: $) > $1 < @ $2 . >
dnl workspace: ... or <match> user <@domain>
dnl no match, try local part
R< > $+ < @ $+ > 	$: < $(generics $1 $: $) > $1 < @ $2 >
R< > $+ + $* < @ $+ > 	$: < $(generics $1+* $@ $2 $: $) > $1 + $2 < @ $3 >
R< > $+ + $* < @ $+ > 	$: < $(generics $1 $: $) > $1 + $2 < @ $3 >
R< $* @ $* > $* < $* >	$@ $>canonify $1 @ $2		found qualified
R< $+ > $* < $* >	$: $>canonify $1 @ *LOCAL*	found unqualified
R< > $*			$: $1				not found',
`dnl')

# do not masquerade anything in class N
R$* < @ $* $=N . >	$@ $1 < @ $2 $3 . >

ifdef(`MASQUERADE_NAME', `dnl
# special case the users that should be exposed
R$=E < @ *LOCAL* >	$@ $1 < @ $j . >		leave exposed
ifdef(`_MASQUERADE_ENTIRE_DOMAIN_',
`R$=E < @ $* $=M . >	$@ $1 < @ $2 $3 . >',
`R$=E < @ $=M . >	$@ $1 < @ $2 . >')
ifdef(`_LIMITED_MASQUERADE_', `dnl',
`R$=E < @ $=w . >	$@ $1 < @ $2 . >')

# handle domain-specific masquerading
ifdef(`_MASQUERADE_ENTIRE_DOMAIN_',
`R$* < @ $* $=M . > $*	$: $1 < @ $2 $3 . @ $M > $4	convert masqueraded doms',
`R$* < @ $=M . > $*	$: $1 < @ $2 . @ $M > $3	convert masqueraded doms')
ifdef(`_LIMITED_MASQUERADE_', `dnl',
`R$* < @ $=w . > $*	$: $1 < @ $2 . @ $M > $3')
R$* < @ *LOCAL* > $*	$: $1 < @ $j . @ $M > $2
R$* < @ $+ @ > $*	$: $1 < @ $2 > $3		$M is null
R$* < @ $+ @ $+ > $*	$: $1 < @ $3 . > $4		$M is not null
dnl', `dnl no masquerading
dnl just fix *LOCAL* leftovers
R$* < @ *LOCAL* >	$@ $1 < @ $j . >')

###################################################################
###  Ruleset 94 -- convert envelope names to masqueraded form	###
###################################################################

SMasqEnv=94
ifdef(`_MASQUERADE_ENVELOPE_',
`R$+			$@ $>MasqHdr $1',
`R$* < @ *LOCAL* > $*	$: $1 < @ $j . > $2')

###################################################################
###  Ruleset 98 -- local part of ruleset zero (can be null)	###
###################################################################

SParseLocal=98
undivert(3)dnl LOCAL_RULE_0

ifdef(`_LDAP_ROUTING_', `dnl
######################################################################
###  LDAPExpand: Expand address using LDAP routing
###
###	Parameters:
###		<$1> -- parsed address (user < @ domain . >) (pass through)
###		<$2> -- RFC822 address (user @ domain) (used for lookup)
###		<$3> -- +detail information
###
###	Returns:
###		Mailer triplet ($#mailer $@ host $: address)
###		Parsed address (user < @ domain . >)
######################################################################

SLDAPExpand
# do the LDAP lookups
R<$+><$+><$*>	$: <$(ldapmra $2 $: $)> <$(ldapmh $2 $: $)> <$1> <$2> <$3>

# look for temporary failures and...
R<$* <TMPF>> <$*> <$+> <$+> <$*>	$: $&{opMode} $| TMPF <$&{addr_type}> $| $3
R<$*> <$* <TMPF>> <$+> <$+> <$*>	$: $&{opMode} $| TMPF <$&{addr_type}> $| $3
ifelse(_LDAP_ROUTE_MAPTEMP_, `_TEMPFAIL_', `dnl
# ... temp fail RCPT SMTP commands
R$={SMTPOpModes} $| TMPF <e r> $| $+	$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."')
# ... return original address for MTA to queue up
R$* $| TMPF <$*> $| $+			$@ $3

# if mailRoutingAddress and local or non-existant mailHost,
# return the new mailRoutingAddress
ifelse(_LDAP_ROUTE_DETAIL_, `_PRESERVE_', `dnl
R<$+@$+> <$=w> <$+> <$+> <$*>	$@ $>Parse0 $>canonify $1 $6 @ $2
R<$+@$+> <> <$+> <$+> <$*>	$@ $>Parse0 $>canonify $1 $5 @ $2')
R<$+> <$=w> <$+> <$+> <$*>	$@ $>Parse0 $>canonify $1
R<$+> <> <$+> <$+> <$*>		$@ $>Parse0 $>canonify $1


# if mailRoutingAddress and non-local mailHost,
# relay to mailHost with new mailRoutingAddress
ifelse(_LDAP_ROUTE_DETAIL_, `_PRESERVE_', `dnl
ifdef(`_MAILER_TABLE_', `dnl
# check mailertable for host, relay from there
R<$+@$+> <$+> <$+> <$+> <$*>	$>LDAPMailertable <$3> $>canonify $1 $6 @ $2',
`R<$+@$+> <$+> <$+> <$+> <$*>	$#_RELAY_ $@ $3 $: $>canonify $1 $6 @ $2')')
ifdef(`_MAILER_TABLE_', `dnl
# check mailertable for host, relay from there
R<$+> <$+> <$+> <$+> <$*>	$>LDAPMailertable <$2> $>canonify $1',
`R<$+> <$+> <$+> <$+> <$*>	$#_RELAY_ $@ $2 $: $>canonify $1')

# if no mailRoutingAddress and local mailHost,
# return original address
R<> <$=w> <$+> <$+> <$*>	$@ $2


# if no mailRoutingAddress and non-local mailHost,
# relay to mailHost with original address
ifdef(`_MAILER_TABLE_', `dnl
# check mailertable for host, relay from there
R<> <$+> <$+> <$+> <$*>		$>LDAPMailertable <$1> $2',
`R<> <$+> <$+> <$+> <$*>	$#_RELAY_ $@ $1 $: $2')

ifdef(`_LDAP_ROUTE_DETAIL_',
`# if no mailRoutingAddress and no mailHost,
# try without +detail
R<> <> <$+> <$+ + $* @ $+> <>	$@ $>LDAPExpand <$1> <$2 @ $4> <+$3>')dnl

ifdef(`_LDAP_ROUTE_NODOMAIN_', `
# pretend we did the @domain lookup
R<> <> <$+> <$+ @ $+> <$*>	$: <> <> <$1> <@ $3> <$4>', `
# if still no mailRoutingAddress and no mailHost,
# try @domain
ifelse(_LDAP_ROUTE_DETAIL_, `_PRESERVE_', `dnl
R<> <> <$+> <$+ + $* @ $+> <>	$@ $>LDAPExpand <$1> <@ $4> <+$3>')
R<> <> <$+> <$+ @ $+> <$*>	$@ $>LDAPExpand <$1> <@ $3> <$4>')

# if no mailRoutingAddress and no mailHost and this was a domain attempt,
ifelse(_LDAP_ROUTING_, `_MUST_EXIST_', `dnl
# user does not exist
R<> <> <$+> <@ $+> <$*>		$: <?> < $&{addr_type} > < $1 >
# only give error for envelope recipient
R<?> <e r> <$+>			$#error $@ nouser $: "550 User unknown"
ifdef(`_LDAP_SENDER_MUST_EXIST_', `dnl
# and the sender too
R<?> <e s> <$+>			$#error $@ nouser $: "550 User unknown"')
R<?> <$*> <$+>			$@ $2',
`dnl
# return the original address
R<> <> <$+> <@ $+> <$*>		$@ $1')
')


ifelse(substr(confDELIVERY_MODE,0,1), `d', `errprint(`WARNING: Antispam rules not available in deferred delivery mode.
')')
ifdef(`_ACCESS_TABLE_', `dnl', `divert(-1)')
######################################################################
###  D: LookUpDomain -- search for domain in access database
###
###	Parameters:
###		<$1> -- key (domain name)
###		<$2> -- default (what to return if not found in db)
dnl			must not be empty
###		<$3> -- mark (must be <(!|+) single-token>)
###			! does lookup only with tag
###			+ does lookup with and without tag
###		<$4> -- passthru (additional data passed unchanged through)
dnl returns:		<default> <passthru>
dnl 			<result> <passthru>
######################################################################

SD
dnl workspace <key> <default> <passthru> <mark>
dnl lookup with tag (in front, no delimiter here)
dnl    2    3  4    5
R<$*> <$+> <$- $-> <$*>		$: < $(access $4`'_TAG_DELIM_`'$1 $: ? $) > <$1> <$2> <$3 $4> <$5>
dnl workspace <result-of-lookup|?> <key> <default> <passthru> <mark>
dnl lookup without tag?
dnl   1    2      3    4
R<?> <$+> <$+> <+ $-> <$*>	$: < $(access $1 $: ? $) > <$1> <$2> <+ $3> <$4>
ifdef(`_LOOKUPDOTDOMAIN_', `dnl omit first component: lookup .rest
dnl XXX apply this also to IP addresses?
dnl currently it works the wrong way round for [1.2.3.4]
dnl   1  2    3    4  5    6
R<?> <$+.$+> <$+> <$- $-> <$*>	$: < $(access $5`'_TAG_DELIM_`'.$2 $: ? $) > <$1.$2> <$3> <$4 $5> <$6>
dnl   1  2    3      4    5
R<?> <$+.$+> <$+> <+ $-> <$*>	$: < $(access .$2 $: ? $) > <$1.$2> <$3> <+ $4> <$5>', `dnl')
ifdef(`_ACCESS_SKIP_', `dnl
dnl found SKIP: return <default> and <passthru>
dnl      1    2    3  4    5
R<SKIP> <$+> <$+> <$- $-> <$*>	$@ <$2> <$5>', `dnl')
dnl not found: IPv4 net (no check is done whether it is an IP number!)
dnl    1  2     3    4  5    6
R<?> <[$+.$-]> <$+> <$- $-> <$*>	$@ $>D <[$1]> <$3> <$4 $5> <$6>
ifdef(`NO_NETINET6', `dnl',
`dnl not found: IPv6 net
dnl (could be merged with previous rule if we have a class containing .:)
dnl    1   2     3    4  5    6
R<?> <[$+::$-]> <$+> <$- $-> <$*>	$: $>D <[$1]> <$3> <$4 $5> <$6>
R<?> <[$+:$-]> <$+> <$- $-> <$*>	$: $>D <[$1]> <$3> <$4 $5> <$6>')
dnl not found, but subdomain: try again
dnl   1  2    3    4  5    6
R<?> <$+.$+> <$+> <$- $-> <$*>	$@ $>D <$2> <$3> <$4 $5> <$6>
ifdef(`_FFR_LOOKUPTAG_', `dnl lookup Tag:
dnl   1    2      3    4
R<?> <$+> <$+> <! $-> <$*>	$: < $(access $3`'_TAG_DELIM_ $: ? $) > <$1> <$2> <! $3> <$4>', `dnl')
dnl not found, no subdomain: return <default> and <passthru>
dnl   1    2    3  4    5
R<?> <$+> <$+> <$- $-> <$*>	$@ <$2> <$5>
ifdef(`_ATMPF_', `dnl tempfail?
dnl            2    3    4  5    6
R<$* _ATMPF_> <$+> <$+> <$- $-> <$*>	$@ <_ATMPF_> <$6>', `dnl')
dnl return <result of lookup> and <passthru>
dnl    2    3    4  5    6
R<$*> <$+> <$+> <$- $-> <$*>	$@ <$1> <$6>

######################################################################
###  A: LookUpAddress -- search for host address in access database
###
###	Parameters:
###		<$1> -- key (dot quadded host address)
###		<$2> -- default (what to return if not found in db)
dnl			must not be empty
###		<$3> -- mark (must be <(!|+) single-token>)
###			! does lookup only with tag
###			+ does lookup with and without tag
###		<$4> -- passthru (additional data passed through)
dnl	returns:	<default> <passthru>
dnl			<result> <passthru>
######################################################################

SA
dnl lookup with tag
dnl    2    3  4    5
R<$+> <$+> <$- $-> <$*>		$: < $(access $4`'_TAG_DELIM_`'$1 $: ? $) > <$1> <$2> <$3 $4> <$5>
dnl lookup without tag
dnl   1    2      3    4
R<?> <$+> <$+> <+ $-> <$*>	$: < $(access $1 $: ? $) > <$1> <$2> <+ $3> <$4>
dnl workspace <result-of-lookup|?> <key> <default> <mark> <passthru>
ifdef(`_ACCESS_SKIP_', `dnl
dnl found SKIP: return <default> and <passthru>
dnl      1    2    3  4    5
R<SKIP> <$+> <$+> <$- $-> <$*>	$@ <$2> <$5>', `dnl')
ifdef(`NO_NETINET6', `dnl',
`dnl no match; IPv6: remove last part
dnl   1   2    3    4  5    6
R<?> <$+::$-> <$+> <$- $-> <$*>		$@ $>A <$1> <$3> <$4 $5> <$6>
R<?> <$+:$-> <$+> <$- $-> <$*>		$@ $>A <$1> <$3> <$4 $5> <$6>')
dnl no match; IPv4: remove last part
dnl   1  2    3    4  5    6
R<?> <$+.$-> <$+> <$- $-> <$*>		$@ $>A <$1> <$3> <$4 $5> <$6>
dnl no match: return default
dnl   1    2    3  4    5
R<?> <$+> <$+> <$- $-> <$*>	$@ <$2> <$5>
ifdef(`_ATMPF_', `dnl tempfail?
dnl            2    3    4  5    6
R<$* _ATMPF_> <$+> <$+> <$- $-> <$*>	$@ <_ATMPF_> <$6>', `dnl')
dnl match: return result
dnl    2    3    4  5    6
R<$*> <$+> <$+> <$- $-> <$*>	$@ <$1> <$6>
dnl endif _ACCESS_TABLE_
divert(0)
######################################################################
###  CanonAddr --	Convert an address into a standard form for
###			relay checking.  Route address syntax is
###			crudely converted into a %-hack address.
###
###	Parameters:
###		$1 -- full recipient address
###
###	Returns:
###		parsed address, not in source route form
dnl		user%host%host<@domain>
dnl		host!user<@domain>
######################################################################

SCanonAddr
R$*			$: $>Parse0 $>canonify $1	make domain canonical
ifdef(`_USE_DEPRECATED_ROUTE_ADDR_',`dnl
R< @ $+ > : $* @ $*	< @ $1 > : $2 % $3	change @ to % in src route
R$* < @ $+ > : $* : $*	$3 $1 < @ $2 > : $4	change to % hack.
R$* < @ $+ > : $*	$3 $1 < @ $2 >
dnl')

######################################################################
###  ParseRecipient --	Strip off hosts in $=R as well as possibly
###			$* $=m or the access database.
###			Check user portion for host separators.
###
###	Parameters:
###		$1 -- full recipient address
###
###	Returns:
###		parsed, non-local-relaying address
######################################################################

SParseRecipient
dnl mark and canonify address
R$*				$: <?> $>CanonAddr $1
dnl workspace: <?> localpart<@domain[.]>
R<?> $* < @ $* . >		<?> $1 < @ $2 >			strip trailing dots
dnl workspace: <?> localpart<@domain>
R<?> $- < @ $* >		$: <?> $(dequote $1 $) < @ $2 >	dequote local part

# if no $=O character, no host in the user portion, we are done
R<?> $* $=O $* < @ $* >		$: <NO> $1 $2 $3 < @ $4>
dnl no $=O in localpart: return
R<?> $*				$@ $1

dnl workspace: <NO> localpart<@domain>, where localpart contains $=O
dnl mark everything which has an "authorized" domain with <RELAY>
ifdef(`_RELAY_ENTIRE_DOMAIN_', `dnl
# if we relay, check username portion for user%host so host can be checked also
R<NO> $* < @ $* $=m >		$: <RELAY> $1 < @ $2 $3 >', `dnl')
dnl workspace: <(NO|RELAY)> localpart<@domain>, where localpart contains $=O
dnl if mark is <NO> then change it to <RELAY> if domain is "authorized"

dnl what if access map returns something else than RELAY?
dnl we are only interested in RELAY entries...
dnl other To: entries: blacklist recipient; generic entries?
dnl if it is an error we probably do not want to relay anyway
ifdef(`_RELAY_HOSTS_ONLY_',
`R<NO> $* < @ $=R >		$: <RELAY> $1 < @ $2 >
ifdef(`_ACCESS_TABLE_', `dnl
R<NO> $* < @ $+ >		$: <$(access To:$2 $: NO $)> $1 < @ $2 >
R<NO> $* < @ $+ >		$: <$(access $2 $: NO $)> $1 < @ $2 >',`dnl')',
`R<NO> $* < @ $* $=R >		$: <RELAY> $1 < @ $2 $3 >
ifdef(`_ACCESS_TABLE_', `dnl
R<NO> $* < @ $+ >		$: $>D <$2> <NO> <+ To> <$1 < @ $2 >>
R<$+> <$+>			$: <$1> $2',`dnl')')


ifdef(`_RELAY_MX_SERVED_', `dnl
dnl do "we" ($=w) act as backup MX server for the destination domain?
R<NO> $* < @ $+ >		$: <MX> < : $(mxserved $2 $) : > < $1 < @$2 > >
R<MX> < : $* <TEMP> : > $*	$#TEMP $@ 4.4.0 $: "450 Can not check MX records for recipient host " $1
dnl yes: mark it as <RELAY>
R<MX> < $* : $=w. : $* > < $+ >	$: <RELAY> $4
dnl no: put old <NO> mark back
R<MX> < : $* : > < $+ >		$: <NO> $2', `dnl')

dnl do we relay to this recipient domain?
R<RELAY> $* < @ $* >		$@ $>ParseRecipient $1
dnl something else
R<$+> $*			$@ $2


######################################################################
###  check_relay -- check hostname/address on SMTP startup
######################################################################

ifdef(`_CONTROL_IMMEDIATE_',`dnl
Scheck_relay
ifdef(`_RATE_CONTROL_IMMEDIATE_',`dnl
dnl workspace: ignored...
R$*		$: $>"RateControl" dummy', `dnl')
ifdef(`_CONN_CONTROL_IMMEDIATE_',`dnl
dnl workspace: ignored...
R$*		$: $>"ConnControl" dummy', `dnl')
dnl')

SLocal_check_relay
Scheck`'_U_`'relay
ifdef(`_USE_CLIENT_PTR_',`dnl
R$* $| $*		$: $&{client_ptr} $| $2', `dnl')
R$*			$: $1 $| $>"Local_check_relay" $1
R$* $| $* $| $#$*	$#$3
R$* $| $* $| $*		$@ $>"Basic_check_relay" $1 $| $2

SBasic_check_relay
# check for deferred delivery mode
R$*			$: < $&{deliveryMode} > $1
R< d > $*		$@ deferred
R< $* > $*		$: $2

ifdef(`_ACCESS_TABLE_', `dnl
dnl workspace: {client_name} $| {client_addr}
R$+ $| $+		$: $>D < $1 > <?> <+ Connect> < $2 >
dnl workspace: <result-of-lookup> <{client_addr}>
dnl OR $| $+ if client_name is empty
R   $| $+		$: $>A < $1 > <?> <+ Connect> <>	empty client_name
dnl workspace: <result-of-lookup> <{client_addr}>
R<?> <$+>		$: $>A < $1 > <?> <+ Connect> <>	no: another lookup
dnl workspace: <result-of-lookup> (<>|<{client_addr}>)
R<?> <$*>		$: OK				found nothing
dnl workspace: <result-of-lookup> (<>|<{client_addr}>) | OK
R<$={Accept}> <$*>	$@ $1				return value of lookup
R<REJECT> <$*>		$#error ifdef(`confREJECT_MSG', `$: confREJECT_MSG', `$@ 5.7.1 $: "550 Access denied"')
R<DISCARD> <$*>		$#discard $: discard
R<QUARANTINE:$+> <$*>	$#error $@ quarantine $: $1
dnl error tag
R<ERROR:$-.$-.$-:$+> <$*>	$#error $@ $1.$2.$3 $: $4
R<ERROR:$+> <$*>		$#error $: $1
ifdef(`_ATMPF_', `R<$* _ATMPF_> <$*>		$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
dnl generic error from access map
R<$+> <$*>		$#error $: $1', `dnl')

ifdef(`_RBL_',`dnl
# DNS based IP address spam list
dnl workspace: ignored...
R$*			$: $&{client_addr}
R$-.$-.$-.$-		$: <?> $(host $4.$3.$2.$1._RBL_. $: OK $)
R<?>OK			$: OKSOFAR
R<?>$+			$#error $@ 5.7.1 $: "550 Rejected: " $&{client_addr} " listed at _RBL_"',
`dnl')
ifdef(`_RATE_CONTROL_',`dnl
ifdef(`_RATE_CONTROL_IMMEDIATE_',`', `dnl
dnl workspace: ignored...
R$*		$: $>"RateControl" dummy')', `dnl')
ifdef(`_CONN_CONTROL_',`dnl
ifdef(`_CONN_CONTROL_IMMEDIATE_',`',`dnl
dnl workspace: ignored...
R$*		$: $>"ConnControl" dummy')', `dnl')
undivert(8)dnl LOCAL_DNSBL
ifdef(`_REQUIRE_RDNS_', `dnl
R$*			$: $&{client_addr} $| $&{client_resolve}
R$=R $*			$@ RELAY		We relay for these
R$* $| OK		$@ OK			Resolves.
R$* $| FAIL		$#error $@ 5.7.1 $: 550 Fix reverse DNS for $1
R$* $| TEMP		$#error $@ 4.1.8 $: 451 Client IP address $1 does not resolve
R$* $| FORGED		$#error $@ 4.1.8 $: 451 Possibly forged hostname for $1
', `dnl')

######################################################################
###  check_mail -- check SMTP ``MAIL FROM:'' command argument
######################################################################

SLocal_check_mail
Scheck`'_U_`'mail
R$*			$: $1 $| $>"Local_check_mail" $1
R$* $| $#$*		$#$2
R$* $| $*		$@ $>"Basic_check_mail" $1

SBasic_check_mail
# check for deferred delivery mode
R$*			$: < $&{deliveryMode} > $1
R< d > $*		$@ deferred
R< $* > $*		$: $2

# authenticated?
dnl done first: we can require authentication for every mail transaction
dnl workspace: address as given by MAIL FROM: (sender)
R$*			$: $1 $| $>"tls_client" $&{verify} $| MAIL
R$* $| $#$+		$#$2
dnl undo damage: remove result of tls_client call
R$* $| $*		$: $1

dnl workspace: address as given by MAIL FROM:
R<>			$@ <OK>			we MUST accept <> (RFC 1123)
ifdef(`_ACCEPT_UNQUALIFIED_SENDERS_',`dnl',`dnl
dnl do some additional checks
dnl no user@host
dnl no user@localhost (if nonlocal sender)
dnl this is a pretty simple canonification, it will not catch every case
dnl just make sure the address has <> around it (which is required by
dnl the RFC anyway, maybe we should complain if they are missing...)
dnl dirty trick: if it is user@host, just add a dot: user@host. this will
dnl not be modified by host lookups.
R$+			$: <?> $1
R<?><$+>		$: <@> <$1>
R<?>$+			$: <@> <$1>
dnl workspace: <@> <address>
dnl prepend daemon_flags
R$*			$: $&{daemon_flags} $| $1
dnl workspace: ${daemon_flags} $| <@> <address>
dnl do not allow these at all or only from local systems?
R$* f $* $| <@> < $* @ $- >	$: < ? $&{client_name} > < $3 @ $4 >
dnl accept unqualified sender: change mark to avoid test
R$* u $* $| <@> < $* >	$: <?> < $3 >
dnl workspace: ${daemon_flags} $| <@> <address>
dnl        or:                    <? ${client_name} > <address>
dnl        or:                    <?> <address>
dnl remove daemon_flags
R$* $| $*		$: $2
# handle case of @localhost on address
R<@> < $* @ localhost >	$: < ? $&{client_name} > < $1 @ localhost >
R<@> < $* @ [127.0.0.1] >
			$: < ? $&{client_name} > < $1 @ [127.0.0.1] >
R<@> < $* @ [IPv6:0:0:0:0:0:0:0:1] >
			$: < ? $&{client_name} > < $1 @ [IPv6:0:0:0:0:0:0:0:1] >
R<@> < $* @ [IPv6:::1] >
			$: < ? $&{client_name} > < $1 @ [IPv6:::1] >
R<@> < $* @ localhost.$m >
			$: < ? $&{client_name} > < $1 @ localhost.$m >
ifdef(`_NO_UUCP_', `dnl',
`R<@> < $* @ localhost.UUCP >
			$: < ? $&{client_name} > < $1 @ localhost.UUCP >')
dnl workspace: < ? $&{client_name} > <user@localhost|host>
dnl	or:    <@> <address>
dnl	or:    <?> <address>	(thanks to u in ${daemon_flags})
R<@> $*			$: $1			no localhost as domain
dnl workspace: < ? $&{client_name} > <user@localhost|host>
dnl	or:    <address>
dnl	or:    <?> <address>	(thanks to u in ${daemon_flags})
R<? $=w> $*		$: $2			local client: ok
R<? $+> <$+>		$#error $@ 5.5.4 $: "_CODE553 Real domain name required for sender address"
dnl remove <?> (happens only if ${client_name} == "" or u in ${daemon_flags})
R<?> $*			$: $1')
dnl workspace: address (or <address>)
R$*			$: <?> $>CanonAddr $1		canonify sender address and mark it
dnl workspace: <?> CanonicalAddress (i.e. address in canonical form localpart<@host>)
dnl there is nothing behind the <@host> so no trailing $* needed
R<?> $* < @ $+ . >	<?> $1 < @ $2 >			strip trailing dots
# handle non-DNS hostnames (*.bitnet, *.decnet, *.uucp, etc)
R<?> $* < @ $* $=P >	$: <_RES_OK_> $1 < @ $2 $3 >
dnl workspace <mark> CanonicalAddress	where mark is ? or OK
dnl A sender address with my local host name ($j) is safe
R<?> $* < @ $j >	$: <_RES_OK_> $1 < @ $j >
ifdef(`_ACCEPT_UNRESOLVABLE_DOMAINS_',
`R<?> $* < @ $+ >	$: <_RES_OK_> $1 < @ $2 >		... unresolvable OK',
`R<?> $* < @ $+ >	$: <? $(resolve $2 $: $2 <PERM> $) > $1 < @ $2 >
R<? $* <$->> $* < @ $+ >
			$: <$2> $3 < @ $4 >')
dnl workspace <mark> CanonicalAddress	where mark is ?, _RES_OK_, PERM, TEMP
dnl mark is ? iff the address is user (wo @domain)

ifdef(`_ACCESS_TABLE_', `dnl
# check sender address: user@address, user@, address
dnl should we remove +ext from user?
dnl workspace: <mark> CanonicalAddress where mark is: ?, _RES_OK_, PERM, TEMP
R<$+> $+ < @ $* >	$: @<$1> <$2 < @ $3 >> $| <F:$2@$3> <U:$2@> <D:$3>
R<$+> $+		$: @<$1> <$2> $| <U:$2@>
dnl workspace: @<mark> <CanonicalAddress> $| <@type:address> ....
dnl $| is used as delimiter, otherwise false matches may occur: <user<@domain>>
dnl will only return user<@domain when "reversing" the args
R@ <$+> <$*> $| <$+>	$: <@> <$1> <$2> $| $>SearchList <+ From> $| <$3> <>
dnl workspace: <@><mark> <CanonicalAddress> $| <result>
R<@> <$+> <$*> $| <$*>	$: <$3> <$1> <$2>		reverse result
dnl workspace: <result> <mark> <CanonicalAddress>
# retransform for further use
dnl required form:
dnl <ResultOfLookup|mark> CanonicalAddress
R<?> <$+> <$*>		$: <$1> $2	no match
R<$+> <$+> <$*>		$: <$1> $3	relevant result, keep it', `dnl')
dnl workspace <ResultOfLookup|mark> CanonicalAddress
dnl mark is ? iff the address is user (wo @domain)

ifdef(`_ACCEPT_UNQUALIFIED_SENDERS_',`dnl',`dnl
# handle case of no @domain on address
dnl prepend daemon_flags
R<?> $*			$: $&{daemon_flags} $| <?> $1
dnl accept unqualified sender: change mark to avoid test
R$* u $* $| <?> $*	$: <_RES_OK_> $3
dnl remove daemon_flags
R$* $| $*		$: $2
R<?> $*			$: < ? $&{client_addr} > $1
R<?> $*			$@ <_RES_OK_>			...local unqualed ok
R<? $+> $*		$#error $@ 5.5.4 $: "_CODE553 Domain name required for sender address " $&f
							...remote is not')
# check results
R<?> $*			$: @ $1		mark address: nothing known about it
R<$={ResOk}> $*		$: @ $2		domain ok
R<TEMP> $*		$#error $@ 4.1.8 $: "451 Domain of sender address " $&f " does not resolve"
R<PERM> $*		$#error $@ 5.1.8 $: "_CODE553 Domain of sender address " $&f " does not exist"
ifdef(`_ACCESS_TABLE_', `dnl
R<$={Accept}> $*	$# $1		accept from access map
R<DISCARD> $*		$#discard $: discard
R<QUARANTINE:$+> $*	$#error $@ quarantine $: $1
R<REJECT> $*		$#error ifdef(`confREJECT_MSG', `$: confREJECT_MSG', `$@ 5.7.1 $: "550 Access denied"')
dnl error tag
R<ERROR:$-.$-.$-:$+> $*		$#error $@ $1.$2.$3 $: $4
R<ERROR:$+> $*		$#error $: $1
ifdef(`_ATMPF_', `R<_ATMPF_> $*		$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
dnl generic error from access map
R<$+> $*		$#error $: $1		error from access db',
`dnl')
dnl workspace: @ CanonicalAddress (i.e. address in canonical form localpart<@host>)

ifdef(`_BADMX_CHK_', `dnl
R@ $*<@$+>$*		$: $1<@$2>$3 $| $>BadMX $2
R$* $| $#$*		$#$2

SBadMX
# Look up MX records and ferret away a copy of the original address.
# input: domain part of address to check
R$+				$:<MX><$1><:$(mxlist $1$):><:>
# workspace: <MX><domain><: mxlist-result $><:>
R<MX><$+><:$*<TEMP>:><$*>	$#error $@ 4.1.2 $: "450 MX lookup failure for "$1
# workspace: <MX> <original destination> <unchecked mxlist> <checked mxlist>
# Recursively run badmx check on each mx.
R<MX><$*><:$+:$*><:$*>		<MX><$1><:$3><: $4 $(badmx $2 $):>
# See if any of them fail.
R<MX><$*><$*><$*<BADMX>:$*>	$#error $@ 5.1.2 $:"550 Illegal MX record for host "$1
# Reverse the mxlists so we can use the same argument order again.
R<MX><$*><$*><$*>		$:<MX><$1><$3><$2>
R<MX><$*><:$+:$*><:$*>		<MX><$1><:$3><:$4 $(dnsA $2 $) :>

# Reverse the lists so we can use the same argument order again.
R<MX><$*><$*><$*>		$:<MX><$1><$3><$2>
R<MX><$*><:$+:$*><:$*>		<MX><$1><:$3><:$4 $(BadMXIP $2 $) :>

R<MX><$*><$*><$*<BADMXIP>:$*>	$#error $@ 5.1.2 $:"550 Invalid MX record for host "$1',
`dnl')


######################################################################
###  check_rcpt -- check SMTP ``RCPT TO:'' command argument
######################################################################

SLocal_check_rcpt
Scheck`'_U_`'rcpt
R$*			$: $1 $| $>"Local_check_rcpt" $1
R$* $| $#$*		$#$2
R$* $| $*		$@ $>"Basic_check_rcpt" $1

SBasic_check_rcpt
# empty address?
R<>			$#error $@ nouser $: "553 User address required"
R$@			$#error $@ nouser $: "553 User address required"
# check for deferred delivery mode
R$*			$: < $&{deliveryMode} > $1
R< d > $*		$@ deferred
R< $* > $*		$: $2

ifdef(`_REQUIRE_QUAL_RCPT_', `dnl
dnl this code checks for user@host where host is not a FQHN.
dnl it is not activated.
dnl notice: code to check for a recipient without a domain name is
dnl available down below; look for the same macro.
dnl this check is done here because the name might be qualified by the
dnl canonicalization.
# require fully qualified domain part?
dnl very simple canonification: make sure the address is in < >
R$+			$: <?> $1
R<?> <$+>		$: <@> <$1>
R<?> $+			$: <@> <$1>
R<@> < postmaster >	$: postmaster
R<@> < $* @ $+ . $+ >	$: < $1 @ $2 . $3 >
dnl prepend daemon_flags
R<@> $*			$: $&{daemon_flags} $| <@> $1
dnl workspace: ${daemon_flags} $| <@> <address>
dnl _r_equire qual.rcpt: ok
R$* r $* $| <@> < $+ @ $+ >	$: < $3 @ $4 >
dnl do not allow these at all or only from local systems?
R$* r $* $| <@> < $* >	$: < ? $&{client_name} > < $3 >
R<?> < $* >		$: <$1>
R<? $=w> < $* >		$: <$1>
R<? $+> <$+>		$#error $@ 5.5.4 $: "553 Fully qualified domain name required"
dnl remove daemon_flags for other cases
R$* $| <@> $*		$: $2', `dnl')

dnl ##################################################################
dnl call subroutines for recipient and relay
dnl possible returns from subroutines:
dnl $#TEMP	temporary failure
dnl $#error	permanent failure (or temporary if from access map)
dnl $#other	stop processing
dnl RELAY	RELAYing allowed
dnl other	otherwise
######################################################################
R$*			$: $1 $| @ $>"Rcpt_ok" $1
dnl temporary failure? remove mark @ and remember
R$* $| @ $#TEMP $+	$: $1 $| T $2
dnl error or ok (stop)
R$* $| @ $#$*		$#$2
ifdef(`_PROMISCUOUS_RELAY_', `divert(-1)', `dnl')
R$* $| @ RELAY		$@ RELAY
dnl something else: call check sender (relay)
R$* $| @ $*		$: O $| $>"Relay_ok" $1
dnl temporary failure: call check sender (relay)
R$* $| T $+		$: T $2 $| $>"Relay_ok" $1
dnl temporary failure? return that
R$* $| $#TEMP $+	$#error $2
dnl error or ok (stop)
R$* $| $#$*		$#$2
R$* $| RELAY		$@ RELAY
dnl something else: return previous temp failure
R T $+ $| $*		$#error $1
# anything else is bogus
R$*			$#error $@ 5.7.1 $: confRELAY_MSG
divert(0)

######################################################################
### Rcpt_ok: is the recipient ok?
dnl input: recipient address (RCPT TO)
dnl output: see explanation at call
######################################################################
SRcpt_ok
ifdef(`_LOOSE_RELAY_CHECK_',`dnl
R$*			$: $>CanonAddr $1
R$* < @ $* . >		$1 < @ $2 >			strip trailing dots',
`R$*			$: $>ParseRecipient $1		strip relayable hosts')

ifdef(`_BESTMX_IS_LOCAL_',`dnl
ifelse(_BESTMX_IS_LOCAL_, `', `dnl
# unlimited bestmx
R$* < @ $* > $*			$: $1 < @ $2 @@ $(bestmx $2 $) > $3',
`dnl
# limit bestmx to $=B
R$* < @ $* $=B > $*		$: $1 < @ $2 $3 @@ $(bestmx $2 $3 $) > $4')
R$* $=O $* < @ $* @@ $=w . > $*	$@ $>"Rcpt_ok" $1 $2 $3
R$* < @ $* @@ $=w . > $*	$: $1 < @ $3 > $4
R$* < @ $* @@ $* > $*		$: $1 < @ $2 > $4')

ifdef(`_BLACKLIST_RCPT_',`dnl
ifdef(`_ACCESS_TABLE_', `dnl
# blacklist local users or any host from receiving mail
R$*			$: <?> $1
dnl user is now tagged with @ to be consistent with check_mail
dnl and to distinguish users from hosts (com would be host, com@ would be user)
R<?> $+ < @ $=w >	$: <> <$1 < @ $2 >> $| <F:$1@$2> <U:$1@> <D:$2>
R<?> $+ < @ $* >	$: <> <$1 < @ $2 >> $| <F:$1@$2> <D:$2>
R<?> $+			$: <> <$1> $| <U:$1@>
dnl $| is used as delimiter, otherwise false matches may occur: <user<@domain>>
dnl will only return user<@domain when "reversing" the args
R<> <$*> $| <$+>	$: <@> <$1> $| $>SearchList <+ To> $| <$2> <>
R<@> <$*> $| <$*>	$: <$2> <$1>		reverse result
R<?> <$*>		$: @ $1		mark address as no match
dnl we may have to filter here because otherwise some RHSs
dnl would be interpreted as generic error messages...
dnl error messages should be "tagged" by prefixing them with error: !
dnl that would make a lot of things easier.
R<$={Accept}> <$*>	$: @ $2		mark address as no match
ifdef(`_ACCESS_SKIP_', `dnl
R<SKIP> <$*>		$: @ $1		mark address as no match', `dnl')
ifdef(`_DELAY_COMPAT_8_10_',`dnl
dnl compatility with 8.11/8.10:
dnl we have to filter these because otherwise they would be interpreted
dnl as generic error message...
dnl error messages should be "tagged" by prefixing them with error: !
dnl that would make a lot of things easier.
dnl maybe we should stop checks already here (if SPAM_xyx)?
R<$={SpamTag}> <$*>	$: @ $2		mark address as no match')
R<REJECT> $*		$#error $@ 5.2.1 $: confRCPTREJ_MSG
R<DISCARD> $*		$#discard $: discard
R<QUARANTINE:$+> $*	$#error $@ quarantine $: $1
dnl error tag
R<ERROR:$-.$-.$-:$+> $*		$#error $@ $1.$2.$3 $: $4
R<ERROR:$+> $*		$#error $: $1
ifdef(`_ATMPF_', `R<_ATMPF_> $*		$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
dnl generic error from access map
R<$+> $*		$#error $: $1		error from access db
R@ $*			$1		remove mark', `dnl')', `dnl')

ifdef(`_PROMISCUOUS_RELAY_', `divert(-1)', `dnl')
# authenticated via TLS?
R$*			$: $1 $| $>RelayTLS	client authenticated?
R$* $| $# $+		$# $2			error/ok?
R$* $| $*		$: $1			no

R$*			$: $1 $| $>"Local_Relay_Auth" $&{auth_type}
dnl workspace: localpart<@domain> $| result of Local_Relay_Auth
R$* $| $# $*		$# $2
dnl if Local_Relay_Auth returns NO then do not check $={TrustAuthMech}
R$* $| NO		$: $1
R$* $| $*		$: $1 $| $&{auth_type}
dnl workspace: localpart<@domain> [ $| ${auth_type} ]
dnl empty ${auth_type}?
R$* $|			$: $1
dnl mechanism ${auth_type} accepted?
dnl use $# to override further tests (delay_checks): see check_rcpt below
R$* $| $={TrustAuthMech}	$# RELAY
dnl remove ${auth_type}
R$* $| $*		$: $1
dnl workspace: localpart<@domain> | localpart
ifelse(defn(`_NO_UUCP_'), `r',
`R$* ! $* < @ $* >	$: <REMOTE> $2 < @ BANG_PATH >
R$* ! $* 		$: <REMOTE> $2 < @ BANG_PATH >', `dnl')
ifelse(defn(`_NO_PERCENTHACK_'), `r',
`R$* % $* < @ $* >	$: <REMOTE> $1 < @ PERCENT_HACK >
R$* % $* 		$: <REMOTE> $1 < @ PERCENT_HACK >', `dnl')
# anything terminating locally is ok
ifdef(`_RELAY_ENTIRE_DOMAIN_', `dnl
R$+ < @ $* $=m >	$@ RELAY', `dnl')
R$+ < @ $=w >		$@ RELAY
ifdef(`_RELAY_HOSTS_ONLY_',
`R$+ < @ $=R >		$@ RELAY
ifdef(`_ACCESS_TABLE_', `dnl
ifdef(`_RELAY_FULL_ADDR_', `dnl
R$+ < @ $+ >		$: <$(access To:$1@$2 $: ? $)> <$1 < @ $2 >>
R<?> <$+ < @ $+ >>	$: <$(access To:$2 $: ? $)> <$1 < @ $2 >>',`
R$+ < @ $+ >		$: <$(access To:$2 $: ? $)> <$1 < @ $2 >>')
dnl workspace: <Result-of-lookup | ?> <localpart<@domain>>
R<?> <$+ < @ $+ >>	$: <$(access $2 $: ? $)> <$1 < @ $2 >>',`dnl')',
`R$+ < @ $* $=R >	$@ RELAY
ifdef(`_ACCESS_TABLE_', `dnl
ifdef(`_RELAY_FULL_ADDR_', `dnl
R$+ < @ $+ >		$: $1 < @ $2 > $| $>SearchList <+ To> $| <F:$1@$2> <D:$2> <F:$1@> <>
R$+ < @ $+ > $| <$*>	$: <$3> <$1 <@ $2>>
R$+ < @ $+ > $| $*	$: <$3> <$1 <@ $2>>',
`R$+ < @ $+ >		$: $>D <$2> <?> <+ To> <$1 < @ $2 >>')')')
ifdef(`_ACCESS_TABLE_', `dnl
dnl workspace: <Result-of-lookup | ?> <localpart<@domain>>
R<RELAY> $*		$@ RELAY
ifdef(`_ATMPF_', `R<$* _ATMPF_> $*		$#TEMP $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
R<$*> <$*>		$: $2',`dnl')


ifdef(`_RELAY_MX_SERVED_', `dnl
# allow relaying for hosts which we MX serve
R$+ < @ $+ >		$: < : $(mxserved $2 $) : > $1 < @ $2 >
dnl this must not necessarily happen if the client is checked first...
R< : $* <TEMP> : > $*	$#TEMP $@ 4.4.0 $: "450 Can not check MX records for recipient host " $1
R<$* : $=w . : $*> $*	$@ RELAY
R< : $* : > $*		$: $2',
`dnl')

# check for local user (i.e. unqualified address)
R$*			$: <?> $1
R<?> $* < @ $+ >	$: <REMOTE> $1 < @ $2 >
# local user is ok
dnl is it really? the standard requires user@domain, not just user
dnl but we should accept it anyway (maybe making it an option:
dnl RequireFQDN ?)
dnl postmaster must be accepted without domain (DRUMS)
ifdef(`_REQUIRE_QUAL_RCPT_', `dnl
R<?> postmaster		$@ OK
# require qualified recipient?
dnl prepend daemon_flags
R<?> $+			$: $&{daemon_flags} $| <?> $1
dnl workspace: ${daemon_flags} $| <?> localpart
dnl do not allow these at all or only from local systems?
dnl r flag? add client_name
R$* r $* $| <?> $+	$: < ? $&{client_name} > <?> $3
dnl no r flag: relay to local user (only local part)
# no qualified recipient required
R$* $| <?> $+		$@ RELAY
dnl client_name is empty
R<?> <?> $+		$@ RELAY
dnl client_name is local
R<? $=w> <?> $+		$@ RELAY
dnl client_name is not local
R<? $+> $+		$#error $@ 5.5.4 $: "553 Domain name required"', `dnl
dnl no qualified recipient required
R<?> $+			$@ RELAY')
dnl it is a remote user: remove mark and then check client
R<$+> $*		$: $2
dnl currently the recipient address is not used below

######################################################################
### Relay_ok: is the relay/sender ok?
dnl input: ignored
dnl output: see explanation at call
######################################################################
SRelay_ok
# anything originating locally is ok
# check IP address
R$*			$: $&{client_addr}
R$@			$@ RELAY		originated locally
R0			$@ RELAY		originated locally
R127.0.0.1		$@ RELAY		originated locally
RIPv6:0:0:0:0:0:0:0:1	$@ RELAY		originated locally
dnl if compiled with IPV6_FULL=0
RIPv6:::1		$@ RELAY		originated locally
R$=R $*			$@ RELAY		relayable IP address
ifdef(`_ACCESS_TABLE_', `dnl
R$*			$: $>A <$1> <?> <+ Connect> <$1>
R<RELAY> $* 		$@ RELAY		relayable IP address
ifdef(`_FFR_REJECT_IP_IN_CHECK_RCPT_',`dnl
dnl this will cause rejections in cases like:
dnl Connect:My.Host.Domain	RELAY
dnl Connect:My.Net		REJECT
dnl since in check_relay client_name is checked before client_addr
R<REJECT> $* 		$@ REJECT		rejected IP address')
ifdef(`_ATMPF_', `R<_ATMPF_> $*		$#TEMP $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
R<$*> <$*>		$: $2', `dnl')
R$*			$: [ $1 ]		put brackets around it...
R$=w			$@ RELAY		... and see if it is local

ifdef(`_RELAY_DB_FROM_', `define(`_RELAY_MAIL_FROM_', `1')')dnl
ifdef(`_RELAY_LOCAL_FROM_', `define(`_RELAY_MAIL_FROM_', `1')')dnl
ifdef(`_RELAY_MAIL_FROM_', `dnl
dnl input: {client_addr} or something "broken"
dnl just throw the input away; we do not need it.
# check whether FROM is allowed to use system as relay
R$*			$: <?> $>CanonAddr $&f
R<?> $+ < @ $+ . >	<?> $1 < @ $2 >		remove trailing dot
ifdef(`_RELAY_LOCAL_FROM_', `dnl
# check whether local FROM is ok
R<?> $+ < @ $=w >	$@ RELAY		FROM local', `dnl')
ifdef(`_RELAY_DB_FROM_', `dnl
R<?> $+ < @ $+ >	$: <@> $>SearchList <! From> $| <F:$1@$2> ifdef(`_RELAY_DB_FROM_DOMAIN_', ifdef(`_RELAY_HOSTS_ONLY_', `<E:$2>', `<D:$2>')) <>
R<@> <RELAY>		$@ RELAY		RELAY FROM sender ok
ifdef(`_ATMPF_', `R<@> <_ATMPF_>		$#TEMP $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
', `dnl
ifdef(`_RELAY_DB_FROM_DOMAIN_',
`errprint(`*** ERROR: _RELAY_DB_FROM_DOMAIN_ requires _RELAY_DB_FROM_
')',
`dnl')
dnl')', `dnl')
dnl notice: the rulesets above do not leave a unique workspace behind.
dnl it does not matter in this case because the following rule ignores
dnl the input. otherwise these rules must "clean up" the workspace.

# check client name: first: did it resolve?
dnl input: ignored
R$*			$: < $&{client_resolve} >
R<TEMP>			$#TEMP $@ 4.4.0 $: "450 Relaying temporarily denied. Cannot resolve PTR record for " $&{client_addr}
R<FORGED>		$#error $@ 5.7.1 $: "550 Relaying denied. IP name possibly forged " $&{client_name}
R<FAIL>			$#error $@ 5.7.1 $: "550 Relaying denied. IP name lookup failed " $&{client_name}
dnl ${client_resolve} should be OK, so go ahead
R$*			$: <@> $&{client_name}
dnl should not be necessary since it has been done for client_addr already
dnl this rule actually may cause a problem if {client_name} resolves to ""
dnl however, this should not happen since the forward lookup should fail
dnl and {client_resolve} should be TEMP or FAIL.
dnl nevertheless, removing the rule doesn't hurt.
dnl R<@>			$@ RELAY
dnl workspace: <@> ${client_name} (not empty)
# pass to name server to make hostname canonical
R<@> $* $=P 		$:<?>  $1 $2
R<@> $+			$:<?>  $[ $1 $]
dnl workspace: <?> ${client_name} (canonified)
R$* .			$1			strip trailing dots
ifdef(`_RELAY_ENTIRE_DOMAIN_', `dnl
R<?> $* $=m		$@ RELAY', `dnl')
R<?> $=w		$@ RELAY
ifdef(`_RELAY_HOSTS_ONLY_',
`R<?> $=R		$@ RELAY
ifdef(`_ACCESS_TABLE_', `dnl
R<?> $*			$: <$(access Connect:$1 $: ? $)> <$1>
R<?> <$*>		$: <$(access $1 $: ? $)> <$1>',`dnl')',
`R<?> $* $=R			$@ RELAY
ifdef(`_ACCESS_TABLE_', `dnl
R<?> $*			$: $>D <$1> <?> <+ Connect> <$1>',`dnl')')
ifdef(`_ACCESS_TABLE_', `dnl
R<RELAY> $*		$@ RELAY
ifdef(`_ATMPF_', `R<$* _ATMPF_> $*		$#TEMP $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
R<$*> <$*>		$: $2',`dnl')
dnl end of _PROMISCUOUS_RELAY_
divert(0)
ifdef(`_DELAY_CHECKS_',`dnl
# turn a canonical address in the form user<@domain>
# qualify unqual. addresses with $j
dnl it might have been only user (without <@domain>)
SFullAddr
R$* <@ $+ . >		$1 <@ $2 >
R$* <@ $* >		$@ $1 <@ $2 >
R$+			$@ $1 <@ $j >

SDelay_TLS_Clt
# authenticated?
dnl code repeated here from Basic_check_mail
dnl only called from check_rcpt in delay mode if checkrcpt returns $#
R$*			$: $1 $| $>"tls_client" $&{verify} $| MAIL
R$* $| $#$+		$#$2
dnl return result from checkrcpt
R$* $| $*		$# $1
R$*			$# $1

SDelay_TLS_Clt2
# authenticated?
dnl code repeated here from Basic_check_mail
dnl only called from check_rcpt in delay mode if stopping due to Friend/Hater
R$*			$: $1 $| $>"tls_client" $&{verify} $| MAIL
R$* $| $#$+		$#$2
dnl return result from friend/hater check
R$* $| $*		$@ $1
R$*			$@ $1

# call all necessary rulesets
Scheck_rcpt
dnl this test should be in the Basic_check_rcpt ruleset
dnl which is the correct DSN code?
# R$@			$#error $@ 5.1.3 $: "553 Recipient address required"

R$+			$: $1 $| $>checkrcpt $1
dnl now we can simply stop checks by returning "$# xyz" instead of just "ok"
dnl on error (or discard) stop now
R$+ $| $#error $*	$#error $2
R$+ $| $#discard $*	$#discard $2
dnl otherwise call tls_client; see above
R$+ $| $#$*		$@ $>"Delay_TLS_Clt" $2
R$+ $| $*		$: <?> $>FullAddr $>CanonAddr $1
ifdef(`_SPAM_FH_',
`dnl lookup user@ and user@address
ifdef(`_ACCESS_TABLE_', `',
`errprint(`*** ERROR: FEATURE(`delay_checks', `argument') requires FEATURE(`access_db')
')')dnl
dnl one of the next two rules is supposed to match
dnl this code has been copied from BLACKLIST... etc
dnl and simplified by omitting some < >.
R<?> $+ < @ $=w >	$: <> $1 < @ $2 > $| <F: $1@$2 > <D: $2 > <U: $1@>
R<?> $+ < @ $* >	$: <> $1 < @ $2 > $| <F: $1@$2 > <D: $2 >
dnl R<?>		$@ something_is_very_wrong_here
# lookup the addresses only with Spam tag
R<> $* $| <$+>		$: <@> $1 $| $>SearchList <! Spam> $| <$2> <>
R<@> $* $| $*		$: $2 $1		reverse result
dnl', `dnl')
ifdef(`_SPAM_FRIEND_',
`# is the recipient a spam friend?
ifdef(`_SPAM_HATER_',
	`errprint(`*** ERROR: define either Hater or Friend -- not both.
')', `dnl')
R<FRIEND> $+		$@ $>"Delay_TLS_Clt2" SPAMFRIEND
R<$*> $+		$: $2',
`dnl')
ifdef(`_SPAM_HATER_',
`# is the recipient no spam hater?
R<HATER> $+		$: $1			spam hater: continue checks
R<$*> $+		$@ $>"Delay_TLS_Clt2" NOSPAMHATER	everyone else: stop
dnl',`dnl')

dnl run further checks: check_mail
dnl should we "clean up" $&f?
ifdef(`_FFR_MAIL_MACRO',
`R$*			$: $1 $| $>checkmail $&{mail_from}',
`R$*			$: $1 $| $>checkmail <$&f>')
dnl recipient (canonical format) $| result of checkmail
R$* $| $#$*		$#$2
dnl run further checks: check_relay
R$* $| $*		$: $1 $| $>checkrelay $&{client_name} $| $&{client_addr}
R$* $| $#$*		$#$2
R$* $| $*		$: $1
', `dnl')

ifdef(`_BLOCK_BAD_HELO_', `dnl
R$*			$: $1 $| <$&{auth_authen}>	Get auth info
dnl Bypass the test for users who have authenticated.
R$* $| <$+>		$: $1				skip if auth
R$* $| <$*>		$: $1 $| <$&{client_addr}> [$&s]	Get connection info
dnl Bypass for local clients -- IP address starts with $=R
R$* $| <$=R $*> [$*]	$: $1				skip if local client
dnl Bypass a "sendmail -bs" session, which use 0 for client ip address
R$* $| <0> [$*]		$: $1				skip if sendmail -bs
dnl Reject our IP - assumes "[ip]" is in class $=w
R$* $| <$*> $=w		$#error $@ 5.7.1 $:"550 bogus HELO name used: " $&s
dnl Reject our hostname
R$* $| <$*> [$=w]	$#error $@ 5.7.1 $:"550 bogus HELO name used: " $&s
dnl Pass anything else with a "." in the domain parameter
R$* $| <$*> [$+.$+]	$: $1				qualified domain ok
dnl Pass IPv6: address literals
R$* $| <$*> [IPv6:$+]	$: $1				qualified domain ok
dnl Reject if there was no "." or only an initial or final "."
R$* $| <$*> [$*]	$#error $@ 5.7.1 $:"550 bogus HELO name used: " $&s
dnl Clean up the workspace
R$* $| $*		$: $1
', `dnl')

ifdef(`_ACCESS_TABLE_', `dnl', `divert(-1)')
######################################################################
###  F: LookUpFull -- search for an entry in access database
###
###	lookup of full key (which should be an address) and
###	variations if +detail exists: +* and without +detail
###
###	Parameters:
###		<$1> -- key
###		<$2> -- default (what to return if not found in db)
dnl			must not be empty
###		<$3> -- mark (must be <(!|+) single-token>)
###			! does lookup only with tag
###			+ does lookup with and without tag
###		<$4> -- passthru (additional data passed unchanged through)
dnl returns:		<default> <passthru>
dnl 			<result> <passthru>
######################################################################

SF
dnl workspace: <key> <def> <o tag> <thru>
dnl full lookup
dnl    2    3  4    5
R<$+> <$*> <$- $-> <$*>		$: <$(access $4`'_TAG_DELIM_`'$1 $: ? $)> <$1> <$2> <$3 $4> <$5>
dnl no match, try without tag
dnl   1    2      3    4
R<?> <$+> <$*> <+ $-> <$*>	$: <$(access $1 $: ? $)> <$1> <$2> <+ $3> <$4>
dnl no match, +detail: try +*
dnl   1    2    3    4    5  6    7
R<?> <$+ + $* @ $+> <$*> <$- $-> <$*>
			$: <$(access $6`'_TAG_DELIM_`'$1+*@$3 $: ? $)> <$1+$2@$3> <$4> <$5 $6> <$7>
dnl no match, +detail: try +* without tag
dnl   1    2    3    4      5    6
R<?> <$+ + $* @ $+> <$*> <+ $-> <$*>
			$: <$(access $1+*@$3 $: ? $)> <$1+$2@$3> <$4> <+ $5> <$6>
dnl no match, +detail: try without +detail
dnl   1    2    3    4    5  6    7
R<?> <$+ + $* @ $+> <$*> <$- $-> <$*>
			$: <$(access $6`'_TAG_DELIM_`'$1@$3 $: ? $)> <$1+$2@$3> <$4> <$5 $6> <$7>
dnl no match, +detail: try without +detail and without tag
dnl   1    2    3    4      5    6
R<?> <$+ + $* @ $+> <$*> <+ $-> <$*>
			$: <$(access $1@$3 $: ? $)> <$1+$2@$3> <$4> <+ $5> <$6>
dnl no match, return <default> <passthru>
dnl   1    2    3  4    5
R<?> <$+> <$*> <$- $-> <$*>	$@ <$2> <$5>
ifdef(`_ATMPF_', `dnl tempfail?
dnl            2    3  4    5
R<$+ _ATMPF_> <$*> <$- $-> <$*>	$@ <_ATMPF_> <$5>', `dnl')
dnl match, return <match> <passthru>
dnl    2    3  4    5
R<$+> <$*> <$- $-> <$*>		$@ <$1> <$5>

######################################################################
###  E: LookUpExact -- search for an entry in access database
###
###	Parameters:
###		<$1> -- key
###		<$2> -- default (what to return if not found in db)
dnl			must not be empty
###		<$3> -- mark (must be <(!|+) single-token>)
###			! does lookup only with tag
###			+ does lookup with and without tag
###		<$4> -- passthru (additional data passed unchanged through)
dnl returns:		<default> <passthru>
dnl 			<result> <passthru>
######################################################################

SE
dnl    2    3  4    5
R<$*> <$*> <$- $-> <$*>		$: <$(access $4`'_TAG_DELIM_`'$1 $: ? $)> <$1> <$2> <$3 $4> <$5>
dnl no match, try without tag
dnl   1    2      3    4
R<?> <$+> <$*> <+ $-> <$*>	$: <$(access $1 $: ? $)> <$1> <$2> <+ $3> <$4>
dnl no match, return default passthru
dnl   1    2    3  4    5
R<?> <$+> <$*> <$- $-> <$*>	$@ <$2> <$5>
ifdef(`_ATMPF_', `dnl tempfail?
dnl            2    3  4    5
R<$+ _ATMPF_> <$*> <$- $-> <$*>	$@ <_ATMPF_> <$5>', `dnl')
dnl match, return <match> <passthru>
dnl    2    3  4    5
R<$+> <$*> <$- $-> <$*>		$@ <$1> <$5>

######################################################################
###  U: LookUpUser -- search for an entry in access database
###
###	lookup of key (which should be a local part) and
###	variations if +detail exists: +* and without +detail
###
###	Parameters:
###		<$1> -- key (user@)
###		<$2> -- default (what to return if not found in db)
dnl			must not be empty
###		<$3> -- mark (must be <(!|+) single-token>)
###			! does lookup only with tag
###			+ does lookup with and without tag
###		<$4> -- passthru (additional data passed unchanged through)
dnl returns:		<default> <passthru>
dnl 			<result> <passthru>
######################################################################

SU
dnl user lookups are always with trailing @
dnl    2    3  4    5
R<$+> <$*> <$- $-> <$*>		$: <$(access $4`'_TAG_DELIM_`'$1 $: ? $)> <$1> <$2> <$3 $4> <$5>
dnl no match, try without tag
dnl   1    2      3    4
R<?> <$+> <$*> <+ $-> <$*>	$: <$(access $1 $: ? $)> <$1> <$2> <+ $3> <$4>
dnl do not remove the @ from the lookup:
dnl it is part of the +detail@ which is omitted for the lookup
dnl no match, +detail: try +*
dnl   1    2      3    4  5    6
R<?> <$+ + $* @> <$*> <$- $-> <$*>
			$: <$(access $5`'_TAG_DELIM_`'$1+*@ $: ? $)> <$1+$2@> <$3> <$4 $5> <$6>
dnl no match, +detail: try +* without tag
dnl   1    2      3      4    5
R<?> <$+ + $* @> <$*> <+ $-> <$*>
			$: <$(access $1+*@ $: ? $)> <$1+$2@> <$3> <+ $4> <$5>
dnl no match, +detail: try without +detail
dnl   1    2      3    4  5    6
R<?> <$+ + $* @> <$*> <$- $-> <$*>
			$: <$(access $5`'_TAG_DELIM_`'$1@ $: ? $)> <$1+$2@> <$3> <$4 $5> <$6>
dnl no match, +detail: try without +detail and without tag
dnl   1    2      3      4    5
R<?> <$+ + $* @> <$*> <+ $-> <$*>
			$: <$(access $1@ $: ? $)> <$1+$2@> <$3> <+ $4> <$5>
dnl no match, return <default> <passthru>
dnl   1    2    3  4    5
R<?> <$+> <$*> <$- $-> <$*>	$@ <$2> <$5>
ifdef(`_ATMPF_', `dnl tempfail?
dnl            2    3  4    5
R<$+ _ATMPF_> <$*> <$- $-> <$*>	$@ <_ATMPF_> <$5>', `dnl')
dnl match, return <match> <passthru>
dnl    2    3  4    5
R<$+> <$*> <$- $-> <$*>		$@ <$1> <$5>

######################################################################
###  SearchList: search a list of items in the access map
###	Parameters:
###		<exact tag> $| <mark:address> <mark:address> ... <>
dnl	maybe we should have a @ (again) in front of the mark to
dnl	avoid errorneous matches (with error messages?)
dnl	if we can make sure that tag is always a single token
dnl	then we can omit the delimiter $|, otherwise we need it
dnl	to avoid errorneous matchs (first rule: D: if there
dnl	is that mark somewhere in the list, it will be taken).
dnl	moreover, we can do some tricks to enforce lookup with
dnl	the tag only, e.g.:
###	where "exact" is either "+" or "!":
###	<+ TAG>	lookup with and w/o tag
###	<! TAG>	lookup with tag
dnl	Warning: + and ! should be in OperatorChars (otherwise there must be
dnl		a blank between them and the tag.
###	possible values for "mark" are:
###		D: recursive host lookup (LookUpDomain)
dnl		A: recursive address lookup (LookUpAddress) [not yet required]
###		E: exact lookup, no modifications
###		F: full lookup, try user+ext@domain and user@domain
###		U: user lookup, try user+ext and user (input must have trailing @)
###	return: <RHS of lookup> or <?> (not found)
######################################################################

# class with valid marks for SearchList
dnl if A is activated: add it
C{Src}E F D U ifdef(`_FFR_SRCHLIST_A', `A')
SSearchList
# just call the ruleset with the name of the tag... nice trick...
dnl       2       3    4
R<$+> $| <$={Src}:$*> <$*>	$: <$1> $| <$4> $| $>$2 <$3> <?> <$1> <>
dnl workspace: <o tag> $| <rest> $| <result of lookup> <>
dnl no match and nothing left: return
R<$+> $| <> $| <?> <>		$@ <?>
dnl no match but something left: continue
R<$+> $| <$+> $| <?> <>		$@ $>SearchList <$1> $| <$2>
dnl match: return
R<$+> $| <$*> $| <$+> <>	$@ <$3>
dnl return result from recursive invocation
R<$+> $| <$+>			$@ <$2>
dnl endif _ACCESS_TABLE_
divert(0)

######################################################################
###  trust_auth: is user trusted to authenticate as someone else?
###
###	Parameters:
###		$1: AUTH= parameter from MAIL command
######################################################################

dnl empty ruleset definition so it can be called
SLocal_trust_auth
Strust_auth
R$*			$: $&{auth_type} $| $1
# required by RFC 2554 section 4.
R$@ $| $*		$#error $@ 5.7.1 $: "550 not authenticated"
dnl seems to be useful...
R$* $| $&{auth_authen}		$@ identical
R$* $| <$&{auth_authen}>	$@ identical
dnl call user supplied code
R$* $| $*		$: $1 $| $>"Local_trust_auth" $2
R$* $| $#$*		$#$2
dnl default: error
R$*			$#error $@ 5.7.1 $: "550 " $&{auth_authen} " not allowed to act as " $&{auth_author}

######################################################################
###  Relay_Auth: allow relaying based on authentication?
###
###	Parameters:
###		$1: ${auth_type}
######################################################################
SLocal_Relay_Auth

######################################################################
###  srv_features: which features to offer to a client?
###	(done in server)
######################################################################
Ssrv_features
ifdef(`_LOCAL_SRV_FEATURES_', `dnl
R$*			$: $1 $| $>"Local_srv_features" $1
R$* $| $#$*		$#$2
R$* $| $*		$: $1', `dnl')
ifdef(`_ACCESS_TABLE_', `dnl
R$*		$: $>D <$&{client_name}> <?> <! SRV_FEAT_TAG> <>
R<?>$*		$: $>A <$&{client_addr}> <?> <! SRV_FEAT_TAG> <>
R<?>$*		$: <$(access SRV_FEAT_TAG`'_TAG_DELIM_ $: ? $)>
R<?>$*		$@ OK
ifdef(`_ATMPF_', `dnl tempfail?
R<$* _ATMPF_>$*	$#temp', `dnl')
R<$+>$*		$# $1')

######################################################################
###  try_tls: try to use STARTTLS?
###	(done in client)
######################################################################
Stry_tls
ifdef(`_LOCAL_TRY_TLS_', `dnl
R$*			$: $1 $| $>"Local_try_tls" $1
R$* $| $#$*		$#$2
R$* $| $*		$: $1', `dnl')
ifdef(`_ACCESS_TABLE_', `dnl
R$*		$: $>D <$&{server_name}> <?> <! TLS_TRY_TAG> <>
R<?>$*		$: $>A <$&{server_addr}> <?> <! TLS_TRY_TAG> <>
R<?>$*		$: <$(access TLS_TRY_TAG`'_TAG_DELIM_ $: ? $)>
R<?>$*		$@ OK
ifdef(`_ATMPF_', `dnl tempfail?
R<$* _ATMPF_>$*	$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
R<NO>$*		$#error $@ 5.7.1 $: "550 do not try TLS with " $&{server_name} " ["$&{server_addr}"]"')

######################################################################
###  tls_rcpt: is connection with server "good" enough?
###	(done in client, per recipient)
dnl called from deliver() before RCPT command
###
###	Parameters:
###		$1: recipient
######################################################################
Stls_rcpt
ifdef(`_LOCAL_TLS_RCPT_', `dnl
R$*			$: $1 $| $>"Local_tls_rcpt" $1
R$* $| $#$*		$#$2
R$* $| $*		$: $1', `dnl')
ifdef(`_ACCESS_TABLE_', `dnl
dnl store name of other side
R$*			$: $(macro {TLS_Name} $@ $&{server_name} $) $1
dnl canonify recipient address
R$+			$: <?> $>CanonAddr $1
dnl strip trailing dots
R<?> $+ < @ $+ . >	<?> $1 <@ $2 >
dnl full address?
R<?> $+ < @ $+ >	$: $1 <@ $2 > $| <F:$1@$2> <U:$1@> <D:$2> <E:>
dnl only localpart?
R<?> $+			$: $1 $| <U:$1@> <E:>
dnl look it up
dnl also look up a default value via E:
R$* $| $+	$: $1 $| $>SearchList <! TLS_RCPT_TAG> $| $2 <>
dnl found nothing: stop here
R$* $| <?>	$@ OK
ifdef(`_ATMPF_', `dnl tempfail?
R$* $| <$* _ATMPF_>	$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
dnl use the generic routine (for now)
R$* $| <$+>	$@ $>"TLS_connection" $&{verify} $| <$2>')

######################################################################
###  tls_client: is connection with client "good" enough?
###	(done in server)
###
###	Parameters:
###		${verify} $| (MAIL|STARTTLS)
######################################################################
dnl MAIL: called from check_mail
dnl STARTTLS: called from smtp() after STARTTLS has been accepted
Stls_client
ifdef(`_LOCAL_TLS_CLIENT_', `dnl
R$*			$: $1 <?> $>"Local_tls_client" $1
R$* <?> $#$*		$#$2
R$* <?> $*		$: $1', `dnl')
ifdef(`_ACCESS_TABLE_', `dnl
dnl store name of other side
R$*		$: $(macro {TLS_Name} $@ $&{client_name} $) $1
dnl ignore second arg for now
dnl maybe use it to distinguish permanent/temporary error?
dnl if MAIL: permanent (STARTTLS has not been offered)
dnl if STARTTLS: temporary (offered but maybe failed)
R$* $| $*	$: $1 $| $>D <$&{client_name}> <?> <! TLS_CLT_TAG> <>
R$* $| <?>$*	$: $1 $| $>A <$&{client_addr}> <?> <! TLS_CLT_TAG> <>
dnl do a default lookup: just TLS_CLT_TAG
R$* $| <?>$*	$: $1 $| <$(access TLS_CLT_TAG`'_TAG_DELIM_ $: ? $)>
ifdef(`_ATMPF_', `dnl tempfail?
R$* $| <$* _ATMPF_>	$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
R$*		$@ $>"TLS_connection" $1', `dnl
R$* $| $*	$@ $>"TLS_connection" $1')

######################################################################
###  tls_server: is connection with server "good" enough?
###	(done in client)
###
###	Parameter:
###		${verify}
######################################################################
dnl i.e. has the server been authenticated and is encryption active?
dnl called from deliver() after STARTTLS command
Stls_server
ifdef(`_LOCAL_TLS_SERVER_', `dnl
R$*			$: $1 $| $>"Local_tls_server" $1
R$* $| $#$*		$#$2
R$* $| $*		$: $1', `dnl')
ifdef(`_ACCESS_TABLE_', `dnl
dnl store name of other side
R$*		$: $(macro {TLS_Name} $@ $&{server_name} $) $1
R$*		$: $1 $| $>D <$&{server_name}> <?> <! TLS_SRV_TAG> <>
R$* $| <?>$*	$: $1 $| $>A <$&{server_addr}> <?> <! TLS_SRV_TAG> <>
dnl do a default lookup: just TLS_SRV_TAG
R$* $| <?>$*	$: $1 $| <$(access TLS_SRV_TAG`'_TAG_DELIM_ $: ? $)>
ifdef(`_ATMPF_', `dnl tempfail?
R$* $| <$* _ATMPF_>	$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
R$*		$@ $>"TLS_connection" $1', `dnl
R$*		$@ $>"TLS_connection" $1')

######################################################################
###  TLS_connection: is TLS connection "good" enough?
###
###	Parameters:
ifdef(`_ACCESS_TABLE_', `dnl
###		${verify} $| <Requirement> [<>]', `dnl
###		${verify}')
###		Requirement: RHS from access map, may be ? for none.
dnl	syntax for Requirement:
dnl	[(PERM|TEMP)+] (VERIFY[:bits]|ENCR:bits) [+extensions]
dnl	extensions: could be a list of further requirements
dnl		for now: CN:string	{cn_subject} == string
######################################################################
STLS_connection
ifdef(`_ACCESS_TABLE_', `dnl', `dnl use default error
dnl deal with TLS handshake failures: abort
RSOFTWARE	$#error $@ ifdef(`TLS_PERM_ERR', `5.7.0', `4.7.0') $: "ifdef(`TLS_PERM_ERR', `503', `403') TLS handshake."
divert(-1)')
dnl common ruleset for tls_{client|server}
dnl input: ${verify} $| <ResultOfLookup> [<>]
dnl remove optional <>
R$* $| <$*>$*			$: $1 $| <$2>
dnl workspace: ${verify} $| <ResultOfLookup>
# create the appropriate error codes
dnl permanent or temporary error?
R$* $| <PERM + $={Tls} $*>	$: $1 $| <503:5.7.0> <$2 $3>
R$* $| <TEMP + $={Tls} $*>	$: $1 $| <403:4.7.0> <$2 $3>
dnl default case depends on TLS_PERM_ERR
R$* $| <$={Tls} $*>		$: $1 $| <ifdef(`TLS_PERM_ERR', `503:5.7.0', `403:4.7.0')> <$2 $3>
dnl workspace: ${verify} $| [<SMTP:ESC>] <ResultOfLookup>
# deal with TLS handshake failures: abort
RSOFTWARE $| <$-:$+> $* 	$#error $@ $2 $: $1 " TLS handshake failed."
dnl no <reply:dns> i.e. not requirements in the access map
dnl use default error
RSOFTWARE $| $* 		$#error $@ ifdef(`TLS_PERM_ERR', `5.7.0', `4.7.0') $: "ifdef(`TLS_PERM_ERR', `503', `403') TLS handshake failed."
# deal with TLS protocol errors: abort
RPROTOCOL $| <$-:$+> $* 	$#error $@ $2 $: $1 " STARTTLS failed."
dnl no <reply:dns> i.e. not requirements in the access map
dnl use default error
RPROTOCOL $| $* 		$#error $@ ifdef(`TLS_PERM_ERR', `5.7.0', `4.7.0') $: "ifdef(`TLS_PERM_ERR', `503', `403') STARTTLS failed."
R$* $| <$*> <VERIFY>		$: <$2> <VERIFY> <> $1
dnl separate optional requirements
R$* $| <$*> <VERIFY + $+>	$: <$2> <VERIFY> <$3> $1
R$* $| <$*> <$={Tls}:$->$*	$: <$2> <$3:$4> <> $1
dnl separate optional requirements
R$* $| <$*> <$={Tls}:$- + $+>$*	$: <$2> <$3:$4> <$5> $1
dnl some other value in access map: accept
dnl this also allows to override the default case (if used)
R$* $| $*			$@ OK
# authentication required: give appropriate error
# other side did authenticate (via STARTTLS)
dnl workspace: <SMTP:ESC> <{VERIFY,ENCR}[:BITS]> <[extensions]> ${verify}
dnl only verification required and it succeeded
R<$*><VERIFY> <> OK		$@ OK
dnl verification required and it succeeded but extensions are given
dnl change it to <SMTP:ESC> <REQ:0>  <extensions>
R<$*><VERIFY> <$+> OK		$: <$1> <REQ:0> <$2>
dnl verification required + some level of encryption
R<$*><VERIFY:$-> <$*> OK	$: <$1> <REQ:$2> <$3>
dnl just some level of encryption required
R<$*><ENCR:$-> <$*> $*		$: <$1> <REQ:$2> <$3>
dnl workspace:
dnl 1. <SMTP:ESC> <VERIFY [:bits]>  <[extensions]> {verify} (!= OK)
dnl 2. <SMTP:ESC> <REQ:bits>  <[extensions]>
dnl verification required but ${verify} is not set (case 1.)
R<$-:$+><VERIFY $*> <$*>	$#error $@ $2 $: $1 " authentication required"
R<$-:$+><VERIFY $*> <$*> FAIL	$#error $@ $2 $: $1 " authentication failed"
R<$-:$+><VERIFY $*> <$*> NO	$#error $@ $2 $: $1 " not authenticated"
R<$-:$+><VERIFY $*> <$*> NOT	$#error $@ $2 $: $1 " no authentication requested"
R<$-:$+><VERIFY $*> <$*> NONE	$#error $@ $2 $: $1 " other side does not support STARTTLS"
dnl some other value for ${verify}
R<$-:$+><VERIFY $*> <$*> $+	$#error $@ $2 $: $1 " authentication failure " $4
dnl some level of encryption required: get the maximum level (case 2.)
R<$*><REQ:$-> <$*>		$: <$1> <REQ:$2> <$3> $>max $&{cipher_bits} : $&{auth_ssf}
dnl compare required bits with actual bits
R<$*><REQ:$-> <$*> $-		$: <$1> <$2:$4> <$3> $(arith l $@ $4 $@ $2 $)
R<$-:$+><$-:$-> <$*> TRUE	$#error $@ $2 $: $1 " encryption too weak " $4 " less than " $3
dnl strength requirements fulfilled
dnl TLS Additional Requirements Separator
dnl this should be something which does not appear in the extensions itself
dnl @ could be part of a CN, DN, etc...
dnl use < > ? those are encoded in CN, DN, ...
define(`_TLS_ARS_', `++')dnl
dnl workspace:
dnl <SMTP:ESC> <REQ:bits> <extensions> result-of-compare
R<$-:$+><$-:$-> <$*> $*		$: <$1:$2 _TLS_ARS_ $5>
dnl workspace: <SMTP:ESC _TLS_ARS_ extensions>
dnl continue: check  extensions
R<$-:$+ _TLS_ARS_ >			$@ OK
dnl split extensions into own list
R<$-:$+ _TLS_ARS_ $+ >			$: <$1:$2> <$3>
R<$-:$+> < $+ _TLS_ARS_ $+ >		<$1:$2> <$3> <$4>
R<$-:$+> $+			$@ $>"TLS_req" $3 $| <$1:$2>

######################################################################
###  TLS_req: check additional TLS requirements
###
###	Parameters: [<list> <of> <req>] $| <$-:$+>
###		$-: SMTP reply code
###		$+: Enhanced Status Code
dnl  further requirements for this ruleset:
dnl	name of "other side" is stored is {TLS_name} (client/server_name)
dnl
dnl	currently only CN[:common_name] is implemented
dnl	right now this is only a logical AND
dnl	i.e. all requirements must be true
dnl	how about an OR? CN must be X or CN must be Y or ..
dnl	use a macro to compute this as a trivial sequential
dnl	operations (no precedences etc)?
######################################################################
STLS_req
dnl no additional requirements: ok
R $| $+		$@ OK
dnl require CN: but no CN specified: use name of other side
R<CN> $* $| <$+>		$: <CN:$&{TLS_Name}> $1 $| <$2>
dnl match, check rest
R<CN:$&{cn_subject}> $* $| <$+>		$@ $>"TLS_req" $1 $| <$2>
dnl CN does not match
dnl  1   2      3  4
R<CN:$+> $* $| <$-:$+>	$#error $@ $4 $: $3 " CN " $&{cn_subject} " does not match " $1
dnl cert subject
R<CS:$&{cert_subject}> $* $| <$+>	$@ $>"TLS_req" $1 $| <$2>
dnl CS does not match
dnl  1   2      3  4
R<CS:$+> $* $| <$-:$+>	$#error $@ $4 $: $3 " Cert Subject " $&{cert_subject} " does not match " $1
dnl match, check rest
R<CI:$&{cert_issuer}> $* $| <$+>	$@ $>"TLS_req" $1 $| <$2>
dnl CI does not match
dnl  1   2      3  4
R<CI:$+> $* $| <$-:$+>	$#error $@ $4 $: $3 " Cert Issuer " $&{cert_issuer} " does not match " $1
dnl return from recursive call
ROK			$@ OK

######################################################################
###  max: return the maximum of two values separated by :
###
###	Parameters: [$-]:[$-]
######################################################################
Smax
R:		$: 0
R:$-		$: $1
R$-:		$: $1
R$-:$-		$: $(arith l $@ $1 $@ $2 $) : $1 : $2
RTRUE:$-:$-	$: $2
R$-:$-:$-	$: $2
dnl endif _ACCESS_TABLE_
divert(0)

ifdef(`_TLS_SESSION_FEATURES_', `dnl
Stls_srv_features
ifdef(`_ACCESS_TABLE_', `dnl
R$* $| $*		$: $>D <$1> <?> <! TLS_Srv_Features> <$2>
R<?> <$*> 		$: $>A <$1> <?> <! TLS_Srv_Features> <$1>
R<?> <$*> 		$@ ""
R<$+> <$*> 		$@ $1
', `dnl
R$* 		$@ ""')

Stls_clt_features
ifdef(`_ACCESS_TABLE_', `dnl
R$* $| $*		$: $>D <$1> <?> <! TLS_Clt_Features> <$2>
R<?> <$*> 		$: $>A <$1> <?> <! TLS_Clt_Features> <$1>
R<?> <$*> 		$@ ""
R<$+> <$*> 		$@ $1
', `dnl
R$* 		$@ ""')
')

######################################################################
###  RelayTLS: allow relaying based on TLS authentication
###
###	Parameters:
###		none
######################################################################
SRelayTLS
# authenticated?
dnl we do not allow relaying for anyone who can present a cert
dnl signed by a "trusted" CA. For example, even if we put verisigns
dnl CA in CertPath so we can authenticate users, we do not allow
dnl them to abuse our server (they might be easier to get hold of,
dnl but anyway).
dnl so here is the trick: if the verification succeeded
dnl we look up the cert issuer in the access map
dnl (maybe after extracting a part with a regular expression)
dnl if this returns RELAY we relay without further questions
dnl if it returns SUBJECT we perform a similar check on the
dnl cert subject.
ifdef(`_ACCESS_TABLE_', `dnl
R$*			$: <?> $&{verify}
R<?> OK			$: OK		authenticated: continue
R<?> $*			$@ NO		not authenticated
ifdef(`_CERT_REGEX_ISSUER_', `dnl
R$*			$: $(CERTIssuer $&{cert_issuer} $)',
`R$*			$: $&{cert_issuer}')
R$+			$: $(access CERTISSUER`'_TAG_DELIM_`'$1 $)
dnl use $# to stop further checks (delay_check)
RRELAY			$# RELAY
ifdef(`_CERT_REGEX_SUBJECT_', `dnl
RSUBJECT		$: <@> $(CERTSubject $&{cert_subject} $)',
`RSUBJECT		$: <@> $&{cert_subject}')
R<@> $+			$: <@> $(access CERTSUBJECT`'_TAG_DELIM_`'$1 $)
R<@> RELAY		$# RELAY
R$*			$: NO', `dnl')

######################################################################
###  authinfo: lookup authinfo in the access map
###
###	Parameters:
###		$1: {server_name}
###		$2: {server_addr}
dnl	both are currently ignored
dnl if it should be done via another map, we either need to restrict
dnl functionality (it calls D and A) or copy those rulesets (or add another
dnl parameter which I want to avoid, it's quite complex already)
######################################################################
dnl omit this ruleset if neither is defined?
dnl it causes DefaultAuthInfo to be ignored
dnl (which may be considered a good thing).
Sauthinfo
ifdef(`_AUTHINFO_TABLE_', `dnl
R$*		$: <$(authinfo AuthInfo:$&{server_name} $: ? $)>
R<?>		$: <$(authinfo AuthInfo:$&{server_addr} $: ? $)>
R<?>		$: <$(authinfo AuthInfo: $: ? $)>
R<?>		$@ no				no authinfo available
R<$*>		$# $1
dnl', `dnl
ifdef(`_ACCESS_TABLE_', `dnl
R$*		$: $1 $| $>D <$&{server_name}> <?> <! AuthInfo> <>
R$* $| <?>$*	$: $1 $| $>A <$&{server_addr}> <?> <! AuthInfo> <>
R$* $| <?>$*	$: $1 $| <$(access AuthInfo`'_TAG_DELIM_ $: ? $)> <>
R$* $| <?>$*	$@ no				no authinfo available
R$* $| <$*> <>	$# $2
dnl', `dnl')')

ifdef(`_RATE_CONTROL_',`dnl
######################################################################
###  RateControl: 
###	Parameters:	ignored
###	return: $#error or OK
######################################################################
SRateControl
ifdef(`_ACCESS_TABLE_', `dnl
R$*		$: <A:$&{client_addr}> <E:>
dnl also look up a default value via E:
R$+		$: $>SearchList <! ClientRate> $| $1 <>
dnl found nothing: stop here
R<?>		$@ OK
ifdef(`_ATMPF_', `dnl tempfail?
R<$* _ATMPF_>	$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
dnl use the generic routine (for now)
R<0>		$@ OK		no limit
R<$+>		$: <$1> $| $(arith l $@ $1 $@ $&{client_rate} $)
dnl log this? Connection rate $&{client_rate} exceeds limit $1.
R<$+> $| TRUE	$#error $@ 4.3.2 $: _RATE_CONTROL_REPLY Connection rate limit exceeded.
')')

ifdef(`_CONN_CONTROL_',`dnl
######################################################################
###  ConnControl: 
###	Parameters:	ignored
###	return: $#error or OK
######################################################################
SConnControl
ifdef(`_ACCESS_TABLE_', `dnl
R$*		$: <A:$&{client_addr}> <E:>
dnl also look up a default value via E:
R$+		$: $>SearchList <! ClientConn> $| $1 <>
dnl found nothing: stop here
R<?>		$@ OK
ifdef(`_ATMPF_', `dnl tempfail?
R<$* _ATMPF_>	$#error $@ 4.3.0 $: "451 Temporary system failure. Please try again later."', `dnl')
dnl use the generic routine (for now)
R<0>		$@ OK		no limit
R<$+>		$: <$1> $| $(arith l $@ $1 $@ $&{client_connections} $)
dnl log this: Open connections $&{client_connections} exceeds limit $1.
R<$+> $| TRUE	$#error $@ 4.3.2 $: _CONN_CONTROL_REPLY Too many open connections.
')')

undivert(9)dnl LOCAL_RULESETS
#
######################################################################
######################################################################
#####
`#####			MAIL FILTER DEFINITIONS'
#####
######################################################################
######################################################################
_MAIL_FILTERS_
#
######################################################################
######################################################################
#####
`#####			MAILER DEFINITIONS'
#####
######################################################################
######################################################################
undivert(7)dnl MAILER_DEFINITIONS

