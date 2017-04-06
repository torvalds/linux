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
 * Functions for SGMII initialization, configuration,
 * and monitoring.
 */

#include <asm/octeon/octeon.h>

#include <asm/octeon/cvmx-config.h>

#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-board.h>

#include <asm/octeon/cvmx-gmxx-defs.h>
#include <asm/octeon/cvmx-pcsx-defs.h>

void __cvmx_interrupt_gmxx_enable(int interface);
void __cvmx_interrupt_pcsx_intx_en_reg_enable(int index, int block);
void __cvmx_interrupt_pcsxx_int_en_reg_enable(int index);

/**
 * Perform initialization required only once for an SGMII port.
 *
 * @interface: Interface to init
 * @index:     Index of prot on the interface
 *
 * Returns Zero on success, negative on failure
 */
static int __cvmx_helper_sgmii_hardware_init_one_time(int interface, int index)
{
	const uint64_t clock_mhz = cvmx_sysinfo_get()->cpu_clock_hz / 1000000;
	union cvmx_pcsx_miscx_ctl_reg pcs_misc_ctl_reg;
	union cvmx_pcsx_linkx_timer_count_reg pcsx_linkx_timer_count_reg;
	union cvmx_gmxx_prtx_cfg gmxx_prtx_cfg;

	/* Disable GMX */
	gmxx_prtx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
	gmxx_prtx_cfg.s.en = 0;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmxx_prtx_cfg.u64);

	/*
	 * Write PCS*_LINK*_TIMER_COUNT_REG[COUNT] with the
	 * appropriate value. 1000BASE-X specifies a 10ms
	 * interval. SGMII specifies a 1.6ms interval.
	 */
	pcs_misc_ctl_reg.u64 =
	    cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));
	pcsx_linkx_timer_count_reg.u64 =
	    cvmx_read_csr(CVMX_PCSX_LINKX_TIMER_COUNT_REG(index, interface));
	if (pcs_misc_ctl_reg.s.mode) {
		/* 1000BASE-X */
		pcsx_linkx_timer_count_reg.s.count =
		    (10000ull * clock_mhz) >> 10;
	} else {
		/* SGMII */
		pcsx_linkx_timer_count_reg.s.count =
		    (1600ull * clock_mhz) >> 10;
	}
	cvmx_write_csr(CVMX_PCSX_LINKX_TIMER_COUNT_REG(index, interface),
		       pcsx_linkx_timer_count_reg.u64);

	/*
	 * Write the advertisement register to be used as the
	 * tx_Config_Reg<D15:D0> of the autonegotiation.  In
	 * 1000BASE-X mode, tx_Config_Reg<D15:D0> is PCS*_AN*_ADV_REG.
	 * In SGMII PHY mode, tx_Config_Reg<D15:D0> is
	 * PCS*_SGM*_AN_ADV_REG.  In SGMII MAC mode,
	 * tx_Config_Reg<D15:D0> is the fixed value 0x4001, so this
	 * step can be skipped.
	 */
	if (pcs_misc_ctl_reg.s.mode) {
		/* 1000BASE-X */
		union cvmx_pcsx_anx_adv_reg pcsx_anx_adv_reg;
		pcsx_anx_adv_reg.u64 =
		    cvmx_read_csr(CVMX_PCSX_ANX_ADV_REG(index, interface));
		pcsx_anx_adv_reg.s.rem_flt = 0;
		pcsx_anx_adv_reg.s.pause = 3;
		pcsx_anx_adv_reg.s.hfd = 1;
		pcsx_anx_adv_reg.s.fd = 1;
		cvmx_write_csr(CVMX_PCSX_ANX_ADV_REG(index, interface),
			       pcsx_anx_adv_reg.u64);
	} else {
		union cvmx_pcsx_miscx_ctl_reg pcsx_miscx_ctl_reg;
		pcsx_miscx_ctl_reg.u64 =
		    cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));
		if (pcsx_miscx_ctl_reg.s.mac_phy) {
			/* PHY Mode */
			union cvmx_pcsx_sgmx_an_adv_reg pcsx_sgmx_an_adv_reg;
			pcsx_sgmx_an_adv_reg.u64 =
			    cvmx_read_csr(CVMX_PCSX_SGMX_AN_ADV_REG
					  (index, interface));
			pcsx_sgmx_an_adv_reg.s.link = 1;
			pcsx_sgmx_an_adv_reg.s.dup = 1;
			pcsx_sgmx_an_adv_reg.s.speed = 2;
			cvmx_write_csr(CVMX_PCSX_SGMX_AN_ADV_REG
				       (index, interface),
				       pcsx_sgmx_an_adv_reg.u64);
		} else {
			/* MAC Mode - Nothing to do */
		}
	}
	return 0;
}

/**
 * Initialize the SERTES link for the first time or after a loss
 * of link.
 *
 * @interface: Interface to init
 * @index:     Index of prot on the interface
 *
 * Returns Zero on success, negative on failure
 */
static int __cvmx_helper_sgmii_hardware_init_link(int interface, int index)
{
	union cvmx_pcsx_mrx_control_reg control_reg;

	/*
	 * Take PCS through a reset sequence.
	 * PCS*_MR*_CONTROL_REG[PWR_DN] should be cleared to zero.
	 * Write PCS*_MR*_CONTROL_REG[RESET]=1 (while not changing the
	 * value of the other PCS*_MR*_CONTROL_REG bits).  Read
	 * PCS*_MR*_CONTROL_REG[RESET] until it changes value to
	 * zero.
	 */
	control_reg.u64 =
	    cvmx_read_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface));
	if (cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM) {
		control_reg.s.reset = 1;
		cvmx_write_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface),
			       control_reg.u64);
		if (CVMX_WAIT_FOR_FIELD64
		    (CVMX_PCSX_MRX_CONTROL_REG(index, interface),
		     union cvmx_pcsx_mrx_control_reg, reset, ==, 0, 10000)) {
			cvmx_dprintf("SGMII%d: Timeout waiting for port %d "
				     "to finish reset\n",
			     interface, index);
			return -1;
		}
	}

	/*
	 * Write PCS*_MR*_CONTROL_REG[RST_AN]=1 to ensure a fresh
	 * sgmii negotiation starts.
	 */
	control_reg.s.rst_an = 1;
	control_reg.s.an_en = 1;
	control_reg.s.pwr_dn = 0;
	cvmx_write_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface),
		       control_reg.u64);

	/*
	 * Wait for PCS*_MR*_STATUS_REG[AN_CPT] to be set, indicating
	 * that sgmii autonegotiation is complete. In MAC mode this
	 * isn't an ethernet link, but a link between Octeon and the
	 * PHY.
	 */
	if ((cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM) &&
	    CVMX_WAIT_FOR_FIELD64(CVMX_PCSX_MRX_STATUS_REG(index, interface),
				  union cvmx_pcsx_mrx_status_reg, an_cpt, ==, 1,
				  10000)) {
		/* cvmx_dprintf("SGMII%d: Port %d link timeout\n", interface, index); */
		return -1;
	}
	return 0;
}

/**
 * Configure an SGMII link to the specified speed after the SERTES
 * link is up.
 *
 * @interface: Interface to init
 * @index:     Index of prot on the interface
 * @link_info: Link state to configure
 *
 * Returns Zero on success, negative on failure
 */
static int __cvmx_helper_sgmii_hardware_init_link_speed(int interface,
							int index,
							cvmx_helper_link_info_t
							link_info)
{
	int is_enabled;
	union cvmx_gmxx_prtx_cfg gmxx_prtx_cfg;
	union cvmx_pcsx_miscx_ctl_reg pcsx_miscx_ctl_reg;

	/* Disable GMX before we make any changes. Remember the enable state */
	gmxx_prtx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
	is_enabled = gmxx_prtx_cfg.s.en;
	gmxx_prtx_cfg.s.en = 0;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmxx_prtx_cfg.u64);

	/* Wait for GMX to be idle */
	if (CVMX_WAIT_FOR_FIELD64
	    (CVMX_GMXX_PRTX_CFG(index, interface), union cvmx_gmxx_prtx_cfg,
	     rx_idle, ==, 1, 10000)
	    || CVMX_WAIT_FOR_FIELD64(CVMX_GMXX_PRTX_CFG(index, interface),
				     union cvmx_gmxx_prtx_cfg, tx_idle, ==, 1,
				     10000)) {
		cvmx_dprintf
		    ("SGMII%d: Timeout waiting for port %d to be idle\n",
		     interface, index);
		return -1;
	}

	/* Read GMX CFG again to make sure the disable completed */
	gmxx_prtx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));

	/*
	 * Get the misc control for PCS. We will need to set the
	 * duplication amount.
	 */
	pcsx_miscx_ctl_reg.u64 =
	    cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));

	/*
	 * Use GMXENO to force the link down if the status we get says
	 * it should be down.
	 */
	pcsx_miscx_ctl_reg.s.gmxeno = !link_info.s.link_up;

	/* Only change the duplex setting if the link is up */
	if (link_info.s.link_up)
		gmxx_prtx_cfg.s.duplex = link_info.s.full_duplex;

	/* Do speed based setting for GMX */
	switch (link_info.s.speed) {
	case 10:
		gmxx_prtx_cfg.s.speed = 0;
		gmxx_prtx_cfg.s.speed_msb = 1;
		gmxx_prtx_cfg.s.slottime = 0;
		/* Setting from GMX-603 */
		pcsx_miscx_ctl_reg.s.samp_pt = 25;
		cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 64);
		cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0);
		break;
	case 100:
		gmxx_prtx_cfg.s.speed = 0;
		gmxx_prtx_cfg.s.speed_msb = 0;
		gmxx_prtx_cfg.s.slottime = 0;
		pcsx_miscx_ctl_reg.s.samp_pt = 0x5;
		cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 64);
		cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0);
		break;
	case 1000:
		gmxx_prtx_cfg.s.speed = 1;
		gmxx_prtx_cfg.s.speed_msb = 0;
		gmxx_prtx_cfg.s.slottime = 1;
		pcsx_miscx_ctl_reg.s.samp_pt = 1;
		cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 512);
		cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 8192);
		break;
	default:
		break;
	}

	/* Write the new misc control for PCS */
	cvmx_write_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface),
		       pcsx_miscx_ctl_reg.u64);

	/* Write the new GMX settings with the port still disabled */
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmxx_prtx_cfg.u64);

	/* Read GMX CFG again to make sure the config completed */
	gmxx_prtx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));

	/* Restore the enabled / disabled state */
	gmxx_prtx_cfg.s.en = is_enabled;
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmxx_prtx_cfg.u64);

	return 0;
}

/**
 * Bring up the SGMII interface to be ready for packet I/O but
 * leave I/O disabled using the GMX override. This function
 * follows the bringup documented in 10.6.3 of the manual.
 *
 * @interface: Interface to bringup
 * @num_ports: Number of ports on the interface
 *
 * Returns Zero on success, negative on failure
 */
static int __cvmx_helper_sgmii_hardware_init(int interface, int num_ports)
{
	int index;

	__cvmx_helper_setup_gmx(interface, num_ports);

	for (index = 0; index < num_ports; index++) {
		int ipd_port = cvmx_helper_get_ipd_port(interface, index);
		__cvmx_helper_sgmii_hardware_init_one_time(interface, index);
		/* Linux kernel driver will call ....link_set with the
		 * proper link state. In the simulator there is no
		 * link state polling and hence it is set from
		 * here.
		 */
		if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
			__cvmx_helper_sgmii_link_set(ipd_port,
				       __cvmx_helper_sgmii_link_get(ipd_port));
	}

	return 0;
}

int __cvmx_helper_sgmii_enumerate(int interface)
{
	return 4;
}
/**
 * Probe a SGMII interface and determine the number of ports
 * connected to it. The SGMII interface should still be down after
 * this call.
 *
 * @interface: Interface to probe
 *
 * Returns Number of ports on the interface. Zero to disable.
 */
int __cvmx_helper_sgmii_probe(int interface)
{
	union cvmx_gmxx_inf_mode mode;

	/*
	 * Due to errata GMX-700 on CN56XXp1.x and CN52XXp1.x, the
	 * interface needs to be enabled before IPD otherwise per port
	 * backpressure may not work properly
	 */
	mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));
	mode.s.en = 1;
	cvmx_write_csr(CVMX_GMXX_INF_MODE(interface), mode.u64);
	return __cvmx_helper_sgmii_enumerate(interface);
}

/**
 * Bringup and enable a SGMII interface. After this call packet
 * I/O should be fully functional. This is called with IPD
 * enabled but PKO disabled.
 *
 * @interface: Interface to bring up
 *
 * Returns Zero on success, negative on failure
 */
int __cvmx_helper_sgmii_enable(int interface)
{
	int num_ports = cvmx_helper_ports_on_interface(interface);
	int index;

	__cvmx_helper_sgmii_hardware_init(interface, num_ports);

	for (index = 0; index < num_ports; index++) {
		union cvmx_gmxx_prtx_cfg gmxx_prtx_cfg;
		gmxx_prtx_cfg.u64 =
		    cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
		gmxx_prtx_cfg.s.en = 1;
		cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface),
			       gmxx_prtx_cfg.u64);
		__cvmx_interrupt_pcsx_intx_en_reg_enable(index, interface);
	}
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
cvmx_helper_link_info_t __cvmx_helper_sgmii_link_get(int ipd_port)
{
	cvmx_helper_link_info_t result;
	union cvmx_pcsx_miscx_ctl_reg pcs_misc_ctl_reg;
	int interface = cvmx_helper_get_interface_num(ipd_port);
	int index = cvmx_helper_get_interface_index_num(ipd_port);
	union cvmx_pcsx_mrx_control_reg pcsx_mrx_control_reg;

	result.u64 = 0;

	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM) {
		/* The simulator gives you a simulated 1Gbps full duplex link */
		result.s.link_up = 1;
		result.s.full_duplex = 1;
		result.s.speed = 1000;
		return result;
	}

	pcsx_mrx_control_reg.u64 =
	    cvmx_read_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface));
	if (pcsx_mrx_control_reg.s.loopbck1) {
		/* Force 1Gbps full duplex link for internal loopback */
		result.s.link_up = 1;
		result.s.full_duplex = 1;
		result.s.speed = 1000;
		return result;
	}

	pcs_misc_ctl_reg.u64 =
	    cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));
	if (pcs_misc_ctl_reg.s.mode) {
		/* 1000BASE-X */
		/* FIXME */
	} else {
		union cvmx_pcsx_miscx_ctl_reg pcsx_miscx_ctl_reg;
		pcsx_miscx_ctl_reg.u64 =
		    cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));
		if (pcsx_miscx_ctl_reg.s.mac_phy) {
			/* PHY Mode */
			union cvmx_pcsx_mrx_status_reg pcsx_mrx_status_reg;
			union cvmx_pcsx_anx_results_reg pcsx_anx_results_reg;

			/*
			 * Don't bother continuing if the SERTES low
			 * level link is down
			 */
			pcsx_mrx_status_reg.u64 =
			    cvmx_read_csr(CVMX_PCSX_MRX_STATUS_REG
					  (index, interface));
			if (pcsx_mrx_status_reg.s.lnk_st == 0) {
				if (__cvmx_helper_sgmii_hardware_init_link
				    (interface, index) != 0)
					return result;
			}

			/* Read the autoneg results */
			pcsx_anx_results_reg.u64 =
			    cvmx_read_csr(CVMX_PCSX_ANX_RESULTS_REG
					  (index, interface));
			if (pcsx_anx_results_reg.s.an_cpt) {
				/*
				 * Auto negotiation is complete. Set
				 * status accordingly.
				 */
				result.s.full_duplex =
				    pcsx_anx_results_reg.s.dup;
				result.s.link_up =
				    pcsx_anx_results_reg.s.link_ok;
				switch (pcsx_anx_results_reg.s.spd) {
				case 0:
					result.s.speed = 10;
					break;
				case 1:
					result.s.speed = 100;
					break;
				case 2:
					result.s.speed = 1000;
					break;
				default:
					result.s.speed = 0;
					result.s.link_up = 0;
					break;
				}
			} else {
				/*
				 * Auto negotiation isn't
				 * complete. Return link down.
				 */
				result.s.speed = 0;
				result.s.link_up = 0;
			}
		} else {	/* MAC Mode */

			result = __cvmx_helper_board_link_get(ipd_port);
		}
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
int __cvmx_helper_sgmii_link_set(int ipd_port,
				 cvmx_helper_link_info_t link_info)
{
	int interface = cvmx_helper_get_interface_num(ipd_port);
	int index = cvmx_helper_get_interface_index_num(ipd_port);
	__cvmx_helper_sgmii_hardware_init_link(interface, index);
	return __cvmx_helper_sgmii_hardware_init_link_speed(interface, index,
							    link_info);
}

/**
 * Configure a port for internal and/or external loopback. Internal
 * loopback causes packets sent by the port to be received by
 * Octeon. External loopback causes packets received from the wire to
 * sent out again.
 *
 * @ipd_port: IPD/PKO port to loopback.
 * @enable_internal:
 *		   Non zero if you want internal loopback
 * @enable_external:
 *		   Non zero if you want external loopback
 *
 * Returns Zero on success, negative on failure.
 */
int __cvmx_helper_sgmii_configure_loopback(int ipd_port, int enable_internal,
					   int enable_external)
{
	int interface = cvmx_helper_get_interface_num(ipd_port);
	int index = cvmx_helper_get_interface_index_num(ipd_port);
	union cvmx_pcsx_mrx_control_reg pcsx_mrx_control_reg;
	union cvmx_pcsx_miscx_ctl_reg pcsx_miscx_ctl_reg;

	pcsx_mrx_control_reg.u64 =
	    cvmx_read_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface));
	pcsx_mrx_control_reg.s.loopbck1 = enable_internal;
	cvmx_write_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface),
		       pcsx_mrx_control_reg.u64);

	pcsx_miscx_ctl_reg.u64 =
	    cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));
	pcsx_miscx_ctl_reg.s.loopbck2 = enable_external;
	cvmx_write_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface),
		       pcsx_miscx_ctl_reg.u64);

	__cvmx_helper_sgmii_hardware_init_link(interface, index);
	return 0;
}
