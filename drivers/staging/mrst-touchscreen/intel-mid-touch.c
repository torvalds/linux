/*
 * intel_mid_touch.c - Intel MID Resistive Touch Screen Driver
 *
 * Copyright (C) 2008 Intel Corp
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; ifnot, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * Questions/Comments/Bug fixes to Sreedhara (sreedhara.ds@intel.com)
 * 			    Ramesh Agarwal (ramesh.agarwal@intel.com)
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * TODO:
 *	kill off mrstouch_debug eventually
 *	review conversion of r/m/w sequences
 *	Replace interrupt mutex abuse
 *	Kill of mrstouchdevp pointer
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/param.h>
#include <linux/spi/spi.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <asm/intel_scu_ipc.h>


#if defined(MRSTOUCH_DEBUG)
#define mrstouch_debug(fmt, args...)\
	do { \
		printk(KERN_DEBUG "\n[MRSTOUCH(%d)] - ", __LINE__); \
		printk(KERN_DEBUG fmt, ##args); \
	} while (0);
#else
#define mrstouch_debug(fmt, args...)
#endif

/* PMIC Interrupt registers */
#define PMIC_REG_ID1   0x00 /*PMIC ID1 register */

/* PMIC Interrupt registers */
#define PMIC_REG_INT   0x04 /*PMIC interrupt register */
#define PMIC_REG_MINT  0x05 /*PMIC interrupt mask register */

/* ADC Interrupt registers */
#define PMIC_REG_ADCINT   0x5F /*ADC interrupt register */
#define PMIC_REG_MADCINT  0x60 /*ADC interrupt mask register */

/* ADC Control registers */
#define PMIC_REG_ADCCNTL1    0x61 /*ADC control register */

/* ADC Channel Selection registers */
#define PMICADDR0     0xA4
#define END_OF_CHANNEL 0x1F

/* ADC Result register */
#define PMIC_REG_ADCSNS0H   0x64

/* ADC channels for touch screen */
#define MRST_TS_CHAN10   0xA /* Touch screen X+ connection */
#define MRST_TS_CHAN11   0xB /* Touch screen X- connection */
#define MRST_TS_CHAN12   0xC /* Touch screen Y+ connection */
#define MRST_TS_CHAN13   0xD /* Touch screen Y- connection */

/* Touch screen coordinate constants */
#define TOUCH_PRESSURE   	50
#define TOUCH_PRESSURE_FS	100

#define XMOVE_LIMIT	5
#define YMOVE_LIMIT	5
#define XYMOVE_CNT	3

#define MAX_10BIT	((1<<10)-1)

/* Touch screen channel BIAS constants */
#define XBIAS		0x20
#define YBIAS		0x40
#define ZBIAS		0x80

/* Touch screen coordinates */
#define MIN_X		10
#define MAX_X		1024
#define MIN_Y		10
#define MAX_Y		1024
#define WAIT_ADC_COMPLETION 10

/* PMIC ADC round robin delays */
#define ADC_LOOP_DELAY0 0x0 /* Continuous loop */
#define ADC_LOOP_DELAY1 0x1 /* 4.5  ms approximate */

/* PMIC Vendor Identifiers */
#define PMIC_VENDOR_FS  0 /* PMIC vendor FreeScale */
#define PMIC_VENDOR_MAXIM 1 /* PMIC vendor MAXIM */
#define PMIC_VENDOR_NEC 2 /* PMIC vendor NEC */
#define MRSTOUCH_MAX_CHANNELS 32 /* Maximum ADC channels */

/* Touch screen device structure */
struct mrstouch_dev {
	struct spi_device *spi; /* SPI device associated with touch screen */
	struct input_dev *input; /* input device for touchscreen*/
	char 		phys[32]; /* Device name */
	struct task_struct *pendet_thrd; /* PENDET interrupt handler */
	struct mutex lock; /* Sync between interrupt and PENDET handler */
	bool            busy; /* Busy flag */
	u16             asr; /* Address selection register */
	int             irq;    /* Touch screen IRQ # */
	uint		vendor;  /* PMIC vendor */
	uint		rev;  /* PMIC revision */
	bool		suspended; /* Device suspended status */
	bool		disabled;  /* Device disabled status */
	u16		x;  /* X coordinate */
	u16		y;  /* Y coordinate */
	bool		pendown;  /* PEN position */
} ;


/* Global Pointer to Touch screen device */
static struct mrstouch_dev *mrstouchdevp;

/* Utility to read PMIC ID */
static int mrstouch_pmic_id(uint *vendor, uint *rev)
{
	int err;
	u8 r;

	err = intel_scu_ipc_ioread8(PMIC_REG_ID1, &r);
	if (err)
		return err;

	*vendor = r & 0x7;
	*rev = (r >> 3) & 0x7;

	return 0;
}

/*
 * Parse ADC channels to find end of the channel configured by other ADC user
 * NEC and MAXIM requires 4 channels and FreeScale needs 18 channels
 */
static int mrstouch_chan_parse(struct mrstouch_dev *tsdev)
{
	int err, i, j, found;
	u32 r32;

	found = -1;

	for (i = 0; i < MRSTOUCH_MAX_CHANNELS; i++) {
		if (found >= 0)
			break;

		err = intel_scu_ipc_ioread32(PMICADDR0, &r32);
		if (err)
			return err;

		for (j = 0; j < 32; j+= 8) {
			if (((r32 >> j) & 0xFF) == END_OF_CHANNEL) {
				found = i;
				break;
			}
		}
	}
	if (found < 0)
		return 0;

	if (tsdev->vendor == PMIC_VENDOR_FS) {
		if (found && found > (MRSTOUCH_MAX_CHANNELS - 18))
			return -ENOSPC;
	} else {
		if (found && found > (MRSTOUCH_MAX_CHANNELS - 4))
			return -ENOSPC;
	}
	return found;
}

/* Utility to enable/disable pendet.
 * pendet set to true enables PENDET interrupt
 * pendet set to false disables PENDET interrupt
 * Also clears RND mask bit
*/
static int pendet_enable(struct mrstouch_dev *tsdev, bool pendet)
{
	u16 reg;
	u8 r;
	u8 pendet_enabled = 0;
	int retry = 0;
	int err;

	err = intel_scu_ipc_ioread16(PMIC_REG_MADCINT, &reg);
	if (err)
		return err;

	if (pendet) {
		reg &= ~0x0005;
		reg |= 0x2000; /* Enable pendet */
	} else
		reg &= 0xDFFF; /* Disable pendet */

	/* Set MADCINT and update ADCCNTL1 (next reg byte) */
	err = intel_scu_ipc_iowrite16(PMIC_REG_MADCINT, reg);
	if (!pendet || err)
		return err;

	/*
	 * Sometimes even after the register write succeeds
	 * the PMIC register value is not updated. Retry few iterations
	 * to enable pendet.
	 */

	err = intel_scu_ipc_ioread8(PMIC_REG_ADCCNTL1, &r);
	pendet_enabled = (r >> 5) & 0x01;

	retry = 0;
	while (!err && !pendet_enabled) {
		retry++;
		msleep(10);
		err = intel_scu_ipc_iowrite8(PMIC_REG_ADCCNTL1, reg >> 8);
		if (err)
			break;
		err = intel_scu_ipc_ioread8(PMIC_REG_ADCCNTL1, &r);
		if (err == 0)
			pendet_enabled = (r >> 5) & 0x01;
		if (retry >= 10) {
			dev_err(&tsdev->spi->dev, "Touch screen disabled.\n");
			return -EIO;
		}
	}
	return 0;
}

/* To read PMIC ADC touch screen result
 * Reads ADC storage registers for higher 7 and lower 3 bits
 * converts the two readings to single value and turns off gain bit
 */
static int mrstouch_ts_chan_read(u16 offset, u16 chan, u16 *vp, u16 *vm)
{
	int err;
	u16 result;
	u32 res;

	result = PMIC_REG_ADCSNS0H + offset;

	if (chan == MRST_TS_CHAN12)
		result += 4;

	err = intel_scu_ipc_ioread32(result, &res);
	if (err)
		return err;

	/* Mash the bits up */

	*vp = (res & 0xFF) << 3;	/* Highest 7 bits */
	*vp |= (res >> 8) & 0x07;	/* Lower 3 bits */
	*vp &= 0x3FF;

	res >>= 16;

	*vm = (res & 0xFF) << 3;	/* Highest 7 bits */
	*vm |= (res >> 8) & 0x07;	/* Lower 3 bits */
	*vm &= 0x3FF;

	return 0;
}

/* To configure touch screen channels
 * Writes touch screen channels to ADC address selection registers
 */
static int mrstouch_ts_chan_set(uint offset)
{
	int count;
	u16 chan;
	u16 reg[5];
	u8 data[5];

	chan = PMICADDR0 + offset;
	for (count = 0; count <= 3; count++) {
		reg[count] = chan++;
		data[count] = MRST_TS_CHAN10 + count;
	}
	reg[count] = chan;
	data[count] = END_OF_CHANNEL;

	return intel_scu_ipc_writev(reg, data, 5);
}

/* Initialize ADC */
static int mrstouch_adc_init(struct mrstouch_dev *tsdev)
{
	int err, start;
	u8 ra, rm;

	err = mrstouch_pmic_id(&tsdev->vendor, &tsdev->rev);
	if (err) {
		dev_err(&tsdev->spi->dev, "Unable to read PMIC id\n");
		return err;
	}

	start = mrstouch_chan_parse(tsdev);
	if (start < 0) {
		dev_err(&tsdev->spi->dev, "Unable to parse channels\n");
		return start;
	}

	tsdev->asr = start;

	mrstouch_debug("Channel offset(%d): 0x%X\n", tsdev->asr, tsdev->vendor);

	/* ADC power on, start, enable PENDET and set loop delay
	 * ADC loop delay is set to 4.5 ms approximately
	 * Loop delay more than this results in jitter in adc readings
	 * Setting loop delay to 0 (continous loop) in MAXIM stops PENDET
	 * interrupt generation sometimes.
	 */

	if (tsdev->vendor == PMIC_VENDOR_FS) {
		ra = 0xE0 | ADC_LOOP_DELAY0;
		rm = 0x5;
	} else {
		/* NEC and MAXIm not consistent with loop delay 0 */
		ra = 0xE0 | ADC_LOOP_DELAY1;
		rm = 0x0;

		/* configure touch screen channels */
		err = mrstouch_ts_chan_set(tsdev->asr);
		if (err)
			return err;
	}
	err = intel_scu_ipc_update_register(PMIC_REG_ADCCNTL1, ra, 0xE7);
	if (err == 0)
		err = intel_scu_ipc_update_register(PMIC_REG_MADCINT, rm, 0x03);
	return err;
}

/* Reports x,y coordinates to event subsystem */
static void mrstouch_report_xy(struct mrstouch_dev *tsdev, u16 x, u16 y, u16 z)
{
	int xdiff, ydiff;

	if (tsdev->pendown && z <= TOUCH_PRESSURE) {
		/* Pen removed, report button release */
		mrstouch_debug("BTN REL(%d)", z);
		input_report_key(tsdev->input, BTN_TOUCH, 0);
		tsdev->pendown = false;
	}

	xdiff = abs(x - tsdev->x);
	ydiff = abs(y - tsdev->y);

	/*
	if x and y values changes for XYMOVE_CNT readings it is considered
	as stylus is moving. This is required to differentiate between stylus
	movement and jitter
	*/
	if (x < MIN_X || x > MAX_X || y < MIN_Y || y > MAX_Y) {
		/* Spurious values, release button if touched and return */
		if (tsdev->pendown) {
			mrstouch_debug("BTN REL(%d)", z);
			input_report_key(tsdev->input, BTN_TOUCH, 0);
			tsdev->pendown = false;
		}
		return;
	} else if (xdiff >= XMOVE_LIMIT || ydiff >= YMOVE_LIMIT) {
		tsdev->x = x;
		tsdev->y = y;

		input_report_abs(tsdev->input, ABS_X, x);
		input_report_abs(tsdev->input, ABS_Y, y);
		input_sync(tsdev->input);
	}


	if (!tsdev->pendown && z > TOUCH_PRESSURE) {
		/* Pen touched, report button touch */
		mrstouch_debug("BTN TCH(%d, %d, %d)", x, y, z);
		input_report_key(tsdev->input, BTN_TOUCH, 1);
		tsdev->pendown = true;
	}
}


/* Utility to start ADC, used by freescale handler */
static int pendet_mask(void)
{
	return 	intel_scu_ipc_update_register(PMIC_REG_MADCINT, 0x02, 0x02);
}

/* Utility to stop ADC, used by freescale handler */
static int pendet_umask(void)
{
	return 	intel_scu_ipc_update_register(PMIC_REG_MADCINT, 0x00, 0x02);
}

/* Utility to read ADC, used by freescale handler */
static int mrstouch_pmic_fs_adc_read(struct mrstouch_dev *tsdev)
{
	int err;
	u16 x, y, z, result;
	u16 reg[4];
	u8 data[4];

	result = PMIC_REG_ADCSNS0H + tsdev->asr;

	reg[0] = result + 4;
	reg[1] = result + 5;
	reg[2] = result + 16;
	reg[3] = result + 17;

	err = intel_scu_ipc_readv(reg, data, 4);
	if (err)
		goto ipc_error;

	x = data[0] << 3; /* Higher 7 bits */
	x |= data[1] & 0x7; /* Lower 3 bits */
	x &= 0x3FF;

	y = data[2] << 3; /* Higher 7 bits */
	y |= data[3] & 0x7; /* Lower 3 bits */
	y &= 0x3FF;

	/* Read Z value */
	reg[0] = result + 28;
	reg[1] = result + 29;

	err = intel_scu_ipc_readv(reg, data, 4);
	if (err)
		goto ipc_error;

	z = data[0] << 3; /* Higher 7 bits */
	z |= data[1] & 0x7; /* Lower 3 bits */
	z &= 0x3FF;

#if defined(MRSTOUCH_PRINT_XYZP)
	mrstouch_debug("X: %d, Y: %d, Z: %d", x, y, z);
#endif

	if (z >= TOUCH_PRESSURE_FS) {
		mrstouch_report_xy(tsdev, x, y, TOUCH_PRESSURE - 1); /* Pen Removed */
		return TOUCH_PRESSURE - 1;
	} else {
		mrstouch_report_xy(tsdev, x, y, TOUCH_PRESSURE + 1); /* Pen Touched */
		return TOUCH_PRESSURE + 1;
	}

	return 0;

ipc_error:
	dev_err(&tsdev->spi->dev, "ipc error during fs_adc read\n");
	return err;
}

/* To handle free scale pmic pendet interrupt */
static int pmic0_pendet(void *dev_id)
{
	int err, count;
	u16 chan;
	unsigned int touched;
	struct mrstouch_dev *tsdev = (struct mrstouch_dev *)dev_id;
	u16 reg[5];
	u8 data[5];

	chan = PMICADDR0 + tsdev->asr;

	/* Set X BIAS */
	for (count = 0; count <= 3; count++) {
		reg[count] = chan++;
		data[count] = 0x2A;
	}
	reg[count] =  chan++; /* Dummy */
	data[count] = 0;

	err = intel_scu_ipc_writev(reg, data, 5);
	if (err)
		goto ipc_error;

	msleep(WAIT_ADC_COMPLETION);

	/* Set Y BIAS */
	for (count = 0; count <= 3; count++) {
		reg[count] = chan++;
		data[count] = 0x4A;
	}
	reg[count] = chan++; /* Dummy */
	data[count] = 0;

	err = intel_scu_ipc_writev(reg, data, 5);
	if (err)
		goto ipc_error;

	msleep(WAIT_ADC_COMPLETION);

	/* Set Z BIAS */
	err = intel_scu_ipc_iowrite32(chan + 2, 0x8A8A8A8A);
	if (err)
		goto ipc_error;

	msleep(WAIT_ADC_COMPLETION);

	/*Read touch screen channels till pen removed
	 * Freescale reports constant value of z for all points
	 * z is high when screen is not touched and low when touched
	 * Map high z value to not touched and low z value to pen touched
	 */
	touched = mrstouch_pmic_fs_adc_read(tsdev);
	while (touched > TOUCH_PRESSURE) {
		touched = mrstouch_pmic_fs_adc_read(tsdev);
		msleep(WAIT_ADC_COMPLETION);
	}

	/* Clear all TS channels */
	chan = PMICADDR0 + tsdev->asr;
	for (count = 0; count <= 4; count++) {
		reg[count] = chan++;
		data[count] = 0;
	}
	err = intel_scu_ipc_writev(reg, data, 5);
	if (err)
		goto ipc_error;

	for (count = 0; count <= 4; count++) {
		reg[count] = chan++;
		data[count] = 0;
	}
	err = intel_scu_ipc_writev(reg, data, 5);
	if (err)
		goto ipc_error;

	err = intel_scu_ipc_iowrite32(chan + 2, 0x00000000);
	if (err)
		goto ipc_error;

	return 0;

ipc_error:
	dev_err(&tsdev->spi->dev, "ipc error during pendet\n");
	return err;
}


/* To enable X, Y and Z bias values
 * Enables YPYM for X channels and XPXM for Y channels
 */
static int mrstouch_ts_bias_set(uint offset, uint bias)
{
	int count;
	u16 chan, start;
	u16 reg[4];
	u8 data[4];

	chan = PMICADDR0 + offset;
	start = MRST_TS_CHAN10;

	for (count = 0; count <= 3; count++) {
		reg[count] = chan++;
		data[count] = bias | (start + count);
	}
	return intel_scu_ipc_writev(reg, data, 4);
}

/* To read touch screen channel values */
static int mrstouch_adc_read(struct mrstouch_dev *tsdev)
{
	int err;
	u16 xp, xm, yp, ym, zp, zm;

	/* configure Y bias for X channels */
	err = mrstouch_ts_bias_set(tsdev->asr, YBIAS);
	if (err)
		goto ipc_error;

	msleep(WAIT_ADC_COMPLETION);

	/* read x+ and x- channels */
	err = mrstouch_ts_chan_read(tsdev->asr, MRST_TS_CHAN10, &xp, &xm);
	if (err)
		goto ipc_error;

	/* configure x bias for y channels */
	err = mrstouch_ts_bias_set(tsdev->asr, XBIAS);
	if (err)
		goto ipc_error;

	msleep(WAIT_ADC_COMPLETION);

	/* read y+ and y- channels */
	err = mrstouch_ts_chan_read(tsdev->asr, MRST_TS_CHAN12, &yp, &ym);
	if (err)
		goto ipc_error;

	/* configure z bias for x and y channels */
	err = mrstouch_ts_bias_set(tsdev->asr, ZBIAS);
	if (err)
		goto ipc_error;

	msleep(WAIT_ADC_COMPLETION);

	/* read z+ and z- channels */
	err = mrstouch_ts_chan_read(tsdev->asr, MRST_TS_CHAN10, &zp, &zm);
	if (err)
		goto ipc_error;

#if defined(MRSTOUCH_PRINT_XYZP)
	printk(KERN_INFO "X+: %d, Y+: %d, Z+: %d\n", xp, yp, zp);
#endif

#if defined(MRSTOUCH_PRINT_XYZM)
	printk(KERN_INFO "X-: %d, Y-: %d, Z-: %d\n", xm, ym, zm);
#endif

	mrstouch_report_xy(tsdev, xp, yp, zp); /* report x and y to eventX */

	return zp;

ipc_error:
	dev_err(&tsdev->spi->dev, "ipc error during adc read\n");
	return err;
}

/* PENDET interrupt handler function for NEC and MAXIM */
static void pmic12_pendet(void *data)
{
	unsigned int touched;
	struct mrstouch_dev *tsdev = (struct mrstouch_dev *)data;

	/* read touch screen channels till pen removed */
	do {
		touched = mrstouch_adc_read(tsdev);
	} while (touched > TOUCH_PRESSURE);
}

/* Handler to process PENDET interrupt */
int mrstouch_pendet(void *data)
{
	struct mrstouch_dev *tsdev = (struct mrstouch_dev *)data;
	while (1) {
		/* Wait for PENDET interrupt */
		if (mutex_lock_interruptible(&tsdev->lock)) {
			msleep(WAIT_ADC_COMPLETION);
			continue;
		}

		if (tsdev->busy)
			return 0;

		tsdev->busy = true;

		if (tsdev->vendor == PMIC_VENDOR_NEC ||
			tsdev->vendor == PMIC_VENDOR_MAXIM) {
			/* PENDET must be disabled in NEC before reading ADC */
			pendet_enable(tsdev,false); /* Disbale PENDET */
			pmic12_pendet(tsdev);
			pendet_enable(tsdev, true); /*Enable PENDET */
		} else if (tsdev->vendor == PMIC_VENDOR_FS) {
			pendet_umask(); /* Stop ADC */
			pmic0_pendet(tsdev);
			pendet_mask(); /* Stop ADC */
		} else
		dev_err(&tsdev->spi->dev, "Unsupported touchscreen: %d\n",
				tsdev->vendor);

		tsdev->busy = false;

	}
	return 0;
}

/* PENDET interrupt handler */
static irqreturn_t pendet_intr_handler(int irq, void *handle)
{
	struct mrstouch_dev *tsdev = (struct mrstouch_dev *)handle;

	mutex_unlock(&tsdev->lock);
	return IRQ_HANDLED;
}

/* Intializes input device and registers with input subsystem */
static int ts_input_dev_init(struct mrstouch_dev *tsdev, struct spi_device *spi)
{
	int err = 0;

	mrstouch_debug("%s", __func__);

	tsdev->input = input_allocate_device();
	if (!tsdev->input) {
		dev_err(&tsdev->spi->dev, "Unable to allocate input device.\n");
		return -EINVAL;
	}

	tsdev->input->name = "mrst_touchscreen";
	snprintf(tsdev->phys, sizeof(tsdev->phys),
			"%s/input0", dev_name(&spi->dev));
	tsdev->input->phys = tsdev->phys;
	tsdev->input->dev.parent = &spi->dev;

	tsdev->input->id.vendor = tsdev->vendor;
	tsdev->input->id.version = tsdev->rev;

	tsdev->input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	tsdev->input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(tsdev->input, ABS_X, MIN_X, MIN_Y, 0, 0);
	input_set_abs_params(tsdev->input, ABS_Y, MIN_X, MIN_Y, 0, 0);

	err = input_register_device(tsdev->input);
	if (err) {
		dev_err(&tsdev->spi->dev, "unable to register input device\n");
		input_free_device(tsdev->input);
		return err;
	}

	mrstouch_debug("%s", "mrstouch initialized");

	return 0;

}

/* Probe function for touch screen driver */
static int __devinit mrstouch_probe(struct spi_device *mrstouch_spi)
{
	int err;
	unsigned int myirq;
	struct mrstouch_dev *tsdev;

	mrstouch_debug("%s(%p)", __func__, mrstouch_spi);

	mrstouchdevp = NULL;
	myirq = mrstouch_spi->irq;

	if (!mrstouch_spi->irq) {
		dev_err(&mrstouch_spi->dev, "no interrupt assigned\n");
		return -EINVAL;
	}

	tsdev = kzalloc(sizeof(struct mrstouch_dev), GFP_KERNEL);
	if (!tsdev) {
		dev_err(&mrstouch_spi->dev, "unable to allocate memory\n");
		return -ENOMEM;
	}

	tsdev->irq = myirq;
	mrstouchdevp = tsdev;

	err = mrstouch_adc_init(tsdev);
	if (err) {
		dev_err(&mrstouch_spi->dev, "ADC init failed\n");
		goto mrstouch_err_free_mem;
	}

	dev_set_drvdata(&mrstouch_spi->dev, tsdev);
	tsdev->spi = mrstouch_spi;

	err = ts_input_dev_init(tsdev, mrstouch_spi);
	if (err) {
		dev_err(&tsdev->spi->dev, "ts_input_dev_init failed");
		goto mrstouch_err_free_mem;
	}

	mutex_init(&tsdev->lock);
	mutex_lock(&tsdev->lock)

	mrstouch_debug("Requesting IRQ-%d", myirq);
	err = request_irq(myirq, pendet_intr_handler,
				0, "mrstouch", tsdev);
	if (err) {
		dev_err(&tsdev->spi->dev, "unable to allocate irq\n");
		goto mrstouch_err_free_mem;
	}

	tsdev->pendet_thrd = kthread_run(mrstouch_pendet,
				(void *)tsdev, "pendet handler");
	if (IS_ERR(tsdev->pendet_thrd)) {
		dev_err(&tsdev->spi->dev, "kthread_run failed\n");
		err = PTR_ERR(tsdev->pendet_thrd);
		goto mrstouch_err_free_mem;
	}
	mrstouch_debug("%s", "Driver initialized");
	return 0;

mrstouch_err_free_mem:
	kfree(tsdev);
	return err;
}

static int mrstouch_suspend(struct spi_device *spi, pm_message_t msg)
{
	mrstouch_debug("%s", __func__);
	mrstouchdevp->suspended = 1;
	return 0;
}

static int mrstouch_resume(struct spi_device *spi)
{
	mrstouch_debug("%s", __func__);
	mrstouchdevp->suspended = 0;
	return 0;
}

static int mrstouch_remove(struct spi_device *spi)
{
	mrstouch_debug("%s", __func__);
	free_irq(mrstouchdevp->irq, mrstouchdevp);
	input_unregister_device(mrstouchdevp->input);
	input_free_device(mrstouchdevp->input);
	if (mrstouchdevp->pendet_thrd)
		kthread_stop(mrstouchdevp->pendet_thrd);
	kfree(mrstouchdevp);
	return 0;
}

static struct spi_driver mrstouch_driver = {
	.driver = {
		.name   = "pmic_touch",
		.bus    = &spi_bus_type,
		.owner  = THIS_MODULE,
	},
	.probe          = mrstouch_probe,
	.suspend        = mrstouch_suspend,
	.resume         = mrstouch_resume,
	.remove         = mrstouch_remove,
};

static int __init mrstouch_module_init(void)
{
	int err;

	mrstouch_debug("%s", __func__);
	err = spi_register_driver(&mrstouch_driver);
	if (err) {
		mrstouch_debug("%s(%d)", "SPI PENDET failed", err);
		return -1;
	}

	return 0;
}

static void __exit mrstouch_module_exit(void)
{
	mrstouch_debug("%s", __func__);
	spi_unregister_driver(&mrstouch_driver);
	return;
}

module_init(mrstouch_module_init);
module_exit(mrstouch_module_exit);

MODULE_AUTHOR("Sreedhara Murthy. D.S, sreedhara.ds@intel.com");
MODULE_DESCRIPTION("Intel Moorestown Resistive Touch Screen Driver");
MODULE_LICENSE("GPL");
