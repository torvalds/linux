// SPDX-License-Identifier: GPL-2.0-only
/* uctrl.c: TS102 Microcontroller interface on Tadpole Sparcbook 3
 *
 * Copyright 1999 Derrick J Brashear (shadow@dementia.org)
 * Copyright 2008 David S. Miller (davem@davemloft.net)
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/ioport.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/irq.h>
#include <asm/io.h>

#define DEBUG 1
#ifdef DEBUG
#define dprintk(x) printk x
#else
#define dprintk(x)
#endif

struct uctrl_regs {
	u32 uctrl_intr;
	u32 uctrl_data;
	u32 uctrl_stat;
	u32 uctrl_xxx[5];
};

struct ts102_regs {
	u32 card_a_intr;
	u32 card_a_stat;
	u32 card_a_ctrl;
	u32 card_a_xxx;
	u32 card_b_intr;
	u32 card_b_stat;
	u32 card_b_ctrl;
	u32 card_b_xxx;
	u32 uctrl_intr;
	u32 uctrl_data;
	u32 uctrl_stat;
	u32 uctrl_xxx;
	u32 ts102_xxx[4];
};

/* Bits for uctrl_intr register */
#define UCTRL_INTR_TXE_REQ         0x01    /* transmit FIFO empty int req */
#define UCTRL_INTR_TXNF_REQ        0x02    /* transmit FIFO not full int req */
#define UCTRL_INTR_RXNE_REQ        0x04    /* receive FIFO not empty int req */
#define UCTRL_INTR_RXO_REQ         0x08    /* receive FIFO overflow int req */
#define UCTRL_INTR_TXE_MSK         0x10    /* transmit FIFO empty mask */
#define UCTRL_INTR_TXNF_MSK        0x20    /* transmit FIFO not full mask */
#define UCTRL_INTR_RXNE_MSK        0x40    /* receive FIFO not empty mask */
#define UCTRL_INTR_RXO_MSK         0x80    /* receive FIFO overflow mask */

/* Bits for uctrl_stat register */
#define UCTRL_STAT_TXE_STA         0x01    /* transmit FIFO empty status */
#define UCTRL_STAT_TXNF_STA        0x02    /* transmit FIFO not full status */
#define UCTRL_STAT_RXNE_STA        0x04    /* receive FIFO not empty status */
#define UCTRL_STAT_RXO_STA         0x08    /* receive FIFO overflow status */

static DEFINE_MUTEX(uctrl_mutex);
static const char *uctrl_extstatus[16] = {
        "main power available",
        "internal battery attached",
        "external battery attached",
        "external VGA attached",
        "external keyboard attached",
        "external mouse attached",
        "lid down",
        "internal battery currently charging",
        "external battery currently charging",
        "internal battery currently discharging",
        "external battery currently discharging",
};

/* Everything required for one transaction with the uctrl */
struct uctrl_txn {
	u8 opcode;
	u8 inbits;
	u8 outbits;
	u8 *inbuf;
	u8 *outbuf;
};

struct uctrl_status {
	u8 current_temp; /* 0x07 */
	u8 reset_status; /* 0x0b */
	u16 event_status; /* 0x0c */
	u16 error_status; /* 0x10 */
	u16 external_status; /* 0x11, 0x1b */
	u8 internal_charge; /* 0x18 */
	u8 external_charge; /* 0x19 */
	u16 control_lcd; /* 0x20 */
	u8 control_bitport; /* 0x21 */
	u8 speaker_volume; /* 0x23 */
	u8 control_tft_brightness; /* 0x24 */
	u8 control_kbd_repeat_delay; /* 0x28 */
	u8 control_kbd_repeat_period; /* 0x29 */
	u8 control_screen_contrast; /* 0x2F */
};

enum uctrl_opcode {
  READ_SERIAL_NUMBER=0x1,
  READ_ETHERNET_ADDRESS=0x2,
  READ_HARDWARE_VERSION=0x3,
  READ_MICROCONTROLLER_VERSION=0x4,
  READ_MAX_TEMPERATURE=0x5,
  READ_MIN_TEMPERATURE=0x6,
  READ_CURRENT_TEMPERATURE=0x7,
  READ_SYSTEM_VARIANT=0x8,
  READ_POWERON_CYCLES=0x9,
  READ_POWERON_SECONDS=0xA,
  READ_RESET_STATUS=0xB,
  READ_EVENT_STATUS=0xC,
  READ_REAL_TIME_CLOCK=0xD,
  READ_EXTERNAL_VGA_PORT=0xE,
  READ_MICROCONTROLLER_ROM_CHECKSUM=0xF,
  READ_ERROR_STATUS=0x10,
  READ_EXTERNAL_STATUS=0x11,
  READ_USER_CONFIGURATION_AREA=0x12,
  READ_MICROCONTROLLER_VOLTAGE=0x13,
  READ_INTERNAL_BATTERY_VOLTAGE=0x14,
  READ_DCIN_VOLTAGE=0x15,
  READ_HORIZONTAL_POINTER_VOLTAGE=0x16,
  READ_VERTICAL_POINTER_VOLTAGE=0x17,
  READ_INTERNAL_BATTERY_CHARGE_LEVEL=0x18,
  READ_EXTERNAL_BATTERY_CHARGE_LEVEL=0x19,
  READ_REAL_TIME_CLOCK_ALARM=0x1A,
  READ_EVENT_STATUS_NO_RESET=0x1B,
  READ_INTERNAL_KEYBOARD_LAYOUT=0x1C,
  READ_EXTERNAL_KEYBOARD_LAYOUT=0x1D,
  READ_EEPROM_STATUS=0x1E,
  CONTROL_LCD=0x20,
  CONTROL_BITPORT=0x21,
  SPEAKER_VOLUME=0x23,
  CONTROL_TFT_BRIGHTNESS=0x24,
  CONTROL_WATCHDOG=0x25,
  CONTROL_FACTORY_EEPROM_AREA=0x26,
  CONTROL_KBD_TIME_UNTIL_REPEAT=0x28,
  CONTROL_KBD_TIME_BETWEEN_REPEATS=0x29,
  CONTROL_TIMEZONE=0x2A,
  CONTROL_MARK_SPACE_RATIO=0x2B,
  CONTROL_DIAGNOSTIC_MODE=0x2E,
  CONTROL_SCREEN_CONTRAST=0x2F,
  RING_BELL=0x30,
  SET_DIAGNOSTIC_STATUS=0x32,
  CLEAR_KEY_COMBINATION_TABLE=0x33,
  PERFORM_SOFTWARE_RESET=0x34,
  SET_REAL_TIME_CLOCK=0x35,
  RECALIBRATE_POINTING_STICK=0x36,
  SET_BELL_FREQUENCY=0x37,
  SET_INTERNAL_BATTERY_CHARGE_RATE=0x39,
  SET_EXTERNAL_BATTERY_CHARGE_RATE=0x3A,
  SET_REAL_TIME_CLOCK_ALARM=0x3B,
  READ_EEPROM=0x40,
  WRITE_EEPROM=0x41,
  WRITE_TO_STATUS_DISPLAY=0x42,
  DEFINE_SPECIAL_CHARACTER=0x43,
  DEFINE_KEY_COMBINATION_ENTRY=0x50,
  DEFINE_STRING_TABLE_ENTRY=0x51,
  DEFINE_STATUS_SCREEN_DISPLAY=0x52,
  PERFORM_EMU_COMMANDS=0x64,
  READ_EMU_REGISTER=0x65,
  WRITE_EMU_REGISTER=0x66,
  READ_EMU_RAM=0x67,
  WRITE_EMU_RAM=0x68,
  READ_BQ_REGISTER=0x69,
  WRITE_BQ_REGISTER=0x6A,
  SET_USER_PASSWORD=0x70,
  VERIFY_USER_PASSWORD=0x71,
  GET_SYSTEM_PASSWORD_KEY=0x72,
  VERIFY_SYSTEM_PASSWORD=0x73,
  POWER_OFF=0x82,
  POWER_RESTART=0x83,
};

static struct uctrl_driver {
	struct uctrl_regs __iomem *regs;
	int irq;
	int pending;
	struct uctrl_status status;
} *global_driver;

static void uctrl_get_event_status(struct uctrl_driver *);
static void uctrl_get_external_status(struct uctrl_driver *);

static long
uctrl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		default:
			return -EINVAL;
	}
	return 0;
}

static int
uctrl_open(struct inode *inode, struct file *file)
{
	mutex_lock(&uctrl_mutex);
	uctrl_get_event_status(global_driver);
	uctrl_get_external_status(global_driver);
	mutex_unlock(&uctrl_mutex);
	return 0;
}

static irqreturn_t uctrl_interrupt(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static const struct file_operations uctrl_fops = {
	.owner =	THIS_MODULE,
	.unlocked_ioctl =	uctrl_ioctl,
	.open =		uctrl_open,
};

static struct miscdevice uctrl_dev = {
	UCTRL_MINOR,
	"uctrl",
	&uctrl_fops
};

/* Wait for space to write, then write to it */
#define WRITEUCTLDATA(value) \
{ \
  unsigned int i; \
  for (i = 0; i < 10000; i++) { \
      if (UCTRL_STAT_TXNF_STA & sbus_readl(&driver->regs->uctrl_stat)) \
      break; \
  } \
  dprintk(("write data 0x%02x\n", value)); \
  sbus_writel(value, &driver->regs->uctrl_data); \
}

/* Wait for something to read, read it, then clear the bit */
#define READUCTLDATA(value) \
{ \
  unsigned int i; \
  value = 0; \
  for (i = 0; i < 10000; i++) { \
      if ((UCTRL_STAT_RXNE_STA & sbus_readl(&driver->regs->uctrl_stat)) == 0) \
      break; \
    udelay(1); \
  } \
  value = sbus_readl(&driver->regs->uctrl_data); \
  dprintk(("read data 0x%02x\n", value)); \
  sbus_writel(UCTRL_STAT_RXNE_STA, &driver->regs->uctrl_stat); \
}

static void uctrl_do_txn(struct uctrl_driver *driver, struct uctrl_txn *txn)
{
	int stat, incnt, outcnt, bytecnt, intr;
	u32 byte;

	stat = sbus_readl(&driver->regs->uctrl_stat);
	intr = sbus_readl(&driver->regs->uctrl_intr);
	sbus_writel(stat, &driver->regs->uctrl_stat);

	dprintk(("interrupt stat 0x%x int 0x%x\n", stat, intr));

	incnt = txn->inbits;
	outcnt = txn->outbits;
	byte = (txn->opcode << 8);
	WRITEUCTLDATA(byte);

	bytecnt = 0;
	while (incnt > 0) {
		byte = (txn->inbuf[bytecnt] << 8);
		WRITEUCTLDATA(byte);
		incnt--;
		bytecnt++;
	}

	/* Get the ack */
	READUCTLDATA(byte);
	dprintk(("ack was %x\n", (byte >> 8)));

	bytecnt = 0;
	while (outcnt > 0) {
		READUCTLDATA(byte);
		txn->outbuf[bytecnt] = (byte >> 8);
		dprintk(("set byte to %02x\n", byte));
		outcnt--;
		bytecnt++;
	}
}

static void uctrl_get_event_status(struct uctrl_driver *driver)
{
	struct uctrl_txn txn;
	u8 outbits[2];

	txn.opcode = READ_EVENT_STATUS;
	txn.inbits = 0;
	txn.outbits = 2;
	txn.inbuf = NULL;
	txn.outbuf = outbits;

	uctrl_do_txn(driver, &txn);

	dprintk(("bytes %x %x\n", (outbits[0] & 0xff), (outbits[1] & 0xff)));
	driver->status.event_status = 
		((outbits[0] & 0xff) << 8) | (outbits[1] & 0xff);
	dprintk(("ev is %x\n", driver->status.event_status));
}

static void uctrl_get_external_status(struct uctrl_driver *driver)
{
	struct uctrl_txn txn;
	u8 outbits[2];
	int i, v;

	txn.opcode = READ_EXTERNAL_STATUS;
	txn.inbits = 0;
	txn.outbits = 2;
	txn.inbuf = NULL;
	txn.outbuf = outbits;

	uctrl_do_txn(driver, &txn);

	dprintk(("bytes %x %x\n", (outbits[0] & 0xff), (outbits[1] & 0xff)));
	driver->status.external_status = 
		((outbits[0] * 256) + (outbits[1]));
	dprintk(("ex is %x\n", driver->status.external_status));
	v = driver->status.external_status;
	for (i = 0; v != 0; i++, v >>= 1) {
		if (v & 1) {
			dprintk(("%s%s", " ", uctrl_extstatus[i]));
		}
	}
	dprintk(("\n"));
	
}

static int uctrl_probe(struct platform_device *op)
{
	struct uctrl_driver *p;
	int err = -ENOMEM;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		printk(KERN_ERR "uctrl: Unable to allocate device struct.\n");
		goto out;
	}

	p->regs = of_ioremap(&op->resource[0], 0,
			     resource_size(&op->resource[0]),
			     "uctrl");
	if (!p->regs) {
		printk(KERN_ERR "uctrl: Unable to map registers.\n");
		goto out_free;
	}

	p->irq = op->archdata.irqs[0];
	err = request_irq(p->irq, uctrl_interrupt, 0, "uctrl", p);
	if (err) {
		printk(KERN_ERR "uctrl: Unable to register irq.\n");
		goto out_iounmap;
	}

	err = misc_register(&uctrl_dev);
	if (err) {
		printk(KERN_ERR "uctrl: Unable to register misc device.\n");
		goto out_free_irq;
	}

	sbus_writel(UCTRL_INTR_RXNE_REQ|UCTRL_INTR_RXNE_MSK, &p->regs->uctrl_intr);
	printk(KERN_INFO "%pOF: uctrl regs[0x%p] (irq %d)\n",
	       op->dev.of_node, p->regs, p->irq);
	uctrl_get_event_status(p);
	uctrl_get_external_status(p);

	dev_set_drvdata(&op->dev, p);
	global_driver = p;

out:
	return err;

out_free_irq:
	free_irq(p->irq, p);

out_iounmap:
	of_iounmap(&op->resource[0], p->regs, resource_size(&op->resource[0]));

out_free:
	kfree(p);
	goto out;
}

static void uctrl_remove(struct platform_device *op)
{
	struct uctrl_driver *p = dev_get_drvdata(&op->dev);

	if (p) {
		misc_deregister(&uctrl_dev);
		free_irq(p->irq, p);
		of_iounmap(&op->resource[0], p->regs, resource_size(&op->resource[0]));
		kfree(p);
	}
}

static const struct of_device_id uctrl_match[] = {
	{
		.name = "uctrl",
	},
	{},
};
MODULE_DEVICE_TABLE(of, uctrl_match);

static struct platform_driver uctrl_driver = {
	.driver = {
		.name = "uctrl",
		.of_match_table = uctrl_match,
	},
	.probe		= uctrl_probe,
	.remove		= uctrl_remove,
};


module_platform_driver(uctrl_driver);

MODULE_DESCRIPTION("Tadpole TS102 Microcontroller driver");
MODULE_LICENSE("GPL");
