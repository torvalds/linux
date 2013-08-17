/*
 *	linux/include/asm-alpha/err_common.h
 *
 *	Copyright (C) 2000 Jeff Wiedemeier (Compaq Computer Corporation)
 *
 *	Contains declarations and macros to support Alpha error handling
 * 	implementations.
 */

#ifndef __ALPHA_ERR_COMMON_H
#define __ALPHA_ERR_COMMON_H 1

/*
 * SCB Vector definitions
 */
#define SCB_Q_SYSERR	0x620
#define SCB_Q_PROCERR	0x630
#define SCB_Q_SYSMCHK	0x660
#define SCB_Q_PROCMCHK	0x670
#define SCB_Q_SYSEVENT	0x680

/*
 * Disposition definitions for logout frame parser
 */
#define MCHK_DISPOSITION_UNKNOWN_ERROR		0x00
#define MCHK_DISPOSITION_REPORT			0x01
#define MCHK_DISPOSITION_DISMISS		0x02

/*
 * Error Log definitions
 */
/*
 * Types
 */

#define EL_CLASS__TERMINATION		(0)
#  define EL_TYPE__TERMINATION__TERMINATION		(0)
#define EL_CLASS__HEADER		(5)
#  define EL_TYPE__HEADER__SYSTEM_ERROR_FRAME		(1)
#  define EL_TYPE__HEADER__SYSTEM_EVENT_FRAME		(2)
#  define EL_TYPE__HEADER__HALT_FRAME			(3)
#  define EL_TYPE__HEADER__LOGOUT_FRAME			(19)
#define EL_CLASS__GENERAL_NOTIFICATION	(9)
#define EL_CLASS__PCI_ERROR_FRAME	(11)
#define EL_CLASS__REGATTA_FAMILY	(12)
#  define EL_TYPE__REGATTA__PROCESSOR_ERROR_FRAME	(1)
#  define EL_TYPE__REGATTA__SYSTEM_ERROR_FRAME		(2)
#  define EL_TYPE__REGATTA__ENVIRONMENTAL_FRAME		(3)
#  define EL_TYPE__REGATTA__TITAN_PCHIP0_EXTENDED	(8)
#  define EL_TYPE__REGATTA__TITAN_PCHIP1_EXTENDED	(9)
#  define EL_TYPE__REGATTA__TITAN_MEMORY_EXTENDED	(10)
#  define EL_TYPE__REGATTA__PROCESSOR_DBL_ERROR_HALT	(11)
#  define EL_TYPE__REGATTA__SYSTEM_DBL_ERROR_HALT	(12)
#define EL_CLASS__PAL                   (14)
#  define EL_TYPE__PAL__LOGOUT_FRAME                    (1)
#  define EL_TYPE__PAL__EV7_PROCESSOR			(4)
#  define EL_TYPE__PAL__EV7_ZBOX			(5)
#  define EL_TYPE__PAL__EV7_RBOX			(6)
#  define EL_TYPE__PAL__EV7_IO				(7)
#  define EL_TYPE__PAL__ENV__AMBIENT_TEMPERATURE	(10)
#  define EL_TYPE__PAL__ENV__AIRMOVER_FAN		(11)
#  define EL_TYPE__PAL__ENV__VOLTAGE			(12)
#  define EL_TYPE__PAL__ENV__INTRUSION			(13)
#  define EL_TYPE__PAL__ENV__POWER_SUPPLY		(14)
#  define EL_TYPE__PAL__ENV__LAN			(15)
#  define EL_TYPE__PAL__ENV__HOT_PLUG			(16)

union el_timestamp {
	struct {
		u8 second;
		u8 minute;
		u8 hour;
		u8 day;
		u8 month;
		u8 year;
	} b;
	u64 as_int;
};

struct el_subpacket {
	u16 length;		/* length of header (in bytes)	*/
	u16 class;		/* header class and type...   	*/
	u16 type;		/* ...determine content     	*/
	u16 revision;		/* header revision 		*/
	union {
		struct {	/* Class 5, Type 1 - System Error	*/
			u32 frame_length;
			u32 frame_packet_count;			
		} sys_err;			
		struct {	/* Class 5, Type 2 - System Event 	*/
			union el_timestamp timestamp;
			u32 frame_length;
			u32 frame_packet_count;			
		} sys_event;
		struct {	/* Class 5, Type 3 - Double Error Halt	*/
			u16 halt_code;
			u16 reserved;
			union el_timestamp timestamp;
			u32 frame_length;
			u32 frame_packet_count;
		} err_halt;
		struct {	/* Clasee 5, Type 19 - Logout Frame Header */
			u32 frame_length;
			u32 frame_flags;
			u32 cpu_offset;	
			u32 system_offset;
		} logout_header;
		struct {	/* Class 12 - Regatta			*/
			u64 cpuid;
			u64 data_start[1];
		} regatta_frame;
		struct {	/* Raw 				        */
			u64 data_start[1];
		} raw;
	} by_type;
};

#endif /* __ALPHA_ERR_COMMON_H */
