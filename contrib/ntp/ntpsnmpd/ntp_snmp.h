/*
 * ntp_snmp.h -- common net-snmp header includes and workaround
 *		 for Autoconf-related PACKAGE_* redefinitions between
 *		 net-snmp and NTP.
 *
 * Currently ntpsnmpd *.c files are exceptions to the rule that every .c
 * file should include <config.h> before any other headers.  It would be
 * ideal to rearrange its includes so that our config.h is first, but
 * that is complicated by the redefinitions between our config.h and
 * net-snmp/net-snmp-config.h.
 */

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#ifdef PACKAGE_BUGREPORT
# undef PACKAGE_BUGREPORT
#endif
#ifdef PACKAGE_NAME
# undef PACKAGE_NAME
#endif
#ifdef PACKAGE_STRING
# undef PACKAGE_STRING
#endif
#ifdef PACKAGE_TARNAME
# undef PACKAGE_TARNAME
#endif
#ifdef PACKAGE_URL
# undef PACKAGE_URL
#endif
#ifdef PACKAGE_VERSION
# undef PACKAGE_VERSION
#endif

#include <ntpSnmpSubagentObject.h>
#include <config.h>
