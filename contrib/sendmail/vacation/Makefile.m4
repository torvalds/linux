dnl $Id: Makefile.m4,v 8.26 2006-06-28 21:08:05 ca Exp $
include(confBUILDTOOLSDIR`/M4/switch.m4')

define(`confREQUIRE_LIBSM', `true')
define(`confREQUIRE_SM_OS_H', `true')
# sendmail dir
SMSRCDIR=	ifdef(`confSMSRCDIR', `confSMSRCDIR', `${SRCDIR}/sendmail')
PREPENDDEF(`confENVDEF', `confMAPDEF')
PREPENDDEF(`confINCDIRS', `-I${SMSRCDIR} ')

bldPRODUCT_START(`executable', `vacation')
define(`bldSOURCES', `vacation.c ')
bldPUSH_SMLIB(`sm')
bldPUSH_SMLIB(`smutil')
bldPUSH_SMLIB(`smdb')
APPENDDEF(`confENVDEF', `-DNOT_SENDMAIL')
bldPRODUCT_END

bldPRODUCT_START(`manpage', `vacation')
define(`bldSOURCES', `vacation.1')
bldPRODUCT_END

bldFINISH
