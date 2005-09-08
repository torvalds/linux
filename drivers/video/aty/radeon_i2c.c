#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/fb.h>


#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/i2c-algo-bit.h>

#include <asm/io.h>

#include <video/radeon.h>
#include "radeonfb.h"
#include "../edid.h"

#define RADEON_DDC 	0x50

static void radeon_gpio_setscl(void* data, int state)
{
	struct radeon_i2c_chan 	*chan = data;
	struct radeonfb_info	*rinfo = chan->rinfo;
	u32			val;
	
	val = INREG(chan->ddc_reg) & ~(VGA_DDC_CLK_OUT_EN);
	if (!state)
		val |= VGA_DDC_CLK_OUT_EN;

	OUTREG(chan->ddc_reg, val);
	(void)INREG(chan->ddc_reg);
}

static void radeon_gpio_setsda(void* data, int state)
{
	struct radeon_i2c_chan 	*chan = data;
	struct radeonfb_info	*rinfo = chan->rinfo;
	u32			val;
	
	val = INREG(chan->ddc_reg) & ~(VGA_DDC_DATA_OUT_EN);
	if (!state)
		val |= VGA_DDC_DATA_OUT_EN;

	OUTREG(chan->ddc_reg, val);
	(void)INREG(chan->ddc_reg);
}

static int radeon_gpio_getscl(void* data)
{
	struct radeon_i2c_chan 	*chan = data;
	struct radeonfb_info	*rinfo = chan->rinfo;
	u32			val;
	
	val = INREG(chan->ddc_reg);

	return (val & VGA_DDC_CLK_INPUT) ? 1 : 0;
}

static int radeon_gpio_getsda(void* data)
{
	struct radeon_i2c_chan 	*chan = data;
	struct radeonfb_info	*rinfo = chan->rinfo;
	u32			val;
	
	val = INREG(chan->ddc_reg);

	return (val & VGA_DDC_DATA_INPUT) ? 1 : 0;
}

static int radeon_setup_i2c_bus(struct radeon_i2c_chan *chan, const char *name)
{
	int rc;

	strcpy(chan->adapter.name, name);
	chan->adapter.owner		= THIS_MODULE;
	chan->adapter.id		= I2C_HW_B_RADEON;
	chan->adapter.algo_data		= &chan->algo;
	chan->adapter.dev.parent	= &chan->rinfo->pdev->dev;
	chan->algo.setsda		= radeon_gpio_setsda;
	chan->algo.setscl		= radeon_gpio_setscl;
	chan->algo.getsda		= radeon_gpio_getsda;
	chan->algo.getscl		= radeon_gpio_getscl;
	chan->algo.udelay		= 40;
	chan->algo.timeout		= 20;
	chan->algo.data 		= chan;	
	
	i2c_set_adapdata(&chan->adapter, chan);
	
	/* Raise SCL and SDA */
	radeon_gpio_setsda(chan, 1);
	radeon_gpio_setscl(chan, 1);
	udelay(20);

	rc = i2c_bit_add_bus(&chan->adapter);
	if (rc == 0)
		dev_dbg(&chan->rinfo->pdev->dev, "I2C bus %s registered.\n", name);
	else
		dev_warn(&chan->rinfo->pdev->dev, "Failed to register I2C bus %s.\n", name);
	return rc;
}

void radeon_create_i2c_busses(struct radeonfb_info *rinfo)
{
	rinfo->i2c[0].rinfo	= rinfo;
	rinfo->i2c[0].ddc_reg	= GPIO_MONID;
	radeon_setup_i2c_bus(&rinfo->i2c[0], "monid");

	rinfo->i2c[1].rinfo	= rinfo;
	rinfo->i2c[1].ddc_reg	= GPIO_DVI_DDC;
	radeon_setup_i2c_bus(&rinfo->i2c[1], "dvi");

	rinfo->i2c[2].rinfo	= rinfo;
	rinfo->i2c[2].ddc_reg	= GPIO_VGA_DDC;
	radeon_setup_i2c_bus(&rinfo->i2c[2], "vga");

	rinfo->i2c[3].rinfo	= rinfo;
	rinfo->i2c[3].ddc_reg	= GPIO_CRT2_DDC;
	radeon_setup_i2c_bus(&rinfo->i2c[3], "crt2");
}

void radeon_delete_i2c_busses(struct radeonfb_info *rinfo)
{
	if (rinfo->i2c[0].rinfo)
		i2c_bit_del_bus(&rinfo->i2c[0].adapter);
	rinfo->i2c[0].rinfo = NULL;

	if (rinfo->i2c[1].rinfo)
		i2c_bit_del_bus(&rinfo->i2c[1].adapter);
	rinfo->i2c[1].rinfo = NULL;

	if (rinfo->i2c[2].rinfo)
		i2c_bit_del_bus(&rinfo->i2c[2].adapter);
	rinfo->i2c[2].rinfo = NULL;

	if (rinfo->i2c[3].rinfo)
		i2c_bit_del_bus(&rinfo->i2c[3].adapter);
	rinfo->i2c[3].rinfo = NULL;
}


static u8 *radeon_do_probe_i2c_edid(struct radeon_i2c_chan *chan)
{
	u8 start = 0x0;
	struct i2c_msg msgs[] = {
		{
			.addr	= RADEON_DDC,
			.len	= 1,
			.buf	= &start,
		}, {
			.addr	= RADEON_DDC,
			.flags	= I2C_M_RD,
			.len	= EDID_LENGTH,
		},
	};
	u8 *buf;

	buf = kmalloc(EDID_LENGTH, GFP_KERNEL);
	if (!buf) {
		dev_warn(&chan->rinfo->pdev->dev, "Out of memory!\n");
		return NULL;
	}
	msgs[1].buf = buf;

	if (i2c_transfer(&chan->adapter, msgs, 2) == 2)
		return buf;
	dev_dbg(&chan->rinfo->pdev->dev, "Unable to read EDID block.\n");
	kfree(buf);
	return NULL;
}


int radeon_probe_i2c_connector(struct radeonfb_info *rinfo, int conn, u8 **out_edid)
{
	u32 reg = rinfo->i2c[conn-1].ddc_reg;
	u8 *edid = NULL;
	int i, j;

	OUTREG(reg, INREG(reg) & 
			~(VGA_DDC_DATA_OUTPUT | VGA_DDC_CLK_OUTPUT));

	OUTREG(reg, INREG(reg) & ~(VGA_DDC_CLK_OUT_EN));
	(void)INREG(reg);

	for (i = 0; i < 3; i++) {
		/* For some old monitors we need the
		 * following process to initialize/stop DDC
		 */
		OUTREG(reg, INREG(reg) & ~(VGA_DDC_DATA_OUT_EN));
		(void)INREG(reg);
		msleep(13);

		OUTREG(reg, INREG(reg) & ~(VGA_DDC_CLK_OUT_EN));
		(void)INREG(reg);
		for (j = 0; j < 5; j++) {
			msleep(10);
			if (INREG(reg) & VGA_DDC_CLK_INPUT)
				break;
		}
		if (j == 5)
			continue;

		OUTREG(reg, INREG(reg) | VGA_DDC_DATA_OUT_EN);
		(void)INREG(reg);
		msleep(15);
		OUTREG(reg, INREG(reg) | VGA_DDC_CLK_OUT_EN);
		(void)INREG(reg);
		msleep(15);
		OUTREG(reg, INREG(reg) & ~(VGA_DDC_DATA_OUT_EN));
		(void)INREG(reg);
		msleep(15);

		/* Do the real work */
		edid = radeon_do_probe_i2c_edid(&rinfo->i2c[conn-1]);

		OUTREG(reg, INREG(reg) | 
				(VGA_DDC_DATA_OUT_EN | VGA_DDC_CLK_OUT_EN));
		(void)INREG(reg);
		msleep(15);
		
		OUTREG(reg, INREG(reg) & ~(VGA_DDC_CLK_OUT_EN));
		(void)INREG(reg);
		for (j = 0; j < 10; j++) {
			msleep(10);
			if (INREG(reg) & VGA_DDC_CLK_INPUT)
				break;
		}

		OUTREG(reg, INREG(reg) & ~(VGA_DDC_DATA_OUT_EN));
		(void)INREG(reg);
		msleep(15);
		OUTREG(reg, INREG(reg) |
				(VGA_DDC_DATA_OUT_EN | VGA_DDC_CLK_OUT_EN));
		(void)INREG(reg);
		if (edid)
			break;
	}
	/* Release the DDC lines when done or the Apple Cinema HD display
	 * will switch off
	 */
	OUTREG(reg, INREG(reg) & ~(VGA_DDC_CLK_OUT_EN | VGA_DDC_DATA_OUT_EN));
	(void)INREG(reg);

	if (out_edid)
		*out_edid = edid;
	if (!edid) {
		RTRACE("radeonfb: I2C (port %d) ... not found\n", conn);
		return MT_NONE;
	}
	if (edid[0x14] & 0x80) {
		/* Fix detection using BIOS tables */
		if (rinfo->is_mobility /*&& conn == ddc_dvi*/ &&
		    (INREG(LVDS_GEN_CNTL) & LVDS_ON)) {
			RTRACE("radeonfb: I2C (port %d) ... found LVDS panel\n", conn);
			return MT_LCD;
		} else {
			RTRACE("radeonfb: I2C (port %d) ... found TMDS panel\n", conn);
			return MT_DFP;
		}
	}
       	RTRACE("radeonfb: I2C (port %d) ... found CRT display\n", conn);
	return MT_CRT;
}

