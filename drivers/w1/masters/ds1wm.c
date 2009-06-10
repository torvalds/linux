/*
 * 1-wire busmaster driver for DS1WM and ASICs with embedded DS1WMs
 * such as HP iPAQs (including h5xxx, h2200, and devices with ASIC3
 * like hx4700).
 *
 * Copyright (c) 2004-2005, Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>
 * Copyright (c) 2004-2007, Matt Reimer <mreimer@vpop.net>
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/mfd/ds1wm.h>

#include <asm/io.h>

#include "../w1.h"
#include "../w1_int.h"


#define DS1WM_CMD	0x00	/* R/W 4 bits command */
#define DS1WM_DATA	0x01	/* R/W 8 bits, transmit/receive buffer */
#define DS1WM_INT	0x02	/* R/W interrupt status */
#define DS1WM_INT_EN	0x03	/* R/W interrupt enable */
#define DS1WM_CLKDIV	0x04	/* R/W 5 bits of divisor and pre-scale */

#define DS1WM_CMD_1W_RESET  (1 << 0)	/* force reset on 1-wire bus */
#define DS1WM_CMD_SRA	    (1 << 1)	/* enable Search ROM accelerator mode */
#define DS1WM_CMD_DQ_OUTPUT (1 << 2)	/* write only - forces bus low */
#define DS1WM_CMD_DQ_INPUT  (1 << 3)	/* read only - reflects state of bus */
#define DS1WM_CMD_RST	    (1 << 5)	/* software reset */
#define DS1WM_CMD_OD	    (1 << 7)	/* overdrive */

#define DS1WM_INT_PD	    (1 << 0)	/* presence detect */
#define DS1WM_INT_PDR	    (1 << 1)	/* presence detect result */
#define DS1WM_INT_TBE	    (1 << 2)	/* tx buffer empty */
#define DS1WM_INT_TSRE	    (1 << 3)	/* tx shift register empty */
#define DS1WM_INT_RBF	    (1 << 4)	/* rx buffer full */
#define DS1WM_INT_RSRF	    (1 << 5)	/* rx shift register full */

#define DS1WM_INTEN_EPD	    (1 << 0)	/* enable presence detect int */
#define DS1WM_INTEN_IAS	    (1 << 1)	/* INTR active state */
#define DS1WM_INTEN_ETBE    (1 << 2)	/* enable tx buffer empty int */
#define DS1WM_INTEN_ETMT    (1 << 3)	/* enable tx shift register empty int */
#define DS1WM_INTEN_ERBF    (1 << 4)	/* enable rx buffer full int */
#define DS1WM_INTEN_ERSRF   (1 << 5)	/* enable rx shift register full int */
#define DS1WM_INTEN_DQO	    (1 << 6)	/* enable direct bus driving ops */


#define DS1WM_TIMEOUT (HZ * 5)

static struct {
	unsigned long freq;
	unsigned long divisor;
} freq[] = {
	{ 4000000, 0x8 },
	{ 5000000, 0x2 },
	{ 6000000, 0x5 },
	{ 7000000, 0x3 },
	{ 8000000, 0xc },
	{ 10000000, 0x6 },
	{ 12000000, 0x9 },
	{ 14000000, 0x7 },
	{ 16000000, 0x10 },
	{ 20000000, 0xa },
	{ 24000000, 0xd },
	{ 28000000, 0xb },
	{ 32000000, 0x14 },
	{ 40000000, 0xe },
	{ 48000000, 0x11 },
	{ 56000000, 0xf },
	{ 64000000, 0x18 },
	{ 80000000, 0x12 },
	{ 96000000, 0x15 },
	{ 112000000, 0x13 },
	{ 128000000, 0x1c },
};

struct ds1wm_data {
	void		__iomem *map;
	int		bus_shift; /* # of shifts to calc register offsets */
	struct platform_device *pdev;
	struct mfd_cell	*cell;
	int		irq;
	int		active_high;
	int		slave_present;
	void		*reset_complete;
	void		*read_complete;
	void		*write_complete;
	u8		read_byte; /* last byte received */
};

static inline void ds1wm_write_register(struct ds1wm_data *ds1wm_data, u32 reg,
					u8 val)
{
	__raw_writeb(val, ds1wm_data->map + (reg << ds1wm_data->bus_shift));
}

static inline u8 ds1wm_read_register(struct ds1wm_data *ds1wm_data, u32 reg)
{
	return __raw_readb(ds1wm_data->map + (reg << ds1wm_data->bus_shift));
}


static irqreturn_t ds1wm_isr(int isr, void *data)
{
	struct ds1wm_data *ds1wm_data = data;
	u8 intr = ds1wm_read_register(ds1wm_data, DS1WM_INT);

	ds1wm_data->slave_present = (intr & DS1WM_INT_PDR) ? 0 : 1;

	if ((intr & DS1WM_INT_PD) && ds1wm_data->reset_complete)
		complete(ds1wm_data->reset_complete);

	if ((intr & DS1WM_INT_TSRE) && ds1wm_data->write_complete)
		complete(ds1wm_data->write_complete);

	if (intr & DS1WM_INT_RBF) {
		ds1wm_data->read_byte = ds1wm_read_register(ds1wm_data,
							    DS1WM_DATA);
		if (ds1wm_data->read_complete)
			complete(ds1wm_data->read_complete);
	}

	return IRQ_HANDLED;
}

static int ds1wm_reset(struct ds1wm_data *ds1wm_data)
{
	unsigned long timeleft;
	DECLARE_COMPLETION_ONSTACK(reset_done);

	ds1wm_data->reset_complete = &reset_done;

	ds1wm_write_register(ds1wm_data, DS1WM_INT_EN, DS1WM_INTEN_EPD |
		(ds1wm_data->active_high ? DS1WM_INTEN_IAS : 0));

	ds1wm_write_register(ds1wm_data, DS1WM_CMD, DS1WM_CMD_1W_RESET);

	timeleft = wait_for_completion_timeout(&reset_done, DS1WM_TIMEOUT);
	ds1wm_data->reset_complete = NULL;
	if (!timeleft) {
		dev_err(&ds1wm_data->pdev->dev, "reset failed\n");
		return 1;
	}

	/* Wait for the end of the reset. According to the specs, the time
	 * from when the interrupt is asserted to the end of the reset is:
	 *     tRSTH  - tPDH  - tPDL - tPDI
	 *     625 us - 60 us - 240 us - 100 ns = 324.9 us
	 *
	 * We'll wait a bit longer just to be sure.
	 * Was udelay(500), but if it is going to busywait the cpu that long,
	 * might as well come back later.
	 */
	msleep(1);

	ds1wm_write_register(ds1wm_data, DS1WM_INT_EN,
		DS1WM_INTEN_ERBF | DS1WM_INTEN_ETMT | DS1WM_INTEN_EPD |
		(ds1wm_data->active_high ? DS1WM_INTEN_IAS : 0));

	if (!ds1wm_data->slave_present) {
		dev_dbg(&ds1wm_data->pdev->dev, "reset: no devices found\n");
		return 1;
	}

	return 0;
}

static int ds1wm_write(struct ds1wm_data *ds1wm_data, u8 data)
{
	DECLARE_COMPLETION_ONSTACK(write_done);
	ds1wm_data->write_complete = &write_done;

	ds1wm_write_register(ds1wm_data, DS1WM_DATA, data);

	wait_for_completion_timeout(&write_done, DS1WM_TIMEOUT);
	ds1wm_data->write_complete = NULL;

	return 0;
}

static int ds1wm_read(struct ds1wm_data *ds1wm_data, unsigned char write_data)
{
	DECLARE_COMPLETION_ONSTACK(read_done);
	ds1wm_data->read_complete = &read_done;

	ds1wm_write(ds1wm_data, write_data);
	wait_for_completion_timeout(&read_done, DS1WM_TIMEOUT);
	ds1wm_data->read_complete = NULL;

	return ds1wm_data->read_byte;
}

static int ds1wm_find_divisor(int gclk)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(freq); i++)
		if (gclk <= freq[i].freq)
			return freq[i].divisor;

	return 0;
}

static void ds1wm_up(struct ds1wm_data *ds1wm_data)
{
	int divisor;
	struct ds1wm_driver_data *plat = ds1wm_data->cell->driver_data;

	if (ds1wm_data->cell->enable)
		ds1wm_data->cell->enable(ds1wm_data->pdev);

	divisor = ds1wm_find_divisor(plat->clock_rate);
	if (divisor == 0) {
		dev_err(&ds1wm_data->pdev->dev,
			"no suitable divisor for %dHz clock\n",
			plat->clock_rate);
		return;
	}
	ds1wm_write_register(ds1wm_data, DS1WM_CLKDIV, divisor);

	/* Let the w1 clock stabilize. */
	msleep(1);

	ds1wm_reset(ds1wm_data);
}

static void ds1wm_down(struct ds1wm_data *ds1wm_data)
{
	ds1wm_reset(ds1wm_data);

	/* Disable interrupts. */
	ds1wm_write_register(ds1wm_data, DS1WM_INT_EN,
			     ds1wm_data->active_high ? DS1WM_INTEN_IAS : 0);

	if (ds1wm_data->cell->disable)
		ds1wm_data->cell->disable(ds1wm_data->pdev);
}

/* --------------------------------------------------------------------- */
/* w1 methods */

static u8 ds1wm_read_byte(void *data)
{
	struct ds1wm_data *ds1wm_data = data;

	return ds1wm_read(ds1wm_data, 0xff);
}

static void ds1wm_write_byte(void *data, u8 byte)
{
	struct ds1wm_data *ds1wm_data = data;

	ds1wm_write(ds1wm_data, byte);
}

static u8 ds1wm_reset_bus(void *data)
{
	struct ds1wm_data *ds1wm_data = data;

	ds1wm_reset(ds1wm_data);

	return 0;
}

static void ds1wm_search(void *data, struct w1_master *master_dev,
			u8 search_type, w1_slave_found_callback slave_found)
{
	struct ds1wm_data *ds1wm_data = data;
	int i;
	unsigned long long rom_id;

	/* XXX We need to iterate for multiple devices per the DS1WM docs.
	 * See http://www.maxim-ic.com/appnotes.cfm/appnote_number/120. */
	if (ds1wm_reset(ds1wm_data))
		return;

	ds1wm_write(ds1wm_data, search_type);
	ds1wm_write_register(ds1wm_data, DS1WM_CMD, DS1WM_CMD_SRA);

	for (rom_id = 0, i = 0; i < 16; i++) {

		unsigned char resp, r, d;

		resp = ds1wm_read(ds1wm_data, 0x00);

		r = ((resp & 0x02) >> 1) |
		    ((resp & 0x08) >> 2) |
		    ((resp & 0x20) >> 3) |
		    ((resp & 0x80) >> 4);

		d = ((resp & 0x01) >> 0) |
		    ((resp & 0x04) >> 1) |
		    ((resp & 0x10) >> 2) |
		    ((resp & 0x40) >> 3);

		rom_id |= (unsigned long long) r << (i * 4);

	}
	dev_dbg(&ds1wm_data->pdev->dev, "found 0x%08llX\n", rom_id);

	ds1wm_write_register(ds1wm_data, DS1WM_CMD, ~DS1WM_CMD_SRA);
	ds1wm_reset(ds1wm_data);

	slave_found(master_dev, rom_id);
}

/* --------------------------------------------------------------------- */

static struct w1_bus_master ds1wm_master = {
	.read_byte  = ds1wm_read_byte,
	.write_byte = ds1wm_write_byte,
	.reset_bus  = ds1wm_reset_bus,
	.search	    = ds1wm_search,
};

static int ds1wm_probe(struct platform_device *pdev)
{
	struct ds1wm_data *ds1wm_data;
	struct ds1wm_driver_data *plat;
	struct resource *res;
	struct mfd_cell *cell;
	int ret;

	if (!pdev)
		return -ENODEV;

	cell = pdev->dev.platform_data;
	if (!cell)
		return -ENODEV;

	ds1wm_data = kzalloc(sizeof(*ds1wm_data), GFP_KERNEL);
	if (!ds1wm_data)
		return -ENOMEM;

	platform_set_drvdata(pdev, ds1wm_data);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENXIO;
		goto err0;
	}
	ds1wm_data->map = ioremap(res->start, resource_size(res));
	if (!ds1wm_data->map) {
		ret = -ENOMEM;
		goto err0;
	}
	plat = cell->driver_data;

	/* calculate bus shift from mem resource */
	ds1wm_data->bus_shift = resource_size(res) >> 3;

	ds1wm_data->pdev = pdev;
	ds1wm_data->cell = cell;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		ret = -ENXIO;
		goto err1;
	}
	ds1wm_data->irq = res->start;
	ds1wm_data->active_high = plat->active_high;

	if (res->flags & IORESOURCE_IRQ_HIGHEDGE)
		set_irq_type(ds1wm_data->irq, IRQ_TYPE_EDGE_RISING);
	if (res->flags & IORESOURCE_IRQ_LOWEDGE)
		set_irq_type(ds1wm_data->irq, IRQ_TYPE_EDGE_FALLING);

	ret = request_irq(ds1wm_data->irq, ds1wm_isr, IRQF_DISABLED,
			  "ds1wm", ds1wm_data);
	if (ret)
		goto err1;

	ds1wm_up(ds1wm_data);

	ds1wm_master.data = (void *)ds1wm_data;

	ret = w1_add_master_device(&ds1wm_master);
	if (ret)
		goto err2;

	return 0;

err2:
	ds1wm_down(ds1wm_data);
	free_irq(ds1wm_data->irq, ds1wm_data);
err1:
	iounmap(ds1wm_data->map);
err0:
	kfree(ds1wm_data);

	return ret;
}

#ifdef CONFIG_PM
static int ds1wm_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct ds1wm_data *ds1wm_data = platform_get_drvdata(pdev);

	ds1wm_down(ds1wm_data);

	return 0;
}

static int ds1wm_resume(struct platform_device *pdev)
{
	struct ds1wm_data *ds1wm_data = platform_get_drvdata(pdev);

	ds1wm_up(ds1wm_data);

	return 0;
}
#else
#define ds1wm_suspend NULL
#define ds1wm_resume NULL
#endif

static int ds1wm_remove(struct platform_device *pdev)
{
	struct ds1wm_data *ds1wm_data = platform_get_drvdata(pdev);

	w1_remove_master_device(&ds1wm_master);
	ds1wm_down(ds1wm_data);
	free_irq(ds1wm_data->irq, ds1wm_data);
	iounmap(ds1wm_data->map);
	kfree(ds1wm_data);

	return 0;
}

static struct platform_driver ds1wm_driver = {
	.driver   = {
		.name = "ds1wm",
	},
	.probe    = ds1wm_probe,
	.remove   = ds1wm_remove,
	.suspend  = ds1wm_suspend,
	.resume   = ds1wm_resume
};

static int __init ds1wm_init(void)
{
	printk("DS1WM w1 busmaster driver - (c) 2004 Szabolcs Gyurko\n");
	return platform_driver_register(&ds1wm_driver);
}

static void __exit ds1wm_exit(void)
{
	platform_driver_unregister(&ds1wm_driver);
}

module_init(ds1wm_init);
module_exit(ds1wm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>, "
	      "Matt Reimer <mreimer@vpop.net>");
MODULE_DESCRIPTION("DS1WM w1 busmaster driver");
