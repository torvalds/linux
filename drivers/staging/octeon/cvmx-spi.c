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
 * Support library for the SPI
 */
#include <asm/octeon/octeon.h>

#include "cvmx-config.h"

#include "cvmx-pko.h"
#include "cvmx-spi.h"

#include "cvmx-spxx-defs.h"
#include "cvmx-stxx-defs.h"
#include "cvmx-srxx-defs.h"

#define INVOKE_CB(function_p, args...)		\
	do {					\
		if (function_p) {		\
			res = function_p(args); \
			if (res)		\
				return res;	\
		}				\
	} while (0)

#if CVMX_ENABLE_DEBUG_PRINTS
static const char *modes[] =
    { "UNKNOWN", "TX Halfplex", "Rx Halfplex", "Duplex" };
#endif

/* Default callbacks, can be overridden
 *  using cvmx_spi_get_callbacks/cvmx_spi_set_callbacks
 */
static cvmx_spi_callbacks_t cvmx_spi_callbacks = {
	.reset_cb = cvmx_spi_reset_cb,
	.calendar_setup_cb = cvmx_spi_calendar_setup_cb,
	.clock_detect_cb = cvmx_spi_clock_detect_cb,
	.training_cb = cvmx_spi_training_cb,
	.calendar_sync_cb = cvmx_spi_calendar_sync_cb,
	.interface_up_cb = cvmx_spi_interface_up_cb
};

/**
 * Get current SPI4 initialization callbacks
 *
 * @callbacks:  Pointer to the callbacks structure.to fill
 *
 * Returns Pointer to cvmx_spi_callbacks_t structure.
 */
void cvmx_spi_get_callbacks(cvmx_spi_callbacks_t *callbacks)
{
	memcpy(callbacks, &cvmx_spi_callbacks, sizeof(cvmx_spi_callbacks));
}

/**
 * Set new SPI4 initialization callbacks
 *
 * @new_callbacks:  Pointer to an updated callbacks structure.
 */
void cvmx_spi_set_callbacks(cvmx_spi_callbacks_t *new_callbacks)
{
	memcpy(&cvmx_spi_callbacks, new_callbacks, sizeof(cvmx_spi_callbacks));
}

/**
 * Initialize and start the SPI interface.
 *
 * @interface: The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @timeout:   Timeout to wait for clock synchronization in seconds
 * @num_ports: Number of SPI ports to configure
 *
 * Returns Zero on success, negative of failure.
 */
int cvmx_spi_start_interface(int interface, cvmx_spi_mode_t mode, int timeout,
			     int num_ports)
{
	int res = -1;

	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		return res;

	/* Callback to perform SPI4 reset */
	INVOKE_CB(cvmx_spi_callbacks.reset_cb, interface, mode);

	/* Callback to perform calendar setup */
	INVOKE_CB(cvmx_spi_callbacks.calendar_setup_cb, interface, mode,
		  num_ports);

	/* Callback to perform clock detection */
	INVOKE_CB(cvmx_spi_callbacks.clock_detect_cb, interface, mode, timeout);

	/* Callback to perform SPI4 link training */
	INVOKE_CB(cvmx_spi_callbacks.training_cb, interface, mode, timeout);

	/* Callback to perform calendar sync */
	INVOKE_CB(cvmx_spi_callbacks.calendar_sync_cb, interface, mode,
		  timeout);

	/* Callback to handle interface coming up */
	INVOKE_CB(cvmx_spi_callbacks.interface_up_cb, interface, mode);

	return res;
}

/**
 * This routine restarts the SPI interface after it has lost synchronization
 * with its correspondent system.
 *
 * @interface: The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @timeout:   Timeout to wait for clock synchronization in seconds
 *
 * Returns Zero on success, negative of failure.
 */
int cvmx_spi_restart_interface(int interface, cvmx_spi_mode_t mode, int timeout)
{
	int res = -1;

	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		return res;

	cvmx_dprintf("SPI%d: Restart %s\n", interface, modes[mode]);

	/* Callback to perform SPI4 reset */
	INVOKE_CB(cvmx_spi_callbacks.reset_cb, interface, mode);

	/* NOTE: Calendar setup is not performed during restart */
	/*       Refer to cvmx_spi_start_interface() for the full sequence */

	/* Callback to perform clock detection */
	INVOKE_CB(cvmx_spi_callbacks.clock_detect_cb, interface, mode, timeout);

	/* Callback to perform SPI4 link training */
	INVOKE_CB(cvmx_spi_callbacks.training_cb, interface, mode, timeout);

	/* Callback to perform calendar sync */
	INVOKE_CB(cvmx_spi_callbacks.calendar_sync_cb, interface, mode,
		  timeout);

	/* Callback to handle interface coming up */
	INVOKE_CB(cvmx_spi_callbacks.interface_up_cb, interface, mode);

	return res;
}

/**
 * Callback to perform SPI4 reset
 *
 * @interface: The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
int cvmx_spi_reset_cb(int interface, cvmx_spi_mode_t mode)
{
	union cvmx_spxx_dbg_deskew_ctl spxx_dbg_deskew_ctl;
	union cvmx_spxx_clk_ctl spxx_clk_ctl;
	union cvmx_spxx_bist_stat spxx_bist_stat;
	union cvmx_spxx_int_msk spxx_int_msk;
	union cvmx_stxx_int_msk stxx_int_msk;
	union cvmx_spxx_trn4_ctl spxx_trn4_ctl;
	int index;
	uint64_t MS = cvmx_sysinfo_get()->cpu_clock_hz / 1000;

	/* Disable SPI error events while we run BIST */
	spxx_int_msk.u64 = cvmx_read_csr(CVMX_SPXX_INT_MSK(interface));
	cvmx_write_csr(CVMX_SPXX_INT_MSK(interface), 0);
	stxx_int_msk.u64 = cvmx_read_csr(CVMX_STXX_INT_MSK(interface));
	cvmx_write_csr(CVMX_STXX_INT_MSK(interface), 0);

	/* Run BIST in the SPI interface */
	cvmx_write_csr(CVMX_SRXX_COM_CTL(interface), 0);
	cvmx_write_csr(CVMX_STXX_COM_CTL(interface), 0);
	spxx_clk_ctl.u64 = 0;
	spxx_clk_ctl.s.runbist = 1;
	cvmx_write_csr(CVMX_SPXX_CLK_CTL(interface), spxx_clk_ctl.u64);
	cvmx_wait(10 * MS);
	spxx_bist_stat.u64 = cvmx_read_csr(CVMX_SPXX_BIST_STAT(interface));
	if (spxx_bist_stat.s.stat0)
		cvmx_dprintf
		    ("ERROR SPI%d: BIST failed on receive datapath FIFO\n",
		     interface);
	if (spxx_bist_stat.s.stat1)
		cvmx_dprintf("ERROR SPI%d: BIST failed on RX calendar table\n",
			     interface);
	if (spxx_bist_stat.s.stat2)
		cvmx_dprintf("ERROR SPI%d: BIST failed on TX calendar table\n",
			     interface);

	/* Clear the calendar table after BIST to fix parity errors */
	for (index = 0; index < 32; index++) {
		union cvmx_srxx_spi4_calx srxx_spi4_calx;
		union cvmx_stxx_spi4_calx stxx_spi4_calx;

		srxx_spi4_calx.u64 = 0;
		srxx_spi4_calx.s.oddpar = 1;
		cvmx_write_csr(CVMX_SRXX_SPI4_CALX(index, interface),
			       srxx_spi4_calx.u64);

		stxx_spi4_calx.u64 = 0;
		stxx_spi4_calx.s.oddpar = 1;
		cvmx_write_csr(CVMX_STXX_SPI4_CALX(index, interface),
			       stxx_spi4_calx.u64);
	}

	/* Re enable reporting of error interrupts */
	cvmx_write_csr(CVMX_SPXX_INT_REG(interface),
		       cvmx_read_csr(CVMX_SPXX_INT_REG(interface)));
	cvmx_write_csr(CVMX_SPXX_INT_MSK(interface), spxx_int_msk.u64);
	cvmx_write_csr(CVMX_STXX_INT_REG(interface),
		       cvmx_read_csr(CVMX_STXX_INT_REG(interface)));
	cvmx_write_csr(CVMX_STXX_INT_MSK(interface), stxx_int_msk.u64);

	/* Setup the CLKDLY right in the middle */
	spxx_clk_ctl.u64 = 0;
	spxx_clk_ctl.s.seetrn = 0;
	spxx_clk_ctl.s.clkdly = 0x10;
	spxx_clk_ctl.s.runbist = 0;
	spxx_clk_ctl.s.statdrv = 0;
	/* This should always be on the opposite edge as statdrv */
	spxx_clk_ctl.s.statrcv = 1;
	spxx_clk_ctl.s.sndtrn = 0;
	spxx_clk_ctl.s.drptrn = 0;
	spxx_clk_ctl.s.rcvtrn = 0;
	spxx_clk_ctl.s.srxdlck = 0;
	cvmx_write_csr(CVMX_SPXX_CLK_CTL(interface), spxx_clk_ctl.u64);
	cvmx_wait(100 * MS);

	/* Reset SRX0 DLL */
	spxx_clk_ctl.s.srxdlck = 1;
	cvmx_write_csr(CVMX_SPXX_CLK_CTL(interface), spxx_clk_ctl.u64);

	/* Waiting for Inf0 Spi4 RX DLL to lock */
	cvmx_wait(100 * MS);

	/* Enable dynamic alignment */
	spxx_trn4_ctl.s.trntest = 0;
	spxx_trn4_ctl.s.jitter = 1;
	spxx_trn4_ctl.s.clr_boot = 1;
	spxx_trn4_ctl.s.set_boot = 0;
	if (OCTEON_IS_MODEL(OCTEON_CN58XX))
		spxx_trn4_ctl.s.maxdist = 3;
	else
		spxx_trn4_ctl.s.maxdist = 8;
	spxx_trn4_ctl.s.macro_en = 1;
	spxx_trn4_ctl.s.mux_en = 1;
	cvmx_write_csr(CVMX_SPXX_TRN4_CTL(interface), spxx_trn4_ctl.u64);

	spxx_dbg_deskew_ctl.u64 = 0;
	cvmx_write_csr(CVMX_SPXX_DBG_DESKEW_CTL(interface),
		       spxx_dbg_deskew_ctl.u64);

	return 0;
}

/**
 * Callback to setup calendar and miscellaneous settings before clock detection
 *
 * @interface: The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @num_ports: Number of ports to configure on SPI
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
int cvmx_spi_calendar_setup_cb(int interface, cvmx_spi_mode_t mode,
			       int num_ports)
{
	int port;
	int index;
	if (mode & CVMX_SPI_MODE_RX_HALFPLEX) {
		union cvmx_srxx_com_ctl srxx_com_ctl;
		union cvmx_srxx_spi4_stat srxx_spi4_stat;

		/* SRX0 number of Ports */
		srxx_com_ctl.u64 = 0;
		srxx_com_ctl.s.prts = num_ports - 1;
		srxx_com_ctl.s.st_en = 0;
		srxx_com_ctl.s.inf_en = 0;
		cvmx_write_csr(CVMX_SRXX_COM_CTL(interface), srxx_com_ctl.u64);

		/* SRX0 Calendar Table. This round robbins through all ports */
		port = 0;
		index = 0;
		while (port < num_ports) {
			union cvmx_srxx_spi4_calx srxx_spi4_calx;
			srxx_spi4_calx.u64 = 0;
			srxx_spi4_calx.s.prt0 = port++;
			srxx_spi4_calx.s.prt1 = port++;
			srxx_spi4_calx.s.prt2 = port++;
			srxx_spi4_calx.s.prt3 = port++;
			srxx_spi4_calx.s.oddpar =
			    ~(cvmx_dpop(srxx_spi4_calx.u64) & 1);
			cvmx_write_csr(CVMX_SRXX_SPI4_CALX(index, interface),
				       srxx_spi4_calx.u64);
			index++;
		}
		srxx_spi4_stat.u64 = 0;
		srxx_spi4_stat.s.len = num_ports;
		srxx_spi4_stat.s.m = 1;
		cvmx_write_csr(CVMX_SRXX_SPI4_STAT(interface),
			       srxx_spi4_stat.u64);
	}

	if (mode & CVMX_SPI_MODE_TX_HALFPLEX) {
		union cvmx_stxx_arb_ctl stxx_arb_ctl;
		union cvmx_gmxx_tx_spi_max gmxx_tx_spi_max;
		union cvmx_gmxx_tx_spi_thresh gmxx_tx_spi_thresh;
		union cvmx_gmxx_tx_spi_ctl gmxx_tx_spi_ctl;
		union cvmx_stxx_spi4_stat stxx_spi4_stat;
		union cvmx_stxx_spi4_dat stxx_spi4_dat;

		/* STX0 Config */
		stxx_arb_ctl.u64 = 0;
		stxx_arb_ctl.s.igntpa = 0;
		stxx_arb_ctl.s.mintrn = 0;
		cvmx_write_csr(CVMX_STXX_ARB_CTL(interface), stxx_arb_ctl.u64);

		gmxx_tx_spi_max.u64 = 0;
		gmxx_tx_spi_max.s.max1 = 8;
		gmxx_tx_spi_max.s.max2 = 4;
		gmxx_tx_spi_max.s.slice = 0;
		cvmx_write_csr(CVMX_GMXX_TX_SPI_MAX(interface),
			       gmxx_tx_spi_max.u64);

		gmxx_tx_spi_thresh.u64 = 0;
		gmxx_tx_spi_thresh.s.thresh = 4;
		cvmx_write_csr(CVMX_GMXX_TX_SPI_THRESH(interface),
			       gmxx_tx_spi_thresh.u64);

		gmxx_tx_spi_ctl.u64 = 0;
		gmxx_tx_spi_ctl.s.tpa_clr = 0;
		gmxx_tx_spi_ctl.s.cont_pkt = 0;
		cvmx_write_csr(CVMX_GMXX_TX_SPI_CTL(interface),
			       gmxx_tx_spi_ctl.u64);

		/* STX0 Training Control */
		stxx_spi4_dat.u64 = 0;
		/*Minimum needed by dynamic alignment */
		stxx_spi4_dat.s.alpha = 32;
		stxx_spi4_dat.s.max_t = 0xFFFF;	/*Minimum interval is 0x20 */
		cvmx_write_csr(CVMX_STXX_SPI4_DAT(interface),
			       stxx_spi4_dat.u64);

		/* STX0 Calendar Table. This round robbins through all ports */
		port = 0;
		index = 0;
		while (port < num_ports) {
			union cvmx_stxx_spi4_calx stxx_spi4_calx;
			stxx_spi4_calx.u64 = 0;
			stxx_spi4_calx.s.prt0 = port++;
			stxx_spi4_calx.s.prt1 = port++;
			stxx_spi4_calx.s.prt2 = port++;
			stxx_spi4_calx.s.prt3 = port++;
			stxx_spi4_calx.s.oddpar =
			    ~(cvmx_dpop(stxx_spi4_calx.u64) & 1);
			cvmx_write_csr(CVMX_STXX_SPI4_CALX(index, interface),
				       stxx_spi4_calx.u64);
			index++;
		}
		stxx_spi4_stat.u64 = 0;
		stxx_spi4_stat.s.len = num_ports;
		stxx_spi4_stat.s.m = 1;
		cvmx_write_csr(CVMX_STXX_SPI4_STAT(interface),
			       stxx_spi4_stat.u64);
	}

	return 0;
}

/**
 * Callback to perform clock detection
 *
 * @interface: The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @timeout:   Timeout to wait for clock synchronization in seconds
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
int cvmx_spi_clock_detect_cb(int interface, cvmx_spi_mode_t mode, int timeout)
{
	int clock_transitions;
	union cvmx_spxx_clk_stat stat;
	uint64_t timeout_time;
	uint64_t MS = cvmx_sysinfo_get()->cpu_clock_hz / 1000;

	/*
	 * Regardless of operating mode, both Tx and Rx clocks must be
	 * present for the SPI interface to operate.
	 */
	cvmx_dprintf("SPI%d: Waiting to see TsClk...\n", interface);
	timeout_time = cvmx_get_cycle() + 1000ull * MS * timeout;
	/*
	 * Require 100 clock transitions in order to avoid any noise
	 * in the beginning.
	 */
	clock_transitions = 100;
	do {
		stat.u64 = cvmx_read_csr(CVMX_SPXX_CLK_STAT(interface));
		if (stat.s.s4clk0 && stat.s.s4clk1 && clock_transitions) {
			/*
			 * We've seen a clock transition, so decrement
			 * the number we still need.
			 */
			clock_transitions--;
			cvmx_write_csr(CVMX_SPXX_CLK_STAT(interface), stat.u64);
			stat.s.s4clk0 = 0;
			stat.s.s4clk1 = 0;
		}
		if (cvmx_get_cycle() > timeout_time) {
			cvmx_dprintf("SPI%d: Timeout\n", interface);
			return -1;
		}
	} while (stat.s.s4clk0 == 0 || stat.s.s4clk1 == 0);

	cvmx_dprintf("SPI%d: Waiting to see RsClk...\n", interface);
	timeout_time = cvmx_get_cycle() + 1000ull * MS * timeout;
	/*
	 * Require 100 clock transitions in order to avoid any noise in the
	 * beginning.
	 */
	clock_transitions = 100;
	do {
		stat.u64 = cvmx_read_csr(CVMX_SPXX_CLK_STAT(interface));
		if (stat.s.d4clk0 && stat.s.d4clk1 && clock_transitions) {
			/*
			 * We've seen a clock transition, so decrement
			 * the number we still need
			 */
			clock_transitions--;
			cvmx_write_csr(CVMX_SPXX_CLK_STAT(interface), stat.u64);
			stat.s.d4clk0 = 0;
			stat.s.d4clk1 = 0;
		}
		if (cvmx_get_cycle() > timeout_time) {
			cvmx_dprintf("SPI%d: Timeout\n", interface);
			return -1;
		}
	} while (stat.s.d4clk0 == 0 || stat.s.d4clk1 == 0);

	return 0;
}

/**
 * Callback to perform link training
 *
 * @interface: The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @timeout:   Timeout to wait for link to be trained (in seconds)
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
int cvmx_spi_training_cb(int interface, cvmx_spi_mode_t mode, int timeout)
{
	union cvmx_spxx_trn4_ctl spxx_trn4_ctl;
	union cvmx_spxx_clk_stat stat;
	uint64_t MS = cvmx_sysinfo_get()->cpu_clock_hz / 1000;
	uint64_t timeout_time = cvmx_get_cycle() + 1000ull * MS * timeout;
	int rx_training_needed;

	/* SRX0 & STX0 Inf0 Links are configured - begin training */
	union cvmx_spxx_clk_ctl spxx_clk_ctl;
	spxx_clk_ctl.u64 = 0;
	spxx_clk_ctl.s.seetrn = 0;
	spxx_clk_ctl.s.clkdly = 0x10;
	spxx_clk_ctl.s.runbist = 0;
	spxx_clk_ctl.s.statdrv = 0;
	/* This should always be on the opposite edge as statdrv */
	spxx_clk_ctl.s.statrcv = 1;
	spxx_clk_ctl.s.sndtrn = 1;
	spxx_clk_ctl.s.drptrn = 1;
	spxx_clk_ctl.s.rcvtrn = 1;
	spxx_clk_ctl.s.srxdlck = 1;
	cvmx_write_csr(CVMX_SPXX_CLK_CTL(interface), spxx_clk_ctl.u64);
	cvmx_wait(1000 * MS);

	/* SRX0 clear the boot bit */
	spxx_trn4_ctl.u64 = cvmx_read_csr(CVMX_SPXX_TRN4_CTL(interface));
	spxx_trn4_ctl.s.clr_boot = 1;
	cvmx_write_csr(CVMX_SPXX_TRN4_CTL(interface), spxx_trn4_ctl.u64);

	/* Wait for the training sequence to complete */
	cvmx_dprintf("SPI%d: Waiting for training\n", interface);
	cvmx_wait(1000 * MS);
	/* Wait a really long time here */
	timeout_time = cvmx_get_cycle() + 1000ull * MS * 600;
	/*
	 * The HRM says we must wait for 34 + 16 * MAXDIST training sequences.
	 * We'll be pessimistic and wait for a lot more.
	 */
	rx_training_needed = 500;
	do {
		stat.u64 = cvmx_read_csr(CVMX_SPXX_CLK_STAT(interface));
		if (stat.s.srxtrn && rx_training_needed) {
			rx_training_needed--;
			cvmx_write_csr(CVMX_SPXX_CLK_STAT(interface), stat.u64);
			stat.s.srxtrn = 0;
		}
		if (cvmx_get_cycle() > timeout_time) {
			cvmx_dprintf("SPI%d: Timeout\n", interface);
			return -1;
		}
	} while (stat.s.srxtrn == 0);

	return 0;
}

/**
 * Callback to perform calendar data synchronization
 *
 * @interface: The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 * @timeout:   Timeout to wait for calendar data in seconds
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
int cvmx_spi_calendar_sync_cb(int interface, cvmx_spi_mode_t mode, int timeout)
{
	uint64_t MS = cvmx_sysinfo_get()->cpu_clock_hz / 1000;
	if (mode & CVMX_SPI_MODE_RX_HALFPLEX) {
		/* SRX0 interface should be good, send calendar data */
		union cvmx_srxx_com_ctl srxx_com_ctl;
		cvmx_dprintf
		    ("SPI%d: Rx is synchronized, start sending calendar data\n",
		     interface);
		srxx_com_ctl.u64 = cvmx_read_csr(CVMX_SRXX_COM_CTL(interface));
		srxx_com_ctl.s.inf_en = 1;
		srxx_com_ctl.s.st_en = 1;
		cvmx_write_csr(CVMX_SRXX_COM_CTL(interface), srxx_com_ctl.u64);
	}

	if (mode & CVMX_SPI_MODE_TX_HALFPLEX) {
		/* STX0 has achieved sync */
		/* The corespondant board should be sending calendar data */
		/* Enable the STX0 STAT receiver. */
		union cvmx_spxx_clk_stat stat;
		uint64_t timeout_time;
		union cvmx_stxx_com_ctl stxx_com_ctl;
		stxx_com_ctl.u64 = 0;
		stxx_com_ctl.s.st_en = 1;
		cvmx_write_csr(CVMX_STXX_COM_CTL(interface), stxx_com_ctl.u64);

		/* Waiting for calendar sync on STX0 STAT */
		cvmx_dprintf("SPI%d: Waiting to sync on STX[%d] STAT\n",
			     interface, interface);
		timeout_time = cvmx_get_cycle() + 1000ull * MS * timeout;
		/* SPX0_CLK_STAT - SPX0_CLK_STAT[STXCAL] should be 1 (bit10) */
		do {
			stat.u64 = cvmx_read_csr(CVMX_SPXX_CLK_STAT(interface));
			if (cvmx_get_cycle() > timeout_time) {
				cvmx_dprintf("SPI%d: Timeout\n", interface);
				return -1;
			}
		} while (stat.s.stxcal == 0);
	}

	return 0;
}

/**
 * Callback to handle interface up
 *
 * @interface: The identifier of the packet interface to configure and
 *                  use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *                  can operate as a full duplex (both Tx and Rx data paths
 *                  active) or as a halfplex (either the Tx data path is
 *                  active or the Rx data path is active, but not both).
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
int cvmx_spi_interface_up_cb(int interface, cvmx_spi_mode_t mode)
{
	union cvmx_gmxx_rxx_frm_min gmxx_rxx_frm_min;
	union cvmx_gmxx_rxx_frm_max gmxx_rxx_frm_max;
	union cvmx_gmxx_rxx_jabber gmxx_rxx_jabber;

	if (mode & CVMX_SPI_MODE_RX_HALFPLEX) {
		union cvmx_srxx_com_ctl srxx_com_ctl;
		srxx_com_ctl.u64 = cvmx_read_csr(CVMX_SRXX_COM_CTL(interface));
		srxx_com_ctl.s.inf_en = 1;
		cvmx_write_csr(CVMX_SRXX_COM_CTL(interface), srxx_com_ctl.u64);
		cvmx_dprintf("SPI%d: Rx is now up\n", interface);
	}

	if (mode & CVMX_SPI_MODE_TX_HALFPLEX) {
		union cvmx_stxx_com_ctl stxx_com_ctl;
		stxx_com_ctl.u64 = cvmx_read_csr(CVMX_STXX_COM_CTL(interface));
		stxx_com_ctl.s.inf_en = 1;
		cvmx_write_csr(CVMX_STXX_COM_CTL(interface), stxx_com_ctl.u64);
		cvmx_dprintf("SPI%d: Tx is now up\n", interface);
	}

	gmxx_rxx_frm_min.u64 = 0;
	gmxx_rxx_frm_min.s.len = 64;
	cvmx_write_csr(CVMX_GMXX_RXX_FRM_MIN(0, interface),
		       gmxx_rxx_frm_min.u64);
	gmxx_rxx_frm_max.u64 = 0;
	gmxx_rxx_frm_max.s.len = 64 * 1024 - 4;
	cvmx_write_csr(CVMX_GMXX_RXX_FRM_MAX(0, interface),
		       gmxx_rxx_frm_max.u64);
	gmxx_rxx_jabber.u64 = 0;
	gmxx_rxx_jabber.s.cnt = 64 * 1024 - 4;
	cvmx_write_csr(CVMX_GMXX_RXX_JABBER(0, interface), gmxx_rxx_jabber.u64);

	return 0;
}
