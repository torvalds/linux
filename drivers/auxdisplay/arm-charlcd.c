// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the on-board character LCD found on some ARM reference boards
 * This is basically an Hitachi HD44780 LCD with a custom IP block to drive it
 * https://en.wikipedia.org/wiki/HD44780_Character_LCD
 * Currently it will just display the text "ARM Linux" and the linux version
 *
 * Author: Linus Walleij <triad@df.lth.se>
 */
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <generated/utsrelease.h>

#define DRIVERNAME "arm-charlcd"
#define CHARLCD_TIMEOUT (msecs_to_jiffies(1000))

/* Offsets to registers */
#define CHAR_COM	0x00U
#define CHAR_DAT	0x04U
#define CHAR_RD		0x08U
#define CHAR_RAW	0x0CU
#define CHAR_MASK	0x10U
#define CHAR_STAT	0x14U

#define CHAR_RAW_CLEAR	0x00000000U
#define CHAR_RAW_VALID	0x00000100U

/* Hitachi HD44780 display commands */
#define HD_CLEAR			0x01U
#define HD_HOME				0x02U
#define HD_ENTRYMODE			0x04U
#define HD_ENTRYMODE_INCREMENT		0x02U
#define HD_ENTRYMODE_SHIFT		0x01U
#define HD_DISPCTRL			0x08U
#define HD_DISPCTRL_ON			0x04U
#define HD_DISPCTRL_CURSOR_ON		0x02U
#define HD_DISPCTRL_CURSOR_BLINK	0x01U
#define HD_CRSR_SHIFT			0x10U
#define HD_CRSR_SHIFT_DISPLAY		0x08U
#define HD_CRSR_SHIFT_DISPLAY_RIGHT	0x04U
#define HD_FUNCSET			0x20U
#define HD_FUNCSET_8BIT			0x10U
#define HD_FUNCSET_2_LINES		0x08U
#define HD_FUNCSET_FONT_5X10		0x04U
#define HD_SET_CGRAM			0x40U
#define HD_SET_DDRAM			0x80U
#define HD_BUSY_FLAG			0x80U

/**
 * struct charlcd - Private data structure
 * @dev: a pointer back to containing device
 * @virtbase: the offset to the controller in virtual memory
 * @irq: reserved interrupt number
 * @complete: completion structure for the last LCD command
 * @init_work: delayed work structure to initialize the display on boot
 */
struct charlcd {
	struct device *dev;
	void __iomem *virtbase;
	int irq;
	struct completion complete;
	struct delayed_work init_work;
};

static irqreturn_t charlcd_interrupt(int irq, void *data)
{
	struct charlcd *lcd = data;
	u8 status;

	status = readl(lcd->virtbase + CHAR_STAT) & 0x01;
	/* Clear IRQ */
	writel(CHAR_RAW_CLEAR, lcd->virtbase + CHAR_RAW);
	if (status)
		complete(&lcd->complete);
	else
		dev_info(lcd->dev, "Spurious IRQ (%02x)\n", status);
	return IRQ_HANDLED;
}


static void charlcd_wait_complete_irq(struct charlcd *lcd)
{
	int ret;

	ret = wait_for_completion_interruptible_timeout(&lcd->complete,
							CHARLCD_TIMEOUT);
	/* Disable IRQ after completion */
	writel(0x00, lcd->virtbase + CHAR_MASK);

	if (ret < 0) {
		dev_err(lcd->dev,
			"wait_for_completion_interruptible_timeout() returned %d waiting for ready\n",
			ret);
		return;
	}

	if (ret == 0) {
		dev_err(lcd->dev, "charlcd controller timed out waiting for ready\n");
		return;
	}
}

static u8 charlcd_4bit_read_char(struct charlcd *lcd)
{
	u8 data;
	u32 val;

	/* If we can, use an IRQ to wait for the data, else poll */
	if (lcd->irq >= 0)
		charlcd_wait_complete_irq(lcd);
	else {
		udelay(100);
		readl_poll_timeout_atomic(lcd->virtbase + CHAR_RAW, val,
					  val & CHAR_RAW_VALID, 100, 1000);
		writel(CHAR_RAW_CLEAR, lcd->virtbase + CHAR_RAW);
	}
	msleep(1);

	/* Read the 4 high bits of the data */
	data = readl(lcd->virtbase + CHAR_RD) & 0xf0;

	/*
	 * The second read for the low bits does not trigger an IRQ
	 * so in this case we have to poll for the 4 lower bits
	 */
	udelay(100);
	readl_poll_timeout_atomic(lcd->virtbase + CHAR_RAW, val,
				  val & CHAR_RAW_VALID, 100, 1000);
	writel(CHAR_RAW_CLEAR, lcd->virtbase + CHAR_RAW);
	msleep(1);

	/* Read the 4 low bits of the data */
	data |= (readl(lcd->virtbase + CHAR_RD) >> 4) & 0x0f;

	return data;
}

static bool charlcd_4bit_read_bf(struct charlcd *lcd)
{
	if (lcd->irq >= 0) {
		/*
		 * If we'll use IRQs to wait for the busyflag, clear any
		 * pending flag and enable IRQ
		 */
		writel(CHAR_RAW_CLEAR, lcd->virtbase + CHAR_RAW);
		init_completion(&lcd->complete);
		writel(0x01, lcd->virtbase + CHAR_MASK);
	}
	readl(lcd->virtbase + CHAR_COM);

	return charlcd_4bit_read_char(lcd) & HD_BUSY_FLAG;
}

static void charlcd_4bit_wait_busy(struct charlcd *lcd)
{
	int retries = 50;

	udelay(100);
	while (charlcd_4bit_read_bf(lcd) && retries)
		retries--;
	if (!retries)
		dev_err(lcd->dev, "timeout waiting for busyflag\n");
}

static void charlcd_4bit_command(struct charlcd *lcd, u8 cmd)
{
	u32 cmdlo = (cmd << 4) & 0xf0;
	u32 cmdhi = (cmd & 0xf0);

	writel(cmdhi, lcd->virtbase + CHAR_COM);
	udelay(10);
	writel(cmdlo, lcd->virtbase + CHAR_COM);
	charlcd_4bit_wait_busy(lcd);
}

static void charlcd_4bit_char(struct charlcd *lcd, u8 ch)
{
	u32 chlo = (ch << 4) & 0xf0;
	u32 chhi = (ch & 0xf0);

	writel(chhi, lcd->virtbase + CHAR_DAT);
	udelay(10);
	writel(chlo, lcd->virtbase + CHAR_DAT);
	charlcd_4bit_wait_busy(lcd);
}

static void charlcd_4bit_print(struct charlcd *lcd, int line, const char *str)
{
	u8 offset;
	int i;

	/*
	 * We support line 0, 1
	 * Line 1 runs from 0x00..0x27
	 * Line 2 runs from 0x28..0x4f
	 */
	if (line == 0)
		offset = 0;
	else if (line == 1)
		offset = 0x28;
	else
		return;

	/* Set offset */
	charlcd_4bit_command(lcd, HD_SET_DDRAM | offset);

	/* Send string */
	for (i = 0; i < strlen(str) && i < 0x28; i++)
		charlcd_4bit_char(lcd, str[i]);
}

static void charlcd_4bit_init(struct charlcd *lcd)
{
	/* These commands cannot be checked with the busy flag */
	writel(HD_FUNCSET | HD_FUNCSET_8BIT, lcd->virtbase + CHAR_COM);
	msleep(5);
	writel(HD_FUNCSET | HD_FUNCSET_8BIT, lcd->virtbase + CHAR_COM);
	udelay(100);
	writel(HD_FUNCSET | HD_FUNCSET_8BIT, lcd->virtbase + CHAR_COM);
	udelay(100);
	/* Go to 4bit mode */
	writel(HD_FUNCSET, lcd->virtbase + CHAR_COM);
	udelay(100);
	/*
	 * 4bit mode, 2 lines, 5x8 font, after this the number of lines
	 * and the font cannot be changed until the next initialization sequence
	 */
	charlcd_4bit_command(lcd, HD_FUNCSET | HD_FUNCSET_2_LINES);
	charlcd_4bit_command(lcd, HD_DISPCTRL | HD_DISPCTRL_ON);
	charlcd_4bit_command(lcd, HD_ENTRYMODE | HD_ENTRYMODE_INCREMENT);
	charlcd_4bit_command(lcd, HD_CLEAR);
	charlcd_4bit_command(lcd, HD_HOME);
	/* Put something useful in the display */
	charlcd_4bit_print(lcd, 0, "ARM Linux");
	charlcd_4bit_print(lcd, 1, UTS_RELEASE);
}

static void charlcd_init_work(struct work_struct *work)
{
	struct charlcd *lcd =
		container_of(work, struct charlcd, init_work.work);

	charlcd_4bit_init(lcd);
}

static int __init charlcd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;
	struct charlcd *lcd;

	lcd = devm_kzalloc(dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	lcd->dev = &pdev->dev;

	lcd->virtbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(lcd->virtbase))
		return PTR_ERR(lcd->virtbase);

	lcd->irq = platform_get_irq(pdev, 0);
	/* If no IRQ is supplied, we'll survive without it */
	if (lcd->irq >= 0) {
		ret = devm_request_irq(dev, lcd->irq, charlcd_interrupt, 0, DRIVERNAME, lcd);
		if (ret)
			return ret;
	}

	platform_set_drvdata(pdev, lcd);

	/*
	 * Initialize the display in a delayed work, because
	 * it is VERY slow and would slow down the boot of the system.
	 */
	INIT_DELAYED_WORK(&lcd->init_work, charlcd_init_work);
	schedule_delayed_work(&lcd->init_work, 0);

	return 0;
}

static int charlcd_suspend(struct device *dev)
{
	struct charlcd *lcd = dev_get_drvdata(dev);

	/* Power the display off */
	charlcd_4bit_command(lcd, HD_DISPCTRL);
	return 0;
}

static int charlcd_resume(struct device *dev)
{
	struct charlcd *lcd = dev_get_drvdata(dev);

	/* Turn the display back on */
	charlcd_4bit_command(lcd, HD_DISPCTRL | HD_DISPCTRL_ON);
	return 0;
}

static const struct dev_pm_ops charlcd_pm_ops = {
	.suspend = charlcd_suspend,
	.resume = charlcd_resume,
};

static const struct of_device_id charlcd_match[] = {
	{ .compatible = "arm,versatile-lcd", },
	{}
};

static struct platform_driver charlcd_driver = {
	.driver = {
		.name = DRIVERNAME,
		.pm = &charlcd_pm_ops,
		.suppress_bind_attrs = true,
		.of_match_table = charlcd_match,
	},
};
builtin_platform_driver_probe(charlcd_driver, charlcd_probe);
