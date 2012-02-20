/* $Id: capidrv.h,v 1.2.8.2 2001/09/23 22:24:33 kai Exp $
 *
 * ISDN4Linux Driver, using capi20 interface (kernelcapi)
 *
 * Copyright 1997 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __CAPIDRV_H__
#define __CAPIDRV_H__

/*
 * LISTEN state machine
 */
#define ST_LISTEN_NONE			0	/* L-0 */
#define ST_LISTEN_WAIT_CONF		1	/* L-0.1 */
#define ST_LISTEN_ACTIVE		2	/* L-1 */
#define ST_LISTEN_ACTIVE_WAIT_CONF	3	/* L-1.1 */


#define EV_LISTEN_REQ			1	/* L-0 -> L-0.1
						   L-1 -> L-1.1 */
#define EV_LISTEN_CONF_ERROR		2	/* L-0.1 -> L-0
						   L-1.1 -> L-1 */
#define EV_LISTEN_CONF_EMPTY		3	/* L-0.1 -> L-0
						   L-1.1 -> L-0 */
#define EV_LISTEN_CONF_OK		4	/* L-0.1 -> L-1
						   L-1.1 -> L.1 */

/*
 * per plci state machine
 */
#define ST_PLCI_NONE			0	/* P-0 */
#define ST_PLCI_OUTGOING		1	/* P-0.1 */
#define ST_PLCI_ALLOCATED		2	/* P-1 */
#define ST_PLCI_ACTIVE			3	/* P-ACT */
#define ST_PLCI_INCOMING		4	/* P-2 */
#define ST_PLCI_FACILITY_IND		5	/* P-3 */
#define ST_PLCI_ACCEPTING		6	/* P-4 */
#define ST_PLCI_DISCONNECTING		7	/* P-5 */
#define ST_PLCI_DISCONNECTED		8	/* P-6 */
#define ST_PLCI_RESUMEING		9	/* P-0.Res */
#define ST_PLCI_RESUME			10	/* P-Res */
#define ST_PLCI_HELD			11	/* P-HELD */

#define EV_PLCI_CONNECT_REQ		1	/* P-0 -> P-0.1
						 */
#define EV_PLCI_CONNECT_CONF_ERROR	2	/* P-0.1 -> P-0
						 */
#define EV_PLCI_CONNECT_CONF_OK		3	/* P-0.1 -> P-1
						 */
#define EV_PLCI_FACILITY_IND_UP		4	/* P-0 -> P-1
						 */
#define EV_PLCI_CONNECT_IND		5	/* P-0 -> P-2
						 */
#define EV_PLCI_CONNECT_ACTIVE_IND	6	/* P-1 -> P-ACT
						 */
#define EV_PLCI_CONNECT_REJECT		7	/* P-2 -> P-5
						   P-3 -> P-5
						*/
#define EV_PLCI_DISCONNECT_REQ		8	/* P-1 -> P-5
						   P-2 -> P-5
						   P-3 -> P-5
						   P-4 -> P-5
						   P-ACT -> P-5
						   P-Res -> P-5 (*)
						   P-HELD -> P-5 (*)
						*/
#define EV_PLCI_DISCONNECT_IND		9	/* P-1 -> P-6
						   P-2 -> P-6
						   P-3 -> P-6
						   P-4 -> P-6
						   P-5 -> P-6
						   P-ACT -> P-6
						   P-Res -> P-6 (*)
						   P-HELD -> P-6 (*)
						*/
#define EV_PLCI_FACILITY_IND_DOWN	10	/* P-0.1 -> P-5
						   P-1 -> P-5
						   P-ACT -> P-5
						   P-2 -> P-5
						   P-3 -> P-5
						   P-4 -> P-5
						*/
#define EV_PLCI_DISCONNECT_RESP		11	/* P-6 -> P-0
						 */
#define EV_PLCI_CONNECT_RESP		12	/* P-6 -> P-0
						 */

#define EV_PLCI_RESUME_REQ		13	/* P-0 -> P-0.Res
						 */
#define EV_PLCI_RESUME_CONF_OK		14	/* P-0.Res -> P-Res
						 */
#define EV_PLCI_RESUME_CONF_ERROR	15	/* P-0.Res -> P-0
						 */
#define EV_PLCI_RESUME_IND		16	/* P-Res -> P-ACT
						 */
#define EV_PLCI_HOLD_IND		17	/* P-ACT -> P-HELD
						 */
#define EV_PLCI_RETRIEVE_IND		18	/* P-HELD -> P-ACT
						 */
#define EV_PLCI_SUSPEND_IND		19	/* P-ACT -> P-5
						 */
#define EV_PLCI_CD_IND			20	/* P-2 -> P-5
						 */

/*
 * per ncci state machine
 */
#define ST_NCCI_PREVIOUS			-1
#define ST_NCCI_NONE				0	/* N-0 */
#define ST_NCCI_OUTGOING			1	/* N-0.1 */
#define ST_NCCI_INCOMING			2	/* N-1 */
#define ST_NCCI_ALLOCATED			3	/* N-2 */
#define ST_NCCI_ACTIVE				4	/* N-ACT */
#define ST_NCCI_RESETING			5	/* N-3 */
#define ST_NCCI_DISCONNECTING			6	/* N-4 */
#define ST_NCCI_DISCONNECTED			7	/* N-5 */

#define EV_NCCI_CONNECT_B3_REQ			1	/* N-0 -> N-0.1 */
#define EV_NCCI_CONNECT_B3_IND			2	/* N-0 -> N.1 */
#define EV_NCCI_CONNECT_B3_CONF_OK		3	/* N-0.1 -> N.2 */
#define EV_NCCI_CONNECT_B3_CONF_ERROR		4	/* N-0.1 -> N.0 */
#define EV_NCCI_CONNECT_B3_REJECT		5	/* N-1 -> N-4 */
#define EV_NCCI_CONNECT_B3_RESP			6	/* N-1 -> N-2 */
#define EV_NCCI_CONNECT_B3_ACTIVE_IND		7	/* N-2 -> N-ACT */
#define EV_NCCI_RESET_B3_REQ			8	/* N-ACT -> N-3 */
#define EV_NCCI_RESET_B3_IND			9	/* N-3 -> N-ACT */
#define EV_NCCI_DISCONNECT_B3_IND		10	/* N-4 -> N.5 */
#define EV_NCCI_DISCONNECT_B3_CONF_ERROR	11	/* N-4 -> previous */
#define EV_NCCI_DISCONNECT_B3_REQ		12	/* N-1 -> N-4
							   N-2 -> N-4
							   N-3 -> N-4
							   N-ACT -> N-4 */
#define EV_NCCI_DISCONNECT_B3_RESP		13	/* N-5 -> N-0 */

#endif				/* __CAPIDRV_H__ */
