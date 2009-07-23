/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2009 Cavium Networks
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
 * Automatically generated functions useful for enabling
 * and decoding RSL_INT_BLOCKS interrupts.
 *
 */

#include <asm/octeon/octeon.h>

#include "cvmx-gmxx-defs.h"
#include "cvmx-pcsx-defs.h"
#include "cvmx-pcsxx-defs.h"
#include "cvmx-spxx-defs.h"
#include "cvmx-stxx-defs.h"

#ifndef PRINT_ERROR
#define PRINT_ERROR(format, ...)
#endif


/**
 * __cvmx_interrupt_gmxx_rxx_int_en_enable enables all interrupt bits in cvmx_gmxx_rxx_int_en_t
 */
void __cvmx_interrupt_gmxx_rxx_int_en_enable(int index, int block)
{
	union cvmx_gmxx_rxx_int_en gmx_rx_int_en;
	cvmx_write_csr(CVMX_GMXX_RXX_INT_REG(index, block),
		       cvmx_read_csr(CVMX_GMXX_RXX_INT_REG(index, block)));
	gmx_rx_int_en.u64 = 0;
	if (OCTEON_IS_MODEL(OCTEON_CN56XX)) {
		/* Skipping gmx_rx_int_en.s.reserved_29_63 */
		gmx_rx_int_en.s.hg2cc = 1;
		gmx_rx_int_en.s.hg2fld = 1;
		gmx_rx_int_en.s.undat = 1;
		gmx_rx_int_en.s.uneop = 1;
		gmx_rx_int_en.s.unsop = 1;
		gmx_rx_int_en.s.bad_term = 1;
		gmx_rx_int_en.s.bad_seq = 1;
		gmx_rx_int_en.s.rem_fault = 1;
		gmx_rx_int_en.s.loc_fault = 1;
		gmx_rx_int_en.s.pause_drp = 1;
		/* Skipping gmx_rx_int_en.s.reserved_16_18 */
		/*gmx_rx_int_en.s.ifgerr = 1; */
		/*gmx_rx_int_en.s.coldet = 1; // Collsion detect */
		/*gmx_rx_int_en.s.falerr = 1; // False carrier error or extend error after slottime */
		/*gmx_rx_int_en.s.rsverr = 1; // RGMII reserved opcodes */
		/*gmx_rx_int_en.s.pcterr = 1; // Bad Preamble / Protocol */
		gmx_rx_int_en.s.ovrerr = 1;
		/* Skipping gmx_rx_int_en.s.reserved_9_9 */
		gmx_rx_int_en.s.skperr = 1;
		gmx_rx_int_en.s.rcverr = 1;
		/* Skipping gmx_rx_int_en.s.reserved_5_6 */
		/*gmx_rx_int_en.s.fcserr = 1; // FCS errors are handled when we get work */
		gmx_rx_int_en.s.jabber = 1;
		/* Skipping gmx_rx_int_en.s.reserved_2_2 */
		gmx_rx_int_en.s.carext = 1;
		/* Skipping gmx_rx_int_en.s.reserved_0_0 */
	}
	if (OCTEON_IS_MODEL(OCTEON_CN30XX)) {
		/* Skipping gmx_rx_int_en.s.reserved_19_63 */
		/*gmx_rx_int_en.s.phy_dupx = 1; */
		/*gmx_rx_int_en.s.phy_spd = 1; */
		/*gmx_rx_int_en.s.phy_link = 1; */
		/*gmx_rx_int_en.s.ifgerr = 1; */
		/*gmx_rx_int_en.s.coldet = 1; // Collsion detect */
		/*gmx_rx_int_en.s.falerr = 1; // False carrier error or extend error after slottime */
		/*gmx_rx_int_en.s.rsverr = 1; // RGMII reserved opcodes */
		/*gmx_rx_int_en.s.pcterr = 1; // Bad Preamble / Protocol */
		gmx_rx_int_en.s.ovrerr = 1;
		gmx_rx_int_en.s.niberr = 1;
		gmx_rx_int_en.s.skperr = 1;
		gmx_rx_int_en.s.rcverr = 1;
		/*gmx_rx_int_en.s.lenerr = 1; // Length errors are handled when we get work */
		gmx_rx_int_en.s.alnerr = 1;
		/*gmx_rx_int_en.s.fcserr = 1; // FCS errors are handled when we get work */
		gmx_rx_int_en.s.jabber = 1;
		gmx_rx_int_en.s.maxerr = 1;
		gmx_rx_int_en.s.carext = 1;
		gmx_rx_int_en.s.minerr = 1;
	}
	if (OCTEON_IS_MODEL(OCTEON_CN50XX)) {
		/* Skipping gmx_rx_int_en.s.reserved_20_63 */
		gmx_rx_int_en.s.pause_drp = 1;
		/*gmx_rx_int_en.s.phy_dupx = 1; */
		/*gmx_rx_int_en.s.phy_spd = 1; */
		/*gmx_rx_int_en.s.phy_link = 1; */
		/*gmx_rx_int_en.s.ifgerr = 1; */
		/*gmx_rx_int_en.s.coldet = 1; // Collsion detect */
		/*gmx_rx_int_en.s.falerr = 1; // False carrier error or extend error after slottime */
		/*gmx_rx_int_en.s.rsverr = 1; // RGMII reserved opcodes */
		/*gmx_rx_int_en.s.pcterr = 1; // Bad Preamble / Protocol */
		gmx_rx_int_en.s.ovrerr = 1;
		gmx_rx_int_en.s.niberr = 1;
		gmx_rx_int_en.s.skperr = 1;
		gmx_rx_int_en.s.rcverr = 1;
		/* Skipping gmx_rx_int_en.s.reserved_6_6 */
		gmx_rx_int_en.s.alnerr = 1;
		/*gmx_rx_int_en.s.fcserr = 1; // FCS errors are handled when we get work */
		gmx_rx_int_en.s.jabber = 1;
		/* Skipping gmx_rx_int_en.s.reserved_2_2 */
		gmx_rx_int_en.s.carext = 1;
		/* Skipping gmx_rx_int_en.s.reserved_0_0 */
	}
	if (OCTEON_IS_MODEL(OCTEON_CN38XX)) {
		/* Skipping gmx_rx_int_en.s.reserved_19_63 */
		/*gmx_rx_int_en.s.phy_dupx = 1; */
		/*gmx_rx_int_en.s.phy_spd = 1; */
		/*gmx_rx_int_en.s.phy_link = 1; */
		/*gmx_rx_int_en.s.ifgerr = 1; */
		/*gmx_rx_int_en.s.coldet = 1; // Collsion detect */
		/*gmx_rx_int_en.s.falerr = 1; // False carrier error or extend error after slottime */
		/*gmx_rx_int_en.s.rsverr = 1; // RGMII reserved opcodes */
		/*gmx_rx_int_en.s.pcterr = 1; // Bad Preamble / Protocol */
		gmx_rx_int_en.s.ovrerr = 1;
		gmx_rx_int_en.s.niberr = 1;
		gmx_rx_int_en.s.skperr = 1;
		gmx_rx_int_en.s.rcverr = 1;
		/*gmx_rx_int_en.s.lenerr = 1; // Length errors are handled when we get work */
		gmx_rx_int_en.s.alnerr = 1;
		/*gmx_rx_int_en.s.fcserr = 1; // FCS errors are handled when we get work */
		gmx_rx_int_en.s.jabber = 1;
		gmx_rx_int_en.s.maxerr = 1;
		gmx_rx_int_en.s.carext = 1;
		gmx_rx_int_en.s.minerr = 1;
	}
	if (OCTEON_IS_MODEL(OCTEON_CN31XX)) {
		/* Skipping gmx_rx_int_en.s.reserved_19_63 */
		/*gmx_rx_int_en.s.phy_dupx = 1; */
		/*gmx_rx_int_en.s.phy_spd = 1; */
		/*gmx_rx_int_en.s.phy_link = 1; */
		/*gmx_rx_int_en.s.ifgerr = 1; */
		/*gmx_rx_int_en.s.coldet = 1; // Collsion detect */
		/*gmx_rx_int_en.s.falerr = 1; // False carrier error or extend error after slottime */
		/*gmx_rx_int_en.s.rsverr = 1; // RGMII reserved opcodes */
		/*gmx_rx_int_en.s.pcterr = 1; // Bad Preamble / Protocol */
		gmx_rx_int_en.s.ovrerr = 1;
		gmx_rx_int_en.s.niberr = 1;
		gmx_rx_int_en.s.skperr = 1;
		gmx_rx_int_en.s.rcverr = 1;
		/*gmx_rx_int_en.s.lenerr = 1; // Length errors are handled when we get work */
		gmx_rx_int_en.s.alnerr = 1;
		/*gmx_rx_int_en.s.fcserr = 1; // FCS errors are handled when we get work */
		gmx_rx_int_en.s.jabber = 1;
		gmx_rx_int_en.s.maxerr = 1;
		gmx_rx_int_en.s.carext = 1;
		gmx_rx_int_en.s.minerr = 1;
	}
	if (OCTEON_IS_MODEL(OCTEON_CN58XX)) {
		/* Skipping gmx_rx_int_en.s.reserved_20_63 */
		gmx_rx_int_en.s.pause_drp = 1;
		/*gmx_rx_int_en.s.phy_dupx = 1; */
		/*gmx_rx_int_en.s.phy_spd = 1; */
		/*gmx_rx_int_en.s.phy_link = 1; */
		/*gmx_rx_int_en.s.ifgerr = 1; */
		/*gmx_rx_int_en.s.coldet = 1; // Collsion detect */
		/*gmx_rx_int_en.s.falerr = 1; // False carrier error or extend error after slottime */
		/*gmx_rx_int_en.s.rsverr = 1; // RGMII reserved opcodes */
		/*gmx_rx_int_en.s.pcterr = 1; // Bad Preamble / Protocol */
		gmx_rx_int_en.s.ovrerr = 1;
		gmx_rx_int_en.s.niberr = 1;
		gmx_rx_int_en.s.skperr = 1;
		gmx_rx_int_en.s.rcverr = 1;
		/*gmx_rx_int_en.s.lenerr = 1; // Length errors are handled when we get work */
		gmx_rx_int_en.s.alnerr = 1;
		/*gmx_rx_int_en.s.fcserr = 1; // FCS errors are handled when we get work */
		gmx_rx_int_en.s.jabber = 1;
		gmx_rx_int_en.s.maxerr = 1;
		gmx_rx_int_en.s.carext = 1;
		gmx_rx_int_en.s.minerr = 1;
	}
	if (OCTEON_IS_MODEL(OCTEON_CN52XX)) {
		/* Skipping gmx_rx_int_en.s.reserved_29_63 */
		gmx_rx_int_en.s.hg2cc = 1;
		gmx_rx_int_en.s.hg2fld = 1;
		gmx_rx_int_en.s.undat = 1;
		gmx_rx_int_en.s.uneop = 1;
		gmx_rx_int_en.s.unsop = 1;
		gmx_rx_int_en.s.bad_term = 1;
		gmx_rx_int_en.s.bad_seq = 0;
		gmx_rx_int_en.s.rem_fault = 1;
		gmx_rx_int_en.s.loc_fault = 0;
		gmx_rx_int_en.s.pause_drp = 1;
		/* Skipping gmx_rx_int_en.s.reserved_16_18 */
		/*gmx_rx_int_en.s.ifgerr = 1; */
		/*gmx_rx_int_en.s.coldet = 1; // Collsion detect */
		/*gmx_rx_int_en.s.falerr = 1; // False carrier error or extend error after slottime */
		/*gmx_rx_int_en.s.rsverr = 1; // RGMII reserved opcodes */
		/*gmx_rx_int_en.s.pcterr = 1; // Bad Preamble / Protocol */
		gmx_rx_int_en.s.ovrerr = 1;
		/* Skipping gmx_rx_int_en.s.reserved_9_9 */
		gmx_rx_int_en.s.skperr = 1;
		gmx_rx_int_en.s.rcverr = 1;
		/* Skipping gmx_rx_int_en.s.reserved_5_6 */
		/*gmx_rx_int_en.s.fcserr = 1; // FCS errors are handled when we get work */
		gmx_rx_int_en.s.jabber = 1;
		/* Skipping gmx_rx_int_en.s.reserved_2_2 */
		gmx_rx_int_en.s.carext = 1;
		/* Skipping gmx_rx_int_en.s.reserved_0_0 */
	}
	cvmx_write_csr(CVMX_GMXX_RXX_INT_EN(index, block), gmx_rx_int_en.u64);
}
/**
 * __cvmx_interrupt_pcsx_intx_en_reg_enable enables all interrupt bits in cvmx_pcsx_intx_en_reg_t
 */
void __cvmx_interrupt_pcsx_intx_en_reg_enable(int index, int block)
{
	union cvmx_pcsx_intx_en_reg pcs_int_en_reg;
	cvmx_write_csr(CVMX_PCSX_INTX_REG(index, block),
		       cvmx_read_csr(CVMX_PCSX_INTX_REG(index, block)));
	pcs_int_en_reg.u64 = 0;
	if (OCTEON_IS_MODEL(OCTEON_CN56XX)) {
		/* Skipping pcs_int_en_reg.s.reserved_12_63 */
		/*pcs_int_en_reg.s.dup = 1; // This happens during normal operation */
		pcs_int_en_reg.s.sync_bad_en = 1;
		pcs_int_en_reg.s.an_bad_en = 1;
		pcs_int_en_reg.s.rxlock_en = 1;
		pcs_int_en_reg.s.rxbad_en = 1;
		/*pcs_int_en_reg.s.rxerr_en = 1; // This happens during normal operation */
		pcs_int_en_reg.s.txbad_en = 1;
		pcs_int_en_reg.s.txfifo_en = 1;
		pcs_int_en_reg.s.txfifu_en = 1;
		pcs_int_en_reg.s.an_err_en = 1;
		/*pcs_int_en_reg.s.xmit_en = 1; // This happens during normal operation */
		/*pcs_int_en_reg.s.lnkspd_en = 1; // This happens during normal operation */
	}
	if (OCTEON_IS_MODEL(OCTEON_CN52XX)) {
		/* Skipping pcs_int_en_reg.s.reserved_12_63 */
		/*pcs_int_en_reg.s.dup = 1; // This happens during normal operation */
		pcs_int_en_reg.s.sync_bad_en = 1;
		pcs_int_en_reg.s.an_bad_en = 1;
		pcs_int_en_reg.s.rxlock_en = 1;
		pcs_int_en_reg.s.rxbad_en = 1;
		/*pcs_int_en_reg.s.rxerr_en = 1; // This happens during normal operation */
		pcs_int_en_reg.s.txbad_en = 1;
		pcs_int_en_reg.s.txfifo_en = 1;
		pcs_int_en_reg.s.txfifu_en = 1;
		pcs_int_en_reg.s.an_err_en = 1;
		/*pcs_int_en_reg.s.xmit_en = 1; // This happens during normal operation */
		/*pcs_int_en_reg.s.lnkspd_en = 1; // This happens during normal operation */
	}
	cvmx_write_csr(CVMX_PCSX_INTX_EN_REG(index, block), pcs_int_en_reg.u64);
}
/**
 * __cvmx_interrupt_pcsxx_int_en_reg_enable enables all interrupt bits in cvmx_pcsxx_int_en_reg_t
 */
void __cvmx_interrupt_pcsxx_int_en_reg_enable(int index)
{
	union cvmx_pcsxx_int_en_reg pcsx_int_en_reg;
	cvmx_write_csr(CVMX_PCSXX_INT_REG(index),
		       cvmx_read_csr(CVMX_PCSXX_INT_REG(index)));
	pcsx_int_en_reg.u64 = 0;
	if (OCTEON_IS_MODEL(OCTEON_CN56XX)) {
		/* Skipping pcsx_int_en_reg.s.reserved_6_63 */
		pcsx_int_en_reg.s.algnlos_en = 1;
		pcsx_int_en_reg.s.synlos_en = 1;
		pcsx_int_en_reg.s.bitlckls_en = 1;
		pcsx_int_en_reg.s.rxsynbad_en = 1;
		pcsx_int_en_reg.s.rxbad_en = 1;
		pcsx_int_en_reg.s.txflt_en = 1;
	}
	if (OCTEON_IS_MODEL(OCTEON_CN52XX)) {
		/* Skipping pcsx_int_en_reg.s.reserved_6_63 */
		pcsx_int_en_reg.s.algnlos_en = 1;
		pcsx_int_en_reg.s.synlos_en = 1;
		pcsx_int_en_reg.s.bitlckls_en = 0;	/* Happens if XAUI module is not installed */
		pcsx_int_en_reg.s.rxsynbad_en = 1;
		pcsx_int_en_reg.s.rxbad_en = 1;
		pcsx_int_en_reg.s.txflt_en = 1;
	}
	cvmx_write_csr(CVMX_PCSXX_INT_EN_REG(index), pcsx_int_en_reg.u64);
}

/**
 * __cvmx_interrupt_spxx_int_msk_enable enables all interrupt bits in cvmx_spxx_int_msk_t
 */
void __cvmx_interrupt_spxx_int_msk_enable(int index)
{
	union cvmx_spxx_int_msk spx_int_msk;
	cvmx_write_csr(CVMX_SPXX_INT_REG(index),
		       cvmx_read_csr(CVMX_SPXX_INT_REG(index)));
	spx_int_msk.u64 = 0;
	if (OCTEON_IS_MODEL(OCTEON_CN38XX)) {
		/* Skipping spx_int_msk.s.reserved_12_63 */
		spx_int_msk.s.calerr = 1;
		spx_int_msk.s.syncerr = 1;
		spx_int_msk.s.diperr = 1;
		spx_int_msk.s.tpaovr = 1;
		spx_int_msk.s.rsverr = 1;
		spx_int_msk.s.drwnng = 1;
		spx_int_msk.s.clserr = 1;
		spx_int_msk.s.spiovr = 1;
		/* Skipping spx_int_msk.s.reserved_2_3 */
		spx_int_msk.s.abnorm = 1;
		spx_int_msk.s.prtnxa = 1;
	}
	if (OCTEON_IS_MODEL(OCTEON_CN58XX)) {
		/* Skipping spx_int_msk.s.reserved_12_63 */
		spx_int_msk.s.calerr = 1;
		spx_int_msk.s.syncerr = 1;
		spx_int_msk.s.diperr = 1;
		spx_int_msk.s.tpaovr = 1;
		spx_int_msk.s.rsverr = 1;
		spx_int_msk.s.drwnng = 1;
		spx_int_msk.s.clserr = 1;
		spx_int_msk.s.spiovr = 1;
		/* Skipping spx_int_msk.s.reserved_2_3 */
		spx_int_msk.s.abnorm = 1;
		spx_int_msk.s.prtnxa = 1;
	}
	cvmx_write_csr(CVMX_SPXX_INT_MSK(index), spx_int_msk.u64);
}
/**
 * __cvmx_interrupt_stxx_int_msk_enable enables all interrupt bits in cvmx_stxx_int_msk_t
 */
void __cvmx_interrupt_stxx_int_msk_enable(int index)
{
	union cvmx_stxx_int_msk stx_int_msk;
	cvmx_write_csr(CVMX_STXX_INT_REG(index),
		       cvmx_read_csr(CVMX_STXX_INT_REG(index)));
	stx_int_msk.u64 = 0;
	if (OCTEON_IS_MODEL(OCTEON_CN38XX)) {
		/* Skipping stx_int_msk.s.reserved_8_63 */
		stx_int_msk.s.frmerr = 1;
		stx_int_msk.s.unxfrm = 1;
		stx_int_msk.s.nosync = 1;
		stx_int_msk.s.diperr = 1;
		stx_int_msk.s.datovr = 1;
		stx_int_msk.s.ovrbst = 1;
		stx_int_msk.s.calpar1 = 1;
		stx_int_msk.s.calpar0 = 1;
	}
	if (OCTEON_IS_MODEL(OCTEON_CN58XX)) {
		/* Skipping stx_int_msk.s.reserved_8_63 */
		stx_int_msk.s.frmerr = 1;
		stx_int_msk.s.unxfrm = 1;
		stx_int_msk.s.nosync = 1;
		stx_int_msk.s.diperr = 1;
		stx_int_msk.s.datovr = 1;
		stx_int_msk.s.ovrbst = 1;
		stx_int_msk.s.calpar1 = 1;
		stx_int_msk.s.calpar0 = 1;
	}
	cvmx_write_csr(CVMX_STXX_INT_MSK(index), stx_int_msk.u64);
}
