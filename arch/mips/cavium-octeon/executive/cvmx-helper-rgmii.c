/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (C) 2003-2018 Cavium, Inc.
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
 * Functions for RGMII/GMII/MII initialization, configuration,
 * and monitoring.
 */
#include <asm/octeon/octeon.h>

#include <asm/octeon/cvmx-config.h>

#include <asm/octeon/cvmx-pko.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-board.h>

#include <asm/octeon/cvmx-npi-defs.h>
#include <asm/octeon/cvmx-gmxx-defs.h>
#include <asm/octeon/cvmx-asxx-defs.h>
#include <asm/octeon/cvmx-dbg-defs.h>

/*
 * Probe RGMII ports and determine the number present
 *
 * @interface: Interface to probe
 *
 * Returns Number of RGMII/GMII/MII ports (0-4).
 */
int __cvmx_helper_rgmii_probe(int interface)
{
	int num_ports = 0;
	union cvmx_gmxx_inf_mode mode;
	mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));

	if (mode.s.type) {
		if (OCTEON_IS_MODEL(OCTEON_CN38XX)
		    || OCTEON_IS_MODEL(OCTEON_CN58XX)) {
			cvmx_dprintf("ERROR: RGMII initialize called in "
				     "SPI interface\n");
		} else if (OCTEON_IS_MODEL(OCTEON_CN31XX)
			   || OCTEON_IS_MODEL(OCTEON_CN30XX)
			   || OCTEON_IS_MODEL(OCTEON_CN50XX)) {
			/*
			 * On these chips "type" says we're in
			 * GMII/MII mode. This limits us to 2 ports
			 */
			num_ports = 2;
		} else {
			cvmx_dprintf("ERROR: Unsupported Octeon model in %s\n",
				     __func__);
		}
	} else {
		if (OCTEON_IS_MODEL(OCTEON_CN38XX)
		    || OCTEON_IS_MODEL(OCTEON_CN58XX)) {
			num_ports = 4;
		} else if (OCTEON_IS_MODEL(OCTEON_CN31XX)
			   || OCTEON_IS_MODEL(OCTEON_CN30XX)
			   || OCTEON_IS_MODEL(OCTEON_CN50XX)) {
			num_ports = 3;
		} else {
			cvmx_dprintf("ERROR: Unsupported Octeon model in %s\n",
				     __func__);
		}
	}
	return num_ports;
}

/*
 * Put an RGMII interface in loopback mode. Internal packets sent
 * out will be received back again on the same port. Externally
 * received packets will echo back out.
 *
 * @port:   IPD port number to loop.
 */
void cvmx_helper_rgmii_internal_loopback(int port)
{
	int interface = (port >> 4) & 1;
	int index = port & 0xf;
	uint64_t tmp;

	union cvmx_gmxx_prtx_cfg gmx_cfg;
	gmx_cfg.u64 = 0;
	gmx_cfg.s.duplex = 1;
	gmx_cfg.s.slottime = 1;
	gmx_cfg.s.speed = 1;
	cvmx_write_csr(CVMX_GMXX_TXX_CLK(index, interface), 1);
	cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 0x200);
	cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0x2000);
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmx_cfg.u64);
	tmp = cvmx_read_csr(CVMX_ASXX_PRT_LOOP(interface));
	cvmx_write_csr(CVMX_ASXX_PRT_LOOP(interface), (1 << index) | tmp);
	tmp = cvmx_read_csr(CVMX_ASXX_TX_PRT_EN(interface));
	cvmx_write_csr(CVMX_ASXX_TX_PRT_EN(interface), (1 << index) | tmp);
	tmp = cvmx_read_csr(CVMX_ASXX_RX_PRT_EN(interface));
	cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(interface), (1 << index) | tmp);
	gmx_cfg.s.en = 1;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmx_cfg.u64);
}

/*
 * Workaround ASX setup errata with CN38XX pass1
 *
 * @interface: Interface to setup
 * @port:      Port to setup (0..3)
 * @cpu_clock_hz:
 *		    Chip frequency in Hertz
 *
 * Returns Zero on success, negative on failure
 */
static int __cvmx_helper_errata_asx_pass1(int interface, int port,
					  int cpu_clock_hz)
{
	/* Set hi water mark as per errata GMX-4 */
	if (cpu_clock_hz >= 325000000 && cpu_clock_hz < 375000000)
		cvmx_write_csr(CVMX_ASXX_TX_HI_WATERX(port, interface), 12);
	else if (cpu_clock_hz >= 375000000 && cpu_clock_hz < 437000000)
		cvmx_write_csr(CVMX_ASXX_TX_HI_WATERX(port, interface), 11);
	else if (cpu_clock_hz >= 437000000 && cpu_clock_hz < 550000000)
		cvmx_write_csr(CVMX_ASXX_TX_HI_WATERX(port, interface), 10);
	else if (cpu_clock_hz >= 550000000 && cpu_clock_hz < 687000000)
		cvmx_write_csr(CVMX_ASXX_TX_HI_WATERX(port, interface), 9);
	else
		cvmx_dprintf("Illegal clock frequency (%d). "
			"CVMX_ASXX_TX_HI_WATERX not set\n", cpu_clock_hz);
	return 0;
}

/*
 * Configure all of the ASX, GMX, and PKO registers required
 * to get RGMII to function on the supplied interface.
 *
 * @interface: PKO Interface to configure (0 or 1)
 *
 * Returns Zero on success
 */
int __cvmx_helper_rgmii_enable(int interface)
{
	int num_ports = cvmx_helper_ports_on_interface(interface);
	int port;
	struct cvmx_sysinfo *sys_info_ptr = cvmx_sysinfo_get();
	union cvmx_gmxx_inf_mode mode;
	union cvmx_asxx_tx_prt_en asx_tx;
	union cvmx_asxx_rx_prt_en asx_rx;

	mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));

	if (mode.s.en == 0)
		return -1;
	if ((OCTEON_IS_MODEL(OCTEON_CN38XX) ||
	     OCTEON_IS_MODEL(OCTEON_CN58XX)) && mode.s.type == 1)
		/* Ignore SPI interfaces */
		return -1;

	/* Configure the ASX registers needed to use the RGMII ports */
	asx_tx.u64 = 0;
	asx_tx.s.prt_en = cvmx_build_mask(num_ports);
	cvmx_write_csr(CVMX_ASXX_TX_PRT_EN(interface), asx_tx.u64);

	asx_rx.u64 = 0;
	asx_rx.s.prt_en = cvmx_build_mask(num_ports);
	cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(interface), asx_rx.u64);

	/* Configure the GMX registers needed to use the RGMII ports */
	for (port = 0; port < num_ports; port++) {
		/* Setting of CVMX_GMXX_TXX_THRESH has been moved to
		   __cvmx_helper_setup_gmx() */

		if (cvmx_octeon_is_pass1())
			__cvmx_helper_errata_asx_pass1(interface, port,
						       sys_info_ptr->
						       cpu_clock_hz);
		else {
			/*
			 * Configure more flexible RGMII preamble
			 * checking. Pass 1 doesn't support this
			 * feature.
			 */
			union cvmx_gmxx_rxx_frm_ctl frm_ctl;
			frm_ctl.u64 =
			    cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL
					  (port, interface));
			/* New field, so must be compile time */
			frm_ctl.s.pre_free = 1;
			cvmx_write_csr(CVMX_GMXX_RXX_FRM_CTL(port, interface),
				       frm_ctl.u64);
		}

		/*
		 * Each pause frame transmitted will ask for about 10M
		 * bit times before resume.  If buffer space comes
		 * available before that time has expired, an XON
		 * pause frame (0 time) will be transmitted to restart
		 * the flow.
		 */
		cvmx_write_csr(CVMX_GMXX_TXX_PAUSE_PKT_TIME(port, interface),
			       20000);
		cvmx_write_csr(CVMX_GMXX_TXX_PAUSE_PKT_INTERVAL
			       (port, interface), 19000);

		if (OCTEON_IS_MODEL(OCTEON_CN50XX)) {
			cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(port, interface),
				       16);
			cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(port, interface),
				       16);
		} else {
			cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(port, interface),
				       24);
			cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(port, interface),
				       24);
		}
	}

	__cvmx_helper_setup_gmx(interface, num_ports);

	/* enable the ports now */
	for (port = 0; port < num_ports; port++) {
		union cvmx_gmxx_prtx_cfg gmx_cfg;

		gmx_cfg.u64 =
		    cvmx_read_csr(CVMX_GMXX_PRTX_CFG(port, interface));
		gmx_cfg.s.en = 1;
		cvmx_write_csr(CVMX_GMXX_PRTX_CFG(port, interface),
			       gmx_cfg.u64);
	}
	__cvmx_interrupt_asxx_enable(interface);
	__cvmx_interrupt_gmxx_enable(interface);

	return 0;
}

/*
 * Return the link state of an IPD/PKO port as returned by
 * auto negotiation. The result of this function may not match
 * Octeon's link config if auto negotiation has changed since
 * the last call to cvmx_helper_link_set().
 *
 * @ipd_port: IPD/PKO port to query
 *
 * Returns Link state
 */
union cvmx_helper_link_info __cvmx_helper_rgmii_link_get(int ipd_port)
{
	int interface = cvmx_helper_get_interface_num(ipd_port);
	int index = cvmx_helper_get_interface_index_num(ipd_port);
	union cvmx_asxx_prt_loop asxx_prt_loop;

	asxx_prt_loop.u64 = cvmx_read_csr(CVMX_ASXX_PRT_LOOP(interface));
	if (asxx_prt_loop.s.int_loop & (1 << index)) {
		/* Force 1Gbps full duplex on internal loopback */
		union cvmx_helper_link_info result;
		result.u64 = 0;
		result.s.full_duplex = 1;
		result.s.link_up = 1;
		result.s.speed = 1000;
		return result;
	} else
		return __cvmx_helper_board_link_get(ipd_port);
}

/*
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
int __cvmx_helper_rgmii_link_set(int ipd_port,
				 union cvmx_helper_link_info link_info)
{
	int result = 0;
	int interface = cvmx_helper_get_interface_num(ipd_port);
	int index = cvmx_helper_get_interface_index_num(ipd_port);
	union cvmx_gmxx_prtx_cfg original_gmx_cfg;
	union cvmx_gmxx_prtx_cfg new_gmx_cfg;
	union cvmx_pko_mem_queue_qos pko_mem_queue_qos;
	union cvmx_pko_mem_queue_qos pko_mem_queue_qos_save[16];
	union cvmx_gmxx_tx_ovr_bp gmx_tx_ovr_bp;
	union cvmx_gmxx_tx_ovr_bp gmx_tx_ovr_bp_save;
	int i;

	/* Ignore speed sets in the simulator */
	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
		return 0;

	/* Read the current settings so we know the current enable state */
	original_gmx_cfg.u64 =
	    cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
	new_gmx_cfg = original_gmx_cfg;

	/* Disable the lowest level RX */
	cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(interface),
		       cvmx_read_csr(CVMX_ASXX_RX_PRT_EN(interface)) &
				     ~(1 << index));

	memset(pko_mem_queue_qos_save, 0, sizeof(pko_mem_queue_qos_save));
	/* Disable all queues so that TX should become idle */
	for (i = 0; i < cvmx_pko_get_num_queues(ipd_port); i++) {
		int queue = cvmx_pko_get_base_queue(ipd_port) + i;
		cvmx_write_csr(CVMX_PKO_REG_READ_IDX, queue);
		pko_mem_queue_qos.u64 = cvmx_read_csr(CVMX_PKO_MEM_QUEUE_QOS);
		pko_mem_queue_qos.s.pid = ipd_port;
		pko_mem_queue_qos.s.qid = queue;
		pko_mem_queue_qos_save[i] = pko_mem_queue_qos;
		pko_mem_queue_qos.s.qos_mask = 0;
		cvmx_write_csr(CVMX_PKO_MEM_QUEUE_QOS, pko_mem_queue_qos.u64);
	}

	/* Disable backpressure */
	gmx_tx_ovr_bp.u64 = cvmx_read_csr(CVMX_GMXX_TX_OVR_BP(interface));
	gmx_tx_ovr_bp_save = gmx_tx_ovr_bp;
	gmx_tx_ovr_bp.s.bp &= ~(1 << index);
	gmx_tx_ovr_bp.s.en |= 1 << index;
	cvmx_write_csr(CVMX_GMXX_TX_OVR_BP(interface), gmx_tx_ovr_bp.u64);
	cvmx_read_csr(CVMX_GMXX_TX_OVR_BP(interface));

	/*
	 * Poll the GMX state machine waiting for it to become
	 * idle. Preferably we should only change speed when it is
	 * idle. If it doesn't become idle we will still do the speed
	 * change, but there is a slight chance that GMX will
	 * lockup.
	 */
	cvmx_write_csr(CVMX_NPI_DBG_SELECT,
		       interface * 0x800 + index * 0x100 + 0x880);
	CVMX_WAIT_FOR_FIELD64(CVMX_DBG_DATA, union cvmx_dbg_data, data & 7,
			==, 0, 10000);
	CVMX_WAIT_FOR_FIELD64(CVMX_DBG_DATA, union cvmx_dbg_data, data & 0xf,
			==, 0, 10000);

	/* Disable the port before we make any changes */
	new_gmx_cfg.s.en = 0;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), new_gmx_cfg.u64);
	cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));

	/* Set full/half duplex */
	if (cvmx_octeon_is_pass1())
		/* Half duplex is broken for 38XX Pass 1 */
		new_gmx_cfg.s.duplex = 1;
	else if (!link_info.s.link_up)
		/* Force full duplex on down links */
		new_gmx_cfg.s.duplex = 1;
	else
		new_gmx_cfg.s.duplex = link_info.s.full_duplex;

	/* Set the link speed. Anything unknown is set to 1Gbps */
	if (link_info.s.speed == 10) {
		new_gmx_cfg.s.slottime = 0;
		new_gmx_cfg.s.speed = 0;
	} else if (link_info.s.speed == 100) {
		new_gmx_cfg.s.slottime = 0;
		new_gmx_cfg.s.speed = 0;
	} else {
		new_gmx_cfg.s.slottime = 1;
		new_gmx_cfg.s.speed = 1;
	}

	/* Adjust the clocks */
	if (link_info.s.speed == 10) {
		cvmx_write_csr(CVMX_GMXX_TXX_CLK(index, interface), 50);
		cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 0x40);
		cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0);
	} else if (link_info.s.speed == 100) {
		cvmx_write_csr(CVMX_GMXX_TXX_CLK(index, interface), 5);
		cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 0x40);
		cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0);
	} else {
		cvmx_write_csr(CVMX_GMXX_TXX_CLK(index, interface), 1);
		cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 0x200);
		cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0x2000);
	}

	if (OCTEON_IS_MODEL(OCTEON_CN30XX) || OCTEON_IS_MODEL(OCTEON_CN50XX)) {
		if ((link_info.s.speed == 10) || (link_info.s.speed == 100)) {
			union cvmx_gmxx_inf_mode mode;
			mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));

	/*
	 * Port	 .en  .type  .p0mii  Configuration
	 * ----	 ---  -----  ------  -----------------------------------------
	 *  X	   0	 X	X    All links are disabled.
	 *  0	   1	 X	0    Port 0 is RGMII
	 *  0	   1	 X	1    Port 0 is MII
	 *  1	   1	 0	X    Ports 1 and 2 are configured as RGMII ports.
	 *  1	   1	 1	X    Port 1: GMII/MII; Port 2: disabled. GMII or
	 *			     MII port is selected by GMX_PRT1_CFG[SPEED].
	 */

			/* In MII mode, CLK_CNT = 1. */
			if (((index == 0) && (mode.s.p0mii == 1))
			    || ((index != 0) && (mode.s.type == 1))) {
				cvmx_write_csr(CVMX_GMXX_TXX_CLK
					       (index, interface), 1);
			}
		}
	}

	/* Do a read to make sure all setup stuff is complete */
	cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));

	/* Save the new GMX setting without enabling the port */
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), new_gmx_cfg.u64);

	/* Enable the lowest level RX */
	cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(interface),
		       cvmx_read_csr(CVMX_ASXX_RX_PRT_EN(interface)) | (1 <<
									index));

	/* Re-enable the TX path */
	for (i = 0; i < cvmx_pko_get_num_queues(ipd_port); i++) {
		int queue = cvmx_pko_get_base_queue(ipd_port) + i;
		cvmx_write_csr(CVMX_PKO_REG_READ_IDX, queue);
		cvmx_write_csr(CVMX_PKO_MEM_QUEUE_QOS,
			       pko_mem_queue_qos_save[i].u64);
	}

	/* Restore backpressure */
	cvmx_write_csr(CVMX_GMXX_TX_OVR_BP(interface), gmx_tx_ovr_bp_save.u64);

	/* Restore the GMX enable state. Port config is complete */
	new_gmx_cfg.s.en = original_gmx_cfg.s.en;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), new_gmx_cfg.u64);

	return result;
}
