// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>

#include "bcm_vk.h"

/* TTYVK base offset is 0x30000 into BAR1 */
#define BAR1_TTYVK_BASE_OFFSET	0x300000
/* Each TTYVK channel (TO or FROM) is 0x10000 */
#define BAR1_TTYVK_CHAN_OFFSET	0x100000
/* Each TTYVK channel has TO and FROM, hence the * 2 */
#define BAR1_TTYVK_BASE(index)	(BAR1_TTYVK_BASE_OFFSET + \
				 ((index) * BAR1_TTYVK_CHAN_OFFSET * 2))
/* TO TTYVK channel base comes before FROM for each index */
#define TO_TTYK_BASE(index)	BAR1_TTYVK_BASE(index)
#define FROM_TTYK_BASE(index)	(BAR1_TTYVK_BASE(index) + \
				 BAR1_TTYVK_CHAN_OFFSET)

struct bcm_vk_tty_chan {
	u32 reserved;
	u32 size;
	u32 wr;
	u32 rd;
	u32 *data;
};

#define VK_BAR_CHAN(v, DIR, e)	((v)->DIR##_offset \
				 + offsetof(struct bcm_vk_tty_chan, e))
#define VK_BAR_CHAN_SIZE(v, DIR)	VK_BAR_CHAN(v, DIR, size)
#define VK_BAR_CHAN_WR(v, DIR)		VK_BAR_CHAN(v, DIR, wr)
#define VK_BAR_CHAN_RD(v, DIR)		VK_BAR_CHAN(v, DIR, rd)
#define VK_BAR_CHAN_DATA(v, DIR, off)	(VK_BAR_CHAN(v, DIR, data) + (off))

#define VK_BAR0_REGSEG_TTY_DB_OFFSET	0x86c

/* Poll every 1/10 of second - temp hack till we use MSI interrupt */
#define SERIAL_TIMER_VALUE (HZ / 10)

static void bcm_vk_tty_poll(struct timer_list *t)
{
	struct bcm_vk *vk = from_timer(vk, t, serial_timer);

	queue_work(vk->tty_wq_thread, &vk->tty_wq_work);
	mod_timer(&vk->serial_timer, jiffies + SERIAL_TIMER_VALUE);
}

irqreturn_t bcm_vk_tty_irqhandler(int irq, void *dev_id)
{
	struct bcm_vk *vk = dev_id;

	queue_work(vk->tty_wq_thread, &vk->tty_wq_work);

	return IRQ_HANDLED;
}

static void bcm_vk_tty_wq_handler(struct work_struct *work)
{
	struct bcm_vk *vk = container_of(work, struct bcm_vk, tty_wq_work);
	struct bcm_vk_tty *vktty;
	int card_status;
	int count;
	unsigned char c;
	int i;
	int wr;

	card_status = vkread32(vk, BAR_0, BAR_CARD_STATUS);
	if (BCM_VK_INTF_IS_DOWN(card_status))
		return;

	for (i = 0; i < BCM_VK_NUM_TTY; i++) {
		count = 0;
		/* Check the card status that the tty channel is ready */
		if ((card_status & BIT(i)) == 0)
			continue;

		vktty = &vk->tty[i];

		/* Don't increment read index if tty app is closed */
		if (!vktty->is_opened)
			continue;

		/* Fetch the wr offset in buffer from VK */
		wr = vkread32(vk, BAR_1, VK_BAR_CHAN_WR(vktty, from));

		/* safe to ignore until bar read gives proper size */
		if (vktty->from_size == 0)
			continue;

		if (wr >= vktty->from_size) {
			dev_err(&vk->pdev->dev,
				"ERROR: wq handler ttyVK%d wr:0x%x > 0x%x\n",
				i, wr, vktty->from_size);
			/* Need to signal and close device in this case */
			continue;
		}

		/*
		 * Simple read of circular buffer and
		 * insert into tty flip buffer
		 */
		while (vk->tty[i].rd != wr) {
			c = vkread8(vk, BAR_1,
				    VK_BAR_CHAN_DATA(vktty, from, vktty->rd));
			vktty->rd++;
			if (vktty->rd >= vktty->from_size)
				vktty->rd = 0;
			tty_insert_flip_char(&vktty->port, c, TTY_NORMAL);
			count++;
		}

		if (count) {
			tty_flip_buffer_push(&vktty->port);

			/* Update read offset from shadow register to card */
			vkwrite32(vk, vktty->rd, BAR_1,
				  VK_BAR_CHAN_RD(vktty, from));
		}
	}
}

static int bcm_vk_tty_open(struct tty_struct *tty, struct file *file)
{
	int card_status;
	struct bcm_vk *vk;
	struct bcm_vk_tty *vktty;
	int index;

	/* initialize the pointer in case something fails */
	tty->driver_data = NULL;

	vk = (struct bcm_vk *)dev_get_drvdata(tty->dev);
	index = tty->index;

	if (index >= BCM_VK_NUM_TTY)
		return -EINVAL;

	vktty = &vk->tty[index];

	vktty->pid = task_pid_nr(current);
	vktty->to_offset = TO_TTYK_BASE(index);
	vktty->from_offset = FROM_TTYK_BASE(index);

	/* Do not allow tty device to be opened if tty on card not ready */
	card_status = vkread32(vk, BAR_0, BAR_CARD_STATUS);
	if (BCM_VK_INTF_IS_DOWN(card_status) || ((card_status & BIT(index)) == 0))
		return -EBUSY;

	/*
	 * Get shadow registers of the buffer sizes and the "to" write offset
	 * and "from" read offset
	 */
	vktty->to_size = vkread32(vk, BAR_1, VK_BAR_CHAN_SIZE(vktty, to));
	vktty->wr = vkread32(vk, BAR_1,  VK_BAR_CHAN_WR(vktty, to));
	vktty->from_size = vkread32(vk, BAR_1, VK_BAR_CHAN_SIZE(vktty, from));
	vktty->rd = vkread32(vk, BAR_1,  VK_BAR_CHAN_RD(vktty, from));
	vktty->is_opened = true;

	if (tty->count == 1 && !vktty->irq_enabled) {
		timer_setup(&vk->serial_timer, bcm_vk_tty_poll, 0);
		mod_timer(&vk->serial_timer, jiffies + SERIAL_TIMER_VALUE);
	}
	return 0;
}

static void bcm_vk_tty_close(struct tty_struct *tty, struct file *file)
{
	struct bcm_vk *vk = dev_get_drvdata(tty->dev);

	if (tty->index >= BCM_VK_NUM_TTY)
		return;

	vk->tty[tty->index].is_opened = false;

	if (tty->count == 1)
		del_timer_sync(&vk->serial_timer);
}

static void bcm_vk_tty_doorbell(struct bcm_vk *vk, u32 db_val)
{
	vkwrite32(vk, db_val, BAR_0,
		  VK_BAR0_REGSEG_DB_BASE + VK_BAR0_REGSEG_TTY_DB_OFFSET);
}

static ssize_t bcm_vk_tty_write(struct tty_struct *tty, const u8 *buffer,
				size_t count)
{
	int index;
	struct bcm_vk *vk;
	struct bcm_vk_tty *vktty;
	int i;

	index = tty->index;
	vk = dev_get_drvdata(tty->dev);
	vktty = &vk->tty[index];

	/* Simple write each byte to circular buffer */
	for (i = 0; i < count; i++) {
		vkwrite8(vk, buffer[i], BAR_1,
			 VK_BAR_CHAN_DATA(vktty, to, vktty->wr));
		vktty->wr++;
		if (vktty->wr >= vktty->to_size)
			vktty->wr = 0;
	}
	/* Update write offset from shadow register to card */
	vkwrite32(vk, vktty->wr, BAR_1, VK_BAR_CHAN_WR(vktty, to));
	bcm_vk_tty_doorbell(vk, 0);

	return count;
}

static unsigned int bcm_vk_tty_write_room(struct tty_struct *tty)
{
	struct bcm_vk *vk = dev_get_drvdata(tty->dev);

	return vk->tty[tty->index].to_size - 1;
}

static const struct tty_operations serial_ops = {
	.open = bcm_vk_tty_open,
	.close = bcm_vk_tty_close,
	.write = bcm_vk_tty_write,
	.write_room = bcm_vk_tty_write_room,
};

int bcm_vk_tty_init(struct bcm_vk *vk, char *name)
{
	int i;
	int err;
	struct tty_driver *tty_drv;
	struct device *dev = &vk->pdev->dev;

	tty_drv = tty_alloc_driver
				(BCM_VK_NUM_TTY,
				 TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(tty_drv))
		return PTR_ERR(tty_drv);

	/* Save struct tty_driver for uninstalling the device */
	vk->tty_drv = tty_drv;

	/* initialize the tty driver */
	tty_drv->driver_name = KBUILD_MODNAME;
	tty_drv->name = kstrdup(name, GFP_KERNEL);
	if (!tty_drv->name) {
		err = -ENOMEM;
		goto err_tty_driver_kref_put;
	}
	tty_drv->type = TTY_DRIVER_TYPE_SERIAL;
	tty_drv->subtype = SERIAL_TYPE_NORMAL;
	tty_drv->init_termios = tty_std_termios;
	tty_set_operations(tty_drv, &serial_ops);

	/* register the tty driver */
	err = tty_register_driver(tty_drv);
	if (err) {
		dev_err(dev, "tty_register_driver failed\n");
		goto err_kfree_tty_name;
	}

	for (i = 0; i < BCM_VK_NUM_TTY; i++) {
		struct device *tty_dev;

		tty_port_init(&vk->tty[i].port);
		tty_dev = tty_port_register_device_attr(&vk->tty[i].port,
							tty_drv, i, dev, vk,
							NULL);
		if (IS_ERR(tty_dev)) {
			err = PTR_ERR(tty_dev);
			goto unwind;
		}
		vk->tty[i].is_opened = false;
	}

	INIT_WORK(&vk->tty_wq_work, bcm_vk_tty_wq_handler);
	vk->tty_wq_thread = create_singlethread_workqueue("tty");
	if (!vk->tty_wq_thread) {
		dev_err(dev, "Fail to create tty workqueue thread\n");
		err = -ENOMEM;
		goto unwind;
	}
	return 0;

unwind:
	while (--i >= 0)
		tty_port_unregister_device(&vk->tty[i].port, tty_drv, i);
	tty_unregister_driver(tty_drv);

err_kfree_tty_name:
	kfree(tty_drv->name);
	tty_drv->name = NULL;

err_tty_driver_kref_put:
	tty_driver_kref_put(tty_drv);

	return err;
}

void bcm_vk_tty_exit(struct bcm_vk *vk)
{
	int i;

	del_timer_sync(&vk->serial_timer);
	for (i = 0; i < BCM_VK_NUM_TTY; ++i) {
		tty_port_unregister_device(&vk->tty[i].port,
					   vk->tty_drv,
					   i);
		tty_port_destroy(&vk->tty[i].port);
	}
	tty_unregister_driver(vk->tty_drv);

	kfree(vk->tty_drv->name);
	vk->tty_drv->name = NULL;

	tty_driver_kref_put(vk->tty_drv);
}

void bcm_vk_tty_terminate_tty_user(struct bcm_vk *vk)
{
	struct bcm_vk_tty *vktty;
	int i;

	for (i = 0; i < BCM_VK_NUM_TTY; ++i) {
		vktty = &vk->tty[i];
		if (vktty->pid)
			kill_pid(find_vpid(vktty->pid), SIGKILL, 1);
	}
}

void bcm_vk_tty_wq_exit(struct bcm_vk *vk)
{
	cancel_work_sync(&vk->tty_wq_work);
	destroy_workqueue(vk->tty_wq_thread);
}
