/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

/*
 *
 * Interface to the SMI/MDIO hardware, including support for both IEEE 802.3
 * clause 22 and clause 45 operations.
 *
 */

#ifndef __CVMX_MIO_H__
#define __CVMX_MIO_H__

#include <asm/octeon/cvmx-smix-defs.h>

/**
 * PHY register 0 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_CONTROL 0
typedef union {
	uint16_t u16;
	struct {
		uint16_t reset:1;
		uint16_t loopback:1;
		uint16_t speed_lsb:1;
		uint16_t autoneg_enable:1;
		uint16_t power_down:1;
		uint16_t isolate:1;
		uint16_t restart_autoneg:1;
		uint16_t duplex:1;
		uint16_t collision_test:1;
		uint16_t speed_msb:1;
		uint16_t unidirectional_enable:1;
		uint16_t reserved_0_4:5;
	} s;
} cvmx_mdio_phy_reg_control_t;

/**
 * PHY register 1 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_STATUS 1
typedef union {
	uint16_t u16;
	struct {
		uint16_t capable_100base_t4:1;
		uint16_t capable_100base_x_full:1;
		uint16_t capable_100base_x_half:1;
		uint16_t capable_10_full:1;
		uint16_t capable_10_half:1;
		uint16_t capable_100base_t2_full:1;
		uint16_t capable_100base_t2_half:1;
		uint16_t capable_extended_status:1;
		uint16_t capable_unidirectional:1;
		uint16_t capable_mf_preamble_suppression:1;
		uint16_t autoneg_complete:1;
		uint16_t remote_fault:1;
		uint16_t capable_autoneg:1;
		uint16_t link_status:1;
		uint16_t jabber_detect:1;
		uint16_t capable_extended_registers:1;

	} s;
} cvmx_mdio_phy_reg_status_t;

/**
 * PHY register 2 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_ID1 2
typedef union {
	uint16_t u16;
	struct {
		uint16_t oui_bits_3_18;
	} s;
} cvmx_mdio_phy_reg_id1_t;

/**
 * PHY register 3 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_ID2 3
typedef union {
	uint16_t u16;
	struct {
		uint16_t oui_bits_19_24:6;
		uint16_t model:6;
		uint16_t revision:4;
	} s;
} cvmx_mdio_phy_reg_id2_t;

/**
 * PHY register 4 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_AUTONEG_ADVER 4
typedef union {
	uint16_t u16;
	struct {
		uint16_t next_page:1;
		uint16_t reserved_14:1;
		uint16_t remote_fault:1;
		uint16_t reserved_12:1;
		uint16_t asymmetric_pause:1;
		uint16_t pause:1;
		uint16_t advert_100base_t4:1;
		uint16_t advert_100base_tx_full:1;
		uint16_t advert_100base_tx_half:1;
		uint16_t advert_10base_tx_full:1;
		uint16_t advert_10base_tx_half:1;
		uint16_t selector:5;
	} s;
} cvmx_mdio_phy_reg_autoneg_adver_t;

/**
 * PHY register 5 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_LINK_PARTNER_ABILITY 5
typedef union {
	uint16_t u16;
	struct {
		uint16_t next_page:1;
		uint16_t ack:1;
		uint16_t remote_fault:1;
		uint16_t reserved_12:1;
		uint16_t asymmetric_pause:1;
		uint16_t pause:1;
		uint16_t advert_100base_t4:1;
		uint16_t advert_100base_tx_full:1;
		uint16_t advert_100base_tx_half:1;
		uint16_t advert_10base_tx_full:1;
		uint16_t advert_10base_tx_half:1;
		uint16_t selector:5;
	} s;
} cvmx_mdio_phy_reg_link_partner_ability_t;

/**
 * PHY register 6 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_AUTONEG_EXPANSION 6
typedef union {
	uint16_t u16;
	struct {
		uint16_t reserved_5_15:11;
		uint16_t parallel_detection_fault:1;
		uint16_t link_partner_next_page_capable:1;
		uint16_t local_next_page_capable:1;
		uint16_t page_received:1;
		uint16_t link_partner_autoneg_capable:1;

	} s;
} cvmx_mdio_phy_reg_autoneg_expansion_t;

/**
 * PHY register 9 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_CONTROL_1000 9
typedef union {
	uint16_t u16;
	struct {
		uint16_t test_mode:3;
		uint16_t manual_master_slave:1;
		uint16_t master:1;
		uint16_t port_type:1;
		uint16_t advert_1000base_t_full:1;
		uint16_t advert_1000base_t_half:1;
		uint16_t reserved_0_7:8;
	} s;
} cvmx_mdio_phy_reg_control_1000_t;

/**
 * PHY register 10 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_STATUS_1000 10
typedef union {
	uint16_t u16;
	struct {
		uint16_t master_slave_fault:1;
		uint16_t is_master:1;
		uint16_t local_receiver_ok:1;
		uint16_t remote_receiver_ok:1;
		uint16_t remote_capable_1000base_t_full:1;
		uint16_t remote_capable_1000base_t_half:1;
		uint16_t reserved_8_9:2;
		uint16_t idle_error_count:8;
	} s;
} cvmx_mdio_phy_reg_status_1000_t;

/**
 * PHY register 15 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_EXTENDED_STATUS 15
typedef union {
	uint16_t u16;
	struct {
		uint16_t capable_1000base_x_full:1;
		uint16_t capable_1000base_x_half:1;
		uint16_t capable_1000base_t_full:1;
		uint16_t capable_1000base_t_half:1;
		uint16_t reserved_0_11:12;
	} s;
} cvmx_mdio_phy_reg_extended_status_t;

/**
 * PHY register 13 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_MMD_CONTROL 13
typedef union {
	uint16_t u16;
	struct {
		uint16_t function:2;
		uint16_t reserved_5_13:9;
		uint16_t devad:5;
	} s;
} cvmx_mdio_phy_reg_mmd_control_t;

/**
 * PHY register 14 from the 802.3 spec
 */
#define CVMX_MDIO_PHY_REG_MMD_ADDRESS_DATA 14
typedef union {
	uint16_t u16;
	struct {
		uint16_t address_data:16;
	} s;
} cvmx_mdio_phy_reg_mmd_address_data_t;

/* Operating request encodings. */
#define MDIO_CLAUSE_22_WRITE    0
#define MDIO_CLAUSE_22_READ     1

#define MDIO_CLAUSE_45_ADDRESS  0
#define MDIO_CLAUSE_45_WRITE    1
#define MDIO_CLAUSE_45_READ_INC 2
#define MDIO_CLAUSE_45_READ     3

/* MMD identifiers, mostly for accessing devices within XENPAK modules. */
#define CVMX_MMD_DEVICE_PMA_PMD      1
#define CVMX_MMD_DEVICE_WIS          2
#define CVMX_MMD_DEVICE_PCS          3
#define CVMX_MMD_DEVICE_PHY_XS       4
#define CVMX_MMD_DEVICE_DTS_XS       5
#define CVMX_MMD_DEVICE_TC           6
#define CVMX_MMD_DEVICE_CL22_EXT     29
#define CVMX_MMD_DEVICE_VENDOR_1     30
#define CVMX_MMD_DEVICE_VENDOR_2     31

/* Helper function to put MDIO interface into clause 45 mode */
static inline void __cvmx_mdio_set_clause45_mode(int bus_id)
{
	union cvmx_smix_clk smi_clk;
	/* Put bus into clause 45 mode */
	smi_clk.u64 = cvmx_read_csr(CVMX_SMIX_CLK(bus_id));
	smi_clk.s.mode = 1;
	smi_clk.s.preamble = 1;
	cvmx_write_csr(CVMX_SMIX_CLK(bus_id), smi_clk.u64);
}

/* Helper function to put MDIO interface into clause 22 mode */
static inline void __cvmx_mdio_set_clause22_mode(int bus_id)
{
	union cvmx_smix_clk smi_clk;
	/* Put bus into clause 22 mode */
	smi_clk.u64 = cvmx_read_csr(CVMX_SMIX_CLK(bus_id));
	smi_clk.s.mode = 0;
	cvmx_write_csr(CVMX_SMIX_CLK(bus_id), smi_clk.u64);
}

/**
 * Perform an MII read. This function is used to read PHY
 * registers controlling auto negotiation.
 *
 * @bus_id:   MDIO bus number. Zero on most chips, but some chips (ex CN56XX)
 *                 support multiple busses.
 * @phy_id:   The MII phy id
 * @location: Register location to read
 *
 * Returns Result from the read or -1 on failure
 */
static inline int cvmx_mdio_read(int bus_id, int phy_id, int location)
{
	union cvmx_smix_cmd smi_cmd;
	union cvmx_smix_rd_dat smi_rd;
	int timeout = 1000;

	if (octeon_has_feature(OCTEON_FEATURE_MDIO_CLAUSE_45))
		__cvmx_mdio_set_clause22_mode(bus_id);

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = MDIO_CLAUSE_22_READ;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = location;
	cvmx_write_csr(CVMX_SMIX_CMD(bus_id), smi_cmd.u64);

	do {
		cvmx_wait(1000);
		smi_rd.u64 = cvmx_read_csr(CVMX_SMIX_RD_DAT(bus_id));
	} while (smi_rd.s.pending && timeout--);

	if (smi_rd.s.val)
		return smi_rd.s.dat;
	else
		return -1;
}

/**
 * Perform an MII write. This function is used to write PHY
 * registers controlling auto negotiation.
 *
 * @bus_id:   MDIO bus number. Zero on most chips, but some chips (ex CN56XX)
 *                 support multiple busses.
 * @phy_id:   The MII phy id
 * @location: Register location to write
 * @val:      Value to write
 *
 * Returns -1 on error
 *         0 on success
 */
static inline int cvmx_mdio_write(int bus_id, int phy_id, int location, int val)
{
	union cvmx_smix_cmd smi_cmd;
	union cvmx_smix_wr_dat smi_wr;
	int timeout = 1000;

	if (octeon_has_feature(OCTEON_FEATURE_MDIO_CLAUSE_45))
		__cvmx_mdio_set_clause22_mode(bus_id);

	smi_wr.u64 = 0;
	smi_wr.s.dat = val;
	cvmx_write_csr(CVMX_SMIX_WR_DAT(bus_id), smi_wr.u64);

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = MDIO_CLAUSE_22_WRITE;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = location;
	cvmx_write_csr(CVMX_SMIX_CMD(bus_id), smi_cmd.u64);

	do {
		cvmx_wait(1000);
		smi_wr.u64 = cvmx_read_csr(CVMX_SMIX_WR_DAT(bus_id));
	} while (smi_wr.s.pending && --timeout);
	if (timeout <= 0)
		return -1;

	return 0;
}

/**
 * Perform an IEEE 802.3 clause 45 MII read. This function is used to
 * read PHY registers controlling auto negotiation.
 *
 * @bus_id:   MDIO bus number. Zero on most chips, but some chips (ex CN56XX)
 *                 support multiple busses.
 * @phy_id:   The MII phy id
 * @device:   MDIO Managable Device (MMD) id
 * @location: Register location to read
 *
 * Returns Result from the read or -1 on failure
 */

static inline int cvmx_mdio_45_read(int bus_id, int phy_id, int device,
				    int location)
{
	union cvmx_smix_cmd smi_cmd;
	union cvmx_smix_rd_dat smi_rd;
	union cvmx_smix_wr_dat smi_wr;
	int timeout = 1000;

	if (!octeon_has_feature(OCTEON_FEATURE_MDIO_CLAUSE_45))
		return -1;

	__cvmx_mdio_set_clause45_mode(bus_id);

	smi_wr.u64 = 0;
	smi_wr.s.dat = location;
	cvmx_write_csr(CVMX_SMIX_WR_DAT(bus_id), smi_wr.u64);

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = MDIO_CLAUSE_45_ADDRESS;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = device;
	cvmx_write_csr(CVMX_SMIX_CMD(bus_id), smi_cmd.u64);

	do {
		cvmx_wait(1000);
		smi_wr.u64 = cvmx_read_csr(CVMX_SMIX_WR_DAT(bus_id));
	} while (smi_wr.s.pending && --timeout);
	if (timeout <= 0) {
		cvmx_dprintf("cvmx_mdio_45_read: bus_id %d phy_id %2d "
			     "device %2d register %2d   TIME OUT(address)\n",
		     bus_id, phy_id, device, location);
		return -1;
	}

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = MDIO_CLAUSE_45_READ;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = device;
	cvmx_write_csr(CVMX_SMIX_CMD(bus_id), smi_cmd.u64);

	do {
		cvmx_wait(1000);
		smi_rd.u64 = cvmx_read_csr(CVMX_SMIX_RD_DAT(bus_id));
	} while (smi_rd.s.pending && --timeout);

	if (timeout <= 0) {
		cvmx_dprintf("cvmx_mdio_45_read: bus_id %d phy_id %2d "
			     "device %2d register %2d   TIME OUT(data)\n",
		     bus_id, phy_id, device, location);
		return -1;
	}

	if (smi_rd.s.val)
		return smi_rd.s.dat;
	else {
		cvmx_dprintf("cvmx_mdio_45_read: bus_id %d phy_id %2d "
			     "device %2d register %2d   INVALID READ\n",
		     bus_id, phy_id, device, location);
		return -1;
	}
}

/**
 * Perform an IEEE 802.3 clause 45 MII write. This function is used to
 * write PHY registers controlling auto negotiation.
 *
 * @bus_id:   MDIO bus number. Zero on most chips, but some chips (ex CN56XX)
 *                 support multiple busses.
 * @phy_id:   The MII phy id
 * @device:   MDIO Managable Device (MMD) id
 * @location: Register location to write
 * @val:      Value to write
 *
 * Returns -1 on error
 *         0 on success
 */
static inline int cvmx_mdio_45_write(int bus_id, int phy_id, int device,
				     int location, int val)
{
	union cvmx_smix_cmd smi_cmd;
	union cvmx_smix_wr_dat smi_wr;
	int timeout = 1000;

	if (!octeon_has_feature(OCTEON_FEATURE_MDIO_CLAUSE_45))
		return -1;

	__cvmx_mdio_set_clause45_mode(bus_id);

	smi_wr.u64 = 0;
	smi_wr.s.dat = location;
	cvmx_write_csr(CVMX_SMIX_WR_DAT(bus_id), smi_wr.u64);

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = MDIO_CLAUSE_45_ADDRESS;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = device;
	cvmx_write_csr(CVMX_SMIX_CMD(bus_id), smi_cmd.u64);

	do {
		cvmx_wait(1000);
		smi_wr.u64 = cvmx_read_csr(CVMX_SMIX_WR_DAT(bus_id));
	} while (smi_wr.s.pending && --timeout);
	if (timeout <= 0)
		return -1;

	smi_wr.u64 = 0;
	smi_wr.s.dat = val;
	cvmx_write_csr(CVMX_SMIX_WR_DAT(bus_id), smi_wr.u64);

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = MDIO_CLAUSE_45_WRITE;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = device;
	cvmx_write_csr(CVMX_SMIX_CMD(bus_id), smi_cmd.u64);

	do {
		cvmx_wait(1000);
		smi_wr.u64 = cvmx_read_csr(CVMX_SMIX_WR_DAT(bus_id));
	} while (smi_wr.s.pending && --timeout);
	if (timeout <= 0)
		return -1;

	return 0;
}

#endif
