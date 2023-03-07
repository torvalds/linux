/*
	backpack.c (c) 2001 Micro Solutions Inc.
		Released under the terms of the GNU General Public license

	backpack.c is a low-level protocol driver for the Micro Solutions
		"BACKPACK" parallel port IDE adapter
		(Works on Series 6 drives)

	Written by: Ken Hahn     (linux-dev@micro-solutions.com)
	            Clive Turvey (linux-dev@micro-solutions.com)

*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/parport.h>
#include "pata_parport.h"
#include "ppc6lnx.c"

static int bpck6_read_regr(struct pi_adapter *pi, int cont, int reg)
{
	u8 port = cont ? reg | 8 : reg;

	ppc6_send_cmd(pi, port | ACCESS_PORT | ACCESS_READ);
	return ppc6_rd_data_byte(pi);
}

static void bpck6_write_regr(struct pi_adapter *pi, int cont, int reg, int val)
{
	ppc6_wr_port(pi, cont?reg|8:reg, val);
}

static void bpck6_write_block(struct pi_adapter *pi, char *buf, int len)
{
	ppc6_wr_port16_blk(pi, ATA_REG_DATA, buf, (u32)len>>1);
}

static void bpck6_read_block(struct pi_adapter *pi, char *buf, int len)
{
	ppc6_rd_port16_blk(pi, ATA_REG_DATA, buf, (u32)len>>1);
}

static void bpck6_connect(struct pi_adapter *pi)
{
	dev_dbg(&pi->dev, "connect\n");

	ppc6_open(pi);
	ppc6_wr_extout(pi, 0x3);
}

static void bpck6_disconnect(struct pi_adapter *pi)
{
	dev_dbg(&pi->dev, "disconnect\n");
	ppc6_wr_extout(pi, 0x0);
	ppc6_deselect(pi);
}

static int bpck6_test_port(struct pi_adapter *pi)   /* check for 8-bit port */
{
	dev_dbg(&pi->dev, "PARPORT indicates modes=%x for lp=0x%lx\n",
		pi->pardev->port->modes, pi->pardev->port->base);

	/* look at the parport device to see what modes we can use */
	if (pi->pardev->port->modes & PARPORT_MODE_EPP)
		return 5; /* Can do EPP */
	if (pi->pardev->port->modes & PARPORT_MODE_TRISTATE)
		return 2;
	return 1; /* Just flat SPP */
}

static int bpck6_probe_unit(struct pi_adapter *pi)
{
	int out, saved_mode;

	dev_dbg(&pi->dev, "PROBE UNIT %x on port:%x\n", pi->unit, pi->port);

	saved_mode = pi->mode;
	/*LOWER DOWN TO UNIDIRECTIONAL*/
	pi->mode = 0;

	out = ppc6_open(pi);

	dev_dbg(&pi->dev, "ppc_open returned %2x\n", out);

  	if(out)
 	{
		ppc6_deselect(pi);
		dev_dbg(&pi->dev, "leaving probe\n");
		pi->mode = saved_mode;
               return(1);
	}
  	else
  	{
		dev_dbg(&pi->dev, "Failed open\n");
		pi->mode = saved_mode;
    		return(0);
  	}
}

static void bpck6_log_adapter(struct pi_adapter *pi)
{
	char *mode_string[5]=
		{"4-bit","8-bit","EPP-8","EPP-16","EPP-32"};

	dev_info(&pi->dev, "Micro Solutions BACKPACK Drive unit %d at 0x%x, mode:%d (%s), delay %d\n",
		pi->unit, pi->port, pi->mode, mode_string[pi->mode], pi->delay);
}

static struct pi_protocol bpck6 = {
	.owner		= THIS_MODULE,
	.name		= "bpck6",
	.max_mode	= 5,
	.epp_first	= 2, /* 2-5 use epp (need 8 ports) */
	.max_units	= 255,
	.write_regr	= bpck6_write_regr,
	.read_regr	= bpck6_read_regr,
	.write_block	= bpck6_write_block,
	.read_block	= bpck6_read_block,
	.connect	= bpck6_connect,
	.disconnect	= bpck6_disconnect,
	.test_port	= bpck6_test_port,
	.probe_unit	= bpck6_probe_unit,
	.log_adapter	= bpck6_log_adapter,
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Micro Solutions Inc.");
MODULE_DESCRIPTION("BACKPACK Protocol module, compatible with PARIDE");
module_pata_parport_driver(bpck6);
