/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************/

/*
 *	mcfslt.h -- ColdFire internal Slice (SLT) timer support defines.
 *
 *	(C) Copyright 2004, Greg Ungerer (gerg@snapgear.com)
 *	(C) Copyright 2009, Philippe De Muyter (phdm@macqel.be)
 */

/****************************************************************************/
#ifndef mcfslt_h
#define mcfslt_h
/****************************************************************************/

/*
 *	Define the SLT timer register set addresses.
 */
#define MCFSLT_STCNT		0x00	/* Terminal count */
#define MCFSLT_SCR		0x04	/* Control */
#define MCFSLT_SCNT		0x08	/* Current count */
#define MCFSLT_SSR		0x0C	/* Status */

/*
 *	Bit definitions for the SCR control register.
 */
#define MCFSLT_SCR_RUN		0x04000000	/* Run mode (continuous) */
#define MCFSLT_SCR_IEN		0x02000000	/* Interrupt enable */
#define MCFSLT_SCR_TEN		0x01000000	/* Timer enable */

/*
 *	Bit definitions for the SSR status register.
 */
#define MCFSLT_SSR_BE		0x02000000	/* Bus error condition */
#define MCFSLT_SSR_TE		0x01000000	/* Timeout condition */

/****************************************************************************/
#endif	/* mcfslt_h */
