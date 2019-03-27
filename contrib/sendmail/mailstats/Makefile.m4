dnl $Id: Makefile.m4,v 8.36 2006-06-28 21:08:02 ca Exp $
include(confBUILDTOOLSDIR`/M4/switch.m4')

define(`confREQUIRE_LIBSM', `true')
define(`confREQUIRE_SM_OS_H', `true')
# sendmail dir
SMSRCDIR=	ifdef(`confSMSRCDIR', `confSMSRCDIR', `${SRCDIR}/sendmail')
PREPENDDEF(`confENVDEF', `confMAPDEF')
PREPENDDEF(`confINCDIRS', `-I${SMSRCDIR} ')

bldPRODUCT_START(`executable', `mailstats')
define(`bldINSTALL_DIR', `S')
define(`bldSOURCES', `mailstats.c ')
bldPUSH_SMLIB(`sm')
bldPUSH_SMLIB(`smutil')
APPENDDEF(`confENVDEF', `-DNOT_SENDMAIL')
bldPRODUCT_END

bldPRODUCT_START(`manpage', `mailstats')
define(`bldSOURCES', `mailstats.8')
bldPRODUCT_END

bldFINISH

