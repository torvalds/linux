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
 * Functions for XAUI initialization, configuration,
 * and monitoring.
 *
 */

#include <asm/octeon/octeon.h>

#include <asm/octeon/cvmx-config.h>

#include <asm/octeon/cvmx-helper.h>

#include <asm/octeon/cvmx-pko-defs.h>
#include <asm/octeon/cvmx-gmxx-defs.h>
#include <asm/octeon/cvmx-pcsxx-defs.h>

void __cvmx_interrupt_gmxx_enable(int interface);
void __cvmx_interrupt_pcsx_intx_en_reg_enable(int index, int block);
void __cvmx_interrupt_pcsxx_int_en_reg_enable(int index);

int __cvmx_helper_xaui_enumerate(int interface)
{
	union cvmx_gmxx_hg2_control gmx_hg2_control;

	/* If HiGig2 is enabled return 16 ports, otherwise return 1 port */
	gmx_hg2_control.u64 = cvmx_read_csr(CVMX_GMXX_HG2_CONTROL(interface));
	if (gmx_hg2_control.s.hg2tx_en)
		return 16;
	else
		return 1;
}

/**
 * Probe a XAUI interface and determine the number of ports
 * connected to it. The XAUI interface should still be down
 * after this call.
 *
 * @interface: Interface to probe
 *
 * Returns Number of ports on the interface. Zero to disable.
 */
int __cvmx_helper_xaui_probe(int interface)
{
	int i;
	union cvmx_gmxx_inf_mode mode;

	/*
	 * Due to errata GMX-700 on CN56XXp1.x and CN52XXp1.x, the
	 * interface needs to be enabled before IPD otherwise per port
	 * backpressure may not work properly.
	 */
	mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));
	mode.s.en = 1;
	cvmx_write_csr(CVMX_GMXX_INF_MODE(interface), mode.u64);

	__cvmx_helper_setup_gmx(interface, 1);

	/*
	 * Setup PKO to support 16 ports for HiGig2 virtual
	 * ports. We're pointing all of the PKO packet ports for this
	 * interface to the XAUI. This allows us to use HiGig2
	 * backpressure per port.
	 */
	for (i = 0; i < 16; i++) {
		union cvmx_pko_mem_port_ptrs pko_mem_port_ptrs;
		pko_mem_port_ptrs.u64 = 0;
		/*
		 * We set each PKO port to have equal priority in a
		 * round robin fashion.
		 */
		pko_mem_port_ptrs.s.static_p = 0;
		pko_mem_port_ptrs.s.qos_mask = 0xff;
		/* All PKO ports map to the same XAUI hardware port */
		pko_mem_port_ptrs.s.eid = interface * 4;
		pko_mem_port_ptrs.s.pid = interface * 16 + i;
		cvmx_write_csr(CVMX_PKO_MEM_PORT_PTRS, pko_mem_port_ptrs.u64);
	}
	return __cvmx_helper_xaui_enumerate(interface);
}

/**
 * Bringup and enable a XAUI interface. After this call packet
 * I/O should be fully functional. This is called with IPD
 * enabled but PKO disabled.
 *
 * @interface: Interface to bring up
 *
 * Returns Zero on success, negative on failure
 */
int __cvmx_helper_xaui_enable(int interface)
{
	union cvmx_gmxx_prtx_cfg gmx_cfg;
	union cvmx_pcsxx_control1_reg xauiCtl;
	union cvmx_pcsxx_misc_ctl_reg xauiMiscCtl;
	union cvmx_gmxx_tx_xaui_ctl gmxXauiTxCtl;
	union cvmx_gmxx_rxx_int_en gmx_rx_int_en;
	union cvmx_gmxx_tx_int_en gmx_tx_int_en;
	union cvmx_pcsxx_int_en_reg pcsx_int_en_reg;

	/* Setup PKND */
	if (octeon_has_feature(OCTEON_FEATURE_PKND)) {
		gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(0, interface));
		gmx_cfg.s.pknd = cvmx_helper_get_ipd_port(interface, 0);
		cvmx_write_csr(CVMX_GMXX_PRTX_CFG(0, interface), gmx_cfg.u64);
	}

	/* (1) Interface has already been enabled. */

	/* (2) Disable GMX. */
	xauiMiscCtl.u64 = cvmx_read_csr(CVMX_PCSXX_MISC_CTL_REG(interface));
	xauiMiscCtl.s.gmxeno = 1;
	cvmx_write_csr(CVMX_PCSXX_MISC_CTL_REG(interface), xauiMiscCtl.u64);

	/* (3) Disable GMX and PCSX interrupts. */
	gmx_rx_int_en.u64 = cvmx_read_csr(CVMX_GMXX_RXX_INT_EN(0, interface));
	cvmx_write_csr(CVMX_GMXX_RXX_INT_EN(0, interface), 0x0);
	gmx_tx_int_en.u64 = cvmx_read_csr(CVMX_GMXX_TX_INT_EN(interface));
	cvmx_write_csr(CVMX_GMXX_TX_INT_EN(interface), 0x0);
	pcsx_int_en_reg.u64 = cvmx_read_csr(CVMX_PCSXX_INT_EN_REG(interface));
	cvmx_write_csr(CVMX_PCSXX_INT_EN_REG(interface), 0x0);

	/* (4) Bring up the PCSX and GMX reconciliation layer. */
	/* (4)a Set polarity and lane swapping. */
	/* (4)b */
	gmxXauiTxCtl.u64 = cvmx_read_csr(CVMX_GMXX_TX_XAUI_CTL(interface));
	/* Enable better IFG packing and improves performance */
	gmxXauiTxCtl.s.dic_en = 1;
	gmxXauiTxCtl.s.uni_en = 0;
	cvmx_write_csr(CVMX_GMXX_TX_XAUI_CTL(interface), gmxXauiTxCtl.u64);

	/* (4)c Aply reset sequence */
	xauiCtl.u64 = cvmx_read_csr(CVMX_PCSXX_CONTROL1_REG(interface));
	xauiCtl.s.lo_pwr = 0;

	/* Issuing a reset here seems to hang some CN68XX chips. */
	if (!OCTEON_IS_MODEL(OCTEON_CN68XX_PASS1_X) &&
	    !OCTEON_IS_MODEL(OCTEON_CN68XX_PASS2_X))
		xauiCtl.s.reset = 1;

	cvmx_write_csr(CVMX_PCSXX_CONTROL1_REG(interface), xauiCtl.u64);

	/* Wait for PCS to come out of reset */
	if (CVMX_WAIT_FOR_FIELD64
	    (CVMX_PCSXX_CONTROL1_REG(interface), union cvmx_pcsxx_control1_reg,
	     reset, ==, 0, 10000))
		return -1;
	/* Wait for PCS to be aligned */
	if (CVMX_WAIT_FOR_FIELD64
	    (CVMX_PCSXX_10GBX_STATUS_REG(interface),
	     union cvmx_pcsxx_10gbx_status_reg, alignd, ==, 1, 10000))
		return -1;
	/* Wait for RX to be ready */
	if (CVMX_WAIT_FOR_FIELD64
	    (CVMX_GMXX_RX_XAUI_CTL(interface), union cvmx_gmxx_rx_xaui_ctl,
		    status, ==, 0, 10000))
		return -1;

	/* (6) Configure GMX */
	gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(0, interface));
	gmx_cfg.s.en = 0;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(0, interface), gmx_cfg.u64);

	/* Wait for GMX RX to be idle */
	if (CVMX_WAIT_FOR_FIELD64
	    (CVMX_GMXX_PRTX_CFG(0, interface), union cvmx_gmxx_prtx_cfg,
		    rx_idle, ==, 1, 10000))
		return -1;
	/* Wait for GMX TX to be idle */
	if (CVMX_WAIT_FOR_FIELD64
	    (CVMX_GMXX_PRTX_CFG(0, interface), union cvmx_gmxx_prtx_cfg,
		    tx_idle, ==, 1, 10000))
		return -1;

	/* GMX configure */
	gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(0, interface));
	gmx_cfg.s.speed = 1;
	gmx_cfg.s.speed_msb = 0;
	gmx_cfg.s.slottime = 1;
	cvmx_write_csr(CVMX_GMXX_TX_PRTS(interface), 1);
	cvmx_write_csr(CVMX_GMXX_TXX_SLOT(0, interface), 512);
	cvmx_write_csr(CVMX_GMXX_TXX_BURST(0, interface), 8192);
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(0, interface), gmx_cfg.u64);

	/* (7) Clear out any error state */
	cvmx_write_csr(CVMX_GMXX_RXX_INT_REG(0, interface),
		       cvmx_read_csr(CVMX_GMXX_RXX_INT_REG(0, interface)));
	cvmx_write_csr(CVMX_GMXX_TX_INT_REG(interface),
		       cvmx_read_csr(CVMX_GMXX_TX_INT_REG(interface)));
	cvmx_write_csr(CVMX_PCSXX_INT_REG(interface),
		       cvmx_read_csr(CVMX_PCSXX_INT_REG(interface)));

	/* Wait for receive link */
	if (CVMX_WAIT_FOR_FIELD64
	    (CVMX_PCSXX_STATUS1_REG(interface), union cvmx_pcsxx_status1_reg,
	     rcv_lnk, ==, 1, 10000))
		return -1;
	if (CVMX_WAIT_FOR_FIELD64
	    (CVMX_PCSXX_STATUS2_REG(interface), union cvmx_pcsxx_status2_reg,
	     xmtflt, ==, 0, 10000))
		return -1;
	if (CVMX_WAIT_FOR_FIELD64
	    (CVMX_PCSXX_STATUS2_REG(interface), union cvmx_pcsxx_status2_reg,
	     rcvflt, ==, 0, 10000))
		return -1;

	cvmx_write_csr(CVMX_GMXX_RXX_INT_EN(0, interface), gmx_rx_int_en.u64);
	cvmx_write_csr(CVMX_GMXX_TX_INT_EN(interface), gmx_tx_int_en.u64);
	cvmx_write_csr(CVMX_PCSXX_INT_EN_REG(interface), pcsx_int_en_reg.u64);

	/* (8) Enable packet reception */
	xauiMiscCtl.s.gmxeno = 0;
	cvmx_write_csr(CVMX_PCSXX_MISC_CTL_REG(interface), xauiMiscCtl.u64);

	gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(0, interface));
	gmx_cfg.s.en = 1;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(0, interface), gmx_cfg.u64);

	__cvmx_interrupt_pcsx_intx_en_reg_enable(0, interface);
	__cvmx_interrupt_pcsx_intx_en_reg_enable(1, interface);
	__cvmx_interrupt_pcsx_intx_en_reg_enable(2, interface);
	__cvmx_interrupt_pcsx_intx_en_reg_enable(3, interface);
	__cvmx_interrupt_pcsxx_int_en_reg_enable(interface);
	__cvmx_interrupt_gmxx_enable(interface);

	return 0;
}

/**
 * Return the link state of an IPD/PKO port as returned by
 * auto negotiation. The result of this function may not match
 * Octeon's link config if auto negotiation has changed since
 * the last call to cvmx_helper_link_set().
 *
 * @ipd_port: IPD/PKO port to query
 *
 * Returns Link state
 */
cvmx_helper_link_info_t __cvmx_helper_xaui_link_get(int ipd_port)
{
	int interface = cvmx_helper_get_interface_num(ipd_port);
	union cvmx_gmxx_tx_xaui_ctl gmxx_tx_xaui_ctl;
	union cvmx_gmxx_rx_xaui_ctl gmxx_rx_xaui_ctl;
	union cvmx_pcsxx_status1_reg pcsxx_status1_reg;
	cvmx_helper_link_info_t result;

	gmxx_tx_xaui_ctl.u64 = cvmx_read_csr(CVMX_GMXX_TX_XAUI_CTL(interface));
	gmxx_rx_xaui_ctl.u64 = cvmx_read_csr(CVMX_GMXX_RX_XAUI_CTL(interface));
	pcsxx_status1_reg.u64 =
	    cvmx_read_csr(CVMX_PCSXX_STATUS1_REG(interface));
	result.u64 = 0;

	/* Only return a link if both RX and TX are happy */
	if ((gmxx_tx_xaui_ctl.s.ls == 0) && (gmxx_rx_xaui_ctl.s.status == 0) &&
	    (pcsxx_status1_reg.s.rcv_lnk == 1)) {
		result.s.link_up = 1;
		result.s.full_duplex = 1;
		result.s.speed = 10000;
	} else {
		/* Disable GMX and PCSX interrupts. */
		cvmx_write_csr(CVMX_GMXX_RXX_INT_EN(0, interface), 0x0);
		cvmx_write_csr(CVMX_GMXX_TX_INT_EN(interface), 0x0);
		cvmx_write_csr(CVMX_PCSXX_INT_EN_REG(interface), 0x0);
	}
	return result;
}

/**
 * Configure an IPD/PKO port for the specified link state. This
 * function does not influence auto negotiation at the PHY level.
 * The passed link state must always match the link state returned
 * by cvmx_helper_link_get().
 *
 * @ipd_port:  IPD/PKO port to configure
 * @link_info: The new link state
 *
 * Returns Zero on success, negative on failure
 */
int __cvmx_helper_xaui_link_set(int ipd_port, cvmx_helper_link_info_t link_info)
{
	int interface = cvmx_helper_get_interface_num(ipd_port);
	union cvmx_gmxx_tx_xaui_ctl gmxx_tx_xaui_ctl;
	union cvmx_gmxx_rx_xaui_ctl gmxx_rx_xaui_ctl;

	gmxx_tx_xaui_ctl.u64 = cvmx_read_csr(CVMX_GMXX_TX_XAUI_CTL(interface));
	gmxx_rx_xaui_ctl.u64 = cvmx_read_csr(CVMX_GMXX_RX_XAUI_CTL(interface));

	/* If the link shouldn't be up, then just return */
	if (!link_info.s.link_up)
		return 0;

	/* Do nothing if both RX and TX are happy */
	if ((gmxx_tx_xaui_ctl.s.ls == 0) && (gmxx_rx_xaui_ctl.s.status == 0))
		return 0;

	/* Bring the link up */
	return __cvmx_helper_xaui_enable(interface);
}

/**
 * Configure a port for internal and/or external loopback. Internal loopback
 * causes packets sent by the port to be received by Octeon. External loopback
 * causes packets received from the wire to sent out again.
 *
 * @ipd_port: IPD/PKO port to loopback.
 * @enable_internal:
 *		   Non zero if you want internal loopback
 * @enable_external:
 *		   Non zero if you want external loopback
 *
 * Returns Zero on success, negative on failure.
 */
extern int __cvmx_helper_xaui_configure_loopback(int ipd_port,
						 int enable_internal,
						 int enable_external)
{
	int interface = cvmx_helper_get_interface_num(ipd_port);
	union cvmx_pcsxx_control1_reg pcsxx_control1_reg;
	union cvmx_gmxx_xaui_ext_loopback gmxx_xaui_ext_loopback;

	/* Set the internal loop */
	pcsxx_control1_reg.u64 =
	    cvmx_read_csr(CVMX_PCSXX_CONTROL1_REG(interface));
	pcsxx_control1_reg.s.loopbck1 = enable_internal;
	cvmx_write_csr(CVMX_PCSXX_CONTROL1_REG(interface),
		       pcsxx_control1_reg.u64);

	/* Set the external loop */
	gmxx_xaui_ext_loopback.u64 =
	    cvmx_read_csr(CVMX_GMXX_XAUI_EXT_LOOPBACK(interface));
	gmxx_xaui_ext_loopback.s.en = enable_external;
	cvmx_write_csr(CVMX_GMXX_XAUI_EXT_LOOPBACK(interface),
		       gmxx_xaui_ext_loopback.u64);

	/* Take the link through a reset */
	return __cvmx_helper_xaui_enable(interface);
}
