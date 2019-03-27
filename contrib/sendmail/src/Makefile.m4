dnl $Id: Makefile.m4,v 8.143 2013-09-04 19:49:04 ca Exp $
include(confBUILDTOOLSDIR`/M4/switch.m4')

define(`confREQUIRE_LIBSM', `true')
define(`confREQUIRE_SM_OS_H', `true')
bldPRODUCT_START(`executable', `sendmail')
define(`bldBIN_TYPE', `G')
define(`bldINSTALL_DIR', `')
define(`bldSOURCES', `main.c alias.c arpadate.c bf.c collect.c conf.c control.c convtime.c daemon.c deliver.c domain.c envelope.c err.c headers.c macro.c map.c mci.c milter.c mime.c parseaddr.c queue.c ratectrl.c readcf.c recipient.c sasl.c savemail.c sfsasl.c shmticklib.c sm_resolve.c srvrsmtp.c stab.c stats.c sysexits.c timers.c tls.c trace.c udb.c usersmtp.c util.c version.c ')
PREPENDDEF(`confENVDEF', `confMAPDEF')
bldPUSH_SMLIB(`sm')
bldPUSH_SMLIB(`smutil')


dnl hack: /etc/mail is not defined as "location of .cf" in the build system
define(`bldTARGET_INST_DEP', ifdef(`confINST_DEP', `confINST_DEP',
`${DESTDIR}/etc/mail/submit.cf ${DESTDIR}${MSPQ}'))dnl
define(`bldTARGET_LINKS', ifdef(`confLINKS', `confLINKS',
`${DESTDIR}${UBINDIR}/newaliases ${DESTDIR}${UBINDIR}/mailq ${DESTDIR}${UBINDIR}/hoststat ${DESTDIR}${UBINDIR}/purgestat')
)dnl

# location of sendmail statistics file (usually /etc/mail/ or /var/log)
STDIR= ifdef(`confSTDIR', `confSTDIR', `/etc/mail')

# statistics file name
STFILE=	ifdef(`confSTFILE', `confSTFILE', `statistics')
MSPSTFILE=ifdef(`confMSP_STFILE', `confMSP_STFILE', `sm-client.st')

# full path to installed statistics file (usually ${STDIR}/statistics)
STPATH= ${STDIR}/${STFILE}

# location of sendmail helpfile file (usually /etc/mail)
HFDIR= ifdef(`confHFDIR', `confHFDIR', `/etc/mail')

# full path to installed help file (usually ${HFDIR}/helpfile)
HFFILE= ${HFDIR}/ifdef(`confHFFILE', `confHFFILE', `helpfile')

ifdef(`confSMSRCADD', `APPENDDEF(`confSRCADD', `confSMSRCADD')')
ifdef(`confSMOBJADD', `APPENDDEF(`confOBJADD', `confSMOBJADD')')

bldPUSH_TARGET(`statistics')
divert(bldTARGETS_SECTION)
statistics:
	${CP} /dev/null statistics

${DESTDIR}/etc/mail/submit.cf:
	@echo "Please read INSTALL if anything fails while installing the binary."
	@echo "${DESTDIR}/etc/mail/submit.cf will be installed now."
	cd ${SRCDIR}/cf/cf && make install-submit-cf

MSPQ=ifdef(`confMSP_QUEUE_DIR', `confMSP_QUEUE_DIR', `/var/spool/clientmqueue')

${DESTDIR}${MSPQ}:
	@echo "Please read INSTALL if anything fails while installing the binary."
	@echo "You must have setup a new user ${MSPQOWN} and a new group ${GBINGRP}"
	@echo "as explained in sendmail/SECURITY."
	mkdir -p ${DESTDIR}${MSPQ}
	chown ${MSPQOWN} ${DESTDIR}${MSPQ}
	chgrp ${GBINGRP} ${DESTDIR}${MSPQ}
	chmod 0770 ${DESTDIR}${MSPQ}

divert(0)

ifdef(`confSETUSERID_INSTALL', `bldPUSH_INSTALL_TARGET(`install-set-user-id')')
ifdef(`confMTA_INSTALL', `bldPUSH_INSTALL_TARGET(`install-sm-mta')')
ifdef(`confNO_HELPFILE_INSTALL',, `bldPUSH_INSTALL_TARGET(`install-hf')')
ifdef(`confNO_STATISTICS_INSTALL',, `bldPUSH_INSTALL_TARGET(`install-st')')
divert(bldTARGETS_SECTION)

install-set-user-id: bldCURRENT_PRODUCT ifdef(`confNO_HELPFILE_INSTALL',, `install-hf') ifdef(`confNO_STATISTICS_INSTALL',, `install-st') ifdef(`confNO_MAN_BUILD',, `install-docs')
	${INSTALL} -c -o ${S`'BINOWN} -g ${S`'BINGRP} -m ${S`'BINMODE} bldCURRENT_PRODUCT ${DESTDIR}${M`'BINDIR}
	for i in ${sendmailTARGET_LINKS}; do \
		rm -f $$i; \
		${LN} ${LNOPTS} ${M`'BINDIR}/sendmail $$i; \
	done

define(`confMTA_LINKS', `${DESTDIR}${UBINDIR}/newaliases ${DESTDIR}${UBINDIR}/mailq ${DESTDIR}${UBINDIR}/hoststat ${DESTDIR}${UBINDIR}/purgestat')
install-sm-mta: bldCURRENT_PRODUCT
	${INSTALL} -c -o ${M`'BINOWN} -g ${M`'BINGRP} -m ${M`'BINMODE} bldCURRENT_PRODUCT ${DESTDIR}${M`'BINDIR}/sm-mta
	for i in confMTA_LINKS; do \
		rm -f $$i; \
		${LN} ${LNOPTS} ${M`'BINDIR}/sm-mta $$i; \
	done

install-hf:
	if [ ! -d ${DESTDIR}${HFDIR} ]; then mkdir -p ${DESTDIR}${HFDIR}; else :; fi
	${INSTALL} -c -o ${UBINOWN} -g ${UBINGRP} -m 444 helpfile ${DESTDIR}${HFFILE}

install-st: statistics
	if [ ! -d ${DESTDIR}${STDIR} ]; then mkdir -p ${DESTDIR}${STDIR}; else :; fi
	${INSTALL} -c -o ${SBINOWN} -g ${UBINGRP} -m ifdef(`confSTMODE', `confSTMODE', `0600') statistics ${DESTDIR}${STPATH}

install-submit-st: statistics ${DESTDIR}${MSPQ}
	${INSTALL} -c -o ${MSPQOWN} -g ${GBINGRP} -m ifdef(`confSTMODE', `confSTMODE', `0600') statistics ${DESTDIR}${MSPQ}/${MSPSTFILE}

divert(0)
bldPRODUCT_END

bldPRODUCT_START(`manpage', `sendmail')
define(`bldSOURCES', `sendmail.8 aliases.5 mailq.1 newaliases.1')
bldPRODUCT_END

bldFINISH
