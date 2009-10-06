/*****************************************************************************
* Copyright 2001 - 2009 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/
#include <mach/csp/chipcHw_def.h>

#define CLK_TYPE_PRIMARY         1	/* primary clock must NOT have a parent */
#define CLK_TYPE_PLL1            2	/* PPL1 */
#define CLK_TYPE_PLL2            4	/* PPL2 */
#define CLK_TYPE_PROGRAMMABLE    8	/* programmable clock rate */
#define CLK_TYPE_BYPASSABLE      16	/* parent can be changed */

#define CLK_MODE_XTAL            1	/* clock source is from crystal */

struct clk {
	const char *name;	/* clock name */
	unsigned int type;	/* clock type */
	unsigned int mode;	/* current mode */
	volatile int use_bypass;	/* indicate if it's in bypass mode */
	chipcHw_CLOCK_e csp_id;	/* clock ID for CSP CHIPC */
	unsigned long rate_hz;	/* clock rate in Hz */
	unsigned int use_cnt;	/* usage count */
	struct clk *parent;	/* parent clock */
};
