/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Networks (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Networks nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/


/**
 * cvmx-usbnx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon usbnx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_USBNX_TYPEDEFS_H__
#define __CVMX_USBNX_TYPEDEFS_H__

#define CVMX_USBNX_BIST_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800680007F8ull) + ((block_id) & 1) * 0x10000000ull)
#define CVMX_USBNX_CLK_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180068000010ull) + ((block_id) & 1) * 0x10000000ull)
#define CVMX_USBNX_CTL_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000800ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_INB_CHN0(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000818ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_INB_CHN1(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000820ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_INB_CHN2(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000828ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_INB_CHN3(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000830ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_INB_CHN4(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000838ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_INB_CHN5(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000840ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_INB_CHN6(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000848ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_INB_CHN7(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000850ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_OUTB_CHN0(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000858ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_OUTB_CHN1(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000860ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_OUTB_CHN2(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000868ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_OUTB_CHN3(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000870ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_OUTB_CHN4(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000878ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_OUTB_CHN5(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000880ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_OUTB_CHN6(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000888ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA0_OUTB_CHN7(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000890ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_DMA_TEST(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000808ull) + ((block_id) & 1) * 0x100000000000ull)
#define CVMX_USBNX_INT_ENB(block_id) (CVMX_ADD_IO_SEG(0x0001180068000008ull) + ((block_id) & 1) * 0x10000000ull)
#define CVMX_USBNX_INT_SUM(block_id) (CVMX_ADD_IO_SEG(0x0001180068000000ull) + ((block_id) & 1) * 0x10000000ull)
#define CVMX_USBNX_USBP_CTL_STATUS(block_id) (CVMX_ADD_IO_SEG(0x0001180068000018ull) + ((block_id) & 1) * 0x10000000ull)

/**
 * cvmx_usbn#_clk_ctl
 *
 * USBN_CLK_CTL = USBN's Clock Control
 *
 * This register is used to control the frequency of the hclk and the hreset and phy_rst signals.
 */
union cvmx_usbnx_clk_ctl {
	uint64_t u64;
	struct cvmx_usbnx_clk_ctl_s {
		uint64_t reserved_20_63	: 44;
		uint64_t divide2	: 2;	    /**< The 'hclk' used by the USB subsystem is derived
                                                         from the eclk.
                                                         Also see the field DIVIDE. DIVIDE2<1> must currently
                                                         be zero because it is not implemented, so the maximum
                                                         ratio of eclk/hclk is currently 16.
                                                         The actual divide number for hclk is:
                                                         (DIVIDE2 + 1) * (DIVIDE + 1) */
		uint64_t hclk_rst	: 1;	    /**< When this field is '0' the HCLK-DIVIDER used to
                                                         generate the hclk in the USB Subsystem is held
                                                         in reset. This bit must be set to '0' before
                                                         changing the value os DIVIDE in this register.
                                                         The reset to the HCLK_DIVIDERis also asserted
                                                         when core reset is asserted. */
		uint64_t p_x_on		: 1;	    /**< Force USB-PHY on during suspend.
                                                         '1' USB-PHY XO block is powered-down during
                                                             suspend.
                                                         '0' USB-PHY XO block is powered-up during
                                                             suspend.
                                                         The value of this field must be set while POR is
                                                         active. */
		uint64_t reserved_14_15	: 2;
		uint64_t p_com_on	: 1;	    /**< '0' Force USB-PHY XO Bias, Bandgap and PLL to
                                                             remain powered in Suspend Mode.
                                                         '1' The USB-PHY XO Bias, Bandgap and PLL are
                                                             powered down in suspend mode.
                                                         The value of this field must be set while POR is
                                                         active. */
		uint64_t p_c_sel	: 2;	    /**< Phy clock speed select.
                                                         Selects the reference clock / crystal frequency.
                                                         '11': Reserved
                                                         '10': 48 MHz (reserved when a crystal is used)
                                                         '01': 24 MHz (reserved when a crystal is used)
                                                         '00': 12 MHz
                                                         The value of this field must be set while POR is
                                                         active.
                                                         NOTE: if a crystal is used as a reference clock,
                                                         this field must be set to 12 MHz. */
		uint64_t cdiv_byp	: 1;	    /**< Used to enable the bypass input to the USB_CLK_DIV. */
		uint64_t sd_mode	: 2;	    /**< Scaledown mode for the USBC. Control timing events
                                                         in the USBC, for normal operation this must be '0'. */
		uint64_t s_bist		: 1;	    /**< Starts bist on the hclk memories, during the '0'
                                                         to '1' transition. */
		uint64_t por		: 1;	    /**< Power On Reset for the PHY.
                                                         Resets all the PHYS registers and state machines. */
		uint64_t enable		: 1;	    /**< When '1' allows the generation of the hclk. When
                                                         '0' the hclk will not be generated. SEE DIVIDE
                                                         field of this register. */
		uint64_t prst		: 1;	    /**< When this field is '0' the reset associated with
                                                         the phy_clk functionality in the USB Subsystem is
                                                         help in reset. This bit should not be set to '1'
                                                         until the time it takes 6 clocks (hclk or phy_clk,
                                                         whichever is slower) has passed. Under normal
                                                         operation once this bit is set to '1' it should not
                                                         be set to '0'. */
		uint64_t hrst		: 1;	    /**< When this field is '0' the reset associated with
                                                         the hclk functioanlity in the USB Subsystem is
                                                         held in reset.This bit should not be set to '1'
                                                         until 12ms after phy_clk is stable. Under normal
                                                         operation, once this bit is set to '1' it should
                                                         not be set to '0'. */
		uint64_t divide		: 3;	    /**< The frequency of 'hclk' used by the USB subsystem
                                                         is the eclk frequency divided by the value of
                                                         (DIVIDE2 + 1) * (DIVIDE + 1), also see the field
                                                         DIVIDE2 of this register.
                                                         The hclk frequency should be less than 125Mhz.
                                                         After writing a value to this field the SW should
                                                         read the field for the value written.
                                                         The ENABLE field of this register should not be set
                                                         until AFTER this field is set and then read. */
	} s;
	struct cvmx_usbnx_clk_ctl_cn30xx {
		uint64_t reserved_18_63	: 46;
		uint64_t hclk_rst	: 1;	    /**< When this field is '0' the HCLK-DIVIDER used to
                                                         generate the hclk in the USB Subsystem is held
                                                         in reset. This bit must be set to '0' before
                                                         changing the value os DIVIDE in this register.
                                                         The reset to the HCLK_DIVIDERis also asserted
                                                         when core reset is asserted. */
		uint64_t p_x_on		: 1;	    /**< Force USB-PHY on during suspend.
                                                         '1' USB-PHY XO block is powered-down during
                                                             suspend.
                                                         '0' USB-PHY XO block is powered-up during
                                                             suspend.
                                                         The value of this field must be set while POR is
                                                         active. */
		uint64_t p_rclk		: 1;	    /**< Phy refrence clock enable.
                                                         '1' The PHY PLL uses the XO block output as a
                                                         reference.
                                                         '0' Reserved. */
		uint64_t p_xenbn	: 1;	    /**< Phy external clock enable.
                                                         '1' The XO block uses the clock from a crystal.
                                                         '0' The XO block uses an external clock supplied
                                                             on the XO pin. USB_XI should be tied to
                                                             ground for this usage. */
		uint64_t p_com_on	: 1;	    /**< '0' Force USB-PHY XO Bias, Bandgap and PLL to
                                                             remain powered in Suspend Mode.
                                                         '1' The USB-PHY XO Bias, Bandgap and PLL are
                                                             powered down in suspend mode.
                                                         The value of this field must be set while POR is
                                                         active. */
		uint64_t p_c_sel	: 2;	    /**< Phy clock speed select.
                                                         Selects the reference clock / crystal frequency.
                                                         '11': Reserved
                                                         '10': 48 MHz
                                                         '01': 24 MHz
                                                         '00': 12 MHz
                                                         The value of this field must be set while POR is
                                                         active. */
		uint64_t cdiv_byp	: 1;	    /**< Used to enable the bypass input to the USB_CLK_DIV. */
		uint64_t sd_mode	: 2;	    /**< Scaledown mode for the USBC. Control timing events
                                                         in the USBC, for normal operation this must be '0'. */
		uint64_t s_bist		: 1;	    /**< Starts bist on the hclk memories, during the '0'
                                                         to '1' transition. */
		uint64_t por		: 1;	    /**< Power On Reset for the PHY.
                                                         Resets all the PHYS registers and state machines. */
		uint64_t enable		: 1;	    /**< When '1' allows the generation of the hclk. When
                                                         '0' the hclk will not be generated. */
		uint64_t prst		: 1;	    /**< When this field is '0' the reset associated with
                                                         the phy_clk functionality in the USB Subsystem is
                                                         help in reset. This bit should not be set to '1'
                                                         until the time it takes 6 clocks (hclk or phy_clk,
                                                         whichever is slower) has passed. Under normal
                                                         operation once this bit is set to '1' it should not
                                                         be set to '0'. */
		uint64_t hrst		: 1;	    /**< When this field is '0' the reset associated with
                                                         the hclk functioanlity in the USB Subsystem is
                                                         held in reset.This bit should not be set to '1'
                                                         until 12ms after phy_clk is stable. Under normal
                                                         operation, once this bit is set to '1' it should
                                                         not be set to '0'. */
		uint64_t divide		: 3;	    /**< The 'hclk' used by the USB subsystem is derived
                                                         from the eclk. The eclk will be divided by the
                                                         value of this field +1 to determine the hclk
                                                         frequency. (Also see HRST of this register).
                                                         The hclk frequency must be less than 125 MHz. */
	} cn30xx;
	struct cvmx_usbnx_clk_ctl_cn30xx cn31xx;
	struct cvmx_usbnx_clk_ctl_cn50xx {
		uint64_t reserved_20_63	: 44;
		uint64_t divide2	: 2;	    /**< The 'hclk' used by the USB subsystem is derived
                                                         from the eclk.
                                                         Also see the field DIVIDE. DIVIDE2<1> must currently
                                                         be zero because it is not implemented, so the maximum
                                                         ratio of eclk/hclk is currently 16.
                                                         The actual divide number for hclk is:
                                                         (DIVIDE2 + 1) * (DIVIDE + 1) */
		uint64_t hclk_rst	: 1;	    /**< When this field is '0' the HCLK-DIVIDER used to
                                                         generate the hclk in the USB Subsystem is held
                                                         in reset. This bit must be set to '0' before
                                                         changing the value os DIVIDE in this register.
                                                         The reset to the HCLK_DIVIDERis also asserted
                                                         when core reset is asserted. */
		uint64_t reserved_16_16 : 1;
		uint64_t p_rtype	: 2;	    /**< PHY reference clock type
                                                         '0' The USB-PHY uses a 12MHz crystal as a clock
                                                             source at the USB_XO and USB_XI pins
                                                         '1' Reserved
                                                         '2' The USB_PHY uses 12/24/48MHz 2.5V board clock
                                                             at the USB_XO pin. USB_XI should be tied to
                                                             ground in this case.
                                                         '3' Reserved
                                                         (bit 14 was P_XENBN on 3xxx)
                                                         (bit 15 was P_RCLK on 3xxx) */
		uint64_t p_com_on	: 1;	    /**< '0' Force USB-PHY XO Bias, Bandgap and PLL to
                                                             remain powered in Suspend Mode.
                                                         '1' The USB-PHY XO Bias, Bandgap and PLL are
                                                             powered down in suspend mode.
                                                         The value of this field must be set while POR is
                                                         active. */
		uint64_t p_c_sel	: 2;	    /**< Phy clock speed select.
                                                         Selects the reference clock / crystal frequency.
                                                         '11': Reserved
                                                         '10': 48 MHz (reserved when a crystal is used)
                                                         '01': 24 MHz (reserved when a crystal is used)
                                                         '00': 12 MHz
                                                         The value of this field must be set while POR is
                                                         active.
                                                         NOTE: if a crystal is used as a reference clock,
                                                         this field must be set to 12 MHz. */
		uint64_t cdiv_byp	: 1;	    /**< Used to enable the bypass input to the USB_CLK_DIV. */
		uint64_t sd_mode	: 2;	    /**< Scaledown mode for the USBC. Control timing events
                                                         in the USBC, for normal operation this must be '0'. */
		uint64_t s_bist		: 1;	    /**< Starts bist on the hclk memories, during the '0'
                                                         to '1' transition. */
		uint64_t por		: 1;	    /**< Power On Reset for the PHY.
                                                         Resets all the PHYS registers and state machines. */
		uint64_t enable		: 1;	    /**< When '1' allows the generation of the hclk. When
                                                         '0' the hclk will not be generated. SEE DIVIDE
                                                         field of this register. */
		uint64_t prst		: 1;	    /**< When this field is '0' the reset associated with
                                                         the phy_clk functionality in the USB Subsystem is
                                                         help in reset. This bit should not be set to '1'
                                                         until the time it takes 6 clocks (hclk or phy_clk,
                                                         whichever is slower) has passed. Under normal
                                                         operation once this bit is set to '1' it should not
                                                         be set to '0'. */
		uint64_t hrst		: 1;	    /**< When this field is '0' the reset associated with
                                                         the hclk functioanlity in the USB Subsystem is
                                                         held in reset.This bit should not be set to '1'
                                                         until 12ms after phy_clk is stable. Under normal
                                                         operation, once this bit is set to '1' it should
                                                         not be set to '0'. */
		uint64_t divide		: 3;	    /**< The frequency of 'hclk' used by the USB subsystem
                                                         is the eclk frequency divided by the value of
                                                         (DIVIDE2 + 1) * (DIVIDE + 1), also see the field
                                                         DIVIDE2 of this register.
                                                         The hclk frequency should be less than 125Mhz.
                                                         After writing a value to this field the SW should
                                                         read the field for the value written.
                                                         The ENABLE field of this register should not be set
                                                         until AFTER this field is set and then read. */
	} cn50xx;
	struct cvmx_usbnx_clk_ctl_cn50xx cn52xx;
	struct cvmx_usbnx_clk_ctl_cn50xx cn56xx;
};
typedef union cvmx_usbnx_clk_ctl cvmx_usbnx_clk_ctl_t;

/**
 * cvmx_usbn#_usbp_ctl_status
 *
 * USBN_USBP_CTL_STATUS = USBP Control And Status Register
 *
 * Contains general control and status information for the USBN block.
 */
union cvmx_usbnx_usbp_ctl_status {
	uint64_t u64;
	struct cvmx_usbnx_usbp_ctl_status_s {
		uint64_t txrisetune		: 1;    /**< HS Transmitter Rise/Fall Time Adjustment */
		uint64_t txvreftune		: 4;    /**< HS DC Voltage Level Adjustment */
		uint64_t txfslstune		: 4;    /**< FS/LS Source Impedence Adjustment */
		uint64_t txhsxvtune		: 2;    /**< Transmitter High-Speed Crossover Adjustment */
		uint64_t sqrxtune		: 3;    /**< Squelch Threshold Adjustment */
		uint64_t compdistune		: 3;    /**< Disconnect Threshold Adjustment */
		uint64_t otgtune		: 3;    /**< VBUS Valid Threshold Adjustment */
		uint64_t otgdisable		: 1;    /**< OTG Block Disable */
		uint64_t portreset		: 1;    /**< Per_Port Reset */
		uint64_t drvvbus		: 1;    /**< Drive VBUS */
		uint64_t lsbist			: 1;    /**< Low-Speed BIST Enable. */
		uint64_t fsbist			: 1;    /**< Full-Speed BIST Enable. */
		uint64_t hsbist			: 1;    /**< High-Speed BIST Enable. */
		uint64_t bist_done		: 1;    /**< PHY Bist Done.
                                                         Asserted at the end of the PHY BIST sequence. */
		uint64_t bist_err		: 1;    /**< PHY Bist Error.
                                                         Indicates an internal error was detected during
                                                         the BIST sequence. */
		uint64_t tdata_out		: 4;    /**< PHY Test Data Out.
                                                         Presents either internaly generated signals or
                                                         test register contents, based upon the value of
                                                         test_data_out_sel. */
		uint64_t siddq			: 1;    /**< Drives the USBP (USB-PHY) SIDDQ input.
                                                         Normally should be set to zero.
                                                         When customers have no intent to use USB PHY
                                                         interface, they should:
                                                           - still provide 3.3V to USB_VDD33, and
                                                           - tie USB_REXT to 3.3V supply, and
                                                           - set USBN*_USBP_CTL_STATUS[SIDDQ]=1 */
		uint64_t txpreemphasistune	: 1;    /**< HS Transmitter Pre-Emphasis Enable */
		uint64_t dma_bmode		: 1;    /**< When set to 1 the L2C DMA address will be updated
                                                         with byte-counts between packets. When set to 0
                                                         the L2C DMA address is incremented to the next
                                                         4-byte aligned address after adding byte-count. */
		uint64_t usbc_end		: 1;    /**< Bigendian input to the USB Core. This should be
                                                         set to '0' for operation. */
		uint64_t usbp_bist		: 1;    /**< PHY, This is cleared '0' to run BIST on the USBP. */
		uint64_t tclk			: 1;    /**< PHY Test Clock, used to load TDATA_IN to the USBP. */
		uint64_t dp_pulld		: 1;    /**< PHY DP_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D+ line. '1' pull down-resistance is connected
                                                         to D+/ '0' pull down resistance is not connected
                                                         to D+. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
		uint64_t dm_pulld		: 1;    /**< PHY DM_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D- line. '1' pull down-resistance is connected
                                                         to D-. '0' pull down resistance is not connected
                                                         to D-. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
		uint64_t hst_mode		: 1;    /**< When '0' the USB is acting as HOST, when '1'
                                                         USB is acting as device. This field needs to be
                                                         set while the USB is in reset. */
		uint64_t tuning			: 4;    /**< Transmitter Tuning for High-Speed Operation.
                                                         Tunes the current supply and rise/fall output
                                                         times for high-speed operation.
                                                         [20:19] == 11: Current supply increased
                                                         approximately 9%
                                                         [20:19] == 10: Current supply increased
                                                         approximately 4.5%
                                                         [20:19] == 01: Design default.
                                                         [20:19] == 00: Current supply decreased
                                                         approximately 4.5%
                                                         [22:21] == 11: Rise and fall times are increased.
                                                         [22:21] == 10: Design default.
                                                         [22:21] == 01: Rise and fall times are decreased.
                                                         [22:21] == 00: Rise and fall times are decreased
                                                         further as compared to the 01 setting. */
		uint64_t tx_bs_enh		: 1;    /**< Transmit Bit Stuffing on [15:8].
                                                         Enables or disables bit stuffing on data[15:8]
                                                         when bit-stuffing is enabled. */
		uint64_t tx_bs_en		: 1;    /**< Transmit Bit Stuffing on [7:0].
                                                         Enables or disables bit stuffing on data[7:0]
                                                         when bit-stuffing is enabled. */
		uint64_t loop_enb		: 1;    /**< PHY Loopback Test Enable.
                                                         '1': During data transmission the receive is
                                                         enabled.
                                                         '0': During data transmission the receive is
                                                         disabled.
                                                         Must be '0' for normal operation. */
		uint64_t vtest_enb		: 1;    /**< Analog Test Pin Enable.
                                                         '1' The PHY's analog_test pin is enabled for the
                                                         input and output of applicable analog test signals.
                                                         '0' THe analog_test pin is disabled. */
		uint64_t bist_enb		: 1;    /**< Built-In Self Test Enable.
                                                         Used to activate BIST in the PHY. */
		uint64_t tdata_sel		: 1;    /**< Test Data Out Select.
                                                         '1' test_data_out[3:0] (PHY) register contents
                                                         are output. '0' internaly generated signals are
                                                         output. */
		uint64_t taddr_in		: 4;    /**< Mode Address for Test Interface.
                                                         Specifies the register address for writing to or
                                                         reading from the PHY test interface register. */
		uint64_t tdata_in		: 8;    /**< Internal Testing Register Input Data and Select
                                                         This is a test bus. Data is present on [3:0],
                                                         and its corresponding select (enable) is present
                                                         on bits [7:4]. */
		uint64_t ate_reset		: 1;    /**< Reset input from automatic test equipment.
                                                         This is a test signal. When the USB Core is
                                                         powered up (not in Susned Mode), an automatic
                                                         tester can use this to disable phy_clock and
                                                         free_clk, then re-eanable them with an aligned
                                                         phase.
                                                         '1': The phy_clk and free_clk outputs are
                                                         disabled. "0": The phy_clock and free_clk outputs
                                                         are available within a specific period after the
                                                         de-assertion. */
	} s;
	struct cvmx_usbnx_usbp_ctl_status_cn30xx {
		uint64_t reserved_38_63	: 26;
		uint64_t bist_done	: 1;	    /**< PHY Bist Done.
                                                         Asserted at the end of the PHY BIST sequence. */
		uint64_t bist_err	: 1;	    /**< PHY Bist Error.
                                                         Indicates an internal error was detected during
                                                         the BIST sequence. */
		uint64_t tdata_out	: 4;	    /**< PHY Test Data Out.
                                                         Presents either internaly generated signals or
                                                         test register contents, based upon the value of
                                                         test_data_out_sel. */
		uint64_t reserved_30_31	: 2;
		uint64_t dma_bmode	: 1;	    /**< When set to 1 the L2C DMA address will be updated
                                                         with byte-counts between packets. When set to 0
                                                         the L2C DMA address is incremented to the next
                                                         4-byte aligned address after adding byte-count. */
		uint64_t usbc_end	: 1;	    /**< Bigendian input to the USB Core. This should be
                                                         set to '0' for operation. */
		uint64_t usbp_bist	: 1;	    /**< PHY, This is cleared '0' to run BIST on the USBP. */
		uint64_t tclk		: 1;	    /**< PHY Test Clock, used to load TDATA_IN to the USBP. */
		uint64_t dp_pulld	: 1;	    /**< PHY DP_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D+ line. '1' pull down-resistance is connected
                                                         to D+/ '0' pull down resistance is not connected
                                                         to D+. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
		uint64_t dm_pulld	: 1;	    /**< PHY DM_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D- line. '1' pull down-resistance is connected
                                                         to D-. '0' pull down resistance is not connected
                                                         to D-. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
		uint64_t hst_mode	: 1;	    /**< When '0' the USB is acting as HOST, when '1'
                                                         USB is acting as device. This field needs to be
                                                         set while the USB is in reset. */
		uint64_t tuning		: 4;	    /**< Transmitter Tuning for High-Speed Operation.
                                                         Tunes the current supply and rise/fall output
                                                         times for high-speed operation.
                                                         [20:19] == 11: Current supply increased
                                                         approximately 9%
                                                         [20:19] == 10: Current supply increased
                                                         approximately 4.5%
                                                         [20:19] == 01: Design default.
                                                         [20:19] == 00: Current supply decreased
                                                         approximately 4.5%
                                                         [22:21] == 11: Rise and fall times are increased.
                                                         [22:21] == 10: Design default.
                                                         [22:21] == 01: Rise and fall times are decreased.
                                                         [22:21] == 00: Rise and fall times are decreased
                                                         further as compared to the 01 setting. */
		uint64_t tx_bs_enh	: 1;	    /**< Transmit Bit Stuffing on [15:8].
                                                         Enables or disables bit stuffing on data[15:8]
                                                         when bit-stuffing is enabled. */
		uint64_t tx_bs_en	: 1;	    /**< Transmit Bit Stuffing on [7:0].
                                                         Enables or disables bit stuffing on data[7:0]
                                                         when bit-stuffing is enabled. */
		uint64_t loop_enb	: 1;	    /**< PHY Loopback Test Enable.
                                                         '1': During data transmission the receive is
                                                         enabled.
                                                         '0': During data transmission the receive is
                                                         disabled.
                                                         Must be '0' for normal operation. */
		uint64_t vtest_enb	: 1;	    /**< Analog Test Pin Enable.
                                                         '1' The PHY's analog_test pin is enabled for the
                                                         input and output of applicable analog test signals.
                                                         '0' THe analog_test pin is disabled. */
		uint64_t bist_enb	: 1;	    /**< Built-In Self Test Enable.
                                                         Used to activate BIST in the PHY. */
		uint64_t tdata_sel	: 1;	    /**< Test Data Out Select.
                                                         '1' test_data_out[3:0] (PHY) register contents
                                                         are output. '0' internaly generated signals are
                                                         output. */
		uint64_t taddr_in	: 4;	    /**< Mode Address for Test Interface.
                                                         Specifies the register address for writing to or
                                                         reading from the PHY test interface register. */
		uint64_t tdata_in	: 8;	    /**< Internal Testing Register Input Data and Select
                                                         This is a test bus. Data is present on [3:0],
                                                         and its corresponding select (enable) is present
                                                         on bits [7:4]. */
		uint64_t ate_reset	: 1;	    /**< Reset input from automatic test equipment.
                                                         This is a test signal. When the USB Core is
                                                         powered up (not in Susned Mode), an automatic
                                                         tester can use this to disable phy_clock and
                                                         free_clk, then re-eanable them with an aligned
                                                         phase.
                                                         '1': The phy_clk and free_clk outputs are
                                                         disabled. "0": The phy_clock and free_clk outputs
                                                         are available within a specific period after the
                                                         de-assertion. */
	} cn30xx;
	struct cvmx_usbnx_usbp_ctl_status_cn50xx {
		uint64_t txrisetune		: 1;	/**< HS Transmitter Rise/Fall Time Adjustment */
		uint64_t txvreftune		: 4;	/**< HS DC Voltage Level Adjustment */
		uint64_t txfslstune		: 4;	/**< FS/LS Source Impedence Adjustment */
		uint64_t txhsxvtune		: 2;	/**< Transmitter High-Speed Crossover Adjustment */
		uint64_t sqrxtune		: 3;	/**< Squelch Threshold Adjustment */
		uint64_t compdistune		: 3;	/**< Disconnect Threshold Adjustment */
		uint64_t otgtune		: 3;	/**< VBUS Valid Threshold Adjustment */
		uint64_t otgdisable		: 1;	/**< OTG Block Disable */
		uint64_t portreset		: 1;	/**< Per_Port Reset */
		uint64_t drvvbus		: 1;	/**< Drive VBUS */
		uint64_t lsbist			: 1;	/**< Low-Speed BIST Enable. */
		uint64_t fsbist			: 1;	/**< Full-Speed BIST Enable. */
		uint64_t hsbist			: 1;	/**< High-Speed BIST Enable. */
		uint64_t bist_done		: 1;	/**< PHY Bist Done.
                                                         Asserted at the end of the PHY BIST sequence. */
		uint64_t bist_err		: 1;	/**< PHY Bist Error.
                                                         Indicates an internal error was detected during
                                                         the BIST sequence. */
		uint64_t tdata_out		: 4;	/**< PHY Test Data Out.
                                                         Presents either internaly generated signals or
                                                         test register contents, based upon the value of
                                                         test_data_out_sel. */
		uint64_t reserved_31_31		: 1;
		uint64_t txpreemphasistune	: 1;	/**< HS Transmitter Pre-Emphasis Enable */
		uint64_t dma_bmode		: 1;	/**< When set to 1 the L2C DMA address will be updated
                                                         with byte-counts between packets. When set to 0
                                                         the L2C DMA address is incremented to the next
                                                         4-byte aligned address after adding byte-count. */
		uint64_t usbc_end		: 1;	/**< Bigendian input to the USB Core. This should be
                                                         set to '0' for operation. */
		uint64_t usbp_bist		: 1;	/**< PHY, This is cleared '0' to run BIST on the USBP. */
		uint64_t tclk			: 1;	/**< PHY Test Clock, used to load TDATA_IN to the USBP. */
		uint64_t dp_pulld		: 1;	/**< PHY DP_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D+ line. '1' pull down-resistance is connected
                                                         to D+/ '0' pull down resistance is not connected
                                                         to D+. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
		uint64_t dm_pulld		: 1;	/**< PHY DM_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D- line. '1' pull down-resistance is connected
                                                         to D-. '0' pull down resistance is not connected
                                                         to D-. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
		uint64_t hst_mode		: 1;	/**< When '0' the USB is acting as HOST, when '1'
                                                         USB is acting as device. This field needs to be
                                                         set while the USB is in reset. */
		uint64_t reserved_19_22		: 4;
		uint64_t tx_bs_enh		: 1;	/**< Transmit Bit Stuffing on [15:8].
                                                         Enables or disables bit stuffing on data[15:8]
                                                         when bit-stuffing is enabled. */
		uint64_t tx_bs_en		: 1;	/**< Transmit Bit Stuffing on [7:0].
                                                         Enables or disables bit stuffing on data[7:0]
                                                         when bit-stuffing is enabled. */
		uint64_t loop_enb		: 1;	/**< PHY Loopback Test Enable.
                                                         '1': During data transmission the receive is
                                                         enabled.
                                                         '0': During data transmission the receive is
                                                         disabled.
                                                         Must be '0' for normal operation. */
		uint64_t vtest_enb		: 1;	/**< Analog Test Pin Enable.
                                                         '1' The PHY's analog_test pin is enabled for the
                                                         input and output of applicable analog test signals.
                                                         '0' THe analog_test pin is disabled. */
		uint64_t bist_enb		: 1;	/**< Built-In Self Test Enable.
                                                         Used to activate BIST in the PHY. */
		uint64_t tdata_sel		: 1;	/**< Test Data Out Select.
                                                         '1' test_data_out[3:0] (PHY) register contents
                                                         are output. '0' internaly generated signals are
                                                         output. */
		uint64_t taddr_in		: 4;	/**< Mode Address for Test Interface.
                                                         Specifies the register address for writing to or
                                                         reading from the PHY test interface register. */
		uint64_t tdata_in		: 8;	/**< Internal Testing Register Input Data and Select
                                                         This is a test bus. Data is present on [3:0],
                                                         and its corresponding select (enable) is present
                                                         on bits [7:4]. */
		uint64_t ate_reset		: 1;	/**< Reset input from automatic test equipment.
                                                         This is a test signal. When the USB Core is
                                                         powered up (not in Susned Mode), an automatic
                                                         tester can use this to disable phy_clock and
                                                         free_clk, then re-eanable them with an aligned
                                                         phase.
                                                         '1': The phy_clk and free_clk outputs are
                                                         disabled. "0": The phy_clock and free_clk outputs
                                                         are available within a specific period after the
                                                         de-assertion. */
	} cn50xx;
	struct cvmx_usbnx_usbp_ctl_status_cn52xx {
		uint64_t txrisetune		: 1;    /**< HS Transmitter Rise/Fall Time Adjustment */
		uint64_t txvreftune		: 4;    /**< HS DC Voltage Level Adjustment */
		uint64_t txfslstune		: 4;    /**< FS/LS Source Impedence Adjustment */
		uint64_t txhsxvtune		: 2;    /**< Transmitter High-Speed Crossover Adjustment */
		uint64_t sqrxtune		: 3;    /**< Squelch Threshold Adjustment */
		uint64_t compdistune		: 3;    /**< Disconnect Threshold Adjustment */
		uint64_t otgtune		: 3;    /**< VBUS Valid Threshold Adjustment */
		uint64_t otgdisable		: 1;    /**< OTG Block Disable */
		uint64_t portreset		: 1;    /**< Per_Port Reset */
		uint64_t drvvbus		: 1;    /**< Drive VBUS */
		uint64_t lsbist			: 1;    /**< Low-Speed BIST Enable. */
		uint64_t fsbist			: 1;    /**< Full-Speed BIST Enable. */
		uint64_t hsbist			: 1;    /**< High-Speed BIST Enable. */
		uint64_t bist_done		: 1;    /**< PHY Bist Done.
                                                         Asserted at the end of the PHY BIST sequence. */
		uint64_t bist_err		: 1;    /**< PHY Bist Error.
                                                         Indicates an internal error was detected during
                                                         the BIST sequence. */
		uint64_t tdata_out		: 4;    /**< PHY Test Data Out.
                                                         Presents either internaly generated signals or
                                                         test register contents, based upon the value of
                                                         test_data_out_sel. */
		uint64_t siddq			: 1;    /**< Drives the USBP (USB-PHY) SIDDQ input.
                                                         Normally should be set to zero.
                                                         When customers have no intent to use USB PHY
                                                         interface, they should:
                                                           - still provide 3.3V to USB_VDD33, and
                                                           - tie USB_REXT to 3.3V supply, and
                                                           - set USBN*_USBP_CTL_STATUS[SIDDQ]=1 */
		uint64_t txpreemphasistune	: 1;    /**< HS Transmitter Pre-Emphasis Enable */
		uint64_t dma_bmode		: 1;    /**< When set to 1 the L2C DMA address will be updated
                                                         with byte-counts between packets. When set to 0
                                                         the L2C DMA address is incremented to the next
                                                         4-byte aligned address after adding byte-count. */
		uint64_t usbc_end		: 1;    /**< Bigendian input to the USB Core. This should be
                                                         set to '0' for operation. */
		uint64_t usbp_bist		: 1;    /**< PHY, This is cleared '0' to run BIST on the USBP. */
		uint64_t tclk			: 1;    /**< PHY Test Clock, used to load TDATA_IN to the USBP. */
		uint64_t dp_pulld		: 1;    /**< PHY DP_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D+ line. '1' pull down-resistance is connected
                                                         to D+/ '0' pull down resistance is not connected
                                                         to D+. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
		uint64_t dm_pulld		: 1;    /**< PHY DM_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D- line. '1' pull down-resistance is connected
                                                         to D-. '0' pull down resistance is not connected
                                                         to D-. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
		uint64_t hst_mode		: 1;    /**< When '0' the USB is acting as HOST, when '1'
                                                         USB is acting as device. This field needs to be
                                                         set while the USB is in reset. */
		uint64_t reserved_19_22		: 4;
		uint64_t tx_bs_enh		: 1;    /**< Transmit Bit Stuffing on [15:8].
                                                         Enables or disables bit stuffing on data[15:8]
                                                         when bit-stuffing is enabled. */
		uint64_t tx_bs_en		: 1;    /**< Transmit Bit Stuffing on [7:0].
                                                         Enables or disables bit stuffing on data[7:0]
                                                         when bit-stuffing is enabled. */
		uint64_t loop_enb		: 1;    /**< PHY Loopback Test Enable.
                                                         '1': During data transmission the receive is
                                                         enabled.
                                                         '0': During data transmission the receive is
                                                         disabled.
                                                         Must be '0' for normal operation. */
		uint64_t vtest_enb		: 1;    /**< Analog Test Pin Enable.
                                                         '1' The PHY's analog_test pin is enabled for the
                                                         input and output of applicable analog test signals.
                                                         '0' THe analog_test pin is disabled. */
		uint64_t bist_enb		: 1;    /**< Built-In Self Test Enable.
                                                         Used to activate BIST in the PHY. */
		uint64_t tdata_sel		: 1;    /**< Test Data Out Select.
                                                         '1' test_data_out[3:0] (PHY) register contents
                                                         are output. '0' internaly generated signals are
                                                         output. */
		uint64_t taddr_in		: 4;    /**< Mode Address for Test Interface.
                                                         Specifies the register address for writing to or
                                                         reading from the PHY test interface register. */
		uint64_t tdata_in		: 8;    /**< Internal Testing Register Input Data and Select
                                                         This is a test bus. Data is present on [3:0],
                                                         and its corresponding select (enable) is present
                                                         on bits [7:4]. */
		uint64_t ate_reset		: 1;    /**< Reset input from automatic test equipment.
                                                         This is a test signal. When the USB Core is
                                                         powered up (not in Susned Mode), an automatic
                                                         tester can use this to disable phy_clock and
                                                         free_clk, then re-eanable them with an aligned
                                                         phase.
                                                         '1': The phy_clk and free_clk outputs are
                                                         disabled. "0": The phy_clock and free_clk outputs
                                                         are available within a specific period after the
                                                         de-assertion. */
	} cn52xx;
};
typedef union cvmx_usbnx_usbp_ctl_status cvmx_usbnx_usbp_ctl_status_t;

#endif
