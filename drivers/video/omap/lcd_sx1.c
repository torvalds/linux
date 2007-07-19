/*
 * LCD panel support for the Siemens SX1 mobile phone
 *
 * Current version : Vovan888@gmail.com, great help from FCA00000
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/arch/gpio.h>
#include <asm/arch/omapfb.h>
#include <asm/arch/mcbsp.h>
#include <asm/arch/mux.h>

/*
 * OMAP310 GPIO registers
 */
#define GPIO_DATA_INPUT		0xfffce000
#define GPIO_DATA_OUTPUT	0xfffce004
#define GPIO_DIR_CONTROL	0xfffce008
#define GPIO_INT_CONTROL	0xfffce00c
#define GPIO_INT_MASK		0xfffce010
#define GPIO_INT_STATUS		0xfffce014
#define GPIO_PIN_CONTROL	0xfffce018


#define A_LCD_SSC_RD	3
#define A_LCD_SSC_SD	7
#define _A_LCD_RESET	9
#define _A_LCD_SSC_CS	12
#define _A_LCD_SSC_A0	13

#define DSP_REG		0xE1017024

const unsigned char INIT_1[12] = {
	0x1C, 0x02, 0x88, 0x00, 0x1E, 0xE0, 0x00, 0xDC, 0x00, 0x02, 0x00
};

const unsigned char INIT_2[127] = {
	0x15, 0x00, 0x29, 0x00, 0x3E, 0x00, 0x51, 0x00,
	0x65, 0x00, 0x7A, 0x00, 0x8D, 0x00, 0xA1, 0x00,
	0xB6, 0x00, 0xC7, 0x00, 0xD8, 0x00, 0xEB, 0x00,
	0xFB, 0x00, 0x0B, 0x01, 0x1B, 0x01, 0x27, 0x01,
	0x34, 0x01, 0x41, 0x01, 0x4C, 0x01, 0x55, 0x01,
	0x5F, 0x01, 0x68, 0x01, 0x70, 0x01, 0x78, 0x01,
	0x7E, 0x01, 0x86, 0x01, 0x8C, 0x01, 0x94, 0x01,
	0x9B, 0x01, 0xA1, 0x01, 0xA4, 0x01, 0xA9, 0x01,
	0xAD, 0x01, 0xB2, 0x01, 0xB7, 0x01, 0xBC, 0x01,
	0xC0, 0x01, 0xC4, 0x01, 0xC8, 0x01, 0xCB, 0x01,
	0xCF, 0x01, 0xD2, 0x01, 0xD5, 0x01, 0xD8, 0x01,
	0xDB, 0x01, 0xE0, 0x01, 0xE3, 0x01, 0xE6, 0x01,
	0xE8, 0x01, 0xEB, 0x01, 0xEE, 0x01, 0xF1, 0x01,
	0xF3, 0x01, 0xF8, 0x01, 0xF9, 0x01, 0xFC, 0x01,
	0x00, 0x02, 0x03, 0x02, 0x07, 0x02, 0x09, 0x02,
	0x0E, 0x02, 0x13, 0x02, 0x1C, 0x02, 0x00
};

const unsigned char INIT_3[15] = {
	0x14, 0x26, 0x33, 0x3D, 0x45, 0x4D, 0x53, 0x59,
	0x5E, 0x63, 0x67, 0x6D, 0x71, 0x78, 0xFF
};

static void epson_sendbyte(int flag, unsigned char byte)
{
	int i, shifter = 0x80;

	if (!flag)
		omap_set_gpio_dataout(_A_LCD_SSC_A0, 0);
	mdelay(2);
	omap_set_gpio_dataout(A_LCD_SSC_RD, 1);

	omap_set_gpio_dataout(A_LCD_SSC_SD, flag);

	OMAP_MCBSP_WRITE(OMAP1510_MCBSP3_BASE, PCR0, 0x2200);
	OMAP_MCBSP_WRITE(OMAP1510_MCBSP3_BASE, PCR0, 0x2202);
	for (i = 0; i < 8; i++) {
		OMAP_MCBSP_WRITE(OMAP1510_MCBSP3_BASE, PCR0, 0x2200);
		omap_set_gpio_dataout(A_LCD_SSC_SD, shifter & byte);
		OMAP_MCBSP_WRITE(OMAP1510_MCBSP3_BASE, PCR0, 0x2202);
		shifter >>= 1;
	}
	omap_set_gpio_dataout(_A_LCD_SSC_A0, 1);
}

static void init_system(void)
{
	omap_mcbsp_request(OMAP_MCBSP3);
	omap_mcbsp_stop(OMAP_MCBSP3);
}

static void setup_GPIO(void)
{
	/* new wave */
	omap_request_gpio(A_LCD_SSC_RD);
	omap_request_gpio(A_LCD_SSC_SD);
	omap_request_gpio(_A_LCD_RESET);
	omap_request_gpio(_A_LCD_SSC_CS);
	omap_request_gpio(_A_LCD_SSC_A0);

	/* set all GPIOs to output */
	omap_set_gpio_direction(A_LCD_SSC_RD, 0);
	omap_set_gpio_direction(A_LCD_SSC_SD, 0);
	omap_set_gpio_direction(_A_LCD_RESET, 0);
	omap_set_gpio_direction(_A_LCD_SSC_CS, 0);
	omap_set_gpio_direction(_A_LCD_SSC_A0, 0);

	/* set GPIO data */
	omap_set_gpio_dataout(A_LCD_SSC_RD, 1);
	omap_set_gpio_dataout(A_LCD_SSC_SD, 0);
	omap_set_gpio_dataout(_A_LCD_RESET, 0);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	omap_set_gpio_dataout(_A_LCD_SSC_A0, 1);
}

static void display_init(void)
{
	int i;

	omap_cfg_reg(MCBSP3_CLKX);

	mdelay(2);
	setup_GPIO();
	mdelay(2);

	/* reset LCD */
	omap_set_gpio_dataout(A_LCD_SSC_SD, 1);
	epson_sendbyte(0, 0x25);

	omap_set_gpio_dataout(_A_LCD_RESET, 0);
	mdelay(10);
	omap_set_gpio_dataout(_A_LCD_RESET, 1);

	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	mdelay(2);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	/* init LCD, phase 1 */
	epson_sendbyte(0, 0xCA);
	for (i = 0; i < 10; i++)
		epson_sendbyte(1, INIT_1[i]);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	/* init LCD phase 2 */
	epson_sendbyte(0, 0xCB);
	for (i = 0; i < 125; i++)
		epson_sendbyte(1, INIT_2[i]);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	/* init LCD phase 2a */
	epson_sendbyte(0, 0xCC);
	for (i = 0; i < 14; i++)
		epson_sendbyte(1, INIT_3[i]);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	/* init LCD phase 3 */
	epson_sendbyte(0, 0xBC);
	epson_sendbyte(1, 0x08);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	/* init LCD phase 4 */
	epson_sendbyte(0, 0x07);
	epson_sendbyte(1, 0x05);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	/* init LCD phase 5 */
	epson_sendbyte(0, 0x94);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	/* init LCD phase 6 */
	epson_sendbyte(0, 0xC6);
	epson_sendbyte(1, 0x80);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	mdelay(100); /* used to be 1000 */
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	/* init LCD phase 7 */
	epson_sendbyte(0, 0x16);
	epson_sendbyte(1, 0x02);
	epson_sendbyte(1, 0x00);
	epson_sendbyte(1, 0xB1);
	epson_sendbyte(1, 0x00);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	/* init LCD phase 8 */
	epson_sendbyte(0, 0x76);
	epson_sendbyte(1, 0x00);
	epson_sendbyte(1, 0x00);
	epson_sendbyte(1, 0xDB);
	epson_sendbyte(1, 0x00);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	/* init LCD phase 9 */
	epson_sendbyte(0, 0xAF);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
}

static int sx1_panel_init(struct lcd_panel *panel, struct omapfb_device *fbdev)
{
	return 0;
}

static void sx1_panel_cleanup(struct lcd_panel *panel)
{
}

static void sx1_panel_disable(struct lcd_panel *panel)
{
	printk(KERN_INFO "SX1: LCD panel disable\n");
	sx1_setmmipower(0);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);

	epson_sendbyte(0, 0x25);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	epson_sendbyte(0, 0xAE);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
	mdelay(100);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 0);

	epson_sendbyte(0, 0x95);
	omap_set_gpio_dataout(_A_LCD_SSC_CS, 1);
}

static int sx1_panel_enable(struct lcd_panel *panel)
{
	printk(KERN_INFO "lcd_sx1: LCD panel enable\n");
	init_system();
	display_init();

	sx1_setmmipower(1);
	sx1_setbacklight(0x18);
	sx1_setkeylight (0x06);
	return 0;
}


static unsigned long sx1_panel_get_caps(struct lcd_panel *panel)
{
	return 0;
}

struct lcd_panel sx1_panel = {
	.name		= "sx1",
	.config		= OMAP_LCDC_PANEL_TFT | OMAP_LCDC_INV_VSYNC |
			  OMAP_LCDC_INV_HSYNC | OMAP_LCDC_INV_PIX_CLOCK |
			  OMAP_LCDC_INV_OUTPUT_EN,

	.x_res		= 176,
	.y_res		= 220,
	.data_lines	= 16,
	.bpp		= 16,
	.hsw		= 5,
	.hfp		= 5,
	.hbp		= 5,
	.vsw		= 2,
	.vfp		= 1,
	.vbp		= 1,
	.pixel_clock	= 1500,

	.init		= sx1_panel_init,
	.cleanup	= sx1_panel_cleanup,
	.enable		= sx1_panel_enable,
	.disable	= sx1_panel_disable,
	.get_caps	= sx1_panel_get_caps,
};

static int sx1_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&sx1_panel);
	return 0;
}

static int sx1_panel_remove(struct platform_device *pdev)
{
	return 0;
}

static int sx1_panel_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	return 0;
}

static int sx1_panel_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver sx1_panel_driver = {
	.probe		= sx1_panel_probe,
	.remove		= sx1_panel_remove,
	.suspend	= sx1_panel_suspend,
	.resume		= sx1_panel_resume,
	.driver	= {
		.name	= "lcd_sx1",
		.owner	= THIS_MODULE,
	},
};

static int sx1_panel_drv_init(void)
{
	return platform_driver_register(&sx1_panel_driver);
}

static void sx1_panel_drv_cleanup(void)
{
	platform_driver_unregister(&sx1_panel_driver);
}

module_init(sx1_panel_drv_init);
module_exit(sx1_panel_drv_cleanup);
