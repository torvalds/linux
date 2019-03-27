/*****************************************************************************
 *
 *  ntpSnmpSubAgentObject.h
 *
 *	Definitions and macros for ntpSnmpSubAgentObject.c
 *
 ****************************************************************************/


#ifndef NTPSNMPSUBAGENTOBJECT_H
#define NTPSNMPSUBAGENTOBJECT_H

/* Function Prototypes */
size_t ntpsnmpd_parse_string(const char *string, char *field, size_t
			     fieldsize, char *value, size_t valuesize);
size_t ntpsnmpd_cut_string(const char *string, char *dest, char delim,
			   int fieldnumber, size_t maxsize);
size_t read_ntp_value(const char *variable, char *value,
		      size_t valuesize);

/* Initialization */
void init_ntpSnmpSubagentObject(void);

/* MIB Section 1 Callback Functions*/
Netsnmp_Node_Handler get_ntpEntSoftwareName;
Netsnmp_Node_Handler get_ntpEntSoftwareVersion;
Netsnmp_Node_Handler get_ntpEntSoftwareVendor;
Netsnmp_Node_Handler get_ntpEntSystemType;
Netsnmp_Node_Handler get_ntpEntTimeResolution;
Netsnmp_Node_Handler get_ntpEntTimePrecision;
Netsnmp_Node_Handler get_ntpEntTimeDistance;

/* MIB Section 2 Callback Functions (TODO) */
Netsnmp_Node_Handler get_ntpEntStatusCurrentMode;
Netsnmp_Node_Handler get_ntpEntStatusCurrentModeVal;
Netsnmp_Node_Handler get_ntpEntStatusStratum;
Netsnmp_Node_Handler get_ntpEntStatusActiveRefSourceId;
Netsnmp_Node_Handler get_ntpEntStatusActiveRefSourceName;
Netsnmp_Node_Handler get_ntpEntStatusActiveOffset;

#define NTPV4_OID 1,3,6,1,2,1,197	/* mib-2 197 */


/*
 * The following macros simplify the registration of the callback
 * functions and register the name and OID of either read-only (RO) or
 * read-write (RW) functions.
 */
 
#define SETUP_OID_RO(oidname, ...)				\
static oid oidname##_oid [] = { __VA_ARGS__ };			\
{								\
	netsnmp_register_read_only_instance(			\
		netsnmp_create_handler_registration(		\
			"#oidname",				\
			get_##oidname,				\
			oidname##_oid,				\
			OID_LENGTH				\
			( oidname##_oid ),			\
			HANDLER_CAN_RONLY));			\
}

#define SETUP_OID_RW(oidname, ...)				\
static oid oidname##_oid [] = { __VA_ARGS__ };			\
{								\
	netsnmp_register_instance(				\
		netsnmp_create_handler_registration(		\
			"#oidname",				\
			do_##oidname,				\
			oidname##_oid,				\
			OID_LENGTH				\
			( oidname##_oid ),			\
			HANDLER_CAN_RWRITE));			\
}

#define NTP_OID_RO(oidname, w, x, y, z)				\
	SETUP_OID_RO(oidname, NTPV4_OID, w, x, y, z)
#define NTP_OID_RW(oidname, w, x, y, z)				\
	SETUP_OID_RW(oidname, NTPV4_OID, w, x, y, z)

#endif
