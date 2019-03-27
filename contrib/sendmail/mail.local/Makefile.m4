dnl $Id: Makefile.m4,v 8.52 2006-06-28 21:08:02 ca Exp $
include(confBUILDTOOLSDIR`/M4/switch.m4')

define(`confREQUIRE_LIBSM', `true')
define(`confREQUIRE_SM_OS_H', `true')
# sendmail dir
SMSRCDIR=     ifdef(`confSMSRCDIR', `confSMSRCDIR', `${SRCDIR}/sendmail')
PREPENDDEF(`confENVDEF', `confMAPDEF')
PREPENDDEF(`confINCDIRS', `-I${SMSRCDIR} ')

bldPRODUCT_START(`executable', `mail.local')
define(`bldINSTALL_DIR', `E')
define(`bldNO_INSTALL', `true')
define(`bldSOURCES', `mail.local.c ')
bldPUSH_SMLIB(`sm')
APPENDDEF(`confENVDEF', `-DNOT_SENDMAIL')
bldPRODUCT_END

bldPRODUCT_START(`manpage', `mail.local')
define(`bldSOURCES', `mail.local.8')
bldPRODUCT_END

divert(bldTARGETS_SECTION)
install:
	@echo "NOTE: This version of mail.local is not suited for some operating"
	@echo "      systems such as HP-UX and Solaris.  Please consult the"
	@echo "      README file in the mail.local directory.  You can force"
	@echo "      the install using 'Build force-install'."

force-install: install-mail.local ifdef(`confNO_MAN_BUILD',, `install-docs')

install-mail.local: mail.local
	${INSTALL} -c -o ${UBINOWN} -g ${UBINGRP} -m ${UBINMODE} mail.local ${DESTDIR}${EBINDIR}
divert

bldFINISH
