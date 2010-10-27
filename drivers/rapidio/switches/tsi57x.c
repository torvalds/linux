/*
 * RapidIO Tsi57x switch family support
 *
 * Copyright 2009-2010 Integrated Device Technology, Inc.
 * Alexandre Bounine <alexandre.bounine@idt.com>
 *  - Added EM support
 *  - Modified switch operations initialization.
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_ids.h>
#include <linux/delay.h>
#include "../rio.h"

/* Global (broadcast) route registers */
#define SPBC_ROUTE_CFG_DESTID	0x10070
#define SPBC_ROUTE_CFG_PORT	0x10074

/* Per port route registers */
#define SPP_ROUTE_CFG_DESTID(n)	(0x11070 + 0x100*n)
#define SPP_ROUTE_CFG_PORT(n)	(0x11074 + 0x100*n)

#define TSI578_SP_MODE(n)	(0x11004 + n*0x100)
#define TSI578_SP_MODE_GLBL	0x10004
#define  TSI578_SP_MODE_PW_DIS	0x08000000
#define  TSI578_SP_MODE_LUT_512	0x01000000

#define TSI578_SP_CTL_INDEP(n)	(0x13004 + n*0x100)
#define TSI578_SP_LUT_PEINF(n)	(0x13010 + n*0x100)
#define TSI578_SP_CS_TX(n)	(0x13014 + n*0x100)
#define TSI578_SP_INT_STATUS(n) (0x13018 + n*0x100)

#define TSI578_GLBL_ROUTE_BASE	0x10078

static int
tsi57x_route_add_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table, u16 route_destid, u8 route_port)
{
	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_write_config_32(mport, destid, hopcount,
					  SPBC_ROUTE_CFG_DESTID, route_destid);
		rio_mport_write_config_32(mport, destid, hopcount,
					  SPBC_ROUTE_CFG_PORT, route_port);
	} else {
		rio_mport_write_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_DESTID(table), route_destid);
		rio_mport_write_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_PORT(table), route_port);
	}

	udelay(10);

	return 0;
}

static int
tsi57x_route_get_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table, u16 route_destid, u8 *route_port)
{
	int ret = 0;
	u32 result;

	if (table == RIO_GLOBAL_TABLE) {
		/* Use local RT of the ingress port to avoid possible
		   race condition */
		rio_mport_read_config_32(mport, destid, hopcount,
			RIO_SWP_INFO_CAR, &result);
		table = (result & RIO_SWP_INFO_PORT_NUM_MASK);
	}

	rio_mport_write_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_DESTID(table), route_destid);
	rio_mport_read_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_PORT(table), &result);

	*route_port = (u8)result;
	if (*route_port > 15)
		ret = -1;

	return ret;
}

static int
tsi57x_route_clr_table(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table)
{
	u32 route_idx;
	u32 lut_size;

	lut_size = (mport->sys_size) ? 0x1ff : 0xff;

	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_write_config_32(mport, destid, hopcount,
					  SPBC_ROUTE_CFG_DESTID, 0x80000000);
		for (route_idx = 0; route_idx <= lut_size; route_idx++)
			rio_mport_write_config_32(mport, destid, hopcount,
						  SPBC_ROUTE_CFG_PORT,
						  RIO_INVALID_ROUTE);
	} else {
		rio_mport_write_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_DESTID(table), 0x80000000);
		for (route_idx = 0; route_idx <= lut_size; route_idx++)
			rio_mport_write_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_PORT(table) , RIO_INVALID_ROUTE);
	}

	return 0;
}

static int
tsi57x_set_domain(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u8 sw_domain)
{
	u32 regval;

	/*
	 * Switch domain configuration operates only at global level
	 */

	/* Turn off flat (LUT_512) mode */
	rio_mport_read_config_32(mport, destid, hopcount,
				 TSI578_SP_MODE_GLBL, &regval);
	rio_mport_write_config_32(mport, destid, hopcount, TSI578_SP_MODE_GLBL,
				  regval & ~TSI578_SP_MODE_LUT_512);
	/* Set switch domain base */
	rio_mport_write_config_32(mport, destid, hopcount,
				  TSI578_GLBL_ROUTE_BASE,
				  (u32)(sw_domain << 24));
	return 0;
}

static int
tsi57x_get_domain(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u8 *sw_domain)
{
	u32 regval;

	/*
	 * Switch domain configuration operates only at global level
	 */
	rio_mport_read_config_32(mport, destid, hopcount,
				TSI578_GLBL_ROUTE_BASE, &regval);

	*sw_domain = (u8)(regval >> 24);

	return 0;
}

static int
tsi57x_em_init(struct rio_dev *rdev)
{
	struct rio_mport *mport = rdev->net->hport;
	u16 destid = rdev->rswitch->destid;
	u8 hopcount = rdev->rswitch->hopcount;
	u32 regval;
	int portnum;

	pr_debug("TSI578 %s [%d:%d]\n", __func__, destid, hopcount);

	for (portnum = 0;
	     portnum < RIO_GET_TOTAL_PORTS(rdev->swpinfo); portnum++) {
		/* Make sure that Port-Writes are enabled (for all ports) */
		rio_mport_read_config_32(mport, destid, hopcount,
				TSI578_SP_MODE(portnum), &regval);
		rio_mport_write_config_32(mport, destid, hopcount,
				TSI578_SP_MODE(portnum),
				regval & ~TSI578_SP_MODE_PW_DIS);

		/* Clear all pending interrupts */
		rio_mport_read_config_32(mport, destid, hopcount,
				rdev->phys_efptr +
					RIO_PORT_N_ERR_STS_CSR(portnum),
				&regval);
		rio_mport_write_config_32(mport, destid, hopcount,
				rdev->phys_efptr +
					RIO_PORT_N_ERR_STS_CSR(portnum),
				regval & 0x07120214);

		rio_mport_read_config_32(mport, destid, hopcount,
				TSI578_SP_INT_STATUS(portnum), &regval);
		rio_mport_write_config_32(mport, destid, hopcount,
				TSI578_SP_INT_STATUS(portnum),
				regval & 0x000700bd);

		/* Enable all interrupts to allow ports to send a port-write */
		rio_mport_read_config_32(mport, destid, hopcount,
				TSI578_SP_CTL_INDEP(portnum), &regval);
		rio_mport_write_config_32(mport, destid, hopcount,
				TSI578_SP_CTL_INDEP(portnum),
				regval | 0x000b0000);

		/* Skip next (odd) port if the current port is in x4 mode */
		rio_mport_read_config_32(mport, destid, hopcount,
				rdev->phys_efptr + RIO_PORT_N_CTL_CSR(portnum),
				&regval);
		if ((regval & RIO_PORT_N_CTL_PWIDTH) == RIO_PORT_N_CTL_PWIDTH_4)
			portnum++;
	}

	/* set TVAL = ~50us */
	rio_mport_write_config_32(mport, destid, hopcount,
		rdev->phys_efptr + RIO_PORT_LINKTO_CTL_CSR, 0x9a << 8);

	return 0;
}

static int
tsi57x_em_handler(struct rio_dev *rdev, u8 portnum)
{
	struct rio_mport *mport = rdev->net->hport;
	u16 destid = rdev->rswitch->destid;
	u8 hopcount = rdev->rswitch->hopcount;
	u32 intstat, err_status;
	int sendcount, checkcount;
	u8 route_port;
	u32 regval;

	rio_mport_read_config_32(mport, destid, hopcount,
			rdev->phys_efptr + RIO_PORT_N_ERR_STS_CSR(portnum),
			&err_status);

	if ((err_status & RIO_PORT_N_ERR_STS_PORT_OK) &&
	    (err_status & (RIO_PORT_N_ERR_STS_PW_OUT_ES |
			  RIO_PORT_N_ERR_STS_PW_INP_ES))) {
		/* Remove any queued packets by locking/unlocking port */
		rio_mport_read_config_32(mport, destid, hopcount,
			rdev->phys_efptr + RIO_PORT_N_CTL_CSR(portnum),
			&regval);
		if (!(regval & RIO_PORT_N_CTL_LOCKOUT)) {
			rio_mport_write_config_32(mport, destid, hopcount,
				rdev->phys_efptr + RIO_PORT_N_CTL_CSR(portnum),
				regval | RIO_PORT_N_CTL_LOCKOUT);
			udelay(50);
			rio_mport_write_config_32(mport, destid, hopcount,
				rdev->phys_efptr + RIO_PORT_N_CTL_CSR(portnum),
				regval);
		}

		/* Read from link maintenance response register to clear
		 * valid bit
		 */
		rio_mport_read_config_32(mport, destid, hopcount,
			rdev->phys_efptr + RIO_PORT_N_MNT_RSP_CSR(portnum),
			&regval);

		/* Send a Packet-Not-Accepted/Link-Request-Input-Status control
		 * symbol to recover from IES/OES
		 */
		sendcount = 3;
		while (sendcount) {
			rio_mport_write_config_32(mport, destid, hopcount,
					  TSI578_SP_CS_TX(portnum), 0x40fc8000);
			checkcount = 3;
			while (checkcount--) {
				udelay(50);
				rio_mport_read_config_32(
					mport, destid, hopcount,
					rdev->phys_efptr +
						RIO_PORT_N_MNT_RSP_CSR(portnum),
					&regval);
				if (regval & RIO_PORT_N_MNT_RSP_RVAL)
					goto exit_es;
			}

			sendcount--;
		}
	}

exit_es:
	/* Clear implementation specific error status bits */
	rio_mport_read_config_32(mport, destid, hopcount,
				 TSI578_SP_INT_STATUS(portnum), &intstat);
	pr_debug("TSI578[%x:%x] SP%d_INT_STATUS=0x%08x\n",
		 destid, hopcount, portnum, intstat);

	if (intstat & 0x10000) {
		rio_mport_read_config_32(mport, destid, hopcount,
				TSI578_SP_LUT_PEINF(portnum), &regval);
		regval = (mport->sys_size) ? (regval >> 16) : (regval >> 24);
		route_port = rdev->rswitch->route_table[regval];
		pr_debug("RIO: TSI578[%s] P%d LUT Parity Error (destID=%d)\n",
			rio_name(rdev), portnum, regval);
		tsi57x_route_add_entry(mport, destid, hopcount,
				RIO_GLOBAL_TABLE, regval, route_port);
	}

	rio_mport_write_config_32(mport, destid, hopcount,
				  TSI578_SP_INT_STATUS(portnum),
				  intstat & 0x000700bd);

	return 0;
}

static int tsi57x_switch_init(struct rio_dev *rdev, int do_enum)
{
	pr_debug("RIO: %s for %s\n", __func__, rio_name(rdev));
	rdev->rswitch->add_entry = tsi57x_route_add_entry;
	rdev->rswitch->get_entry = tsi57x_route_get_entry;
	rdev->rswitch->clr_table = tsi57x_route_clr_table;
	rdev->rswitch->set_domain = tsi57x_set_domain;
	rdev->rswitch->get_domain = tsi57x_get_domain;
	rdev->rswitch->em_init = tsi57x_em_init;
	rdev->rswitch->em_handle = tsi57x_em_handler;

	return 0;
}

DECLARE_RIO_SWITCH_INIT(RIO_VID_TUNDRA, RIO_DID_TSI572, tsi57x_switch_init);
DECLARE_RIO_SWITCH_INIT(RIO_VID_TUNDRA, RIO_DID_TSI574, tsi57x_switch_init);
DECLARE_RIO_SWITCH_INIT(RIO_VID_TUNDRA, RIO_DID_TSI577, tsi57x_switch_init);
DECLARE_RIO_SWITCH_INIT(RIO_VID_TUNDRA, RIO_DID_TSI578, tsi57x_switch_init);
