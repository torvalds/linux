dnl $Id: Makefile.m4,v 8.36 2006-06-28 21:08:04 ca Exp $
include(confBUILDTOOLSDIR`/M4/switch.m4')

define(`confREQUIRE_LIBSM', `true')
define(`confREQUIRE_SM_OS_H', `true')
# sendmail dir
SMSRCDIR=	ifdef(`confSMSRCDIR', `confSMSRCDIR', `${SRCDIR}/sendmail')
PREPENDDEF(`confENVDEF', `confMAPDEF')
PREPENDDEF(`confINCDIRS', `-I${SMSRCDIR} ')

bldPRODUCT_START(`executable', `smrsh')
define(`bldINSTALL_DIR', `E')
define(`bldSOURCES', `smrsh.c ')
bldPUSH_SMLIB(`sm')
APPENDDEF(`confENVDEF', `-DNOT_SENDMAIL')
bldPRODUCT_END

bldPRODUCT_START(`manpage', `smrsh')
define(`bldSOURCES', `smrsh.8')
bldPRODUCT_END

bldFINISH
