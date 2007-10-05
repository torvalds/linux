/*
 * arch/powerpc/sysdev/qe_lib/qe_io.c
 *
 * QE Parallel I/O ports configuration routines
 *
 * Copyright (C) Freescale Semicondutor, Inc. 2006. All rights reserved.
 *
 * Author: Li Yang <LeoLi@freescale.com>
 * Based on code from Shlomi Gridish <gridish@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <sysdev/fsl_soc.h>

#undef DEBUG

#define NUM_OF_PINS	32

struct port_regs {
	__be32	cpodr;		/* Open drain register */
	__be32	cpdata;		/* Data register */
	__be32	cpdir1;		/* Direction register */
	__be32	cpdir2;		/* Direction register */
	__be32	cppar1;		/* Pin assignment register */
	__be32	cppar2;		/* Pin assignment register */
#ifdef CONFIG_PPC_85xx
	u8	pad[8];
#endif
};

static struct port_regs *par_io = NULL;
static int num_par_io_ports = 0;

int par_io_init(struct device_node *np)
{
	struct resource res;
	int ret;
	const u32 *num_ports;

	/* Map Parallel I/O ports registers */
	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return ret;
	par_io = ioremap(res.start, res.end - res.start + 1);

	num_ports = of_get_property(np, "num-ports", NULL);
	if (num_ports)
		num_par_io_ports = *num_ports;

	return 0;
}

int par_io_config_pin(u8 port, u8 pin, int dir, int open_drain,
		      int assignment, int has_irq)
{
	u32 pin_mask1bit, pin_mask2bits, new_mask2bits, tmp_val;

	if (!par_io)
		return -1;

	/* calculate pin location for single and 2 bits information */
	pin_mask1bit = (u32) (1 << (NUM_OF_PINS - (pin + 1)));

	/* Set open drain, if required */
	tmp_val = in_be32(&par_io[port].cpodr);
	if (open_drain)
		out_be32(&par_io[port].cpodr, pin_mask1bit | tmp_val);
	else
		out_be32(&par_io[port].cpodr, ~pin_mask1bit & tmp_val);

	/* define direction */
	tmp_val = (pin > (NUM_OF_PINS / 2) - 1) ?
		in_be32(&par_io[port].cpdir2) :
		in_be32(&par_io[port].cpdir1);

	/* get all bits mask for 2 bit per port */
	pin_mask2bits = (u32) (0x3 << (NUM_OF_PINS -
				(pin % (NUM_OF_PINS / 2) + 1) * 2));

	/* Get the final mask we need for the right definition */
	new_mask2bits = (u32) (dir << (NUM_OF_PINS -
				(pin % (NUM_OF_PINS / 2) + 1) * 2));

	/* clear and set 2 bits mask */
	if (pin > (NUM_OF_PINS / 2) - 1) {
		out_be32(&par_io[port].cpdir2,
			 ~pin_mask2bits & tmp_val);
		tmp_val &= ~pin_mask2bits;
		out_be32(&par_io[port].cpdir2, new_mask2bits | tmp_val);
	} else {
		out_be32(&par_io[port].cpdir1,
			 ~pin_mask2bits & tmp_val);
		tmp_val &= ~pin_mask2bits;
		out_be32(&par_io[port].cpdir1, new_mask2bits | tmp_val);
	}
	/* define pin assignment */
	tmp_val = (pin > (NUM_OF_PINS / 2) - 1) ?
		in_be32(&par_io[port].cppar2) :
		in_be32(&par_io[port].cppar1);

	new_mask2bits = (u32) (assignment << (NUM_OF_PINS -
			(pin % (NUM_OF_PINS / 2) + 1) * 2));
	/* clear and set 2 bits mask */
	if (pin > (NUM_OF_PINS / 2) - 1) {
		out_be32(&par_io[port].cppar2,
			 ~pin_mask2bits & tmp_val);
		tmp_val &= ~pin_mask2bits;
		out_be32(&par_io[port].cppar2, new_mask2bits | tmp_val);
	} else {
		out_be32(&par_io[port].cppar1,
			 ~pin_mask2bits & tmp_val);
		tmp_val &= ~pin_mask2bits;
		out_be32(&par_io[port].cppar1, new_mask2bits | tmp_val);
	}

	return 0;
}
EXPORT_SYMBOL(par_io_config_pin);

int par_io_data_set(u8 port, u8 pin, u8 val)
{
	u32 pin_mask, tmp_val;

	if (port >= num_par_io_ports)
		return -EINVAL;
	if (pin >= NUM_OF_PINS)
		return -EINVAL;
	/* calculate pin location */
	pin_mask = (u32) (1 << (NUM_OF_PINS - 1 - pin));

	tmp_val = in_be32(&par_io[port].cpdata);

	if (val == 0)		/* clear */
		out_be32(&par_io[port].cpdata, ~pin_mask & tmp_val);
	else			/* set */
		out_be32(&par_io[port].cpdata, pin_mask | tmp_val);

	return 0;
}
EXPORT_SYMBOL(par_io_data_set);

int par_io_of_config(struct device_node *np)
{
	struct device_node *pio;
	const phandle *ph;
	int pio_map_len;
	const unsigned int *pio_map;

	if (par_io == NULL) {
		printk(KERN_ERR "par_io not initialized \n");
		return -1;
	}

	ph = of_get_property(np, "pio-handle", NULL);
	if (ph == 0) {
		printk(KERN_ERR "pio-handle not available \n");
		return -1;
	}

	pio = of_find_node_by_phandle(*ph);

	pio_map = of_get_property(pio, "pio-map", &pio_map_len);
	if (pio_map == NULL) {
		printk(KERN_ERR "pio-map is not set! \n");
		return -1;
	}
	pio_map_len /= sizeof(unsigned int);
	if ((pio_map_len % 6) != 0) {
		printk(KERN_ERR "pio-map format wrong! \n");
		return -1;
	}

	while (pio_map_len > 0) {
		par_io_config_pin((u8) pio_map[0], (u8) pio_map[1],
				(int) pio_map[2], (int) pio_map[3],
				(int) pio_map[4], (int) pio_map[5]);
		pio_map += 6;
		pio_map_len -= 6;
	}
	of_node_put(pio);
	return 0;
}
EXPORT_SYMBOL(par_io_of_config);

#ifdef DEBUG
static void dump_par_io(void)
{
	unsigned int i;

	printk(KERN_INFO "%s: par_io=%p\n", __FUNCTION__, par_io);
	for (i = 0; i < num_par_io_ports; i++) {
		printk(KERN_INFO "	cpodr[%u]=%08x\n", i,
			in_be32(&par_io[i].cpodr));
		printk(KERN_INFO "	cpdata[%u]=%08x\n", i,
			in_be32(&par_io[i].cpdata));
		printk(KERN_INFO "	cpdir1[%u]=%08x\n", i,
			in_be32(&par_io[i].cpdir1));
		printk(KERN_INFO "	cpdir2[%u]=%08x\n", i,
			in_be32(&par_io[i].cpdir2));
		printk(KERN_INFO "	cppar1[%u]=%08x\n", i,
			in_be32(&par_io[i].cppar1));
		printk(KERN_INFO "	cppar2[%u]=%08x\n", i,
			in_be32(&par_io[i].cppar2));
	}

}
EXPORT_SYMBOL(dump_par_io);
#endif /* DEBUG */
