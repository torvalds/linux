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

 * This Software, including technical data, may be subject to U.S. export
 * control laws, including the U.S. Export Administration Act and its associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION
 * OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
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
 */
#ifndef __CVMX_USBNX_TYPEDEFS_H__
#define __CVMX_USBNX_TYPEDEFS_H__

#define CVMX_USBNXBID1(bid) (((bid) & 1) * 0x10000000ull)
#define CVMX_USBNXBID2(bid) (((bid) & 1) * 0x100000000000ull)

#define CVMX_USBNXREG1(reg, bid) \
	(CVMX_ADD_IO_SEG(0x0001180068000000ull | reg) + CVMX_USBNXBID1(bid))
#define CVMX_USBNXREG2(reg, bid) \
	(CVMX_ADD_IO_SEG(0x00016F0000000000ull | reg) + CVMX_USBNXBID2(bid))

#define CVMX_USBNX_CLK_CTL(bid)		CVMX_USBNXREG1(0x10, bid)
#define CVMX_USBNX_DMA0_INB_CHN0(bid)	CVMX_USBNXREG2(0x818, bid)
#define CVMX_USBNX_DMA0_OUTB_CHN0(bid)	CVMX_USBNXREG2(0x858, bid)
#define CVMX_USBNX_USBP_CTL_STATUS(bid)	CVMX_USBNXREG1(0x18, bid)

/**
 * cvmx_usbn#_clk_ctl
 *
 * USBN_CLK_CTL = USBN's Clock Control
 *
 * This register is used to control the frequency of the hclk and the
 * hreset and phy_rst signals.
 */
union cvmx_usbnx_clk_ctl {
	uint64_t u64;
	/**
	 * struct cvmx_usbnx_clk_ctl_s
	 * @divide2: The 'hclk' used by the USB subsystem is derived
	 *	from the eclk.
	 *	Also see the field DIVIDE. DIVIDE2<1> must currently
	 *	be zero because it is not implemented, so the maximum
	 *	ratio of eclk/hclk is currently 16.
	 *	The actual divide number for hclk is:
	 *	(DIVIDE2 + 1) * (DIVIDE + 1)
	 * @hclk_rst: When this field is '0' the HCLK-DIVIDER used to
	 *	generate the hclk in the USB Subsystem is held
	 *	in reset. This bit must be set to '0' before
	 *	changing the value os DIVIDE in this register.
	 *	The reset to the HCLK_DIVIDERis also asserted
	 *	when core reset is asserted.
	 * @p_x_on: Force USB-PHY on during suspend.
	 *	'1' USB-PHY XO block is powered-down during
	 *	suspend.
	 *	'0' USB-PHY XO block is powered-up during
	 *	suspend.
	 *	The value of this field must be set while POR is
	 *	active.
	 * @p_com_on: '0' Force USB-PHY XO Bias, Bandgap and PLL to
	 *	remain powered in Suspend Mode.
	 *	'1' The USB-PHY XO Bias, Bandgap and PLL are
	 *	powered down in suspend mode.
	 *	The value of this field must be set while POR is
	 *	active.
	 * @p_c_sel: Phy clock speed select.
	 *	Selects the reference clock / crystal frequency.
	 *	'11': Reserved
	 *	'10': 48 MHz (reserved when a crystal is used)
	 *	'01': 24 MHz (reserved when a crystal is used)
	 *	'00': 12 MHz
	 *	The value of this field must be set while POR is
	 *	active.
	 *	NOTE: if a crystal is used as a reference clock,
	 *	this field must be set to 12 MHz.
	 * @cdiv_byp: Used to enable the bypass input to the USB_CLK_DIV.
	 * @sd_mode: Scaledown mode for the USBC. Control timing events
	 *	in the USBC, for normal operation this must be '0'.
	 * @s_bist: Starts bist on the hclk memories, during the '0'
	 *	to '1' transition.
	 * @por: Power On Reset for the PHY.
	 *	Resets all the PHYS registers and state machines.
	 * @enable: When '1' allows the generation of the hclk. When
	 *	'0' the hclk will not be generated. SEE DIVIDE
	 *	field of this register.
	 * @prst: When this field is '0' the reset associated with
	 *	the phy_clk functionality in the USB Subsystem is
	 *	help in reset. This bit should not be set to '1'
	 *	until the time it takes 6 clocks (hclk or phy_clk,
	 *	whichever is slower) has passed. Under normal
	 *	operation once this bit is set to '1' it should not
	 *	be set to '0'.
	 * @hrst: When this field is '0' the reset associated with
	 *	the hclk functioanlity in the USB Subsystem is
	 *	held in reset.This bit should not be set to '1'
	 *	until 12ms after phy_clk is stable. Under normal
	 *	operation, once this bit is set to '1' it should
	 *	not be set to '0'.
	 * @divide: The frequency of 'hclk' used by the USB subsystem
	 *	is the eclk frequency divided by the value of
	 *	(DIVIDE2 + 1) * (DIVIDE + 1), also see the field
	 *	DIVIDE2 of this register.
	 *	The hclk frequency should be less than 125Mhz.
	 *	After writing a value to this field the SW should
	 *	read the field for the value written.
	 *	The ENABLE field of this register should not be set
	 *	until AFTER this field is set and then read.
	 */
	struct cvmx_usbnx_clk_ctl_s {
		uint64_t reserved_20_63	: 44;
		uint64_t divide2	: 2;
		uint64_t hclk_rst	: 1;
		uint64_t p_x_on		: 1;
		uint64_t reserved_14_15	: 2;
		uint64_t p_com_on	: 1;
		uint64_t p_c_sel	: 2;
		uint64_t cdiv_byp	: 1;
		uint64_t sd_mode	: 2;
		uint64_t s_bist		: 1;
		uint64_t por		: 1;
		uint64_t enable		: 1;
		uint64_t prst		: 1;
		uint64_t hrst		: 1;
		uint64_t divide		: 3;
	} s;
	/**
	 * struct cvmx_usbnx_clk_ctl_cn30xx
	 * @hclk_rst: When this field is '0' the HCLK-DIVIDER used to
	 *	generate the hclk in the USB Subsystem is held
	 *	in reset. This bit must be set to '0' before
	 *	changing the value os DIVIDE in this register.
	 *	The reset to the HCLK_DIVIDERis also asserted
	 *	when core reset is asserted.
	 * @p_x_on: Force USB-PHY on during suspend.
	 *	'1' USB-PHY XO block is powered-down during
	 *	suspend.
	 *	'0' USB-PHY XO block is powered-up during
	 *	suspend.
	 *	The value of this field must be set while POR is
	 *	active.
	 * @p_rclk: Phy refrence clock enable.
	 *	'1' The PHY PLL uses the XO block output as a
	 *	reference.
	 *	'0' Reserved.
	 * @p_xenbn: Phy external clock enable.
	 *	'1' The XO block uses the clock from a crystal.
	 *	'0' The XO block uses an external clock supplied
	 *	on the XO pin. USB_XI should be tied to
	 *	ground for this usage.
	 * @p_com_on: '0' Force USB-PHY XO Bias, Bandgap and PLL to
	 *	remain powered in Suspend Mode.
	 *	'1' The USB-PHY XO Bias, Bandgap and PLL are
	 *	powered down in suspend mode.
	 *	The value of this field must be set while POR is
	 *	active.
	 * @p_c_sel: Phy clock speed select.
	 *	Selects the reference clock / crystal frequency.
	 *	'11': Reserved
	 *	'10': 48 MHz
	 *	'01': 24 MHz
	 *	'00': 12 MHz
	 *	The value of this field must be set while POR is
	 *	active.
	 * @cdiv_byp: Used to enable the bypass input to the USB_CLK_DIV.
	 * @sd_mode: Scaledown mode for the USBC. Control timing events
	 *	in the USBC, for normal operation this must be '0'.
	 * @s_bist: Starts bist on the hclk memories, during the '0'
	 *	to '1' transition.
	 * @por: Power On Reset for the PHY.
	 *	Resets all the PHYS registers and state machines.
	 * @enable: When '1' allows the generation of the hclk. When
	 *	'0' the hclk will not be generated.
	 * @prst: When this field is '0' the reset associated with
	 *	the phy_clk functionality in the USB Subsystem is
	 *	help in reset. This bit should not be set to '1'
	 *	until the time it takes 6 clocks (hclk or phy_clk,
	 *	whichever is slower) has passed. Under normal
	 *	operation once this bit is set to '1' it should not
	 *	be set to '0'.
	 * @hrst: When this field is '0' the reset associated with
	 *	the hclk functioanlity in the USB Subsystem is
	 *	held in reset.This bit should not be set to '1'
	 *	until 12ms after phy_clk is stable. Under normal
	 *	operation, once this bit is set to '1' it should
	 *	not be set to '0'.
	 * @divide: The 'hclk' used by the USB subsystem is derived
	 *	from the eclk. The eclk will be divided by the
	 *	value of this field +1 to determine the hclk
	 *	frequency. (Also see HRST of this register).
	 *	The hclk frequency must be less than 125 MHz.
	 */
	struct cvmx_usbnx_clk_ctl_cn30xx {
		uint64_t reserved_18_63	: 46;
		uint64_t hclk_rst	: 1;
		uint64_t p_x_on		: 1;
		uint64_t p_rclk		: 1;
		uint64_t p_xenbn	: 1;
		uint64_t p_com_on	: 1;
		uint64_t p_c_sel	: 2;
		uint64_t cdiv_byp	: 1;
		uint64_t sd_mode	: 2;
		uint64_t s_bist		: 1;
		uint64_t por		: 1;
		uint64_t enable		: 1;
		uint64_t prst		: 1;
		uint64_t hrst		: 1;
		uint64_t divide		: 3;
	} cn30xx;
	struct cvmx_usbnx_clk_ctl_cn30xx cn31xx;
	/**
	 * struct cvmx_usbnx_clk_ctl_cn50xx
	 * @divide2: The 'hclk' used by the USB subsystem is derived
	 *	from the eclk.
	 *	Also see the field DIVIDE. DIVIDE2<1> must currently
	 *	be zero because it is not implemented, so the maximum
	 *	ratio of eclk/hclk is currently 16.
	 *	The actual divide number for hclk is:
	 *	(DIVIDE2 + 1) * (DIVIDE + 1)
	 * @hclk_rst: When this field is '0' the HCLK-DIVIDER used to
	 *	generate the hclk in the USB Subsystem is held
	 *	in reset. This bit must be set to '0' before
	 *	changing the value os DIVIDE in this register.
	 *	The reset to the HCLK_DIVIDERis also asserted
	 *	when core reset is asserted.
	 * @p_rtype: PHY reference clock type
	 *	'0' The USB-PHY uses a 12MHz crystal as a clock
	 *	source at the USB_XO and USB_XI pins
	 *	'1' Reserved
	 *	'2' The USB_PHY uses 12/24/48MHz 2.5V board clock
	 *	at the USB_XO pin. USB_XI should be tied to
	 *	ground in this case.
	 *	'3' Reserved
	 *	(bit 14 was P_XENBN on 3xxx)
	 *	(bit 15 was P_RCLK on 3xxx)
	 * @p_com_on: '0' Force USB-PHY XO Bias, Bandgap and PLL to
	 *	remain powered in Suspend Mode.
	 *	'1' The USB-PHY XO Bias, Bandgap and PLL are
	 *	powered down in suspend mode.
	 *	The value of this field must be set while POR is
	 *	active.
	 * @p_c_sel: Phy clock speed select.
	 *	Selects the reference clock / crystal frequency.
	 *	'11': Reserved
	 *	'10': 48 MHz (reserved when a crystal is used)
	 *	'01': 24 MHz (reserved when a crystal is used)
	 *	'00': 12 MHz
	 *	The value of this field must be set while POR is
	 *	active.
	 *	NOTE: if a crystal is used as a reference clock,
	 *	this field must be set to 12 MHz.
	 * @cdiv_byp: Used to enable the bypass input to the USB_CLK_DIV.
	 * @sd_mode: Scaledown mode for the USBC. Control timing events
	 *	in the USBC, for normal operation this must be '0'.
	 * @s_bist: Starts bist on the hclk memories, during the '0'
	 *	to '1' transition.
	 * @por: Power On Reset for the PHY.
	 *	Resets all the PHYS registers and state machines.
	 * @enable: When '1' allows the generation of the hclk. When
	 *	'0' the hclk will not be generated. SEE DIVIDE
	 *	field of this register.
	 * @prst: When this field is '0' the reset associated with
	 *	the phy_clk functionality in the USB Subsystem is
	 *	help in reset. This bit should not be set to '1'
	 *	until the time it takes 6 clocks (hclk or phy_clk,
	 *	whichever is slower) has passed. Under normal
	 *	operation once this bit is set to '1' it should not
	 *	be set to '0'.
	 * @hrst: When this field is '0' the reset associated with
	 *	the hclk functioanlity in the USB Subsystem is
	 *	held in reset.This bit should not be set to '1'
	 *	until 12ms after phy_clk is stable. Under normal
	 *	operation, once this bit is set to '1' it should
	 *	not be set to '0'.
	 * @divide: The frequency of 'hclk' used by the USB subsystem
	 *	is the eclk frequency divided by the value of
	 *	(DIVIDE2 + 1) * (DIVIDE + 1), also see the field
	 *	DIVIDE2 of this register.
	 *	The hclk frequency should be less than 125Mhz.
	 *	After writing a value to this field the SW should
	 *	read the field for the value written.
	 *	The ENABLE field of this register should not be set
	 *	until AFTER this field is set and then read.
	 */
	struct cvmx_usbnx_clk_ctl_cn50xx {
		uint64_t reserved_20_63	: 44;
		uint64_t divide2	: 2;
		uint64_t hclk_rst	: 1;
		uint64_t reserved_16_16 : 1;
		uint64_t p_rtype	: 2;
		uint64_t p_com_on	: 1;
		uint64_t p_c_sel	: 2;
		uint64_t cdiv_byp	: 1;
		uint64_t sd_mode	: 2;
		uint64_t s_bist		: 1;
		uint64_t por		: 1;
		uint64_t enable		: 1;
		uint64_t prst		: 1;
		uint64_t hrst		: 1;
		uint64_t divide		: 3;
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
	/**
	 * struct cvmx_usbnx_usbp_ctl_status_s
	 * @txrisetune: HS Transmitter Rise/Fall Time Adjustment
	 * @txvreftune: HS DC Voltage Level Adjustment
	 * @txfslstune: FS/LS Source Impedence Adjustment
	 * @txhsxvtune: Transmitter High-Speed Crossover Adjustment
	 * @sqrxtune: Squelch Threshold Adjustment
	 * @compdistune: Disconnect Threshold Adjustment
	 * @otgtune: VBUS Valid Threshold Adjustment
	 * @otgdisable: OTG Block Disable
	 * @portreset: Per_Port Reset
	 * @drvvbus: Drive VBUS
	 * @lsbist: Low-Speed BIST Enable.
	 * @fsbist: Full-Speed BIST Enable.
	 * @hsbist: High-Speed BIST Enable.
	 * @bist_done: PHY Bist Done.
	 *	Asserted at the end of the PHY BIST sequence.
	 * @bist_err: PHY Bist Error.
	 *	Indicates an internal error was detected during
	 *	the BIST sequence.
	 * @tdata_out: PHY Test Data Out.
	 *	Presents either internaly generated signals or
	 *	test register contents, based upon the value of
	 *	test_data_out_sel.
	 * @siddq: Drives the USBP (USB-PHY) SIDDQ input.
	 *	Normally should be set to zero.
	 *	When customers have no intent to use USB PHY
	 *	interface, they should:
	 *	- still provide 3.3V to USB_VDD33, and
	 *	- tie USB_REXT to 3.3V supply, and
	 *	- set USBN*_USBP_CTL_STATUS[SIDDQ]=1
	 * @txpreemphasistune: HS Transmitter Pre-Emphasis Enable
	 * @dma_bmode: When set to 1 the L2C DMA address will be updated
	 *	with byte-counts between packets. When set to 0
	 *	the L2C DMA address is incremented to the next
	 *	4-byte aligned address after adding byte-count.
	 * @usbc_end: Bigendian input to the USB Core. This should be
	 *	set to '0' for operation.
	 * @usbp_bist: PHY, This is cleared '0' to run BIST on the USBP.
	 * @tclk: PHY Test Clock, used to load TDATA_IN to the USBP.
	 * @dp_pulld: PHY DP_PULLDOWN input to the USB-PHY.
	 *	This signal enables the pull-down resistance on
	 *	the D+ line. '1' pull down-resistance is connected
	 *	to D+/ '0' pull down resistance is not connected
	 *	to D+. When an A/B device is acting as a host
	 *	(downstream-facing port), dp_pulldown and
	 *	dm_pulldown are enabled. This must not toggle
	 *	during normal opeartion.
	 * @dm_pulld: PHY DM_PULLDOWN input to the USB-PHY.
	 *	This signal enables the pull-down resistance on
	 *	the D- line. '1' pull down-resistance is connected
	 *	to D-. '0' pull down resistance is not connected
	 *	to D-. When an A/B device is acting as a host
	 *	(downstream-facing port), dp_pulldown and
	 *	dm_pulldown are enabled. This must not toggle
	 *	during normal opeartion.
	 * @hst_mode: When '0' the USB is acting as HOST, when '1'
	 *	USB is acting as device. This field needs to be
	 *	set while the USB is in reset.
	 * @tuning: Transmitter Tuning for High-Speed Operation.
	 *	Tunes the current supply and rise/fall output
	 *	times for high-speed operation.
	 *	[20:19] == 11: Current supply increased
	 *	approximately 9%
	 *	[20:19] == 10: Current supply increased
	 *	approximately 4.5%
	 *	[20:19] == 01: Design default.
	 *	[20:19] == 00: Current supply decreased
	 *	approximately 4.5%
	 *	[22:21] == 11: Rise and fall times are increased.
	 *	[22:21] == 10: Design default.
	 *	[22:21] == 01: Rise and fall times are decreased.
	 *	[22:21] == 00: Rise and fall times are decreased
	 *	further as compared to the 01 setting.
	 * @tx_bs_enh: Transmit Bit Stuffing on [15:8].
	 *	Enables or disables bit stuffing on data[15:8]
	 *	when bit-stuffing is enabled.
	 * @tx_bs_en: Transmit Bit Stuffing on [7:0].
	 *	Enables or disables bit stuffing on data[7:0]
	 *	when bit-stuffing is enabled.
	 * @loop_enb: PHY Loopback Test Enable.
	 *	'1': During data transmission the receive is
	 *	enabled.
	 *	'0': During data transmission the receive is
	 *	disabled.
	 *	Must be '0' for normal operation.
	 * @vtest_enb: Analog Test Pin Enable.
	 *	'1' The PHY's analog_test pin is enabled for the
	 *	input and output of applicable analog test signals.
	 *	'0' THe analog_test pin is disabled.
	 * @bist_enb: Built-In Self Test Enable.
	 *	Used to activate BIST in the PHY.
	 * @tdata_sel: Test Data Out Select.
	 *	'1' test_data_out[3:0] (PHY) register contents
	 *	are output. '0' internaly generated signals are
	 *	output.
	 * @taddr_in: Mode Address for Test Interface.
	 *	Specifies the register address for writing to or
	 *	reading from the PHY test interface register.
	 * @tdata_in: Internal Testing Register Input Data and Select
	 *	This is a test bus. Data is present on [3:0],
	 *	and its corresponding select (enable) is present
	 *	on bits [7:4].
	 * @ate_reset: Reset input from automatic test equipment.
	 *	This is a test signal. When the USB Core is
	 *	powered up (not in Susned Mode), an automatic
	 *	tester can use this to disable phy_clock and
	 *	free_clk, then re-eanable them with an aligned
	 *	phase.
	 *	'1': The phy_clk and free_clk outputs are
	 *	disabled. "0": The phy_clock and free_clk outputs
	 *	are available within a specific period after the
	 *	de-assertion.
	 */
	struct cvmx_usbnx_usbp_ctl_status_s {
		uint64_t txrisetune		: 1;
		uint64_t txvreftune		: 4;
		uint64_t txfslstune		: 4;
		uint64_t txhsxvtune		: 2;
		uint64_t sqrxtune		: 3;
		uint64_t compdistune		: 3;
		uint64_t otgtune		: 3;
		uint64_t otgdisable		: 1;
		uint64_t portreset		: 1;
		uint64_t drvvbus		: 1;
		uint64_t lsbist			: 1;
		uint64_t fsbist			: 1;
		uint64_t hsbist			: 1;
		uint64_t bist_done		: 1;
		uint64_t bist_err		: 1;
		uint64_t tdata_out		: 4;
		uint64_t siddq			: 1;
		uint64_t txpreemphasistune	: 1;
		uint64_t dma_bmode		: 1;
		uint64_t usbc_end		: 1;
		uint64_t usbp_bist		: 1;
		uint64_t tclk			: 1;
		uint64_t dp_pulld		: 1;
		uint64_t dm_pulld		: 1;
		uint64_t hst_mode		: 1;
		uint64_t tuning			: 4;
		uint64_t tx_bs_enh		: 1;
		uint64_t tx_bs_en		: 1;
		uint64_t loop_enb		: 1;
		uint64_t vtest_enb		: 1;
		uint64_t bist_enb		: 1;
		uint64_t tdata_sel		: 1;
		uint64_t taddr_in		: 4;
		uint64_t tdata_in		: 8;
		uint64_t ate_reset		: 1;
	} s;
	/**
	 * struct cvmx_usbnx_usbp_ctl_status_cn30xx
	 * @bist_done: PHY Bist Done.
	 *	Asserted at the end of the PHY BIST sequence.
	 * @bist_err: PHY Bist Error.
	 *	Indicates an internal error was detected during
	 *	the BIST sequence.
	 * @tdata_out: PHY Test Data Out.
	 *	Presents either internaly generated signals or
	 *	test register contents, based upon the value of
	 *	test_data_out_sel.
	 * @dma_bmode: When set to 1 the L2C DMA address will be updated
	 *	with byte-counts between packets. When set to 0
	 *	the L2C DMA address is incremented to the next
	 *	4-byte aligned address after adding byte-count.
	 * @usbc_end: Bigendian input to the USB Core. This should be
	 *	set to '0' for operation.
	 * @usbp_bist: PHY, This is cleared '0' to run BIST on the USBP.
	 * @tclk: PHY Test Clock, used to load TDATA_IN to the USBP.
	 * @dp_pulld: PHY DP_PULLDOWN input to the USB-PHY.
	 *	This signal enables the pull-down resistance on
	 *	the D+ line. '1' pull down-resistance is connected
	 *	to D+/ '0' pull down resistance is not connected
	 *	to D+. When an A/B device is acting as a host
	 *	(downstream-facing port), dp_pulldown and
	 *	dm_pulldown are enabled. This must not toggle
	 *	during normal opeartion.
	 * @dm_pulld: PHY DM_PULLDOWN input to the USB-PHY.
	 *	This signal enables the pull-down resistance on
	 *	the D- line. '1' pull down-resistance is connected
	 *	to D-. '0' pull down resistance is not connected
	 *	to D-. When an A/B device is acting as a host
	 *	(downstream-facing port), dp_pulldown and
	 *	dm_pulldown are enabled. This must not toggle
	 *	during normal opeartion.
	 * @hst_mode: When '0' the USB is acting as HOST, when '1'
	 *	USB is acting as device. This field needs to be
	 *	set while the USB is in reset.
	 * @tuning: Transmitter Tuning for High-Speed Operation.
	 *	Tunes the current supply and rise/fall output
	 *	times for high-speed operation.
	 *	[20:19] == 11: Current supply increased
	 *	approximately 9%
	 *	[20:19] == 10: Current supply increased
	 *	approximately 4.5%
	 *	[20:19] == 01: Design default.
	 *	[20:19] == 00: Current supply decreased
	 *	approximately 4.5%
	 *	[22:21] == 11: Rise and fall times are increased.
	 *	[22:21] == 10: Design default.
	 *	[22:21] == 01: Rise and fall times are decreased.
	 *	[22:21] == 00: Rise and fall times are decreased
	 *	further as compared to the 01 setting.
	 * @tx_bs_enh: Transmit Bit Stuffing on [15:8].
	 *	Enables or disables bit stuffing on data[15:8]
	 *	when bit-stuffing is enabled.
	 * @tx_bs_en: Transmit Bit Stuffing on [7:0].
	 *	Enables or disables bit stuffing on data[7:0]
	 *	when bit-stuffing is enabled.
	 * @loop_enb: PHY Loopback Test Enable.
	 *	'1': During data transmission the receive is
	 *	enabled.
	 *	'0': During data transmission the receive is
	 *	disabled.
	 *	Must be '0' for normal operation.
	 * @vtest_enb: Analog Test Pin Enable.
	 *	'1' The PHY's analog_test pin is enabled for the
	 *	input and output of applicable analog test signals.
	 *	'0' THe analog_test pin is disabled.
	 * @bist_enb: Built-In Self Test Enable.
	 *	Used to activate BIST in the PHY.
	 * @tdata_sel: Test Data Out Select.
	 *	'1' test_data_out[3:0] (PHY) register contents
	 *	are output. '0' internaly generated signals are
	 *	output.
	 * @taddr_in: Mode Address for Test Interface.
	 *	Specifies the register address for writing to or
	 *	reading from the PHY test interface register.
	 * @tdata_in: Internal Testing Register Input Data and Select
	 *	This is a test bus. Data is present on [3:0],
	 *	and its corresponding select (enable) is present
	 *	on bits [7:4].
	 * @ate_reset: Reset input from automatic test equipment.
	 *	This is a test signal. When the USB Core is
	 *	powered up (not in Susned Mode), an automatic
	 *	tester can use this to disable phy_clock and
	 *	free_clk, then re-eanable them with an aligned
	 *	phase.
	 *	'1': The phy_clk and free_clk outputs are
	 *	disabled. "0": The phy_clock and free_clk outputs
	 *	are available within a specific period after the
	 *	de-assertion.
	 */
	struct cvmx_usbnx_usbp_ctl_status_cn30xx {
		uint64_t reserved_38_63	: 26;
		uint64_t bist_done	: 1;
		uint64_t bist_err	: 1;
		uint64_t tdata_out	: 4;
		uint64_t reserved_30_31	: 2;
		uint64_t dma_bmode	: 1;
		uint64_t usbc_end	: 1;
		uint64_t usbp_bist	: 1;
		uint64_t tclk		: 1;
		uint64_t dp_pulld	: 1;
		uint64_t dm_pulld	: 1;
		uint64_t hst_mode	: 1;
		uint64_t tuning		: 4;
		uint64_t tx_bs_enh	: 1;
		uint64_t tx_bs_en	: 1;
		uint64_t loop_enb	: 1;
		uint64_t vtest_enb	: 1;
		uint64_t bist_enb	: 1;
		uint64_t tdata_sel	: 1;
		uint64_t taddr_in	: 4;
		uint64_t tdata_in	: 8;
		uint64_t ate_reset	: 1;
	} cn30xx;
	/**
	 * struct cvmx_usbnx_usbp_ctl_status_cn50xx
	 * @txrisetune: HS Transmitter Rise/Fall Time Adjustment
	 * @txvreftune: HS DC Voltage Level Adjustment
	 * @txfslstune: FS/LS Source Impedence Adjustment
	 * @txhsxvtune: Transmitter High-Speed Crossover Adjustment
	 * @sqrxtune: Squelch Threshold Adjustment
	 * @compdistune: Disconnect Threshold Adjustment
	 * @otgtune: VBUS Valid Threshold Adjustment
	 * @otgdisable: OTG Block Disable
	 * @portreset: Per_Port Reset
	 * @drvvbus: Drive VBUS
	 * @lsbist: Low-Speed BIST Enable.
	 * @fsbist: Full-Speed BIST Enable.
	 * @hsbist: High-Speed BIST Enable.
	 * @bist_done: PHY Bist Done.
	 *	Asserted at the end of the PHY BIST sequence.
	 * @bist_err: PHY Bist Error.
	 *	Indicates an internal error was detected during
	 *	the BIST sequence.
	 * @tdata_out: PHY Test Data Out.
	 *	Presents either internaly generated signals or
	 *	test register contents, based upon the value of
	 *	test_data_out_sel.
	 * @txpreemphasistune: HS Transmitter Pre-Emphasis Enable
	 * @dma_bmode: When set to 1 the L2C DMA address will be updated
	 *	with byte-counts between packets. When set to 0
	 *	the L2C DMA address is incremented to the next
	 *	4-byte aligned address after adding byte-count.
	 * @usbc_end: Bigendian input to the USB Core. This should be
	 *	set to '0' for operation.
	 * @usbp_bist: PHY, This is cleared '0' to run BIST on the USBP.
	 * @tclk: PHY Test Clock, used to load TDATA_IN to the USBP.
	 * @dp_pulld: PHY DP_PULLDOWN input to the USB-PHY.
	 *	This signal enables the pull-down resistance on
	 *	the D+ line. '1' pull down-resistance is connected
	 *	to D+/ '0' pull down resistance is not connected
	 *	to D+. When an A/B device is acting as a host
	 *	(downstream-facing port), dp_pulldown and
	 *	dm_pulldown are enabled. This must not toggle
	 *	during normal opeartion.
	 * @dm_pulld: PHY DM_PULLDOWN input to the USB-PHY.
	 *	This signal enables the pull-down resistance on
	 *	the D- line. '1' pull down-resistance is connected
	 *	to D-. '0' pull down resistance is not connected
	 *	to D-. When an A/B device is acting as a host
	 *	(downstream-facing port), dp_pulldown and
	 *	dm_pulldown are enabled. This must not toggle
	 *	during normal opeartion.
	 * @hst_mode: When '0' the USB is acting as HOST, when '1'
	 *	USB is acting as device. This field needs to be
	 *	set while the USB is in reset.
	 * @tx_bs_enh: Transmit Bit Stuffing on [15:8].
	 *	Enables or disables bit stuffing on data[15:8]
	 *	when bit-stuffing is enabled.
	 * @tx_bs_en: Transmit Bit Stuffing on [7:0].
	 *	Enables or disables bit stuffing on data[7:0]
	 *	when bit-stuffing is enabled.
	 * @loop_enb: PHY Loopback Test Enable.
	 *	'1': During data transmission the receive is
	 *	enabled.
	 *	'0': During data transmission the receive is
	 *	disabled.
	 *	Must be '0' for normal operation.
	 * @vtest_enb: Analog Test Pin Enable.
	 *	'1' The PHY's analog_test pin is enabled for the
	 *	input and output of applicable analog test signals.
	 *	'0' THe analog_test pin is disabled.
	 * @bist_enb: Built-In Self Test Enable.
	 *	Used to activate BIST in the PHY.
	 * @tdata_sel: Test Data Out Select.
	 *	'1' test_data_out[3:0] (PHY) register contents
	 *	are output. '0' internaly generated signals are
	 *	output.
	 * @taddr_in: Mode Address for Test Interface.
	 *	Specifies the register address for writing to or
	 *	reading from the PHY test interface register.
	 * @tdata_in: Internal Testing Register Input Data and Select
	 *	This is a test bus. Data is present on [3:0],
	 *	and its corresponding select (enable) is present
	 *	on bits [7:4].
	 * @ate_reset: Reset input from automatic test equipment.
	 *	This is a test signal. When the USB Core is
	 *	powered up (not in Susned Mode), an automatic
	 *	tester can use this to disable phy_clock and
	 *	free_clk, then re-eanable them with an aligned
	 *	phase.
	 *	'1': The phy_clk and free_clk outputs are
	 *	disabled. "0": The phy_clock and free_clk outputs
	 *	are available within a specific period after the
	 *	de-assertion.
	 */
	struct cvmx_usbnx_usbp_ctl_status_cn50xx {
		uint64_t txrisetune		: 1;
		uint64_t txvreftune		: 4;
		uint64_t txfslstune		: 4;
		uint64_t txhsxvtune		: 2;
		uint64_t sqrxtune		: 3;
		uint64_t compdistune		: 3;
		uint64_t otgtune		: 3;
		uint64_t otgdisable		: 1;
		uint64_t portreset		: 1;
		uint64_t drvvbus		: 1;
		uint64_t lsbist			: 1;
		uint64_t fsbist			: 1;
		uint64_t hsbist			: 1;
		uint64_t bist_done		: 1;
		uint64_t bist_err		: 1;
		uint64_t tdata_out		: 4;
		uint64_t reserved_31_31		: 1;
		uint64_t txpreemphasistune	: 1;
		uint64_t dma_bmode		: 1;
		uint64_t usbc_end		: 1;
		uint64_t usbp_bist		: 1;
		uint64_t tclk			: 1;
		uint64_t dp_pulld		: 1;
		uint64_t dm_pulld		: 1;
		uint64_t hst_mode		: 1;
		uint64_t reserved_19_22		: 4;
		uint64_t tx_bs_enh		: 1;
		uint64_t tx_bs_en		: 1;
		uint64_t loop_enb		: 1;
		uint64_t vtest_enb		: 1;
		uint64_t bist_enb		: 1;
		uint64_t tdata_sel		: 1;
		uint64_t taddr_in		: 4;
		uint64_t tdata_in		: 8;
		uint64_t ate_reset		: 1;
	} cn50xx;
	/**
	 * struct cvmx_usbnx_usbp_ctl_status_cn52xx
	 * @txrisetune: HS Transmitter Rise/Fall Time Adjustment
	 * @txvreftune: HS DC Voltage Level Adjustment
	 * @txfslstune: FS/LS Source Impedence Adjustment
	 * @txhsxvtune: Transmitter High-Speed Crossover Adjustment
	 * @sqrxtune: Squelch Threshold Adjustment
	 * @compdistune: Disconnect Threshold Adjustment
	 * @otgtune: VBUS Valid Threshold Adjustment
	 * @otgdisable: OTG Block Disable
	 * @portreset: Per_Port Reset
	 * @drvvbus: Drive VBUS
	 * @lsbist: Low-Speed BIST Enable.
	 * @fsbist: Full-Speed BIST Enable.
	 * @hsbist: High-Speed BIST Enable.
	 * @bist_done: PHY Bist Done.
	 *	Asserted at the end of the PHY BIST sequence.
	 * @bist_err: PHY Bist Error.
	 *	Indicates an internal error was detected during
	 *	the BIST sequence.
	 * @tdata_out: PHY Test Data Out.
	 *	Presents either internaly generated signals or
	 *	test register contents, based upon the value of
	 *	test_data_out_sel.
	 * @siddq: Drives the USBP (USB-PHY) SIDDQ input.
	 *	Normally should be set to zero.
	 *	When customers have no intent to use USB PHY
	 *	interface, they should:
	 *	- still provide 3.3V to USB_VDD33, and
	 *	- tie USB_REXT to 3.3V supply, and
	 *	- set USBN*_USBP_CTL_STATUS[SIDDQ]=1
	 * @txpreemphasistune: HS Transmitter Pre-Emphasis Enable
	 * @dma_bmode: When set to 1 the L2C DMA address will be updated
	 *	with byte-counts between packets. When set to 0
	 *	the L2C DMA address is incremented to the next
	 *	4-byte aligned address after adding byte-count.
	 * @usbc_end: Bigendian input to the USB Core. This should be
	 *	set to '0' for operation.
	 * @usbp_bist: PHY, This is cleared '0' to run BIST on the USBP.
	 * @tclk: PHY Test Clock, used to load TDATA_IN to the USBP.
	 * @dp_pulld: PHY DP_PULLDOWN input to the USB-PHY.
	 *	This signal enables the pull-down resistance on
	 *	the D+ line. '1' pull down-resistance is connected
	 *	to D+/ '0' pull down resistance is not connected
	 *	to D+. When an A/B device is acting as a host
	 *	(downstream-facing port), dp_pulldown and
	 *	dm_pulldown are enabled. This must not toggle
	 *	during normal opeartion.
	 * @dm_pulld: PHY DM_PULLDOWN input to the USB-PHY.
	 *	This signal enables the pull-down resistance on
	 *	the D- line. '1' pull down-resistance is connected
	 *	to D-. '0' pull down resistance is not connected
	 *	to D-. When an A/B device is acting as a host
	 *	(downstream-facing port), dp_pulldown and
	 *	dm_pulldown are enabled. This must not toggle
	 *	during normal opeartion.
	 * @hst_mode: When '0' the USB is acting as HOST, when '1'
	 *	USB is acting as device. This field needs to be
	 *	set while the USB is in reset.
	 * @tx_bs_enh: Transmit Bit Stuffing on [15:8].
	 *	Enables or disables bit stuffing on data[15:8]
	 *	when bit-stuffing is enabled.
	 * @tx_bs_en: Transmit Bit Stuffing on [7:0].
	 *	Enables or disables bit stuffing on data[7:0]
	 *	when bit-stuffing is enabled.
	 * @loop_enb: PHY Loopback Test Enable.
	 *	'1': During data transmission the receive is
	 *	enabled.
	 *	'0': During data transmission the receive is
	 *	disabled.
	 *	Must be '0' for normal operation.
	 * @vtest_enb: Analog Test Pin Enable.
	 *	'1' The PHY's analog_test pin is enabled for the
	 *	input and output of applicable analog test signals.
	 *	'0' THe analog_test pin is disabled.
	 * @bist_enb: Built-In Self Test Enable.
	 *	Used to activate BIST in the PHY.
	 * @tdata_sel: Test Data Out Select.
	 *	'1' test_data_out[3:0] (PHY) register contents
	 *	are output. '0' internaly generated signals are
	 *	output.
	 * @taddr_in: Mode Address for Test Interface.
	 *	Specifies the register address for writing to or
	 *	reading from the PHY test interface register.
	 * @tdata_in: Internal Testing Register Input Data and Select
	 *	This is a test bus. Data is present on [3:0],
	 *	and its corresponding select (enable) is present
	 *	on bits [7:4].
	 * @ate_reset: Reset input from automatic test equipment.
	 *	This is a test signal. When the USB Core is
	 *	powered up (not in Susned Mode), an automatic
	 *	tester can use this to disable phy_clock and
	 *	free_clk, then re-eanable them with an aligned
	 *	phase.
	 *	'1': The phy_clk and free_clk outputs are
	 *	disabled. "0": The phy_clock and free_clk outputs
	 *	are available within a specific period after the
	 *	de-assertion.
	 */
	struct cvmx_usbnx_usbp_ctl_status_cn52xx {
		uint64_t txrisetune		: 1;
		uint64_t txvreftune		: 4;
		uint64_t txfslstune		: 4;
		uint64_t txhsxvtune		: 2;
		uint64_t sqrxtune		: 3;
		uint64_t compdistune		: 3;
		uint64_t otgtune		: 3;
		uint64_t otgdisable		: 1;
		uint64_t portreset		: 1;
		uint64_t drvvbus		: 1;
		uint64_t lsbist			: 1;
		uint64_t fsbist			: 1;
		uint64_t hsbist			: 1;
		uint64_t bist_done		: 1;
		uint64_t bist_err		: 1;
		uint64_t tdata_out		: 4;
		uint64_t siddq			: 1;
		uint64_t txpreemphasistune	: 1;
		uint64_t dma_bmode		: 1;
		uint64_t usbc_end		: 1;
		uint64_t usbp_bist		: 1;
		uint64_t tclk			: 1;
		uint64_t dp_pulld		: 1;
		uint64_t dm_pulld		: 1;
		uint64_t hst_mode		: 1;
		uint64_t reserved_19_22		: 4;
		uint64_t tx_bs_enh		: 1;
		uint64_t tx_bs_en		: 1;
		uint64_t loop_enb		: 1;
		uint64_t vtest_enb		: 1;
		uint64_t bist_enb		: 1;
		uint64_t tdata_sel		: 1;
		uint64_t taddr_in		: 4;
		uint64_t tdata_in		: 8;
		uint64_t ate_reset		: 1;
	} cn52xx;
};
typedef union cvmx_usbnx_usbp_ctl_status cvmx_usbnx_usbp_ctl_status_t;

#endif
