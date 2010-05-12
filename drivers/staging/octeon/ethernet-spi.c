/**********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
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
**********************************************************************/
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <net/dst.h>

#include <asm/octeon/octeon.h>

#include "ethernet-defines.h"
#include "octeon-ethernet.h"
#include "ethernet-util.h"

#include "cvmx-spi.h"

#include <asm/octeon/cvmx-npi-defs.h>
#include "cvmx-spxx-defs.h"
#include "cvmx-stxx-defs.h"

static int number_spi_ports;
static int need_retrain[2] = { 0, 0 };

static irqreturn_t cvm_oct_spi_rml_interrupt(int cpl, void *dev_id)
{
	irqreturn_t return_status = IRQ_NONE;
	union cvmx_npi_rsl_int_blocks rsl_int_blocks;

	/* Check and see if this interrupt was caused by the GMX block */
	rsl_int_blocks.u64 = cvmx_read_csr(CVMX_NPI_RSL_INT_BLOCKS);
	if (rsl_int_blocks.s.spx1) {	/* 19 - SPX1_INT_REG & STX1_INT_REG */

		union cvmx_spxx_int_reg spx_int_reg;
		union cvmx_stxx_int_reg stx_int_reg;

		spx_int_reg.u64 = cvmx_read_csr(CVMX_SPXX_INT_REG(1));
		cvmx_write_csr(CVMX_SPXX_INT_REG(1), spx_int_reg.u64);
		if (!need_retrain[1]) {

			spx_int_reg.u64 &= cvmx_read_csr(CVMX_SPXX_INT_MSK(1));
			if (spx_int_reg.s.spf)
				pr_err("SPI1: SRX Spi4 interface down\n");
			if (spx_int_reg.s.calerr)
				pr_err("SPI1: SRX Spi4 Calendar table "
				       "parity error\n");
			if (spx_int_reg.s.syncerr)
				pr_err("SPI1: SRX Consecutive Spi4 DIP4 "
				       "errors have exceeded "
				       "SPX_ERR_CTL[ERRCNT]\n");
			if (spx_int_reg.s.diperr)
				pr_err("SPI1: SRX Spi4 DIP4 error\n");
			if (spx_int_reg.s.tpaovr)
				pr_err("SPI1: SRX Selected port has hit "
				       "TPA overflow\n");
			if (spx_int_reg.s.rsverr)
				pr_err("SPI1: SRX Spi4 reserved control "
				       "word detected\n");
			if (spx_int_reg.s.drwnng)
				pr_err("SPI1: SRX Spi4 receive FIFO "
				       "drowning/overflow\n");
			if (spx_int_reg.s.clserr)
				pr_err("SPI1: SRX Spi4 packet closed on "
				       "non-16B alignment without EOP\n");
			if (spx_int_reg.s.spiovr)
				pr_err("SPI1: SRX Spi4 async FIFO overflow\n");
			if (spx_int_reg.s.abnorm)
				pr_err("SPI1: SRX Abnormal packet "
				       "termination (ERR bit)\n");
			if (spx_int_reg.s.prtnxa)
				pr_err("SPI1: SRX Port out of range\n");
		}

		stx_int_reg.u64 = cvmx_read_csr(CVMX_STXX_INT_REG(1));
		cvmx_write_csr(CVMX_STXX_INT_REG(1), stx_int_reg.u64);
		if (!need_retrain[1]) {

			stx_int_reg.u64 &= cvmx_read_csr(CVMX_STXX_INT_MSK(1));
			if (stx_int_reg.s.syncerr)
				pr_err("SPI1: STX Interface encountered a "
				       "fatal error\n");
			if (stx_int_reg.s.frmerr)
				pr_err("SPI1: STX FRMCNT has exceeded "
				       "STX_DIP_CNT[MAXFRM]\n");
			if (stx_int_reg.s.unxfrm)
				pr_err("SPI1: STX Unexpected framing "
				       "sequence\n");
			if (stx_int_reg.s.nosync)
				pr_err("SPI1: STX ERRCNT has exceeded "
				       "STX_DIP_CNT[MAXDIP]\n");
			if (stx_int_reg.s.diperr)
				pr_err("SPI1: STX DIP2 error on the Spi4 "
				       "Status channel\n");
			if (stx_int_reg.s.datovr)
				pr_err("SPI1: STX Spi4 FIFO overflow error\n");
			if (stx_int_reg.s.ovrbst)
				pr_err("SPI1: STX Transmit packet burst "
				       "too big\n");
			if (stx_int_reg.s.calpar1)
				pr_err("SPI1: STX Calendar Table Parity "
				       "Error Bank1\n");
			if (stx_int_reg.s.calpar0)
				pr_err("SPI1: STX Calendar Table Parity "
				       "Error Bank0\n");
		}

		cvmx_write_csr(CVMX_SPXX_INT_MSK(1), 0);
		cvmx_write_csr(CVMX_STXX_INT_MSK(1), 0);
		need_retrain[1] = 1;
		return_status = IRQ_HANDLED;
	}

	if (rsl_int_blocks.s.spx0) {	/* 18 - SPX0_INT_REG & STX0_INT_REG */
		union cvmx_spxx_int_reg spx_int_reg;
		union cvmx_stxx_int_reg stx_int_reg;

		spx_int_reg.u64 = cvmx_read_csr(CVMX_SPXX_INT_REG(0));
		cvmx_write_csr(CVMX_SPXX_INT_REG(0), spx_int_reg.u64);
		if (!need_retrain[0]) {

			spx_int_reg.u64 &= cvmx_read_csr(CVMX_SPXX_INT_MSK(0));
			if (spx_int_reg.s.spf)
				pr_err("SPI0: SRX Spi4 interface down\n");
			if (spx_int_reg.s.calerr)
				pr_err("SPI0: SRX Spi4 Calendar table "
				       "parity error\n");
			if (spx_int_reg.s.syncerr)
				pr_err("SPI0: SRX Consecutive Spi4 DIP4 "
				       "errors have exceeded "
				       "SPX_ERR_CTL[ERRCNT]\n");
			if (spx_int_reg.s.diperr)
				pr_err("SPI0: SRX Spi4 DIP4 error\n");
			if (spx_int_reg.s.tpaovr)
				pr_err("SPI0: SRX Selected port has hit "
				       "TPA overflow\n");
			if (spx_int_reg.s.rsverr)
				pr_err("SPI0: SRX Spi4 reserved control "
				       "word detected\n");
			if (spx_int_reg.s.drwnng)
				pr_err("SPI0: SRX Spi4 receive FIFO "
				       "drowning/overflow\n");
			if (spx_int_reg.s.clserr)
				pr_err("SPI0: SRX Spi4 packet closed on "
				       "non-16B alignment without EOP\n");
			if (spx_int_reg.s.spiovr)
				pr_err("SPI0: SRX Spi4 async FIFO overflow\n");
			if (spx_int_reg.s.abnorm)
				pr_err("SPI0: SRX Abnormal packet "
				       "termination (ERR bit)\n");
			if (spx_int_reg.s.prtnxa)
				pr_err("SPI0: SRX Port out of range\n");
		}

		stx_int_reg.u64 = cvmx_read_csr(CVMX_STXX_INT_REG(0));
		cvmx_write_csr(CVMX_STXX_INT_REG(0), stx_int_reg.u64);
		if (!need_retrain[0]) {

			stx_int_reg.u64 &= cvmx_read_csr(CVMX_STXX_INT_MSK(0));
			if (stx_int_reg.s.syncerr)
				pr_err("SPI0: STX Interface encountered a "
				       "fatal error\n");
			if (stx_int_reg.s.frmerr)
				pr_err("SPI0: STX FRMCNT has exceeded "
				       "STX_DIP_CNT[MAXFRM]\n");
			if (stx_int_reg.s.unxfrm)
				pr_err("SPI0: STX Unexpected framing "
				       "sequence\n");
			if (stx_int_reg.s.nosync)
				pr_err("SPI0: STX ERRCNT has exceeded "
				       "STX_DIP_CNT[MAXDIP]\n");
			if (stx_int_reg.s.diperr)
				pr_err("SPI0: STX DIP2 error on the Spi4 "
				       "Status channel\n");
			if (stx_int_reg.s.datovr)
				pr_err("SPI0: STX Spi4 FIFO overflow error\n");
			if (stx_int_reg.s.ovrbst)
				pr_err("SPI0: STX Transmit packet burst "
				       "too big\n");
			if (stx_int_reg.s.calpar1)
				pr_err("SPI0: STX Calendar Table Parity "
				       "Error Bank1\n");
			if (stx_int_reg.s.calpar0)
				pr_err("SPI0: STX Calendar Table Parity "
				       "Error Bank0\n");
		}

		cvmx_write_csr(CVMX_SPXX_INT_MSK(0), 0);
		cvmx_write_csr(CVMX_STXX_INT_MSK(0), 0);
		need_retrain[0] = 1;
		return_status = IRQ_HANDLED;
	}

	return return_status;
}

static void cvm_oct_spi_enable_error_reporting(int interface)
{
	union cvmx_spxx_int_msk spxx_int_msk;
	union cvmx_stxx_int_msk stxx_int_msk;

	spxx_int_msk.u64 = cvmx_read_csr(CVMX_SPXX_INT_MSK(interface));
	spxx_int_msk.s.calerr = 1;
	spxx_int_msk.s.syncerr = 1;
	spxx_int_msk.s.diperr = 1;
	spxx_int_msk.s.tpaovr = 1;
	spxx_int_msk.s.rsverr = 1;
	spxx_int_msk.s.drwnng = 1;
	spxx_int_msk.s.clserr = 1;
	spxx_int_msk.s.spiovr = 1;
	spxx_int_msk.s.abnorm = 1;
	spxx_int_msk.s.prtnxa = 1;
	cvmx_write_csr(CVMX_SPXX_INT_MSK(interface), spxx_int_msk.u64);

	stxx_int_msk.u64 = cvmx_read_csr(CVMX_STXX_INT_MSK(interface));
	stxx_int_msk.s.frmerr = 1;
	stxx_int_msk.s.unxfrm = 1;
	stxx_int_msk.s.nosync = 1;
	stxx_int_msk.s.diperr = 1;
	stxx_int_msk.s.datovr = 1;
	stxx_int_msk.s.ovrbst = 1;
	stxx_int_msk.s.calpar1 = 1;
	stxx_int_msk.s.calpar0 = 1;
	cvmx_write_csr(CVMX_STXX_INT_MSK(interface), stxx_int_msk.u64);
}

static void cvm_oct_spi_poll(struct net_device *dev)
{
	static int spi4000_port;
	struct octeon_ethernet *priv = netdev_priv(dev);
	int interface;

	for (interface = 0; interface < 2; interface++) {

		if ((priv->port == interface * 16) && need_retrain[interface]) {

			if (cvmx_spi_restart_interface
			    (interface, CVMX_SPI_MODE_DUPLEX, 10) == 0) {
				need_retrain[interface] = 0;
				cvm_oct_spi_enable_error_reporting(interface);
			}
		}

		/*
		 * The SPI4000 TWSI interface is very slow. In order
		 * not to bring the system to a crawl, we only poll a
		 * single port every second. This means negotiation
		 * speed changes take up to 10 seconds, but at least
		 * we don't waste absurd amounts of time waiting for
		 * TWSI.
		 */
		if (priv->port == spi4000_port) {
			/*
			 * This function does nothing if it is called on an
			 * interface without a SPI4000.
			 */
			cvmx_spi4000_check_speed(interface, priv->port);
			/*
			 * Normal ordering increments. By decrementing
			 * we only match once per iteration.
			 */
			spi4000_port--;
			if (spi4000_port < 0)
				spi4000_port = 10;
		}
	}
}

int cvm_oct_spi_init(struct net_device *dev)
{
	int r;
	struct octeon_ethernet *priv = netdev_priv(dev);

	if (number_spi_ports == 0) {
		r = request_irq(OCTEON_IRQ_RML, cvm_oct_spi_rml_interrupt,
				IRQF_SHARED, "SPI", &number_spi_ports);
	}
	number_spi_ports++;

	if ((priv->port == 0) || (priv->port == 16)) {
		cvm_oct_spi_enable_error_reporting(INTERFACE(priv->port));
		priv->poll = cvm_oct_spi_poll;
	}
	cvm_oct_common_init(dev);
	return 0;
}

void cvm_oct_spi_uninit(struct net_device *dev)
{
	int interface;

	cvm_oct_common_uninit(dev);
	number_spi_ports--;
	if (number_spi_ports == 0) {
		for (interface = 0; interface < 2; interface++) {
			cvmx_write_csr(CVMX_SPXX_INT_MSK(interface), 0);
			cvmx_write_csr(CVMX_STXX_INT_MSK(interface), 0);
		}
		free_irq(OCTEON_IRQ_RML, &number_spi_ports);
	}
}
