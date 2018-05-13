// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: utresdecode - Resource descriptor keyword strings
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acresrc.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utresdecode")

#if defined (ACPI_DEBUG_OUTPUT) || \
	defined (ACPI_DISASSEMBLER) || \
	defined (ACPI_DEBUGGER)
/*
 * Strings used to decode resource descriptors.
 * Used by both the disassembler and the debugger resource dump routines
 */
const char *acpi_gbl_bm_decode[] = {
	"NotBusMaster",
	"BusMaster"
};

const char *acpi_gbl_config_decode[] = {
	"0 - Good Configuration",
	"1 - Acceptable Configuration",
	"2 - Suboptimal Configuration",
	"3 - ***Invalid Configuration***",
};

const char *acpi_gbl_consume_decode[] = {
	"ResourceProducer",
	"ResourceConsumer"
};

const char *acpi_gbl_dec_decode[] = {
	"PosDecode",
	"SubDecode"
};

const char *acpi_gbl_he_decode[] = {
	"Level",
	"Edge"
};

const char *acpi_gbl_io_decode[] = {
	"Decode10",
	"Decode16"
};

const char *acpi_gbl_ll_decode[] = {
	"ActiveHigh",
	"ActiveLow",
	"ActiveBoth",
	"Reserved"
};

const char *acpi_gbl_max_decode[] = {
	"MaxNotFixed",
	"MaxFixed"
};

const char *acpi_gbl_mem_decode[] = {
	"NonCacheable",
	"Cacheable",
	"WriteCombining",
	"Prefetchable"
};

const char *acpi_gbl_min_decode[] = {
	"MinNotFixed",
	"MinFixed"
};

const char *acpi_gbl_mtp_decode[] = {
	"AddressRangeMemory",
	"AddressRangeReserved",
	"AddressRangeACPI",
	"AddressRangeNVS"
};

const char *acpi_gbl_rng_decode[] = {
	"InvalidRanges",
	"NonISAOnlyRanges",
	"ISAOnlyRanges",
	"EntireRange"
};

const char *acpi_gbl_rw_decode[] = {
	"ReadOnly",
	"ReadWrite"
};

const char *acpi_gbl_shr_decode[] = {
	"Exclusive",
	"Shared",
	"ExclusiveAndWake",	/* ACPI 5.0 */
	"SharedAndWake"		/* ACPI 5.0 */
};

const char *acpi_gbl_siz_decode[] = {
	"Transfer8",
	"Transfer8_16",
	"Transfer16",
	"InvalidSize"
};

const char *acpi_gbl_trs_decode[] = {
	"DenseTranslation",
	"SparseTranslation"
};

const char *acpi_gbl_ttp_decode[] = {
	"TypeStatic",
	"TypeTranslation"
};

const char *acpi_gbl_typ_decode[] = {
	"Compatibility",
	"TypeA",
	"TypeB",
	"TypeF"
};

const char *acpi_gbl_ppc_decode[] = {
	"PullDefault",
	"PullUp",
	"PullDown",
	"PullNone"
};

const char *acpi_gbl_ior_decode[] = {
	"IoRestrictionNone",
	"IoRestrictionInputOnly",
	"IoRestrictionOutputOnly",
	"IoRestrictionNoneAndPreserve"
};

const char *acpi_gbl_dts_decode[] = {
	"Width8bit",
	"Width16bit",
	"Width32bit",
	"Width64bit",
	"Width128bit",
	"Width256bit",
};

/* GPIO connection type */

const char *acpi_gbl_ct_decode[] = {
	"Interrupt",
	"I/O"
};

/* Serial bus type */

const char *acpi_gbl_sbt_decode[] = {
	"/* UNKNOWN serial bus type */",
	"I2C",
	"SPI",
	"UART"
};

/* I2C serial bus access mode */

const char *acpi_gbl_am_decode[] = {
	"AddressingMode7Bit",
	"AddressingMode10Bit"
};

/* I2C serial bus slave mode */

const char *acpi_gbl_sm_decode[] = {
	"ControllerInitiated",
	"DeviceInitiated"
};

/* SPI serial bus wire mode */

const char *acpi_gbl_wm_decode[] = {
	"FourWireMode",
	"ThreeWireMode"
};

/* SPI serial clock phase */

const char *acpi_gbl_cph_decode[] = {
	"ClockPhaseFirst",
	"ClockPhaseSecond"
};

/* SPI serial bus clock polarity */

const char *acpi_gbl_cpo_decode[] = {
	"ClockPolarityLow",
	"ClockPolarityHigh"
};

/* SPI serial bus device polarity */

const char *acpi_gbl_dp_decode[] = {
	"PolarityLow",
	"PolarityHigh"
};

/* UART serial bus endian */

const char *acpi_gbl_ed_decode[] = {
	"LittleEndian",
	"BigEndian"
};

/* UART serial bus bits per byte */

const char *acpi_gbl_bpb_decode[] = {
	"DataBitsFive",
	"DataBitsSix",
	"DataBitsSeven",
	"DataBitsEight",
	"DataBitsNine",
	"/* UNKNOWN Bits per byte */",
	"/* UNKNOWN Bits per byte */",
	"/* UNKNOWN Bits per byte */"
};

/* UART serial bus stop bits */

const char *acpi_gbl_sb_decode[] = {
	"StopBitsZero",
	"StopBitsOne",
	"StopBitsOnePlusHalf",
	"StopBitsTwo"
};

/* UART serial bus flow control */

const char *acpi_gbl_fc_decode[] = {
	"FlowControlNone",
	"FlowControlHardware",
	"FlowControlXON",
	"/* UNKNOWN flow control keyword */"
};

/* UART serial bus parity type */

const char *acpi_gbl_pt_decode[] = {
	"ParityTypeNone",
	"ParityTypeEven",
	"ParityTypeOdd",
	"ParityTypeMark",
	"ParityTypeSpace",
	"/* UNKNOWN parity keyword */",
	"/* UNKNOWN parity keyword */",
	"/* UNKNOWN parity keyword */"
};

/* pin_config type */

const char *acpi_gbl_ptyp_decode[] = {
	"Default",
	"Bias Pull-up",
	"Bias Pull-down",
	"Bias Default",
	"Bias Disable",
	"Bias High Impedance",
	"Bias Bus Hold",
	"Drive Open Drain",
	"Drive Open Source",
	"Drive Push Pull",
	"Drive Strength",
	"Slew Rate",
	"Input Debounce",
	"Input Schmitt Trigger",
};

#endif
