/*
 * ImgTec IR Raw Decoder found in PowerDown Controller.
 *
 * Copyright 2010-2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This ties into the input subsystem using the RC-core in raw mode. Raw IR
 * signal edges are reported and decoded by generic software decoders.
 */

#include <linux/spinlock.h>
#include <media/rc-core.h>
#include "img-ir.h"

#define ECHO_TIMEOUT_MS 150	/* ms between echos */

/* must be called with priv->lock held */
static void img_ir_refresh_raw(struct img_ir_priv *priv, u32 irq_status)
{
	struct img_ir_priv_raw *raw = &priv->raw;
	struct rc_dev *rc_dev = priv->raw.rdev;
	int multiple;
	u32 ir_status;

	/* find whether both rise and fall was detected */
	multiple = ((irq_status & IMG_IR_IRQ_EDGE) == IMG_IR_IRQ_EDGE);
	/*
	 * If so, we need to see if the level has actually changed.
	 * If it's just noise that we didn't have time to process,
	 * there's no point reporting it.
	 */
	ir_status = img_ir_read(priv, IMG_IR_STATUS) & IMG_IR_IRRXD;
	if (multiple && ir_status == raw->last_status)
		return;
	raw->last_status = ir_status;

	/* report the edge to the IR raw decoders */
	if (ir_status) /* low */
		ir_raw_event_store_edge(rc_dev, IR_SPACE);
	else /* high */
		ir_raw_event_store_edge(rc_dev, IR_PULSE);
	ir_raw_event_handle(rc_dev);
}

/* called with priv->lock held */
void img_ir_isr_raw(struct img_ir_priv *priv, u32 irq_status)
{
	struct img_ir_priv_raw *raw = &priv->raw;

	/* check not removing */
	if (!raw->rdev)
		return;

	img_ir_refresh_raw(priv, irq_status);

	/* start / push back the echo timer */
	mod_timer(&raw->timer, jiffies + msecs_to_jiffies(ECHO_TIMEOUT_MS));
}

/*
 * Echo timer callback function.
 * The raw decoders expect to get a final sample even if there are no edges, in
 * order to be assured of the final space. If there are no edges for a certain
 * time we use this timer to emit a final sample to satisfy them.
 */
static void img_ir_echo_timer(unsigned long arg)
{
	struct img_ir_priv *priv = (struct img_ir_priv *)arg;

	spin_lock_irq(&priv->lock);

	/* check not removing */
	if (priv->raw.rdev)
		/*
		 * It's safe to pass irq_status=0 since it's only used to check
		 * for double edges.
		 */
		img_ir_refresh_raw(priv, 0);

	spin_unlock_irq(&priv->lock);
}

void img_ir_setup_raw(struct img_ir_priv *priv)
{
	u32 irq_en;

	if (!priv->raw.rdev)
		return;

	/* clear and enable edge interrupts */
	spin_lock_irq(&priv->lock);
	irq_en = img_ir_read(priv, IMG_IR_IRQ_ENABLE);
	irq_en |= IMG_IR_IRQ_EDGE;
	img_ir_write(priv, IMG_IR_IRQ_CLEAR, IMG_IR_IRQ_EDGE);
	img_ir_write(priv, IMG_IR_IRQ_ENABLE, irq_en);
	spin_unlock_irq(&priv->lock);
}

int img_ir_probe_raw(struct img_ir_priv *priv)
{
	struct img_ir_priv_raw *raw = &priv->raw;
	struct rc_dev *rdev;
	int error;

	/* Set up the echo timer */
	setup_timer(&raw->timer, img_ir_echo_timer, (unsigned long)priv);

	/* Allocate raw decoder */
	raw->rdev = rdev = rc_allocate_device();
	if (!rdev) {
		dev_err(priv->dev, "cannot allocate raw input device\n");
		return -ENOMEM;
	}
	rdev->priv = priv;
	rdev->map_name = RC_MAP_EMPTY;
	rdev->input_name = "IMG Infrared Decoder Raw";
	rdev->driver_type = RC_DRIVER_IR_RAW;

	/* Register raw decoder */
	error = rc_register_device(rdev);
	if (error) {
		dev_err(priv->dev, "failed to register raw IR input device\n");
		rc_free_device(rdev);
		raw->rdev = NULL;
		return error;
	}

	return 0;
}

void img_ir_remove_raw(struct img_ir_priv *priv)
{
	struct img_ir_priv_raw *raw = &priv->raw;
	struct rc_dev *rdev = raw->rdev;
	u32 irq_en;

	if (!rdev)
		return;

	/* switch off and disable raw (edge) interrupts */
	spin_lock_irq(&priv->lock);
	raw->rdev = NULL;
	irq_en = img_ir_read(priv, IMG_IR_IRQ_ENABLE);
	irq_en &= ~IMG_IR_IRQ_EDGE;
	img_ir_write(priv, IMG_IR_IRQ_ENABLE, irq_en);
	img_ir_write(priv, IMG_IR_IRQ_CLEAR, IMG_IR_IRQ_EDGE);
	spin_unlock_irq(&priv->lock);

	rc_unregister_device(rdev);

	del_timer_sync(&raw->timer);
}
