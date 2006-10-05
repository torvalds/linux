/*
 * Embedded Planet RPX Lite MPC8xx CPM I2C interface.
 * Copyright (c) 1999 Dan Malek (dmalek@jlc.net).
 *
 * moved into proper i2c interface;
 * Brad Parker (brad@heeltoe.com)
 *
 * RPX lite specific parts of the i2c interface
 * Update:  There actually isn't anything RPXLite-specific about this module.
 * This should work for most any 8xx board.  The console messages have been 
 * changed to eliminate RPXLite references.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/stddef.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-8xx.h>
#include <asm/mpc8xx.h>
#include <asm/commproc.h>


static void
rpx_iic_init(struct i2c_algo_8xx_data *data)
{
	volatile cpm8xx_t *cp;
	volatile immap_t *immap;

	cp = cpmp;	/* Get pointer to Communication Processor */
	immap = (immap_t *)IMAP_ADDR;	/* and to internal registers */

	data->iip = (iic_t *)&cp->cp_dparam[PROFF_IIC];

	/* Check for and use a microcode relocation patch.
	*/
	if ((data->reloc = data->iip->iic_rpbase))
		data->iip = (iic_t *)&cp->cp_dpmem[data->iip->iic_rpbase];
		
	data->i2c = (i2c8xx_t *)&(immap->im_i2c);
	data->cp = cp;

	/* Initialize Port B IIC pins.
	*/
	cp->cp_pbpar |= 0x00000030;
	cp->cp_pbdir |= 0x00000030;
	cp->cp_pbodr |= 0x00000030;

	/* Allocate space for two transmit and two receive buffer
	 * descriptors in the DP ram.
	 */
	data->dp_addr = cpm_dpalloc(sizeof(cbd_t) * 4, 8);
		
	/* ptr to i2c area */
	data->i2c = (i2c8xx_t *)&(((immap_t *)IMAP_ADDR)->im_i2c);
}

static int rpx_install_isr(int irq, void (*func)(void *), void *data)
{
	/* install interrupt handler */
	cpm_install_handler(irq, func, data);

	return 0;
}

static struct i2c_algo_8xx_data rpx_data = {
	.setisr = rpx_install_isr
};

static struct i2c_adapter rpx_ops = {
	.owner		= THIS_MODULE,
	.name		= "m8xx",
	.id		= I2C_HW_MPC8XX_EPON,
	.algo_data	= &rpx_data,
};

int __init i2c_rpx_init(void)
{
	printk(KERN_INFO "i2c-rpx: i2c MPC8xx driver\n");

	/* reset hardware to sane state */
	rpx_iic_init(&rpx_data);

	if (i2c_8xx_add_bus(&rpx_ops) < 0) {
		printk(KERN_ERR "i2c-rpx: Unable to register with I2C\n");
		return -ENODEV;
	}

	return 0;
}

void __exit i2c_rpx_exit(void)
{
	i2c_8xx_del_bus(&rpx_ops);
}

MODULE_AUTHOR("Dan Malek <dmalek@jlc.net>");
MODULE_DESCRIPTION("I2C-Bus adapter routines for MPC8xx boards");

module_init(i2c_rpx_init);
module_exit(i2c_rpx_exit);
