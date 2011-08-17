/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 *------------------------------------------------------------------------------
 *
 * et1310_phy.h - Defines, structs, enums, prototypes, etc. pertaining to the
 *                PHY.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#ifndef _ET1310_PHY_H_
#define _ET1310_PHY_H_

#include "et1310_address_map.h"

/* MI Register Addresses */
#define MI_CONTROL_REG                      0
#define MI_STATUS_REG                       1
#define MI_PHY_IDENTIFIER_1_REG             2
#define MI_PHY_IDENTIFIER_2_REG             3
#define MI_AUTONEG_ADVERTISEMENT_REG        4
#define MI_AUTONEG_LINK_PARTNER_ABILITY_REG 5
#define MI_AUTONEG_EXPANSION_REG            6
#define MI_AUTONEG_NEXT_PAGE_TRANSMIT_REG   7
#define MI_LINK_PARTNER_NEXT_PAGE_REG       8
#define MI_1000BASET_CONTROL_REG            9
#define MI_1000BASET_STATUS_REG             10
#define MI_RESERVED11_REG                   11
#define MI_RESERVED12_REG                   12
#define MI_RESERVED13_REG                   13
#define MI_RESERVED14_REG                   14
#define MI_EXTENDED_STATUS_REG              15

/* VMI Register Addresses */
#define VMI_RESERVED16_REG                  16
#define VMI_RESERVED17_REG                  17
#define VMI_RESERVED18_REG                  18
#define VMI_LOOPBACK_CONTROL_REG            19
#define VMI_RESERVED20_REG                  20
#define VMI_MI_CONTROL_REG                  21
#define VMI_PHY_CONFIGURATION_REG           22
#define VMI_PHY_CONTROL_REG                 23
#define VMI_INTERRUPT_MASK_REG              24
#define VMI_INTERRUPT_STATUS_REG            25
#define VMI_PHY_STATUS_REG                  26
#define VMI_LED_CONTROL_1_REG               27
#define VMI_LED_CONTROL_2_REG               28
#define VMI_RESERVED29_REG                  29
#define VMI_RESERVED30_REG                  30
#define VMI_RESERVED31_REG                  31

/* PHY Register Mapping(MI) Management Interface Regs */
struct mi_regs {
	u8 bmcr;	/* Basic mode control reg(Reg 0x00) */
	u8 bmsr;	/* Basic mode status reg(Reg 0x01) */
	u8 idr1;	/* Phy identifier reg 1(Reg 0x02) */
	u8 idr2;	/* Phy identifier reg 2(Reg 0x03) */
	u8 anar;	/* Auto-Negotiation advertisement(Reg 0x04) */
	u8 anlpar;	/* Auto-Negotiation link Partner Ability(Reg 0x05) */
	u8 aner;	/* Auto-Negotiation expansion reg(Reg 0x06) */
	u8 annptr;	/* Auto-Negotiation next page transmit reg(Reg 0x07) */
	u8 lpnpr;	/* link partner next page reg(Reg 0x08) */
	u8 gcr;		/* Gigabit basic mode control reg(Reg 0x09) */
	u8 gsr;		/* Gigabit basic mode status reg(Reg 0x0A) */
	u8 mi_res1[4];	/* Future use by MI working group(Reg 0x0B - 0x0E) */
	u8 esr;		/* Extended status reg(Reg 0x0F) */
	u8 mi_res2[3];	/* Future use by MI working group(Reg 0x10 - 0x12) */
	u8 loop_ctl;	/* Loopback Control Reg(Reg 0x13) */
	u8 mi_res3;	/* Future use by MI working group(Reg 0x14) */
	u8 mcr;		/* MI Control Reg(Reg 0x15) */
	u8 pcr;		/* Configuration Reg(Reg 0x16) */
	u8 phy_ctl;	/* PHY Control Reg(Reg 0x17) */
	u8 imr;		/* Interrupt Mask Reg(Reg 0x18) */
	u8 isr;		/* Interrupt Status Reg(Reg 0x19) */
	u8 psr;		/* PHY Status Reg(Reg 0x1A) */
	u8 lcr1;		/* LED Control 1 Reg(Reg 0x1B) */
	u8 lcr2;		/* LED Control 2 Reg(Reg 0x1C) */
	u8 mi_res4[3];	/* Future use by MI working group(Reg 0x1D - 0x1F) */
};

/*
 * MI Register 0: Basic mode control register
 *	15:	reset
 *	14:	loopback
 *	13:	speed_sel
 *	12:	enable_autoneg
 *	11:	power_down
 *	10:	isolate
 *	9:	restart_autoneg
 *	8:	duplex_mode
 *	7:	col_test
 *	6:	speed_1000_sel
 *	5-0:	res1
 */

/*
 * MI Register 1:  Basic mode status register
 *	15:	link_100T4
 *	14:	link_100fdx
 *	13:	link_100hdx
 *	12:	link_10fdx
 *	11:	link_10hdx
 *	10:	link_100T2fdx
 *	9:	link_100T2hdx
 *	8:	extend_status
 *	7:	res1
 *	6:	preamble_supress
 *	5:	auto_neg_complete
 *	4:	remote_fault
 *	3:	auto_neg_able
 *	2:	link_status
 *	1:	jabber_detect
 *	0:	ext_cap
 */

#define MI_BMSR_LINK_STATUS	  0x04
#define MI_BMSR_AUTO_NEG_COMPLETE 0x20

/*
 * MI Register 4: Auto-negotiation advertisement register
 *
 *	15:	np_indication
 *	14:	res2
 *	13:	remote_fault
 *	12:	res1
 *	11:	cap_asmpause
 *	10:	cap_pause
 *	9:	cap_100T4
 *	8:	cap_100fdx
 *	7:	cap_100hdx
 *	6:	cap_10fdx
 *	5:	cap_10hdx
 *	4-0:	selector
 */

/* MI Register 5: Auto-negotiation link partner advertisement register
 *	15:	np_indication
 *	14:	acknowledge
 *	13:	remote_fault
 *	12:	res1
 *	11:	cap_asmpause
 *	10:	cap_pause
 *	9:	cap_100T4
 *	8:	cap_100fdx
 *	7:	cap_100hdx
 *	6:	cap_10fdx
 *	5:	cap_10hdx
 *	4-0:	selector
 */

/* MI Register 6: Auto-negotiation expansion register
 *	15-5:	reserved
 *	4:	pdf
 *	3:	lp_np_able
 *	2:	np_able
 *	1:	page_rx
 *	0:	lp_an_able
 */

/* MI Register 7: Auto-negotiation next page transmit reg(0x07)
 *	15:	np
 *	14:	reserved
 *	13:	msg_page
 *	12:	ack2
 *	11:	toggle
 *	10-0	msg
 */

/* MI Register 8: Link Partner Next Page Reg(0x08)
 *	15:	np
 *	14:	ack
 *	13:	msg_page
 *	12:	ack2
 *	11:	toggle
 *	10-0:	msg
 */

/* MI Register 9: 1000BaseT Control Reg(0x09)
 *	15-13:	test_mode
 *	12:	ms_config_en
 *	11:	ms_value
 *	10:	port_type
 *	9:	link_1000fdx
 *	8:	link_1000hdx
 *	7-0:	reserved
 */

/* MI Register 10: 1000BaseT Status Reg(0x0A)
 *	15:	ms_config_fault
 *	14:	ms_resolve
 *	13:	local_rx_status
 *	12:	remote_rx_status
 *	11:	link_1000fdx
 *	10:	link_1000hdx
 *	9-8:	reserved
 *	7-0:	idle_err_cnt
 */

/* MI Register 11 - 14: Reserved Regs(0x0B - 0x0E) */

/* MI Register 15: Extended status Reg(0x0F)
 *	15:	link_1000Xfdx
 *	14:	link_1000Xhdx
 *	13:	link_1000fdx
 *	12:	link_1000hdx
 *	11-0:	reserved
 */

/* MI Register 16 - 18: Reserved Reg(0x10-0x12) */

/* MI Register 19: Loopback Control Reg(0x13)
 *	15:	mii_en
 *	14:	pcs_en
 *	13:	pmd_en
 *	12:	all_digital_en
 *	11:	replica_en
 *	10:	line_driver_en
 *	9-0:	reserved
 */

/* MI Register 20: Reserved Reg(0x14) */

/* MI Register 21: Management Interface Control Reg(0x15)
 *	15-11:	reserved
 *	10-4:	mi_error_count
 *	3:	reserved
 *	2:	ignore_10g_fr
 *	1:	reserved
 *	0:	preamble_supress_en
 */

/* MI Register 22: PHY Configuration Reg(0x16)
 *	15:	crs_tx_en
 *	14:	reserved
 *	13-12:	tx_fifo_depth
 *	11-10:	speed_downshift
 *	9:	pbi_detect
 *	8:	tbi_rate
 *	7:	alternate_np
 *	6:	group_mdio_en
 *	5:	tx_clock_en
 *	4:	sys_clock_en
 *	3:	reserved
 *	2-0:	mac_if_mode
 */

/* MI Register 23: PHY CONTROL Reg(0x17)
 *	15:	reserved
 *	14:	tdr_en
 *	13:	reserved
 *	12-11:	downshift_attempts
 *	10-6:	reserved
 *	5:	jabber_10baseT
 *	4:	sqe_10baseT
 *	3:	tp_loopback_10baseT
 *	2:	preamble_gen_en
 *	1:	reserved
 *	0:	force_int
 */

/* MI Register 24: Interrupt Mask Reg(0x18)
 *	15-10:	reserved
 *	9:	mdio_sync_lost
 *	8:	autoneg_status
 *	7:	hi_bit_err
 *	6:	np_rx
 *	5:	err_counter_full
 *	4:	fifo_over_underflow
 *	3:	rx_status
 *	2:	link_status
 *	1:	automatic_speed
 *	0:	int_en
 */


/* MI Register 25: Interrupt Status Reg(0x19)
 *	15-10:	reserved
 *	9:	mdio_sync_lost
 *	8:	autoneg_status
 *	7:	hi_bit_err
 *	6:	np_rx
 *	5:	err_counter_full
 *	4:	fifo_over_underflow
 *	3:	rx_status
 *	2:	link_status
 *	1:	automatic_speed
 *	0:	int_en
 */

/* MI Register 26: PHY Status Reg(0x1A)
 *	15:	reserved
 *	14-13:	autoneg_fault
 *	12:	autoneg_status
 *	11:	mdi_x_status
 *	10:	polarity_status
 *	9-8:	speed_status
 *	7:	duplex_status
 *	6:	link_status
 *	5:	tx_status
 *	4:	rx_status
 *	3:	collision_status
 *	2:	autoneg_en
 *	1:	pause_en
 *	0:	asymmetric_dir
 */

/* MI Register 27: LED Control Reg 1(0x1B)
 *	15-14:	reserved
 *	13-12:	led_dup_indicate
 *	11-10:	led_10baseT
 *	9-8:	led_collision
 *	7-4:	reserved
 *	3-2:	pulse_dur
 *	1:	pulse_stretch1
 *	0:	pulse_stretch0
 */

/* MI Register 28: LED Control Reg 2(0x1C)
 *	15-12:	led_link
 *	11-8:	led_tx_rx
 *	7-4:	led_100BaseTX
 *	3-0:	led_1000BaseT
 */

/* MI Register 29 - 31: Reserved Reg(0x1D - 0x1E) */


/* Prototypes for ET1310_phy.c */
/* Defines for PHY access routines */

/* Define bit operation flags */
#define TRUEPHY_BIT_CLEAR               0
#define TRUEPHY_BIT_SET                 1
#define TRUEPHY_BIT_READ                2

/* Define read/write operation flags */
#ifndef TRUEPHY_READ
#define TRUEPHY_READ                    0
#define TRUEPHY_WRITE                   1
#define TRUEPHY_MASK                    2
#endif

/* Define speeds */
#define TRUEPHY_SPEED_10MBPS            0
#define TRUEPHY_SPEED_100MBPS           1
#define TRUEPHY_SPEED_1000MBPS          2

/* Define duplex modes */
#define TRUEPHY_DUPLEX_HALF             0
#define TRUEPHY_DUPLEX_FULL             1

/* Define master/slave configuration values */
#define TRUEPHY_CFG_SLAVE               0
#define TRUEPHY_CFG_MASTER              1

/* Define MDI/MDI-X settings */
#define TRUEPHY_MDI                     0
#define TRUEPHY_MDIX                    1
#define TRUEPHY_AUTO_MDI_MDIX           2

/* Define 10Base-T link polarities */
#define TRUEPHY_POLARITY_NORMAL         0
#define TRUEPHY_POLARITY_INVERTED       1

/* Define auto-negotiation results */
#define TRUEPHY_ANEG_NOT_COMPLETE       0
#define TRUEPHY_ANEG_COMPLETE           1
#define TRUEPHY_ANEG_DISABLED           2

/* Define duplex advertisement flags */
#define TRUEPHY_ADV_DUPLEX_NONE         0x00
#define TRUEPHY_ADV_DUPLEX_FULL         0x01
#define TRUEPHY_ADV_DUPLEX_HALF         0x02
#define TRUEPHY_ADV_DUPLEX_BOTH     \
	(TRUEPHY_ADV_DUPLEX_FULL | TRUEPHY_ADV_DUPLEX_HALF)

#define PHY_CONTROL                0x00	/* #define TRU_MI_CONTROL_REGISTER                 0 */
#define PHY_STATUS                 0x01	/* #define TRU_MI_STATUS_REGISTER                  1 */
#define PHY_ID_1                   0x02	/* #define TRU_MI_PHY_IDENTIFIER_1_REGISTER        2 */
#define PHY_ID_2                   0x03	/* #define TRU_MI_PHY_IDENTIFIER_2_REGISTER        3 */
#define PHY_AUTO_ADVERTISEMENT     0x04	/* #define TRU_MI_ADVERTISEMENT_REGISTER           4 */
#define PHY_AUTO_LINK_PARTNER      0x05	/* #define TRU_MI_LINK_PARTNER_ABILITY_REGISTER    5 */
#define PHY_AUTO_EXPANSION         0x06	/* #define TRU_MI_EXPANSION_REGISTER               6 */
#define PHY_AUTO_NEXT_PAGE_TX      0x07	/* #define TRU_MI_NEXT_PAGE_TRANSMIT_REGISTER      7 */
#define PHY_LINK_PARTNER_NEXT_PAGE 0x08	/* #define TRU_MI_LINK_PARTNER_NEXT_PAGE_REGISTER  8 */
#define PHY_1000_CONTROL           0x09	/* #define TRU_MI_1000BASET_CONTROL_REGISTER       9 */
#define PHY_1000_STATUS            0x0A	/* #define TRU_MI_1000BASET_STATUS_REGISTER        10 */

#define PHY_EXTENDED_STATUS        0x0F	/* #define TRU_MI_EXTENDED_STATUS_REGISTER         15 */

/* some defines for modem registers that seem to be 'reserved' */
#define PHY_INDEX_REG              0x10
#define PHY_DATA_REG               0x11

#define PHY_MPHY_CONTROL_REG       0x12	/* #define TRU_VMI_MPHY_CONTROL_REGISTER           18 */

#define PHY_LOOPBACK_CONTROL       0x13	/* #define TRU_VMI_LOOPBACK_CONTROL_1_REGISTER     19 */
					/* #define TRU_VMI_LOOPBACK_CONTROL_2_REGISTER     20 */
#define PHY_REGISTER_MGMT_CONTROL  0x15	/* #define TRU_VMI_MI_SEQ_CONTROL_REGISTER         21 */
#define PHY_CONFIG                 0x16	/* #define TRU_VMI_CONFIGURATION_REGISTER          22 */
#define PHY_PHY_CONTROL            0x17	/* #define TRU_VMI_PHY_CONTROL_REGISTER            23 */
#define PHY_INTERRUPT_MASK         0x18	/* #define TRU_VMI_INTERRUPT_MASK_REGISTER         24 */
#define PHY_INTERRUPT_STATUS       0x19	/* #define TRU_VMI_INTERRUPT_STATUS_REGISTER       25 */
#define PHY_PHY_STATUS             0x1A	/* #define TRU_VMI_PHY_STATUS_REGISTER             26 */
#define PHY_LED_1                  0x1B	/* #define TRU_VMI_LED_CONTROL_1_REGISTER          27 */
#define PHY_LED_2                  0x1C	/* #define TRU_VMI_LED_CONTROL_2_REGISTER          28 */
					/* #define TRU_VMI_LINK_CONTROL_REGISTER           29 */
					/* #define TRU_VMI_TIMING_CONTROL_REGISTER */

#endif /* _ET1310_PHY_H_ */
