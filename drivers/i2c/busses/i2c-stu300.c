/*
 * Copyright (C) 2007-2012 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * ST DDC I2C master mode driver, used in e.g. U300 series platforms.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>

/* the name of this kernel module */
#define NAME "stu300"

/* CR (Control Register) 8bit (R/W) */
#define I2C_CR					(0x00000000)
#define I2C_CR_RESET_VALUE			(0x00)
#define I2C_CR_RESET_UMASK			(0x00)
#define I2C_CR_DDC1_ENABLE			(0x80)
#define I2C_CR_TRANS_ENABLE			(0x40)
#define I2C_CR_PERIPHERAL_ENABLE		(0x20)
#define I2C_CR_DDC2B_ENABLE			(0x10)
#define I2C_CR_START_ENABLE			(0x08)
#define I2C_CR_ACK_ENABLE			(0x04)
#define I2C_CR_STOP_ENABLE			(0x02)
#define I2C_CR_INTERRUPT_ENABLE			(0x01)
/* SR1 (Status Register 1) 8bit (R/-) */
#define I2C_SR1					(0x00000004)
#define I2C_SR1_RESET_VALUE			(0x00)
#define I2C_SR1_RESET_UMASK			(0x00)
#define I2C_SR1_EVF_IND				(0x80)
#define I2C_SR1_ADD10_IND			(0x40)
#define I2C_SR1_TRA_IND				(0x20)
#define I2C_SR1_BUSY_IND			(0x10)
#define I2C_SR1_BTF_IND				(0x08)
#define I2C_SR1_ADSL_IND			(0x04)
#define I2C_SR1_MSL_IND				(0x02)
#define I2C_SR1_SB_IND				(0x01)
/* SR2 (Status Register 2) 8bit (R/-) */
#define I2C_SR2					(0x00000008)
#define I2C_SR2_RESET_VALUE			(0x00)
#define I2C_SR2_RESET_UMASK			(0x40)
#define I2C_SR2_MASK				(0xBF)
#define I2C_SR2_SCLFAL_IND			(0x80)
#define I2C_SR2_ENDAD_IND			(0x20)
#define I2C_SR2_AF_IND				(0x10)
#define I2C_SR2_STOPF_IND			(0x08)
#define I2C_SR2_ARLO_IND			(0x04)
#define I2C_SR2_BERR_IND			(0x02)
#define I2C_SR2_DDC2BF_IND			(0x01)
/* CCR (Clock Control Register) 8bit (R/W) */
#define I2C_CCR					(0x0000000C)
#define I2C_CCR_RESET_VALUE			(0x00)
#define I2C_CCR_RESET_UMASK			(0x00)
#define I2C_CCR_MASK				(0xFF)
#define I2C_CCR_FMSM				(0x80)
#define I2C_CCR_CC_MASK				(0x7F)
/* OAR1 (Own Address Register 1) 8bit (R/W) */
#define I2C_OAR1				(0x00000010)
#define I2C_OAR1_RESET_VALUE			(0x00)
#define I2C_OAR1_RESET_UMASK			(0x00)
#define I2C_OAR1_ADD_MASK			(0xFF)
/* OAR2 (Own Address Register 2) 8bit (R/W) */
#define I2C_OAR2				(0x00000014)
#define I2C_OAR2_RESET_VALUE			(0x40)
#define I2C_OAR2_RESET_UMASK			(0x19)
#define I2C_OAR2_MASK				(0xE6)
#define I2C_OAR2_FR_25_10MHZ			(0x00)
#define I2C_OAR2_FR_10_1667MHZ			(0x20)
#define I2C_OAR2_FR_1667_2667MHZ		(0x40)
#define I2C_OAR2_FR_2667_40MHZ			(0x60)
#define I2C_OAR2_FR_40_5333MHZ			(0x80)
#define I2C_OAR2_FR_5333_66MHZ			(0xA0)
#define I2C_OAR2_FR_66_80MHZ			(0xC0)
#define I2C_OAR2_FR_80_100MHZ			(0xE0)
#define I2C_OAR2_FR_MASK			(0xE0)
#define I2C_OAR2_ADD_MASK			(0x06)
/* DR (Data Register) 8bit (R/W) */
#define I2C_DR					(0x00000018)
#define I2C_DR_RESET_VALUE			(0x00)
#define I2C_DR_RESET_UMASK			(0xFF)
#define I2C_DR_D_MASK				(0xFF)
/* ECCR (Extended Clock Control Register) 8bit (R/W) */
#define I2C_ECCR				(0x0000001C)
#define I2C_ECCR_RESET_VALUE			(0x00)
#define I2C_ECCR_RESET_UMASK			(0xE0)
#define I2C_ECCR_MASK				(0x1F)
#define I2C_ECCR_CC_MASK			(0x1F)

/*
 * These events are more or less responses to commands
 * sent into the hardware, presumably reflecting the state
 * of an internal state machine.
 */
enum stu300_event {
	STU300_EVENT_NONE = 0,
	STU300_EVENT_1,
	STU300_EVENT_2,
	STU300_EVENT_3,
	STU300_EVENT_4,
	STU300_EVENT_5,
	STU300_EVENT_6,
	STU300_EVENT_7,
	STU300_EVENT_8,
	STU300_EVENT_9
};

enum stu300_error {
	STU300_ERROR_NONE = 0,
	STU300_ERROR_ACKNOWLEDGE_FAILURE,
	STU300_ERROR_BUS_ERROR,
	STU300_ERROR_ARBITRATION_LOST,
	STU300_ERROR_UNKNOWN
};

/* timeout waiting for the controller to respond */
#define STU300_TIMEOUT (msecs_to_jiffies(1000))

/*
 * The number of address send athemps tried before giving up.
 * If the first one failes it seems like 5 to 8 attempts are required.
 */
#define NUM_ADDR_RESEND_ATTEMPTS 12

/* I2C clock speed, in Hz 0-400kHz*/
static unsigned int scl_frequency = 100000;
module_param(scl_frequency, uint,  0644);

/**
 * struct stu300_dev - the stu300 driver state holder
 * @pdev: parent platform device
 * @adapter: corresponding I2C adapter
 * @clk: hardware block clock
 * @irq: assigned interrupt line
 * @cmd_issue_lock: this locks the following cmd_ variables
 * @cmd_complete: acknowledge completion for an I2C command
 * @cmd_event: expected event coming in as a response to a command
 * @cmd_err: error code as response to a command
 * @speed: current bus speed in Hz
 * @msg_index: index of current message
 * @msg_len: length of current message
 */

struct stu300_dev {
	struct platform_device	*pdev;
	struct i2c_adapter	adapter;
	void __iomem		*virtbase;
	struct clk		*clk;
	int			irq;
	spinlock_t		cmd_issue_lock;
	struct completion	cmd_complete;
	enum stu300_event	cmd_event;
	enum stu300_error	cmd_err;
	unsigned int		speed;
	int			msg_index;
	int			msg_len;
};

/* Local forward function declarations */
static int stu300_init_hw(struct stu300_dev *dev);

/*
 * The block needs writes in both MSW and LSW in order
 * for all data lines to reach their destination.
 */
static inline void stu300_wr8(u32 value, void __iomem *address)
{
	writel((value << 16) | value, address);
}

/*
 * This merely masks off the duplicates which appear
 * in bytes 1-3. You _MUST_ use 32-bit bus access on this
 * device, else it will not work.
 */
static inline u32 stu300_r8(void __iomem *address)
{
	return readl(address) & 0x000000FFU;
}

static void stu300_irq_enable(struct stu300_dev *dev)
{
	u32 val;
	val = stu300_r8(dev->virtbase + I2C_CR);
	val |= I2C_CR_INTERRUPT_ENABLE;
	/* Twice paranoia (possible HW glitch) */
	stu300_wr8(val, dev->virtbase + I2C_CR);
	stu300_wr8(val, dev->virtbase + I2C_CR);
}

static void stu300_irq_disable(struct stu300_dev *dev)
{
	u32 val;
	val = stu300_r8(dev->virtbase + I2C_CR);
	val &= ~I2C_CR_INTERRUPT_ENABLE;
	/* Twice paranoia (possible HW glitch) */
	stu300_wr8(val, dev->virtbase + I2C_CR);
	stu300_wr8(val, dev->virtbase + I2C_CR);
}


/*
 * Tells whether a certain event or events occurred in
 * response to a command. The events represent states in
 * the internal state machine of the hardware. The events
 * are not very well described in the hardware
 * documentation and can only be treated as abstract state
 * machine states.
 *
 * @ret 0 = event has not occurred or unknown error, any
 * other value means the correct event occurred or an error.
 */

static int stu300_event_occurred(struct stu300_dev *dev,
				   enum stu300_event mr_event) {
	u32 status1;
	u32 status2;

	/* What event happened? */
	status1 = stu300_r8(dev->virtbase + I2C_SR1);

	if (!(status1 & I2C_SR1_EVF_IND))
		/* No event at all */
		return 0;

	status2 = stu300_r8(dev->virtbase + I2C_SR2);

	/* Block any multiple interrupts */
	stu300_irq_disable(dev);

	/* Check for errors first */
	if (status2 & I2C_SR2_AF_IND) {
		dev->cmd_err = STU300_ERROR_ACKNOWLEDGE_FAILURE;
		return 1;
	} else if (status2 & I2C_SR2_BERR_IND) {
		dev->cmd_err = STU300_ERROR_BUS_ERROR;
		return 1;
	} else if (status2 & I2C_SR2_ARLO_IND) {
		dev->cmd_err = STU300_ERROR_ARBITRATION_LOST;
		return 1;
	}

	switch (mr_event) {
	case STU300_EVENT_1:
		if (status1 & I2C_SR1_ADSL_IND)
			return 1;
		break;
	case STU300_EVENT_2:
	case STU300_EVENT_3:
	case STU300_EVENT_7:
	case STU300_EVENT_8:
		if (status1 & I2C_SR1_BTF_IND) {
			return 1;
		}
		break;
	case STU300_EVENT_4:
		if (status2 & I2C_SR2_STOPF_IND)
			return 1;
		break;
	case STU300_EVENT_5:
		if (status1 & I2C_SR1_SB_IND)
			/* Clear start bit */
			return 1;
		break;
	case STU300_EVENT_6:
		if (status2 & I2C_SR2_ENDAD_IND) {
			/* First check for any errors */
			return 1;
		}
		break;
	case STU300_EVENT_9:
		if (status1 & I2C_SR1_ADD10_IND)
			return 1;
		break;
	default:
		break;
	}
	/* If we get here, we're on thin ice.
	 * Here we are in a status where we have
	 * gotten a response that does not match
	 * what we requested.
	 */
	dev->cmd_err = STU300_ERROR_UNKNOWN;
	dev_err(&dev->pdev->dev,
		"Unhandled interrupt! %d sr1: 0x%x sr2: 0x%x\n",
		mr_event, status1, status2);
	return 0;
}

static irqreturn_t stu300_irh(int irq, void *data)
{
	struct stu300_dev *dev = data;
	int res;

	/* Just make sure that the block is clocked */
	clk_enable(dev->clk);

	/* See if this was what we were waiting for */
	spin_lock(&dev->cmd_issue_lock);

	res = stu300_event_occurred(dev, dev->cmd_event);
	if (res || dev->cmd_err != STU300_ERROR_NONE)
		complete(&dev->cmd_complete);

	spin_unlock(&dev->cmd_issue_lock);

	clk_disable(dev->clk);

	return IRQ_HANDLED;
}

/*
 * Sends a command and then waits for the bits masked by *flagmask*
 * to go high or low by IRQ awaiting.
 */
static int stu300_start_and_await_event(struct stu300_dev *dev,
					  u8 cr_value,
					  enum stu300_event mr_event)
{
	int ret;

	if (unlikely(irqs_disabled())) {
		/* TODO: implement polling for this case if need be. */
		WARN(1, "irqs are disabled, cannot poll for event\n");
		return -EIO;
	}

	/* Lock command issue, fill in an event we wait for */
	spin_lock_irq(&dev->cmd_issue_lock);
	init_completion(&dev->cmd_complete);
	dev->cmd_err = STU300_ERROR_NONE;
	dev->cmd_event = mr_event;
	spin_unlock_irq(&dev->cmd_issue_lock);

	/* Turn on interrupt, send command and wait. */
	cr_value |= I2C_CR_INTERRUPT_ENABLE;
	stu300_wr8(cr_value, dev->virtbase + I2C_CR);
	ret = wait_for_completion_interruptible_timeout(&dev->cmd_complete,
							STU300_TIMEOUT);
	if (ret < 0) {
		dev_err(&dev->pdev->dev,
		       "wait_for_completion_interruptible_timeout() "
		       "returned %d waiting for event %04x\n", ret, mr_event);
		return ret;
	}

	if (ret == 0) {
		dev_err(&dev->pdev->dev, "controller timed out "
		       "waiting for event %d, reinit hardware\n", mr_event);
		(void) stu300_init_hw(dev);
		return -ETIMEDOUT;
	}

	if (dev->cmd_err != STU300_ERROR_NONE) {
		dev_err(&dev->pdev->dev, "controller (start) "
		       "error %d waiting for event %d, reinit hardware\n",
		       dev->cmd_err, mr_event);
		(void) stu300_init_hw(dev);
		return -EIO;
	}

	return 0;
}

/*
 * This waits for a flag to be set, if it is not set on entry, an interrupt is
 * configured to wait for the flag using a completion.
 */
static int stu300_await_event(struct stu300_dev *dev,
				enum stu300_event mr_event)
{
	int ret;

	if (unlikely(irqs_disabled())) {
		/* TODO: implement polling for this case if need be. */
		dev_err(&dev->pdev->dev, "irqs are disabled on this "
			"system!\n");
		return -EIO;
	}

	/* Is it already here? */
	spin_lock_irq(&dev->cmd_issue_lock);
	dev->cmd_err = STU300_ERROR_NONE;
	dev->cmd_event = mr_event;

	init_completion(&dev->cmd_complete);

	/* Turn on the I2C interrupt for current operation */
	stu300_irq_enable(dev);

	/* Unlock the command block and wait for the event to occur */
	spin_unlock_irq(&dev->cmd_issue_lock);

	ret = wait_for_completion_interruptible_timeout(&dev->cmd_complete,
							STU300_TIMEOUT);
	if (ret < 0) {
		dev_err(&dev->pdev->dev,
		       "wait_for_completion_interruptible_timeout()"
		       "returned %d waiting for event %04x\n", ret, mr_event);
		return ret;
	}

	if (ret == 0) {
		if (mr_event != STU300_EVENT_6) {
			dev_err(&dev->pdev->dev, "controller "
				"timed out waiting for event %d, reinit "
				"hardware\n", mr_event);
			(void) stu300_init_hw(dev);
		}
		return -ETIMEDOUT;
	}

	if (dev->cmd_err != STU300_ERROR_NONE) {
		if (mr_event != STU300_EVENT_6) {
			dev_err(&dev->pdev->dev, "controller "
				"error (await_event) %d waiting for event %d, "
			       "reinit hardware\n", dev->cmd_err, mr_event);
			(void) stu300_init_hw(dev);
		}
		return -EIO;
	}

	return 0;
}

/*
 * Waits for the busy bit to go low by repeated polling.
 */
#define BUSY_RELEASE_ATTEMPTS 10
static int stu300_wait_while_busy(struct stu300_dev *dev)
{
	unsigned long timeout;
	int i;

	for (i = 0; i < BUSY_RELEASE_ATTEMPTS; i++) {
		timeout = jiffies + STU300_TIMEOUT;

		while (!time_after(jiffies, timeout)) {
			/* Is not busy? */
			if ((stu300_r8(dev->virtbase + I2C_SR1) &
			     I2C_SR1_BUSY_IND) == 0)
				return 0;
			msleep(1);
		}

		dev_err(&dev->pdev->dev, "transaction timed out "
			"waiting for device to be free (not busy). "
		       "Attempt: %d\n", i+1);

		dev_err(&dev->pdev->dev, "base address = "
			"0x%08x, reinit hardware\n", (u32) dev->virtbase);

		(void) stu300_init_hw(dev);
	}

	dev_err(&dev->pdev->dev, "giving up after %d attempts "
		"to reset the bus.\n",  BUSY_RELEASE_ATTEMPTS);

	return -ETIMEDOUT;
}

struct stu300_clkset {
	unsigned long rate;
	u32 setting;
};

static const struct stu300_clkset stu300_clktable[] = {
	{ 0,         0xFFU },
	{ 2500000,   I2C_OAR2_FR_25_10MHZ },
	{ 10000000,  I2C_OAR2_FR_10_1667MHZ },
	{ 16670000,  I2C_OAR2_FR_1667_2667MHZ },
	{ 26670000,  I2C_OAR2_FR_2667_40MHZ },
	{ 40000000,  I2C_OAR2_FR_40_5333MHZ },
	{ 53330000,  I2C_OAR2_FR_5333_66MHZ },
	{ 66000000,  I2C_OAR2_FR_66_80MHZ },
	{ 80000000,  I2C_OAR2_FR_80_100MHZ },
	{ 100000000, 0xFFU },
};


static int stu300_set_clk(struct stu300_dev *dev, unsigned long clkrate)
{

	u32 val;
	int i = 0;

	/* Locate the appropriate clock setting */
	while (i < ARRAY_SIZE(stu300_clktable) - 1 &&
	       stu300_clktable[i].rate < clkrate)
		i++;

	if (stu300_clktable[i].setting == 0xFFU) {
		dev_err(&dev->pdev->dev, "too %s clock rate requested "
			"(%lu Hz).\n", i ? "high" : "low", clkrate);
		return -EINVAL;
	}

	stu300_wr8(stu300_clktable[i].setting,
		   dev->virtbase + I2C_OAR2);

	dev_dbg(&dev->pdev->dev, "Clock rate %lu Hz, I2C bus speed %d Hz "
		"virtbase %p\n", clkrate, dev->speed, dev->virtbase);

	if (dev->speed > 100000)
		/* Fast Mode I2C */
		val = ((clkrate/dev->speed) - 9)/3 + 1;
	else
		/* Standard Mode I2C */
		val = ((clkrate/dev->speed) - 7)/2 + 1;

	/* According to spec the divider must be > 2 */
	if (val < 0x002) {
		dev_err(&dev->pdev->dev, "too low clock rate (%lu Hz).\n",
			clkrate);
		return -EINVAL;
	}

	/* We have 12 bits clock divider only! */
	if (val & 0xFFFFF000U) {
		dev_err(&dev->pdev->dev, "too high clock rate (%lu Hz).\n",
			clkrate);
		return -EINVAL;
	}

	if (dev->speed > 100000) {
		/* CC6..CC0 */
		stu300_wr8((val & I2C_CCR_CC_MASK) | I2C_CCR_FMSM,
			   dev->virtbase + I2C_CCR);
		dev_dbg(&dev->pdev->dev, "set clock divider to 0x%08x, "
			"Fast Mode I2C\n", val);
	} else {
		/* CC6..CC0 */
		stu300_wr8((val & I2C_CCR_CC_MASK),
			   dev->virtbase + I2C_CCR);
		dev_dbg(&dev->pdev->dev, "set clock divider to "
			"0x%08x, Standard Mode I2C\n", val);
	}

	/* CC11..CC7 */
	stu300_wr8(((val >> 7) & 0x1F),
		   dev->virtbase + I2C_ECCR);

	return 0;
}


static int stu300_init_hw(struct stu300_dev *dev)
{
	u32 dummy;
	unsigned long clkrate;
	int ret;

	/* Disable controller */
	stu300_wr8(0x00, dev->virtbase + I2C_CR);
	/*
	 * Set own address to some default value (0x00).
	 * We do not support slave mode anyway.
	 */
	stu300_wr8(0x00, dev->virtbase + I2C_OAR1);
	/*
	 * The I2C controller only operates properly in 26 MHz but we
	 * program this driver as if we didn't know. This will also set the two
	 * high bits of the own address to zero as well.
	 * There is no known hardware issue with running in 13 MHz
	 * However, speeds over 200 kHz are not used.
	 */
	clkrate = clk_get_rate(dev->clk);
	ret = stu300_set_clk(dev, clkrate);

	if (ret)
		return ret;
	/*
	 * Enable block, do it TWICE (hardware glitch)
	 * Setting bit 7 can enable DDC mode. (Not used currently.)
	 */
	stu300_wr8(I2C_CR_PERIPHERAL_ENABLE,
				  dev->virtbase + I2C_CR);
	stu300_wr8(I2C_CR_PERIPHERAL_ENABLE,
				  dev->virtbase + I2C_CR);
	/* Make a dummy read of the status register SR1 & SR2 */
	dummy = stu300_r8(dev->virtbase + I2C_SR2);
	dummy = stu300_r8(dev->virtbase + I2C_SR1);

	return 0;
}



/* Send slave address. */
static int stu300_send_address(struct stu300_dev *dev,
				 struct i2c_msg *msg, int resend)
{
	u32 val;
	int ret;

	if (msg->flags & I2C_M_TEN)
		/* This is probably how 10 bit addresses look */
		val = (0xf0 | (((u32) msg->addr & 0x300) >> 7)) &
			I2C_DR_D_MASK;
	else
		val = ((msg->addr << 1) & I2C_DR_D_MASK);

	if (msg->flags & I2C_M_RD) {
		/* This is the direction bit */
		val |= 0x01;
		if (resend)
			dev_dbg(&dev->pdev->dev, "read resend\n");
	} else if (resend)
		dev_dbg(&dev->pdev->dev, "write resend\n");
	stu300_wr8(val, dev->virtbase + I2C_DR);

	/* For 10bit addressing, await 10bit request (EVENT 9) */
	if (msg->flags & I2C_M_TEN) {
		ret = stu300_await_event(dev, STU300_EVENT_9);
		/*
		 * The slave device wants a 10bit address, send the rest
		 * of the bits (the LSBits)
		 */
		val = msg->addr & I2C_DR_D_MASK;
		/* This clears "event 9" */
		stu300_wr8(val, dev->virtbase + I2C_DR);
		if (ret != 0)
			return ret;
	}
	/* FIXME: Why no else here? two events for 10bit?
	 * Await event 6 (normal) or event 9 (10bit)
	 */

	if (resend)
		dev_dbg(&dev->pdev->dev, "await event 6\n");
	ret = stu300_await_event(dev, STU300_EVENT_6);

	/*
	 * Clear any pending EVENT 6 no matter what happened during
	 * await_event.
	 */
	val = stu300_r8(dev->virtbase + I2C_CR);
	val |= I2C_CR_PERIPHERAL_ENABLE;
	stu300_wr8(val, dev->virtbase + I2C_CR);

	return ret;
}

static int stu300_xfer_msg(struct i2c_adapter *adap,
			     struct i2c_msg *msg, int stop)
{
	u32 cr;
	u32 val;
	u32 i;
	int ret;
	int attempts = 0;
	struct stu300_dev *dev = i2c_get_adapdata(adap);

	clk_enable(dev->clk);

	/* Remove this if (0) to trace each and every message. */
	if (0) {
		dev_dbg(&dev->pdev->dev, "I2C message to: 0x%04x, len: %d, "
			"flags: 0x%04x, stop: %d\n",
			msg->addr, msg->len, msg->flags, stop);
	}

	/* Zero-length messages are not supported by this hardware */
	if (msg->len == 0) {
		ret = -EINVAL;
		goto exit_disable;
	}

	/*
	 * For some reason, sending the address sometimes fails when running
	 * on  the 13 MHz clock. No interrupt arrives. This is a work around,
	 * which tries to restart and send the address up to 10 times before
	 * really giving up. Usually 5 to 8 attempts are enough.
	 */
	do {
		if (attempts)
			dev_dbg(&dev->pdev->dev, "wait while busy\n");
		/* Check that the bus is free, or wait until some timeout */
		ret = stu300_wait_while_busy(dev);
		if (ret != 0)
			goto exit_disable;

		if (attempts)
			dev_dbg(&dev->pdev->dev, "re-int hw\n");
		/*
		 * According to ST, there is no problem if the clock is
		 * changed between 13 and 26 MHz during a transfer.
		 */
		ret = stu300_init_hw(dev);
		if (ret)
			goto exit_disable;

		/* Send a start condition */
		cr = I2C_CR_PERIPHERAL_ENABLE;
		/* Setting the START bit puts the block in master mode */
		if (!(msg->flags & I2C_M_NOSTART))
			cr |= I2C_CR_START_ENABLE;
		if ((msg->flags & I2C_M_RD) && (msg->len > 1))
			/* On read more than 1 byte, we need ack. */
			cr |= I2C_CR_ACK_ENABLE;
		/* Check that it gets through */
		if (!(msg->flags & I2C_M_NOSTART)) {
			if (attempts)
				dev_dbg(&dev->pdev->dev, "send start event\n");
			ret = stu300_start_and_await_event(dev, cr,
							     STU300_EVENT_5);
		}

		if (attempts)
			dev_dbg(&dev->pdev->dev, "send address\n");

		if (ret == 0)
			/* Send address */
			ret = stu300_send_address(dev, msg, attempts != 0);

		if (ret != 0) {
			attempts++;
			dev_dbg(&dev->pdev->dev, "failed sending address, "
				"retrying. Attempt: %d msg_index: %d/%d\n",
			       attempts, dev->msg_index, dev->msg_len);
		}

	} while (ret != 0 && attempts < NUM_ADDR_RESEND_ATTEMPTS);

	if (attempts < NUM_ADDR_RESEND_ATTEMPTS && attempts > 0) {
		dev_dbg(&dev->pdev->dev, "managed to get address "
			"through after %d attempts\n", attempts);
	} else if (attempts == NUM_ADDR_RESEND_ATTEMPTS) {
		dev_dbg(&dev->pdev->dev, "I give up, tried %d times "
			"to resend address.\n",
			NUM_ADDR_RESEND_ATTEMPTS);
		goto exit_disable;
	}


	if (msg->flags & I2C_M_RD) {
		/* READ: we read the actual bytes one at a time */
		for (i = 0; i < msg->len; i++) {
			if (i == msg->len-1) {
				/*
				 * Disable ACK and set STOP condition before
				 * reading last byte
				 */
				val = I2C_CR_PERIPHERAL_ENABLE;

				if (stop)
					val |= I2C_CR_STOP_ENABLE;

				stu300_wr8(val,
					   dev->virtbase + I2C_CR);
			}
			/* Wait for this byte... */
			ret = stu300_await_event(dev, STU300_EVENT_7);
			if (ret != 0)
				goto exit_disable;
			/* This clears event 7 */
			msg->buf[i] = (u8) stu300_r8(dev->virtbase + I2C_DR);
		}
	} else {
		/* WRITE: we send the actual bytes one at a time */
		for (i = 0; i < msg->len; i++) {
			/* Write the byte */
			stu300_wr8(msg->buf[i],
				   dev->virtbase + I2C_DR);
			/* Check status */
			ret = stu300_await_event(dev, STU300_EVENT_8);
			/* Next write to DR will clear event 8 */
			if (ret != 0) {
				dev_err(&dev->pdev->dev, "error awaiting "
				       "event 8 (%d)\n", ret);
				goto exit_disable;
			}
		}
		/* Check NAK */
		if (!(msg->flags & I2C_M_IGNORE_NAK)) {
			if (stu300_r8(dev->virtbase + I2C_SR2) &
			    I2C_SR2_AF_IND) {
				dev_err(&dev->pdev->dev, "I2C payload "
				       "send returned NAK!\n");
				ret = -EIO;
				goto exit_disable;
			}
		}
		if (stop) {
			/* Send stop condition */
			val = I2C_CR_PERIPHERAL_ENABLE;
			val |= I2C_CR_STOP_ENABLE;
			stu300_wr8(val, dev->virtbase + I2C_CR);
		}
	}

	/* Check that the bus is free, or wait until some timeout occurs */
	ret = stu300_wait_while_busy(dev);
	if (ret != 0) {
		dev_err(&dev->pdev->dev, "timout waiting for transfer "
		       "to commence.\n");
		goto exit_disable;
	}

	/* Dummy read status registers */
	val = stu300_r8(dev->virtbase + I2C_SR2);
	val = stu300_r8(dev->virtbase + I2C_SR1);
	ret = 0;

 exit_disable:
	/* Disable controller */
	stu300_wr8(0x00, dev->virtbase + I2C_CR);
	clk_disable(dev->clk);
	return ret;
}

static int stu300_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			 int num)
{
	int ret = -1;
	int i;

	struct stu300_dev *dev = i2c_get_adapdata(adap);
	dev->msg_len = num;

	for (i = 0; i < num; i++) {
		/*
		 * Another driver appears to send stop for each message,
		 * here we only do that for the last message. Possibly some
		 * peripherals require this behaviour, then their drivers
		 * have to send single messages in order to get "stop" for
		 * each message.
		 */
		dev->msg_index = i;

		ret = stu300_xfer_msg(adap, &msgs[i], (i == (num - 1)));

		if (ret != 0) {
			num = ret;
			break;
		}
	}

	return num;
}

static u32 stu300_func(struct i2c_adapter *adap)
{
	/* This is the simplest thing you can think of... */
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR;
}

static const struct i2c_algorithm stu300_algo = {
	.master_xfer	= stu300_xfer,
	.functionality	= stu300_func,
};

static int stu300_probe(struct platform_device *pdev)
{
	struct stu300_dev *dev;
	struct i2c_adapter *adap;
	struct resource *res;
	int bus_nr;
	int ret = 0;

	dev = devm_kzalloc(&pdev->dev, sizeof(struct stu300_dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev, "could not allocate device struct\n");
		return -ENOMEM;
	}

	bus_nr = pdev->id;
	dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk)) {
		dev_err(&pdev->dev, "could not retrieve i2c bus clock\n");
		return PTR_ERR(dev->clk);
	}

	dev->pdev = pdev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->virtbase = devm_ioremap_resource(&pdev->dev, res);
	dev_dbg(&pdev->dev, "initialize bus device I2C%d on virtual "
		"base %p\n", bus_nr, dev->virtbase);
	if (IS_ERR(dev->virtbase))
		return PTR_ERR(dev->virtbase);

	dev->irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, dev->irq, stu300_irh, 0, NAME, dev);
	if (ret < 0)
		return ret;

	dev->speed = scl_frequency;

	clk_prepare_enable(dev->clk);
	ret = stu300_init_hw(dev);
	clk_disable(dev->clk);
	if (ret != 0) {
		dev_err(&dev->pdev->dev, "error initializing hardware.\n");
		return -EIO;
	}

	/* IRQ event handling initialization */
	spin_lock_init(&dev->cmd_issue_lock);
	dev->cmd_event = STU300_EVENT_NONE;
	dev->cmd_err = STU300_ERROR_NONE;

	adap = &dev->adapter;
	adap->owner = THIS_MODULE;
	/* DDC class but actually often used for more generic I2C */
	adap->class = I2C_CLASS_DDC;
	strlcpy(adap->name, "ST Microelectronics DDC I2C adapter",
		sizeof(adap->name));
	adap->nr = bus_nr;
	adap->algo = &stu300_algo;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;
	i2c_set_adapdata(adap, dev);

	/* i2c device drivers may be active on return from add_adapter() */
	ret = i2c_add_numbered_adapter(adap);
	if (ret) {
		dev_err(&pdev->dev, "failure adding ST Micro DDC "
		       "I2C adapter\n");
		return ret;
	}

	platform_set_drvdata(pdev, dev);
	dev_info(&pdev->dev, "ST DDC I2C @ %p, irq %d\n",
		 dev->virtbase, dev->irq);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int stu300_suspend(struct device *device)
{
	struct stu300_dev *dev = dev_get_drvdata(device);

	/* Turn off everything */
	stu300_wr8(0x00, dev->virtbase + I2C_CR);
	return 0;
}

static int stu300_resume(struct device *device)
{
	int ret = 0;
	struct stu300_dev *dev = dev_get_drvdata(device);

	clk_enable(dev->clk);
	ret = stu300_init_hw(dev);
	clk_disable(dev->clk);

	if (ret != 0)
		dev_err(device, "error re-initializing hardware.\n");
	return ret;
}

static SIMPLE_DEV_PM_OPS(stu300_pm, stu300_suspend, stu300_resume);
#define STU300_I2C_PM	(&stu300_pm)
#else
#define STU300_I2C_PM	NULL
#endif

static int stu300_remove(struct platform_device *pdev)
{
	struct stu300_dev *dev = platform_get_drvdata(pdev);

	i2c_del_adapter(&dev->adapter);
	/* Turn off everything */
	stu300_wr8(0x00, dev->virtbase + I2C_CR);
	return 0;
}

static const struct of_device_id stu300_dt_match[] = {
	{ .compatible = "st,ddci2c" },
	{},
};

static struct platform_driver stu300_i2c_driver = {
	.driver = {
		.name	= NAME,
		.owner	= THIS_MODULE,
		.pm	= STU300_I2C_PM,
		.of_match_table = stu300_dt_match,
	},
	.probe = stu300_probe,
	.remove = stu300_remove,

};

static int __init stu300_init(void)
{
	return platform_driver_register(&stu300_i2c_driver);
}

static void __exit stu300_exit(void)
{
	platform_driver_unregister(&stu300_i2c_driver);
}

/*
 * The systems using this bus often have very basic devices such
 * as regulators on the I2C bus, so this needs to be loaded early.
 * Therefore it is registered in the subsys_initcall().
 */
subsys_initcall(stu300_init);
module_exit(stu300_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@stericsson.com>");
MODULE_DESCRIPTION("ST Micro DDC I2C adapter (" NAME ")");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" NAME);
