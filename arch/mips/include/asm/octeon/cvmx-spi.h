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
 * This file contains defines for the SPI interface
 */
#ifndef __CVMX_SPI_H__
#define __CVMX_SPI_H__

#include <asm/octeon/cvmx-gmxx-defs.h>

/* CSR typedefs have been moved to cvmx-csr-*.h */

typedef enum {
	CVMX_SPI_MODE_UNKNOWN = 0,
	CVMX_SPI_MODE_TX_HALFPLEX = 1,
	CVMX_SPI_MODE_RX_HALFPLEX = 2,
	CVMX_SPI_MODE_DUPLEX = 3
} cvmx_spi_mode_t;

/** Callbacks structure to customize SPI4 initialization sequence */
typedef struct {
    /** Called to reset SPI4 DLL */
	int (*reset_cb) (int interface, cvmx_spi_mode_t mode);

    /** Called to setup calendar */
	int (*calendar_setup_cb) (int interface, cvmx_spi_mode_t mode,
				  int num_ports);

    /** Called for Tx and Rx clock detection */
	int (*clock_detect_cb) (int interface, cvmx_spi_mode_t mode,
				int timeout);

    /** Called to perform link training */
	int (*training_cb) (int interface, cvmx_spi_mode_t mode, int timeout);

    /** Called for calendar data synchronization */
	int (*calendar_sync_cb) (int interface, cvmx_spi_mode_t mode,
				 int timeout);

    /** Called when interface is up */
	int (*interface_up_cb) (int interface, cvmx_spi_mode_t mode);

} cvmx_spi_callbacks_t;

/**
 * Return true if the supplied interface is configured for SPI
 *
 * @interface: Interface to check
 * Returns True if interface is SPI
 */
static inline int cvmx_spi_is_spi_interface(int interface)
{
	uint64_t gmxState = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));
	return (gmxState & 0x2) && (gmxState & 0x1);
}

/**
 * Initialize and start the SPI interface.
 *
 * @interface: The identifier of the packet interface to configure and
 *		    use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *		    can operate as a full duplex (both Tx and Rx data paths
 *		    active) or as a halfplex (either the Tx data path is
 *		    active or the Rx data path is active, but not both).
 * @timeout:   Timeout to wait for clock synchronization in seconds
 * @num_ports: Number of SPI ports to configure
 *
 * Returns Zero on success, negative of failure.
 */
extern int cvmx_spi_start_interface(int interface, cvmx_spi_mode_t mode,
				    int timeout, int num_ports);

/**
 * This routine restarts the SPI interface after it has lost synchronization
 * with its corespondant system.
 *
 * @interface: The identifier of the packet interface to configure and
 *		    use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *		    can operate as a full duplex (both Tx and Rx data paths
 *		    active) or as a halfplex (either the Tx data path is
 *		    active or the Rx data path is active, but not both).
 * @timeout:   Timeout to wait for clock synchronization in seconds
 * Returns Zero on success, negative of failure.
 */
extern int cvmx_spi_restart_interface(int interface, cvmx_spi_mode_t mode,
				      int timeout);

/**
 * Return non-zero if the SPI interface has a SPI4000 attached
 *
 * @interface: SPI interface the SPI4000 is connected to
 *
 * Returns
 */
static inline int cvmx_spi4000_is_present(int interface)
{
	return 0;
}

/**
 * Initialize the SPI4000 for use
 *
 * @interface: SPI interface the SPI4000 is connected to
 */
static inline int cvmx_spi4000_initialize(int interface)
{
	return 0;
}

/**
 * Poll all the SPI4000 port and check its speed
 *
 * @interface: Interface the SPI4000 is on
 * @port:      Port to poll (0-9)
 * Returns Status of the port. 0=down. All other values the port is up.
 */
static inline union cvmx_gmxx_rxx_rx_inbnd cvmx_spi4000_check_speed(
	int interface,
	int port)
{
	union cvmx_gmxx_rxx_rx_inbnd r;
	r.u64 = 0;
	return r;
}

/**
 * Get current SPI4 initialization callbacks
 *
 * @callbacks:	Pointer to the callbacks structure.to fill
 *
 * Returns Pointer to cvmx_spi_callbacks_t structure.
 */
extern void cvmx_spi_get_callbacks(cvmx_spi_callbacks_t *callbacks);

/**
 * Set new SPI4 initialization callbacks
 *
 * @new_callbacks:  Pointer to an updated callbacks structure.
 */
extern void cvmx_spi_set_callbacks(cvmx_spi_callbacks_t *new_callbacks);

/**
 * Callback to perform SPI4 reset
 *
 * @interface: The identifier of the packet interface to configure and
 *		    use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *		    can operate as a full duplex (both Tx and Rx data paths
 *		    active) or as a halfplex (either the Tx data path is
 *		    active or the Rx data path is active, but not both).
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
extern int cvmx_spi_reset_cb(int interface, cvmx_spi_mode_t mode);

/**
 * Callback to setup calendar and miscellaneous settings before clock
 * detection
 *
 * @interface: The identifier of the packet interface to configure and
 *		    use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *		    can operate as a full duplex (both Tx and Rx data paths
 *		    active) or as a halfplex (either the Tx data path is
 *		    active or the Rx data path is active, but not both).
 * @num_ports: Number of ports to configure on SPI
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
extern int cvmx_spi_calendar_setup_cb(int interface, cvmx_spi_mode_t mode,
				      int num_ports);

/**
 * Callback to perform clock detection
 *
 * @interface: The identifier of the packet interface to configure and
 *		    use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *		    can operate as a full duplex (both Tx and Rx data paths
 *		    active) or as a halfplex (either the Tx data path is
 *		    active or the Rx data path is active, but not both).
 * @timeout:   Timeout to wait for clock synchronization in seconds
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
extern int cvmx_spi_clock_detect_cb(int interface, cvmx_spi_mode_t mode,
				    int timeout);

/**
 * Callback to perform link training
 *
 * @interface: The identifier of the packet interface to configure and
 *		    use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *		    can operate as a full duplex (both Tx and Rx data paths
 *		    active) or as a halfplex (either the Tx data path is
 *		    active or the Rx data path is active, but not both).
 * @timeout:   Timeout to wait for link to be trained (in seconds)
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
extern int cvmx_spi_training_cb(int interface, cvmx_spi_mode_t mode,
				int timeout);

/**
 * Callback to perform calendar data synchronization
 *
 * @interface: The identifier of the packet interface to configure and
 *		    use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *		    can operate as a full duplex (both Tx and Rx data paths
 *		    active) or as a halfplex (either the Tx data path is
 *		    active or the Rx data path is active, but not both).
 * @timeout:   Timeout to wait for calendar data in seconds
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
extern int cvmx_spi_calendar_sync_cb(int interface, cvmx_spi_mode_t mode,
				     int timeout);

/**
 * Callback to handle interface up
 *
 * @interface: The identifier of the packet interface to configure and
 *		    use as a SPI interface.
 * @mode:      The operating mode for the SPI interface. The interface
 *		    can operate as a full duplex (both Tx and Rx data paths
 *		    active) or as a halfplex (either the Tx data path is
 *		    active or the Rx data path is active, but not both).
 *
 * Returns Zero on success, non-zero error code on failure (will cause
 * SPI initialization to abort)
 */
extern int cvmx_spi_interface_up_cb(int interface, cvmx_spi_mode_t mode);

#endif /* __CVMX_SPI_H__ */
