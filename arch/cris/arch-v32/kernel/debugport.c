/*
 * Copyright (C) 2003, Axis Communications AB.
 */

#include <linux/console.h>
#include <linux/init.h>
#include <asm/system.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/reg_map.h>
#include <hwregs/ser_defs.h>
#include <hwregs/dma_defs.h>
#include <asm/arch/mach/pinmux.h>

struct dbg_port
{
	unsigned char nbr;
	unsigned long instance;
	unsigned int started;
	unsigned long baudrate;
	unsigned char parity;
	unsigned int bits;
};

struct dbg_port ports[] =
{
  {
    0,
    regi_ser0,
    0,
    115200,
    'N',
    8
  },
  {
    1,
    regi_ser1,
    0,
    115200,
    'N',
    8
  },
  {
    2,
    regi_ser2,
    0,
    115200,
    'N',
    8
  },
  {
    3,
    regi_ser3,
    0,
    115200,
    'N',
    8
  },
#if CONFIG_ETRAX_SERIAL_PORTS == 5
  {
    4,
    regi_ser4,
    0,
    115200,
    'N',
    8
  },
#endif
};
static struct dbg_port *port =
#if defined(CONFIG_ETRAX_DEBUG_PORT0)
	&ports[0];
#elif defined(CONFIG_ETRAX_DEBUG_PORT1)
	&ports[1];
#elif defined(CONFIG_ETRAX_DEBUG_PORT2)
	&ports[2];
#elif defined(CONFIG_ETRAX_DEBUG_PORT3)
	&ports[3];
#elif defined(CONFIG_ETRAX_DEBUG_PORT4)
	&ports[4];
#else
	NULL;
#endif

#ifdef CONFIG_ETRAX_KGDB
static struct dbg_port *kgdb_port =
#if defined(CONFIG_ETRAX_KGDB_PORT0)
	&ports[0];
#elif defined(CONFIG_ETRAX_KGDB_PORT1)
	&ports[1];
#elif defined(CONFIG_ETRAX_KGDB_PORT2)
	&ports[2];
#elif defined(CONFIG_ETRAX_KGDB_PORT3)
	&ports[3];
#elif defined(CONFIG_ETRAX_KGDB_PORT4)
	&ports[4];
#else
	NULL;
#endif
#endif

static void
start_port(struct dbg_port* p)
{
	if (!p)
		return;

	if (p->started)
		return;
	p->started = 1;

	if (p->nbr == 1)
		crisv32_pinmux_alloc_fixed(pinmux_ser1);
	else if (p->nbr == 2)
		crisv32_pinmux_alloc_fixed(pinmux_ser2);
	else if (p->nbr == 3)
		crisv32_pinmux_alloc_fixed(pinmux_ser3);
#if CONFIG_ETRAX_SERIAL_PORTS == 5
	else if (p->nbr == 4)
		crisv32_pinmux_alloc_fixed(pinmux_ser4);
#endif

	/* Set up serial port registers */
	reg_ser_rw_tr_ctrl tr_ctrl = {0};
	reg_ser_rw_tr_dma_en tr_dma_en = {0};

	reg_ser_rw_rec_ctrl rec_ctrl = {0};
	reg_ser_rw_tr_baud_div tr_baud_div = {0};
	reg_ser_rw_rec_baud_div rec_baud_div = {0};

	tr_ctrl.base_freq = rec_ctrl.base_freq = regk_ser_f29_493;
	tr_dma_en.en = rec_ctrl.dma_mode = regk_ser_no;
	tr_baud_div.div = rec_baud_div.div = 29493000 / p->baudrate / 8;
	tr_ctrl.en = rec_ctrl.en = 1;

	if (p->parity == 'O')
	{
		tr_ctrl.par_en = regk_ser_yes;
		tr_ctrl.par = regk_ser_odd;
		rec_ctrl.par_en = regk_ser_yes;
		rec_ctrl.par = regk_ser_odd;
	}
	else if (p->parity == 'E')
	{
		tr_ctrl.par_en = regk_ser_yes;
		tr_ctrl.par = regk_ser_even;
		rec_ctrl.par_en = regk_ser_yes;
		rec_ctrl.par = regk_ser_odd;
	}

	if (p->bits == 7)
	{
		tr_ctrl.data_bits = regk_ser_bits7;
		rec_ctrl.data_bits = regk_ser_bits7;
	}

	REG_WR (ser, p->instance, rw_tr_baud_div, tr_baud_div);
	REG_WR (ser, p->instance, rw_rec_baud_div, rec_baud_div);
	REG_WR (ser, p->instance, rw_tr_dma_en, tr_dma_en);
	REG_WR (ser, p->instance, rw_tr_ctrl, tr_ctrl);
	REG_WR (ser, p->instance, rw_rec_ctrl, rec_ctrl);
}

#ifdef CONFIG_ETRAX_KGDB
/* Use polling to get a single character from the kernel debug port */
int
getDebugChar(void)
{
	reg_ser_rs_stat_din stat;
	reg_ser_rw_ack_intr ack_intr = { 0 };

	do {
		stat = REG_RD(ser, kgdb_port->instance, rs_stat_din);
	} while (!stat.dav);

	/* Ack the data_avail interrupt. */
	ack_intr.dav = 1;
	REG_WR(ser, kgdb_port->instance, rw_ack_intr, ack_intr);

	return stat.data;
}

/* Use polling to put a single character to the kernel debug port */
void
putDebugChar(int val)
{
	reg_ser_r_stat_din stat;
	do {
		stat = REG_RD(ser, kgdb_port->instance, r_stat_din);
	} while (!stat.tr_rdy);
	REG_WR_INT(ser, kgdb_port->instance, rw_dout, val);
}
#endif /* CONFIG_ETRAX_KGDB */

/* Register console for printk's, etc. */
int __init
init_etrax_debug(void)
{
        start_port(port);

#ifdef CONFIG_ETRAX_KGDB
	start_port(kgdb_port);
#endif /* CONFIG_ETRAX_KGDB */
	return 0;
}
