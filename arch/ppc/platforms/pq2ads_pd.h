#ifndef __PQ2ADS_PD_H
#define __PQ2ADS_PD_H
/*
 * arch/ppc/platforms/82xx/pq2ads_pd.h
 *
 * Some defines for MPC82xx board-specific PlatformDevice descriptions
 *
 * 2005 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/* FCC1 Clock Source Configuration.  These can be redefined in the board specific file.
   Can only choose from CLK9-12 */

#define F1_RXCLK	11
#define F1_TXCLK	10

/* FCC2 Clock Source Configuration.  These can be redefined in the board specific file.
   Can only choose from CLK13-16 */
#define F2_RXCLK	15
#define F2_TXCLK	16

/* FCC3 Clock Source Configuration.  These can be redefined in the board specific file.
   Can only choose from CLK13-16 */
#define F3_RXCLK	13
#define F3_TXCLK	14

#endif
