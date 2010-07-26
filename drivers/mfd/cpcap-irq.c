/*
 * Copyright (C) 2009, Motorola, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>

#include <linux/spi/cpcap.h>
#include <linux/spi/spi.h>
#include <linux/debugfs.h>

#define NUM_INT_REGS      5
#define NUM_INTS_PER_REG  16

#define CPCAP_INT1_VALID_BITS 0xFFFB
#define CPCAP_INT2_VALID_BITS 0xFFFF
#define CPCAP_INT3_VALID_BITS 0xFFFF
#define CPCAP_INT4_VALID_BITS 0x03FF
#define CPCAP_INT5_VALID_BITS 0xFFFF

struct cpcap_event_handler {
	void (*func)(enum cpcap_irqs, void *);
	void *data;
};

struct cpcap_irqdata {
	struct mutex lock;
	struct work_struct work;
	struct workqueue_struct *workqueue;
	struct cpcap_device *cpcap;
	struct cpcap_event_handler event_handler[CPCAP_IRQ__NUM];
	uint64_t registered;
	uint64_t enabled;
	struct wake_lock wake_lock;
};

#define EVENT_MASK(event) (1 << ((event) % NUM_INTS_PER_REG))

enum pwrkey_states {
	PWRKEY_RELEASE,	/* Power key released state. */
	PWRKEY_PRESS,	/* Power key pressed state. */
	PWRKEY_UNKNOWN,	/* Unknown power key state. */
};

static irqreturn_t event_isr(int irq, void *data)
{
	struct cpcap_irqdata *irq_data = data;
	disable_irq_nosync(irq);
	wake_lock(&irq_data->wake_lock);
	queue_work(irq_data->workqueue, &irq_data->work);

	return IRQ_HANDLED;
}

static unsigned short get_int_reg(enum cpcap_irqs event)
{
	unsigned short ret;

	if ((event) >= CPCAP_IRQ_INT5_INDEX)
		ret = CPCAP_REG_MI1;
	else if ((event) >= CPCAP_IRQ_INT4_INDEX)
		ret = CPCAP_REG_INT4;
	else if ((event) >= CPCAP_IRQ_INT3_INDEX)
		ret = CPCAP_REG_INT3;
	else if ((event) >= CPCAP_IRQ_INT2_INDEX)
		ret = CPCAP_REG_INT2;
	else
		ret = CPCAP_REG_INT1;

	return ret;
}

static unsigned short get_mask_reg(enum cpcap_irqs event)
{
	unsigned short ret;

	if (event >= CPCAP_IRQ_INT5_INDEX)
		ret = CPCAP_REG_MIM1;
	else if (event >= CPCAP_IRQ_INT4_INDEX)
		ret = CPCAP_REG_INTM4;
	else if (event >= CPCAP_IRQ_INT3_INDEX)
		ret = CPCAP_REG_INTM3;
	else if (event >= CPCAP_IRQ_INT2_INDEX)
		ret = CPCAP_REG_INTM2;
	else
		ret = CPCAP_REG_INTM1;

	return ret;
}

static unsigned short get_sense_reg(enum cpcap_irqs event)
{
	unsigned short ret;

	if (event >= CPCAP_IRQ_INT5_INDEX)
		ret = CPCAP_REG_MI2;
	else if (event >= CPCAP_IRQ_INT4_INDEX)
		ret = CPCAP_REG_INTS4;
	else if (event >= CPCAP_IRQ_INT3_INDEX)
		ret = CPCAP_REG_INTS3;
	else if (event >= CPCAP_IRQ_INT2_INDEX)
		ret = CPCAP_REG_INTS2;
	else
		ret = CPCAP_REG_INTS1;

	return ret;
}

void cpcap_irq_mask_all(struct cpcap_device *cpcap)
{
	int i;

	static const struct {
		unsigned short mask_reg;
		unsigned short valid;
	} int_reg[NUM_INT_REGS] = {
		{CPCAP_REG_INTM1, CPCAP_INT1_VALID_BITS},
		{CPCAP_REG_INTM2, CPCAP_INT2_VALID_BITS},
		{CPCAP_REG_INTM3, CPCAP_INT3_VALID_BITS},
		{CPCAP_REG_INTM4, CPCAP_INT4_VALID_BITS},
		{CPCAP_REG_MIM1,  CPCAP_INT5_VALID_BITS}
	};

	for (i = 0; i < NUM_INT_REGS; i++) {
		cpcap_regacc_write(cpcap, int_reg[i].mask_reg,
				   int_reg[i].valid,
				   int_reg[i].valid);
	}
}

struct pwrkey_data {
	struct cpcap_device *cpcap;
	enum pwrkey_states state;
	struct wake_lock wake_lock;
};

static void pwrkey_handler(enum cpcap_irqs irq, void *data)
{
	struct pwrkey_data *pwrkey_data = data;
	enum pwrkey_states new_state, last_state = pwrkey_data->state;
	struct cpcap_device *cpcap = pwrkey_data->cpcap;

	new_state = (enum pwrkey_states) cpcap_irq_sense(cpcap, irq, 0);


	if ((new_state < PWRKEY_UNKNOWN) && (new_state != last_state)) {
		wake_lock_timeout(&pwrkey_data->wake_lock, 20);
		cpcap_broadcast_key_event(cpcap, KEY_END, new_state);
		pwrkey_data->state = new_state;
	} else if ((last_state == PWRKEY_RELEASE) &&
		   (new_state == PWRKEY_RELEASE)) {
		/* Key must have been released before press was handled. Send
		 * both the press and the release. */
		wake_lock_timeout(&pwrkey_data->wake_lock, 20);
		cpcap_broadcast_key_event(cpcap, KEY_END, PWRKEY_PRESS);
		cpcap_broadcast_key_event(cpcap, KEY_END, PWRKEY_RELEASE);
	}
	cpcap_irq_unmask(cpcap, CPCAP_IRQ_ON);
}

static int pwrkey_init(struct cpcap_device *cpcap)
{
	struct pwrkey_data *data = kmalloc(sizeof(struct pwrkey_data),
					   GFP_KERNEL);
	int retval;

	if (!data)
		return -ENOMEM;
	data->cpcap = cpcap;
	data->state = PWRKEY_RELEASE;
	retval = cpcap_irq_register(cpcap, CPCAP_IRQ_ON, pwrkey_handler, data);
	if (retval)
		kfree(data);
	wake_lock_init(&data->wake_lock, WAKE_LOCK_SUSPEND, "pwrkey");
	return retval;
}

static void pwrkey_remove(struct cpcap_device *cpcap)
{
	struct pwrkey_data *data;

	cpcap_irq_get_data(cpcap, CPCAP_IRQ_ON, (void **)&data);
	if (!data)
		return;
	cpcap_irq_free(cpcap, CPCAP_IRQ_ON);
	wake_lock_destroy(&data->wake_lock);
	kfree(data);
}

static int int_read_and_clear(struct cpcap_device *cpcap,
			      unsigned short status_reg,
			      unsigned short mask_reg,
			      unsigned short valid_mask,
			      unsigned short *en)
{
	unsigned short ireg_val, mreg_val;
	int ret;
	ret = cpcap_regacc_read(cpcap, status_reg, &ireg_val);
	if (ret)
		return ret;
	ret = cpcap_regacc_read(cpcap, mask_reg, &mreg_val);
	if (ret)
		return ret;
	*en |= ireg_val & ~mreg_val;
	*en &= valid_mask;
	ret = cpcap_regacc_write(cpcap, mask_reg, *en, *en);
	if (ret)
		return ret;
	ret = cpcap_regacc_write(cpcap, status_reg, *en, *en);
	if (ret)
		return ret;
	return 0;
}


static void irq_work_func(struct work_struct *work)
{
	int retval = 0;
	unsigned short en_ints[NUM_INT_REGS];
	int i;
	struct cpcap_irqdata *data;
	struct cpcap_device *cpcap;
	struct spi_device *spi;

	static const struct {
		unsigned short status_reg;
		unsigned short mask_reg;
		unsigned short valid;
	} int_reg[NUM_INT_REGS] = {
		{CPCAP_REG_INT1, CPCAP_REG_INTM1, CPCAP_INT1_VALID_BITS},
		{CPCAP_REG_INT2, CPCAP_REG_INTM2, CPCAP_INT2_VALID_BITS},
		{CPCAP_REG_INT3, CPCAP_REG_INTM3, CPCAP_INT3_VALID_BITS},
		{CPCAP_REG_INT4, CPCAP_REG_INTM4, CPCAP_INT4_VALID_BITS},
		{CPCAP_REG_MI1,  CPCAP_REG_MIM1,  CPCAP_INT5_VALID_BITS}
	};

	for (i = 0; i < NUM_INT_REGS; ++i)
		en_ints[i] = 0;

	data = container_of(work, struct cpcap_irqdata, work);
	cpcap = data->cpcap;
	spi = cpcap->spi;

	for (i = 0; i < NUM_INT_REGS; ++i) {
		retval = int_read_and_clear(cpcap,
					    int_reg[i].status_reg,
					    int_reg[i].mask_reg,
					    int_reg[i].valid,
					    &en_ints[i]);
		if (retval < 0) {
			dev_err(&spi->dev, "Error reading interrupts\n");
			break;
		}
	}
	enable_irq(spi->irq);

	/* lock protects event handlers and data */
	mutex_lock(&data->lock);
	for (i = 0; i < NUM_INT_REGS; ++i) {
		unsigned char index;

		while (en_ints[i] > 0) {
			struct cpcap_event_handler *event_handler;

			/* find the first set bit */
			index = (unsigned char)(ffs(en_ints[i]) - 1);
			if (index >= CPCAP_IRQ__NUM)
				goto error;
			/* clear the bit */
			en_ints[i] &= ~(1 << index);
			/* find the event that occurred */
			index += CPCAP_IRQ__START + (i * NUM_INTS_PER_REG);
			event_handler = &data->event_handler[index];

			if (event_handler->func)
				event_handler->func(index, event_handler->data);
		}
	}
error:
	mutex_unlock(&data->lock);
	wake_unlock(&data->wake_lock);
}

int cpcap_irq_init(struct cpcap_device *cpcap)
{
	int retval;
	struct spi_device *spi = cpcap->spi;
	struct cpcap_irqdata *data;
	struct dentry *debug_dir;

	data = kzalloc(sizeof(struct cpcap_irqdata), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	cpcap_irq_mask_all(cpcap);

	data->workqueue = create_workqueue("cpcap_irq");
	INIT_WORK(&data->work, irq_work_func);
	mutex_init(&data->lock);
	wake_lock_init(&data->wake_lock, WAKE_LOCK_SUSPEND, "cpcap-irq");
	data->cpcap = cpcap;

	retval = request_irq(spi->irq, event_isr, IRQF_DISABLED |
			     IRQF_TRIGGER_HIGH, "cpcap-irq", data);
	if (retval) {
		printk(KERN_ERR "cpcap_irq: Failed requesting irq.\n");
		goto error;
	}

	enable_irq_wake(spi->irq);

	cpcap->irqdata = data;
	retval = pwrkey_init(cpcap);
	if (retval) {
		printk(KERN_ERR "cpcap_irq: Failed initializing pwrkey.\n");
		goto error;
	}

	debug_dir = debugfs_create_dir("cpcap-irq", NULL);
	debugfs_create_u64("registered", S_IRUGO, debug_dir,
			   &data->registered);
	debugfs_create_u64("enabled", S_IRUGO, debug_dir,
			   &data->enabled);

	return 0;

error:
	free_irq(spi->irq, data);
	kfree(data);
	printk(KERN_ERR "cpcap_irq: Error registering cpcap irq.\n");
	return retval;
}

void cpcap_irq_shutdown(struct cpcap_device *cpcap)
{
	struct spi_device *spi = cpcap->spi;
	struct cpcap_irqdata *data = cpcap->irqdata;

	pwrkey_remove(cpcap);
	cancel_work_sync(&data->work);
	destroy_workqueue(data->workqueue);
	free_irq(spi->irq, data);
	kfree(data);
}

int cpcap_irq_register(struct cpcap_device *cpcap,
		       enum cpcap_irqs irq,
		       void (*cb_func) (enum cpcap_irqs, void *),
		       void *data)
{
	struct cpcap_irqdata *irqdata = cpcap->irqdata;
	int retval = 0;

	if ((irq >= CPCAP_IRQ__NUM) || (!cb_func))
		return -EINVAL;

	mutex_lock(&irqdata->lock);

	if (irqdata->event_handler[irq].func == NULL) {
		irqdata->registered |= 1 << irq;
		cpcap_irq_unmask(cpcap, irq);
		irqdata->event_handler[irq].func = cb_func;
		irqdata->event_handler[irq].data = data;
	} else
		retval = -EPERM;

	mutex_unlock(&irqdata->lock);

	return retval;
}
EXPORT_SYMBOL_GPL(cpcap_irq_register);

int cpcap_irq_free(struct cpcap_device *cpcap, enum cpcap_irqs irq)
{
	struct cpcap_irqdata *data = cpcap->irqdata;
	int retval;

	if (irq >= CPCAP_IRQ__NUM)
		return -EINVAL;

	mutex_lock(&data->lock);
	retval = cpcap_irq_mask(cpcap, irq);
	data->event_handler[irq].func = NULL;
	data->event_handler[irq].data = NULL;
	data->registered &= ~(1 << irq);
	mutex_unlock(&data->lock);

	return retval;
}
EXPORT_SYMBOL_GPL(cpcap_irq_free);

int cpcap_irq_get_data(struct cpcap_device *cpcap,
			enum cpcap_irqs irq,
			void **data)
{
	struct cpcap_irqdata *irqdata = cpcap->irqdata;

	if (irq >= CPCAP_IRQ__NUM)
		return -EINVAL;

	mutex_lock(&irqdata->lock);
	*data = irqdata->event_handler[irq].data;
	mutex_unlock(&irqdata->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(cpcap_irq_get_data);

int cpcap_irq_clear(struct cpcap_device *cpcap,
		    enum cpcap_irqs irq)
{
	int retval = -EINVAL;

	if ((irq < CPCAP_IRQ__NUM) && (irq != CPCAP_IRQ_SECMAC)) {
		retval = cpcap_regacc_write(cpcap,
					    get_int_reg(irq),
					    EVENT_MASK(irq),
					    EVENT_MASK(irq));
	}

	return retval;
}
EXPORT_SYMBOL_GPL(cpcap_irq_clear);

int cpcap_irq_mask(struct cpcap_device *cpcap,
		   enum cpcap_irqs irq)
{
	struct cpcap_irqdata *data = cpcap->irqdata;
	int retval = -EINVAL;

	if ((irq < CPCAP_IRQ__NUM) && (irq != CPCAP_IRQ_SECMAC)) {
		data->enabled &= ~(1 << irq);
		retval = cpcap_regacc_write(cpcap,
					    get_mask_reg(irq),
					    EVENT_MASK(irq),
					    EVENT_MASK(irq));
	}

	return retval;
}
EXPORT_SYMBOL_GPL(cpcap_irq_mask);

int cpcap_irq_unmask(struct cpcap_device *cpcap,
		     enum cpcap_irqs irq)
{
	struct cpcap_irqdata *data = cpcap->irqdata;
	int retval = -EINVAL;

	if ((irq < CPCAP_IRQ__NUM) && (irq != CPCAP_IRQ_SECMAC)) {
		data->enabled |= 1 << irq;
		retval = cpcap_regacc_write(cpcap,
					    get_mask_reg(irq),
					    0,
					    EVENT_MASK(irq));
	}

	return retval;
}
EXPORT_SYMBOL_GPL(cpcap_irq_unmask);

int cpcap_irq_mask_get(struct cpcap_device *cpcap,
		       enum cpcap_irqs irq)
{
	struct cpcap_irqdata *data = cpcap->irqdata;
	int retval = -EINVAL;

	if ((irq < CPCAP_IRQ__NUM) && (irq != CPCAP_IRQ_SECMAC))
		return (data->enabled & (1 << irq)) ? 0 : 1;

	return retval;
}
EXPORT_SYMBOL_GPL(cpcap_irq_mask_get);

int cpcap_irq_sense(struct cpcap_device *cpcap,
		    enum cpcap_irqs irq,
		    unsigned char clear)
{
	unsigned short val;
	int retval;

	if (irq >= CPCAP_IRQ__NUM)
		return -EINVAL;

	retval = cpcap_regacc_read(cpcap, get_sense_reg(irq), &val);
	if (retval)
		return retval;

	if (clear)
		retval = cpcap_irq_clear(cpcap, irq);
	if (retval)
		return retval;

	return ((val & EVENT_MASK(irq)) != 0) ? 1 : 0;
}
EXPORT_SYMBOL_GPL(cpcap_irq_sense);
