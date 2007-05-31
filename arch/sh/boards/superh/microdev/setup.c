/*
 * arch/sh/boards/superh/microdev/setup.c
 *
 * Copyright (C) 2003 Sean McGoogan (Sean.McGoogan@superh.com)
 * Copyright (C) 2003, 2004 SuperH, Inc.
 * Copyright (C) 2004, 2005 Paul Mundt
 *
 * SuperH SH4-202 MicroDev board support.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <video/s1d13xxxfb.h>
#include <asm/microdev.h>
#include <asm/io.h>
#include <asm/machvec.h>

extern void microdev_heartbeat(void);


/****************************************************************************/


	/*
	 * Setup for the SMSC FDC37C93xAPM
	 */
#define SMSC_CONFIG_PORT_ADDR	 (0x3F0)
#define SMSC_INDEX_PORT_ADDR	 SMSC_CONFIG_PORT_ADDR
#define SMSC_DATA_PORT_ADDR	 (SMSC_INDEX_PORT_ADDR + 1)

#define SMSC_ENTER_CONFIG_KEY	 0x55
#define SMSC_EXIT_CONFIG_KEY	 0xaa

#define SMCS_LOGICAL_DEV_INDEX 	 0x07	/* Logical Device Number */
#define SMSC_DEVICE_ID_INDEX	 0x20	/* Device ID */
#define SMSC_DEVICE_REV_INDEX	 0x21	/* Device Revision */
#define SMSC_ACTIVATE_INDEX	 0x30	/* Activate */
#define SMSC_PRIMARY_BASE_INDEX	 0x60	/* Primary Base Address */
#define SMSC_SECONDARY_BASE_INDEX 0x62	/* Secondary Base Address */
#define SMSC_PRIMARY_INT_INDEX	 0x70	/* Primary Interrupt Select */
#define SMSC_SECONDARY_INT_INDEX 0x72	/* Secondary Interrupt Select */
#define SMSC_HDCS0_INDEX	 0xf0	/* HDCS0 Address Decoder */
#define SMSC_HDCS1_INDEX	 0xf1	/* HDCS1 Address Decoder */

#define SMSC_IDE1_DEVICE	1	/* IDE #1 logical device */
#define SMSC_IDE2_DEVICE	2	/* IDE #2 logical device */
#define SMSC_PARALLEL_DEVICE	3	/* Parallel Port logical device */
#define SMSC_SERIAL1_DEVICE	4	/* Serial #1 logical device */
#define SMSC_SERIAL2_DEVICE	5	/* Serial #2 logical device */
#define SMSC_KEYBOARD_DEVICE	7	/* Keyboard logical device */
#define SMSC_CONFIG_REGISTERS	8	/* Configuration Registers (Aux I/O) */

#define SMSC_READ_INDEXED(index) ({ \
	outb((index), SMSC_INDEX_PORT_ADDR); \
	inb(SMSC_DATA_PORT_ADDR); })
#define SMSC_WRITE_INDEXED(val, index) ({ \
	outb((index), SMSC_INDEX_PORT_ADDR); \
	outb((val),   SMSC_DATA_PORT_ADDR); })

#define	IDE1_PRIMARY_BASE	0x01f0	/* Task File Registe base for IDE #1 */
#define	IDE1_SECONDARY_BASE	0x03f6	/* Miscellaneous AT registers for IDE #1 */
#define	IDE2_PRIMARY_BASE	0x0170	/* Task File Registe base for IDE #2 */
#define	IDE2_SECONDARY_BASE	0x0376	/* Miscellaneous AT registers for IDE #2 */

#define SERIAL1_PRIMARY_BASE	0x03f8
#define SERIAL2_PRIMARY_BASE	0x02f8

#define	MSB(x)		( (x) >> 8 )
#define	LSB(x)		( (x) & 0xff )

	/* General-Purpose base address on CPU-board FPGA */
#define	MICRODEV_FPGA_GP_BASE		0xa6100000ul

	/* assume a Keyboard Controller is present */
int microdev_kbd_controller_present = 1;

static struct resource smc91x_resources[] = {
	[0] = {
		.start		= 0x300,
		.end		= 0x300 + 0x0001000 - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= MICRODEV_LINUX_IRQ_ETHERNET,
		.end		= MICRODEV_LINUX_IRQ_ETHERNET,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

#ifdef CONFIG_FB_S1D13XXX
static struct s1d13xxxfb_regval s1d13806_initregs[] = {
	{ S1DREG_MISC,			0x00 },
	{ S1DREG_COM_DISP_MODE,		0x00 },
	{ S1DREG_GPIO_CNF0,		0x00 },
	{ S1DREG_GPIO_CNF1,		0x00 },
	{ S1DREG_GPIO_CTL0,		0x00 },
	{ S1DREG_GPIO_CTL1,		0x00 },
	{ S1DREG_CLK_CNF,		0x02 },
	{ S1DREG_LCD_CLK_CNF,		0x01 },
	{ S1DREG_CRT_CLK_CNF,		0x03 },
	{ S1DREG_MPLUG_CLK_CNF,		0x03 },
	{ S1DREG_CPU2MEM_WST_SEL,	0x02 },
	{ S1DREG_SDRAM_REF_RATE,	0x03 },
	{ S1DREG_SDRAM_TC0,		0x00 },
	{ S1DREG_SDRAM_TC1,		0x01 },
	{ S1DREG_MEM_CNF,		0x80 },
	{ S1DREG_PANEL_TYPE,		0x25 },
	{ S1DREG_MOD_RATE,		0x00 },
	{ S1DREG_LCD_DISP_HWIDTH,	0x63 },
	{ S1DREG_LCD_NDISP_HPER,	0x1e },
	{ S1DREG_TFT_FPLINE_START,	0x06 },
	{ S1DREG_TFT_FPLINE_PWIDTH,	0x03 },
	{ S1DREG_LCD_DISP_VHEIGHT0,	0x57 },
	{ S1DREG_LCD_DISP_VHEIGHT1,	0x02 },
	{ S1DREG_LCD_NDISP_VPER,	0x00 },
	{ S1DREG_TFT_FPFRAME_START,	0x0a },
	{ S1DREG_TFT_FPFRAME_PWIDTH,	0x81 },
	{ S1DREG_LCD_DISP_MODE,		0x03 },
	{ S1DREG_LCD_MISC,		0x00 },
	{ S1DREG_LCD_DISP_START0,	0x00 },
	{ S1DREG_LCD_DISP_START1,	0x00 },
	{ S1DREG_LCD_DISP_START2,	0x00 },
	{ S1DREG_LCD_MEM_OFF0,		0x90 },
	{ S1DREG_LCD_MEM_OFF1,		0x01 },
	{ S1DREG_LCD_PIX_PAN,		0x00 },
	{ S1DREG_LCD_DISP_FIFO_HTC,	0x00 },
	{ S1DREG_LCD_DISP_FIFO_LTC,	0x00 },
	{ S1DREG_CRT_DISP_HWIDTH,	0x63 },
	{ S1DREG_CRT_NDISP_HPER,	0x1f },
	{ S1DREG_CRT_HRTC_START,	0x04 },
	{ S1DREG_CRT_HRTC_PWIDTH,	0x8f },
	{ S1DREG_CRT_DISP_VHEIGHT0,	0x57 },
	{ S1DREG_CRT_DISP_VHEIGHT1,	0x02 },
	{ S1DREG_CRT_NDISP_VPER,	0x1b },
	{ S1DREG_CRT_VRTC_START,	0x00 },
	{ S1DREG_CRT_VRTC_PWIDTH,	0x83 },
	{ S1DREG_TV_OUT_CTL,		0x10 },
	{ S1DREG_CRT_DISP_MODE,		0x05 },
	{ S1DREG_CRT_DISP_START0,	0x00 },
	{ S1DREG_CRT_DISP_START1,	0x00 },
	{ S1DREG_CRT_DISP_START2,	0x00 },
	{ S1DREG_CRT_MEM_OFF0,		0x20 },
	{ S1DREG_CRT_MEM_OFF1,		0x03 },
	{ S1DREG_CRT_PIX_PAN,		0x00 },
	{ S1DREG_CRT_DISP_FIFO_HTC,	0x00 },
	{ S1DREG_CRT_DISP_FIFO_LTC,	0x00 },
	{ S1DREG_LCD_CUR_CTL,		0x00 },
	{ S1DREG_LCD_CUR_START,		0x01 },
	{ S1DREG_LCD_CUR_XPOS0,		0x00 },
	{ S1DREG_LCD_CUR_XPOS1,		0x00 },
	{ S1DREG_LCD_CUR_YPOS0,		0x00 },
	{ S1DREG_LCD_CUR_YPOS1,		0x00 },
	{ S1DREG_LCD_CUR_BCTL0,		0x00 },
	{ S1DREG_LCD_CUR_GCTL0,		0x00 },
	{ S1DREG_LCD_CUR_RCTL0,		0x00 },
	{ S1DREG_LCD_CUR_BCTL1,		0x1f },
	{ S1DREG_LCD_CUR_GCTL1,		0x3f },
	{ S1DREG_LCD_CUR_RCTL1,		0x1f },
	{ S1DREG_LCD_CUR_FIFO_HTC,	0x00 },
	{ S1DREG_CRT_CUR_CTL,		0x00 },
	{ S1DREG_CRT_CUR_START,		0x01 },
	{ S1DREG_CRT_CUR_XPOS0,		0x00 },
	{ S1DREG_CRT_CUR_XPOS1,		0x00 },
	{ S1DREG_CRT_CUR_YPOS0,		0x00 },
	{ S1DREG_CRT_CUR_YPOS1,		0x00 },
	{ S1DREG_CRT_CUR_BCTL0,		0x00 },
	{ S1DREG_CRT_CUR_GCTL0,		0x00 },
	{ S1DREG_CRT_CUR_RCTL0,		0x00 },
	{ S1DREG_CRT_CUR_BCTL1,		0x1f },
	{ S1DREG_CRT_CUR_GCTL1,		0x3f },
	{ S1DREG_CRT_CUR_RCTL1,		0x1f },
	{ S1DREG_CRT_CUR_FIFO_HTC,	0x00 },
	{ S1DREG_BBLT_CTL0,		0x00 },
	{ S1DREG_BBLT_CTL1,		0x00 },
	{ S1DREG_BBLT_CC_EXP,		0x00 },
	{ S1DREG_BBLT_OP,		0x00 },
	{ S1DREG_BBLT_SRC_START0,	0x00 },
	{ S1DREG_BBLT_SRC_START1,	0x00 },
	{ S1DREG_BBLT_SRC_START2,	0x00 },
	{ S1DREG_BBLT_DST_START0,	0x00 },
	{ S1DREG_BBLT_DST_START1,	0x00 },
	{ S1DREG_BBLT_DST_START2,	0x00 },
	{ S1DREG_BBLT_MEM_OFF0,		0x00 },
	{ S1DREG_BBLT_MEM_OFF1,		0x00 },
	{ S1DREG_BBLT_WIDTH0,		0x00 },
	{ S1DREG_BBLT_WIDTH1,		0x00 },
	{ S1DREG_BBLT_HEIGHT0,		0x00 },
	{ S1DREG_BBLT_HEIGHT1,		0x00 },
	{ S1DREG_BBLT_BGC0,		0x00 },
	{ S1DREG_BBLT_BGC1,		0x00 },
	{ S1DREG_BBLT_FGC0,		0x00 },
	{ S1DREG_BBLT_FGC1,		0x00 },
	{ S1DREG_LKUP_MODE,		0x00 },
	{ S1DREG_LKUP_ADDR,		0x00 },
	{ S1DREG_PS_CNF,		0x10 },
	{ S1DREG_PS_STATUS,		0x00 },
	{ S1DREG_CPU2MEM_WDOGT,		0x00 },
	{ S1DREG_COM_DISP_MODE,		0x02 },
};

static struct s1d13xxxfb_pdata s1d13806_platform_data = {
	.initregs	= s1d13806_initregs,
	.initregssize	= ARRAY_SIZE(s1d13806_initregs),
};

static struct resource s1d13806_resources[] = {
	[0] = {
		.start		= 0x07200000,
		.end		= 0x07200000 + 0x00200000 - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= 0x07000000,
		.end		= 0x07000000 + 0x00200000 - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device s1d13806_device = {
	.name		= "s1d13806fb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s1d13806_resources),
	.resource	= s1d13806_resources,

	.dev = {
		.platform_data	= &s1d13806_platform_data,
	},
};
#endif

static struct platform_device *microdev_devices[] __initdata = {
	&smc91x_device,
#ifdef CONFIG_FB_S1D13XXX
	&s1d13806_device,
#endif
};

static int __init microdev_devices_setup(void)
{
	return platform_add_devices(microdev_devices, ARRAY_SIZE(microdev_devices));
}

/*
 * Setup for the SMSC FDC37C93xAPM
 */
static int __init smsc_superio_setup(void)
{

	unsigned char devid, devrev;

		/* Initially the chip is in run state */
		/* Put it into configuration state */
	outb(SMSC_ENTER_CONFIG_KEY, SMSC_CONFIG_PORT_ADDR);

		/* Read device ID info */
	devid  = SMSC_READ_INDEXED(SMSC_DEVICE_ID_INDEX);
	devrev = SMSC_READ_INDEXED(SMSC_DEVICE_REV_INDEX);
	if ( (devid==0x30) && (devrev==0x01) )
	{
  		printk("SMSC FDC37C93xAPM SuperIO device detected\n");
	}
	else
	{		/* not the device identity we expected */
		printk("Not detected a SMSC FDC37C93xAPM SuperIO device (devid=0x%02x, rev=0x%02x)\n",
			devid, devrev);
			/* inform the keyboard driver that we have no keyboard controller */
		microdev_kbd_controller_present = 0;
			/* little point in doing anything else in this functon */
		return 0;
	}

		/* Select the keyboard device */
	SMSC_WRITE_INDEXED(SMSC_KEYBOARD_DEVICE, SMCS_LOGICAL_DEV_INDEX);
		/* enable it */
	SMSC_WRITE_INDEXED(1, SMSC_ACTIVATE_INDEX);
		/* enable the interrupts */
	SMSC_WRITE_INDEXED(MICRODEV_FPGA_IRQ_KEYBOARD, SMSC_PRIMARY_INT_INDEX);
	SMSC_WRITE_INDEXED(MICRODEV_FPGA_IRQ_MOUSE, SMSC_SECONDARY_INT_INDEX);

		/* Select the Serial #1 device */
	SMSC_WRITE_INDEXED(SMSC_SERIAL1_DEVICE, SMCS_LOGICAL_DEV_INDEX);
		/* enable it */
	SMSC_WRITE_INDEXED(1, SMSC_ACTIVATE_INDEX);
		/* program with port addresses */
	SMSC_WRITE_INDEXED(MSB(SERIAL1_PRIMARY_BASE), SMSC_PRIMARY_BASE_INDEX+0);
	SMSC_WRITE_INDEXED(LSB(SERIAL1_PRIMARY_BASE), SMSC_PRIMARY_BASE_INDEX+1);
	SMSC_WRITE_INDEXED(0x00, SMSC_HDCS0_INDEX);
		/* enable the interrupts */
	SMSC_WRITE_INDEXED(MICRODEV_FPGA_IRQ_SERIAL1, SMSC_PRIMARY_INT_INDEX);

		/* Select the Serial #2 device */
	SMSC_WRITE_INDEXED(SMSC_SERIAL2_DEVICE, SMCS_LOGICAL_DEV_INDEX);
		/* enable it */
	SMSC_WRITE_INDEXED(1, SMSC_ACTIVATE_INDEX);
		/* program with port addresses */
	SMSC_WRITE_INDEXED(MSB(SERIAL2_PRIMARY_BASE), SMSC_PRIMARY_BASE_INDEX+0);
	SMSC_WRITE_INDEXED(LSB(SERIAL2_PRIMARY_BASE), SMSC_PRIMARY_BASE_INDEX+1);
	SMSC_WRITE_INDEXED(0x00, SMSC_HDCS0_INDEX);
		/* enable the interrupts */
	SMSC_WRITE_INDEXED(MICRODEV_FPGA_IRQ_SERIAL2, SMSC_PRIMARY_INT_INDEX);

		/* Select the IDE#1 device */
	SMSC_WRITE_INDEXED(SMSC_IDE1_DEVICE, SMCS_LOGICAL_DEV_INDEX);
		/* enable it */
	SMSC_WRITE_INDEXED(1, SMSC_ACTIVATE_INDEX);
		/* program with port addresses */
	SMSC_WRITE_INDEXED(MSB(IDE1_PRIMARY_BASE), SMSC_PRIMARY_BASE_INDEX+0);
	SMSC_WRITE_INDEXED(LSB(IDE1_PRIMARY_BASE), SMSC_PRIMARY_BASE_INDEX+1);
	SMSC_WRITE_INDEXED(MSB(IDE1_SECONDARY_BASE), SMSC_SECONDARY_BASE_INDEX+0);
	SMSC_WRITE_INDEXED(LSB(IDE1_SECONDARY_BASE), SMSC_SECONDARY_BASE_INDEX+1);
	SMSC_WRITE_INDEXED(0x0c, SMSC_HDCS0_INDEX);
	SMSC_WRITE_INDEXED(0x00, SMSC_HDCS1_INDEX);
		/* select the interrupt */
	SMSC_WRITE_INDEXED(MICRODEV_FPGA_IRQ_IDE1, SMSC_PRIMARY_INT_INDEX);

		/* Select the IDE#2 device */
	SMSC_WRITE_INDEXED(SMSC_IDE2_DEVICE, SMCS_LOGICAL_DEV_INDEX);
		/* enable it */
	SMSC_WRITE_INDEXED(1, SMSC_ACTIVATE_INDEX);
		/* program with port addresses */
	SMSC_WRITE_INDEXED(MSB(IDE2_PRIMARY_BASE), SMSC_PRIMARY_BASE_INDEX+0);
	SMSC_WRITE_INDEXED(LSB(IDE2_PRIMARY_BASE), SMSC_PRIMARY_BASE_INDEX+1);
	SMSC_WRITE_INDEXED(MSB(IDE2_SECONDARY_BASE), SMSC_SECONDARY_BASE_INDEX+0);
	SMSC_WRITE_INDEXED(LSB(IDE2_SECONDARY_BASE), SMSC_SECONDARY_BASE_INDEX+1);
		/* select the interrupt */
	SMSC_WRITE_INDEXED(MICRODEV_FPGA_IRQ_IDE2, SMSC_PRIMARY_INT_INDEX);

		/* Select the configuration registers */
	SMSC_WRITE_INDEXED(SMSC_CONFIG_REGISTERS, SMCS_LOGICAL_DEV_INDEX);
		/* enable the appropriate GPIO pins for IDE functionality:
		 * bit[0]   In/Out		1==input;  0==output
		 * bit[1]   Polarity		1==invert; 0==no invert
		 * bit[2]   Int Enb #1		1==Enable Combined IRQ #1; 0==disable
		 * bit[3:4] Function Select	00==original; 01==Alternate Function #1
		 */
	SMSC_WRITE_INDEXED(0x00, 0xc2);	/* GP42 = nIDE1_OE */
	SMSC_WRITE_INDEXED(0x01, 0xc5);	/* GP45 = IDE1_IRQ */
	SMSC_WRITE_INDEXED(0x00, 0xc6);	/* GP46 = nIOROP */
	SMSC_WRITE_INDEXED(0x00, 0xc7);	/* GP47 = nIOWOP */
	SMSC_WRITE_INDEXED(0x08, 0xe8);	/* GP20 = nIDE2_OE */

		/* Exit the configuration state */
	outb(SMSC_EXIT_CONFIG_KEY, SMSC_CONFIG_PORT_ADDR);

	return 0;
}

static void __init microdev_setup(char **cmdline_p)
{
	int * const fpgaRevisionRegister = (int*)(MICRODEV_FPGA_GP_BASE + 0x8ul);
	const int fpgaRevision = *fpgaRevisionRegister;
	int * const CacheControlRegister = (int*)CCR;

	device_initcall(microdev_devices_setup);
	device_initcall(smsc_superio_setup);

	printk("SuperH %s board (FPGA rev: 0x%0x, CCR: 0x%0x)\n",
		get_system_type(), fpgaRevision, *CacheControlRegister);
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_sh4202_microdev __initmv = {
	.mv_name		= "SH4-202 MicroDev",
	.mv_setup		= microdev_setup,
	.mv_nr_irqs		= 72,		/* QQQ need to check this - use the MACRO */

	.mv_inb			= microdev_inb,
	.mv_inw			= microdev_inw,
	.mv_inl			= microdev_inl,
	.mv_outb		= microdev_outb,
	.mv_outw		= microdev_outw,
	.mv_outl		= microdev_outl,

	.mv_inb_p		= microdev_inb_p,
	.mv_inw_p		= microdev_inw_p,
	.mv_inl_p		= microdev_inl_p,
	.mv_outb_p		= microdev_outb_p,
	.mv_outw_p		= microdev_outw_p,
	.mv_outl_p		= microdev_outl_p,

	.mv_insb		= microdev_insb,
	.mv_insw		= microdev_insw,
	.mv_insl		= microdev_insl,
	.mv_outsb		= microdev_outsb,
	.mv_outsw		= microdev_outsw,
	.mv_outsl		= microdev_outsl,

	.mv_init_irq		= init_microdev_irq,

#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= microdev_heartbeat,
#endif
};
ALIAS_MV(sh4202_microdev)
