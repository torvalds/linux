/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _ISCI_PROBE_ROMS_H_
#define _ISCI_PROBE_ROMS_H_

#ifdef __KERNEL__
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/efi.h>
#include "isci.h"

#define SCIC_SDS_PARM_NO_SPEED   0

/* generation 1 (i.e. 1.5 Gb/s) */
#define SCIC_SDS_PARM_GEN1_SPEED 1

/* generation 2 (i.e. 3.0 Gb/s) */
#define SCIC_SDS_PARM_GEN2_SPEED 2

/* generation 3 (i.e. 6.0 Gb/s) */
#define SCIC_SDS_PARM_GEN3_SPEED 3
#define SCIC_SDS_PARM_MAX_SPEED SCIC_SDS_PARM_GEN3_SPEED

/* parameters that can be set by module parameters */
struct sci_user_parameters {
	struct sci_phy_user_params {
		/**
		 * This field specifies the NOTIFY (ENABLE SPIN UP) primitive
		 * insertion frequency for this phy index.
		 */
		u32 notify_enable_spin_up_insertion_frequency;

		/**
		 * This method specifies the number of transmitted DWORDs within which
		 * to transmit a single ALIGN primitive.  This value applies regardless
		 * of what type of device is attached or connection state.  A value of
		 * 0 indicates that no ALIGN primitives will be inserted.
		 */
		u16 align_insertion_frequency;

		/**
		 * This method specifies the number of transmitted DWORDs within which
		 * to transmit 2 ALIGN primitives.  This applies for SAS connections
		 * only.  A minimum value of 3 is required for this field.
		 */
		u16 in_connection_align_insertion_frequency;

		/**
		 * This field indicates the maximum speed generation to be utilized
		 * by phys in the supplied port.
		 * - A value of 1 indicates generation 1 (i.e. 1.5 Gb/s).
		 * - A value of 2 indicates generation 2 (i.e. 3.0 Gb/s).
		 * - A value of 3 indicates generation 3 (i.e. 6.0 Gb/s).
		 */
		u8 max_speed_generation;

	} phys[SCI_MAX_PHYS];

	/**
	 * This field specifies the maximum number of direct attached devices
	 * that can have power supplied to them simultaneously.
	 */
	u8 max_concurr_spinup;

	/**
	 * This field specifies the number of seconds to allow a phy to consume
	 * power before yielding to another phy.
	 *
	 */
	u8 phy_spin_up_delay_interval;

	/**
	 * These timer values specifies how long a link will remain open with no
	 * activity in increments of a microsecond, it can be in increments of
	 * 100 microseconds if the upper most bit is set.
	 *
	 */
	u16 stp_inactivity_timeout;
	u16 ssp_inactivity_timeout;

	/**
	 * These timer values specifies how long a link will remain open in increments
	 * of 100 microseconds.
	 *
	 */
	u16 stp_max_occupancy_timeout;
	u16 ssp_max_occupancy_timeout;

	/**
	 * This timer value specifies how long a link will remain open with no
	 * outbound traffic in increments of a microsecond.
	 *
	 */
	u8 no_outbound_task_timeout;

};

#define SCIC_SDS_PARM_PHY_MASK_MIN 0x0
#define SCIC_SDS_PARM_PHY_MASK_MAX 0xF
#define MAX_CONCURRENT_DEVICE_SPIN_UP_COUNT 4

struct sci_oem_params;
int sci_oem_parameters_validate(struct sci_oem_params *oem, u8 version);

struct isci_orom;
struct isci_orom *isci_request_oprom(struct pci_dev *pdev);
struct isci_orom *isci_request_firmware(struct pci_dev *pdev, const struct firmware *fw);
struct isci_orom *isci_get_efi_var(struct pci_dev *pdev);

struct isci_oem_hdr {
	u8 sig[4];
	u8 rev_major;
	u8 rev_minor;
	u16 len;
	u8 checksum;
	u8 reserved1;
	u16 reserved2;
} __attribute__ ((packed));

#else
#define SCI_MAX_PORTS 4
#define SCI_MAX_PHYS 4
#define SCI_MAX_CONTROLLERS 2
#endif

#define ISCI_FW_NAME		"isci/isci_firmware.bin"

#define ROMSIGNATURE		0xaa55

#define ISCI_OEM_SIG		"$OEM"
#define ISCI_OEM_SIG_SIZE	4
#define ISCI_ROM_SIG		"ISCUOEMB"
#define ISCI_ROM_SIG_SIZE	8

#define ISCI_EFI_VENDOR_GUID	\
	EFI_GUID(0x193dfefa, 0xa445, 0x4302, 0x99, 0xd8, 0xef, 0x3a, 0xad, \
			0x1a, 0x04, 0xc6)
#define ISCI_EFI_VAR_NAME	"RstScuO"

#define ISCI_ROM_VER_1_0	0x10
#define ISCI_ROM_VER_1_1	0x11
#define ISCI_ROM_VER_1_3	0x13
#define ISCI_ROM_VER_LATEST	ISCI_ROM_VER_1_3

/* Allowed PORT configuration modes APC Automatic PORT configuration mode is
 * defined by the OEM configuration parameters providing no PHY_MASK parameters
 * for any PORT. i.e. There are no phys assigned to any of the ports at start.
 * MPC Manual PORT configuration mode is defined by the OEM configuration
 * parameters providing a PHY_MASK value for any PORT.  It is assumed that any
 * PORT with no PHY_MASK is an invalid port and not all PHYs must be assigned.
 * A PORT_PHY mask that assigns just a single PHY to a port and no other PHYs
 * being assigned is sufficient to declare manual PORT configuration.
 */
enum sci_port_configuration_mode {
	SCIC_PORT_MANUAL_CONFIGURATION_MODE = 0,
	SCIC_PORT_AUTOMATIC_CONFIGURATION_MODE = 1
};

struct sci_bios_oem_param_block_hdr {
	uint8_t signature[ISCI_ROM_SIG_SIZE];
	uint16_t total_block_length;
	uint8_t hdr_length;
	uint8_t version;
	uint8_t preboot_source;
	uint8_t num_elements;
	uint16_t element_length;
	uint8_t reserved[8];
} __attribute__ ((packed));

struct sci_oem_params {
	struct {
		uint8_t mode_type;
		uint8_t max_concurr_spin_up;
		/*
		 * This bitfield indicates the OEM's desired default Tx
		 * Spread Spectrum Clocking (SSC) settings for SATA and SAS.
		 * NOTE: Default SSC Modulation Frequency is 31.5KHz.
		 */
		union {
			struct {
			/*
			 * NOTE: Max spread for SATA is +0 / -5000 PPM.
			 * Down-spreading SSC (only method allowed for SATA):
			 *  SATA SSC Tx Disabled                    = 0x0
			 *  SATA SSC Tx at +0 / -1419 PPM Spread    = 0x2
			 *  SATA SSC Tx at +0 / -2129 PPM Spread    = 0x3
			 *  SATA SSC Tx at +0 / -4257 PPM Spread    = 0x6
			 *  SATA SSC Tx at +0 / -4967 PPM Spread    = 0x7
			 */
				uint8_t ssc_sata_tx_spread_level:4;
			/*
			 * SAS SSC Tx Disabled                     = 0x0
			 *
			 * NOTE: Max spread for SAS down-spreading +0 /
			 *	 -2300 PPM
			 * Down-spreading SSC:
			 *  SAS SSC Tx at +0 / -1419 PPM Spread     = 0x2
			 *  SAS SSC Tx at +0 / -2129 PPM Spread     = 0x3
			 *
			 * NOTE: Max spread for SAS center-spreading +2300 /
			 *	 -2300 PPM
			 * Center-spreading SSC:
			 *  SAS SSC Tx at +1064 / -1064 PPM Spread  = 0x3
			 *  SAS SSC Tx at +2129 / -2129 PPM Spread  = 0x6
			 */
				uint8_t ssc_sas_tx_spread_level:3;
			/*
			 * NOTE: Refer to the SSC section of the SAS 2.x
			 * Specification for proper setting of this field.
			 * For standard SAS Initiator SAS PHY operation it
			 * should be 0 for Down-spreading.
			 * SAS SSC Tx spread type:
			 *  Down-spreading SSC      = 0
			 *  Center-spreading SSC    = 1
			 */
				uint8_t ssc_sas_tx_type:1;
			};
			uint8_t do_enable_ssc;
		};
		/*
		 * This field indicates length of the SAS/SATA cable between
		 * host and device.
		 * This field is used make relationship between analog
		 * parameters of the phy in the silicon and length of the cable.
		 * Supported cable attenuation levels:
		 * "short"- up to 3m, "medium"-3m to 6m, and "long"- more than
		 * 6m.
		 *
		 * This is bit mask field:
		 *
		 * BIT:      (MSB) 7     6     5     4
		 * ASSIGNMENT:   <phy3><phy2><phy1><phy0>  - Medium cable
		 *                                           length assignment
		 * BIT:            3     2     1     0  (LSB)
		 * ASSIGNMENT:   <phy3><phy2><phy1><phy0>  - Long cable length
		 *                                           assignment
		 *
		 * BITS 7-4 are set when the cable length is assigned to medium
		 * BITS 3-0 are set when the cable length is assigned to long
		 *
		 * The BIT positions are clear when the cable length is
		 * assigned to short.
		 *
		 * Setting the bits for both long and medium cable length is
		 * undefined.
		 *
		 * A value of 0x84 would assign
		 *    phy3 - medium
		 *    phy2 - long
		 *    phy1 - short
		 *    phy0 - short
		 */
		uint8_t cable_selection_mask;
	} controller;

	struct {
		uint8_t phy_mask;
	} ports[SCI_MAX_PORTS];

	struct sci_phy_oem_params {
		struct {
			uint32_t high;
			uint32_t low;
		} sas_address;

		uint32_t afe_tx_amp_control0;
		uint32_t afe_tx_amp_control1;
		uint32_t afe_tx_amp_control2;
		uint32_t afe_tx_amp_control3;
	} phys[SCI_MAX_PHYS];
} __attribute__ ((packed));

struct isci_orom {
	struct sci_bios_oem_param_block_hdr hdr;
	struct sci_oem_params ctrl[SCI_MAX_CONTROLLERS];
} __attribute__ ((packed));

#endif
