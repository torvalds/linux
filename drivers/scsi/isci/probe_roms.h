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

struct isci_orom *isci_request_oprom(struct pci_dev *pdev);

union scic_oem_parameters;
struct isci_orom;

enum sci_status isci_parse_oem_parameters(
	union scic_oem_parameters *oem_params,
	struct isci_orom *orom,
	int scu_index);
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
#define ISCI_EFI_ATTRIBUTES	0
#define ISCI_EFI_VAR_NAME	"RstScuO"

/* Allowed PORT configuration modes APC Automatic PORT configuration mode is
 * defined by the OEM configuration parameters providing no PHY_MASK parameters
 * for any PORT. i.e. There are no phys assigned to any of the ports at start.
 * MPC Manual PORT configuration mode is defined by the OEM configuration
 * parameters providing a PHY_MASK value for any PORT.  It is assumed that any
 * PORT with no PHY_MASK is an invalid port and not all PHYs must be assigned.
 * A PORT_PHY mask that assigns just a single PHY to a port and no other PHYs
 * being assigned is sufficient to declare manual PORT configuration.
 */
enum scic_port_configuration_mode {
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

struct scic_sds_oem_params {
	struct {
		uint8_t mode_type;
		uint8_t max_concurrent_dev_spin_up;
		uint8_t do_enable_ssc;
		uint8_t reserved;
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
	struct scic_sds_oem_params ctrl[SCI_MAX_CONTROLLERS];
} __attribute__ ((packed));

#endif
