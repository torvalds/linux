/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
 * Definition of the Error Log Structure
 * This structure will be copied into the Error Log buffer
 * during the NDIS General Request ReadErrorLog by the MAC Driver
 */

struct	s_error_log {

	/*
	 * place holder for token ring adapter error log (zeros)
	 */
	u_char	reserved_0 ;			/* byte 0 inside Error Log */
	u_char	reserved_1 ;			/* byte 1 */
	u_char	reserved_2 ;			/* byte 2 */	
	u_char	reserved_3 ;			/* byte 3 */
	u_char	reserved_4 ;			/* byte 4 */
	u_char	reserved_5 ;			/* byte 5 */
	u_char	reserved_6 ;			/* byte 6 */
	u_char	reserved_7 ;			/* byte 7 */
	u_char	reserved_8 ;			/* byte 8 */
	u_char	reserved_9 ;			/* byte 9 */
	u_char	reserved_10 ;			/* byte 10 */
	u_char	reserved_11 ;			/* byte 11 */
	u_char	reserved_12 ;			/* byte 12 */
	u_char	reserved_13 ;			/* byte 13 */

	/*
	 * FDDI link statistics 
	 */
/*
 * smt error low
 */
#define SMT_ERL_AEB	(1<<15)			/* A elast. buffer */
#define SMT_ERL_BLC	(1<<14)			/* B link error condition */
#define SMT_ERL_ALC	(1<<13)			/* A link error condition */
#define SMT_ERL_NCC	(1<<12)			/* not copied condition */
#define SMT_ERL_FEC	(1<<11)			/* frame error condition */

/*
 * smt event low
 */
#define SMT_EVL_NCE	(1<<5)

	u_short	smt_error_low ;			/* byte 14/15 */
	u_short	smt_error_high ;		/* byte 16/17 */
	u_short	smt_event_low ;			/* byte 18/19 */
	u_short	smt_event_high ;		/* byte 20/21 */
	u_short	connection_policy_violation ;	/* byte 22/23 */
	u_short port_event ;			/* byte 24/25 */
	u_short set_count_low ;			/* byte 26/27 */
	u_short set_count_high ;		/* byte 28/29 */
	u_short	aci_id_code ;			/* byte 30/31 */
	u_short	purge_frame_counter ;		/* byte 32/33 */

	/*
	 * CMT and RMT state machines
	 */
	u_short	ecm_state ;			/* byte 34/35 */
	u_short	pcm_a_state ;			/* byte 36/37 */
	u_short pcm_b_state ;			/* byte 38/39 */
	u_short	cfm_state ;			/* byte 40/41 */
	u_short	rmt_state ;			/* byte 42/43 */

	u_short	not_used[30] ;			/* byte 44-103 */

	u_short	ucode_version_level ;		/* byte 104/105 */

	u_short	not_used_1 ;			/* byte 106/107 */
	u_short not_used_2 ;			/* byte 108/109 */
} ;
