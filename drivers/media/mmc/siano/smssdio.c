/*
 *  smssdio.c - Siano 1xxx SDIO interface driver
 *
 *  Copyright 2008 Pierre Ossman
 *
 * Based on code by Siano Mobile Silicon, Inc.,
 * Copyright (C) 2006-2008, Uri Shkolnik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 *
 * This hardware is a bit odd in that all transfers should be done
 * to/from the SMSSDIO_DATA register, yet the "increase address" bit
 * always needs to be set.
 *
 * Also, buffers from the card are always aligned to 128 byte
 * boundaries.
 */

/*
 * General cleanup notes:
 *
 * - only typedefs should be name *_t
 *
 * - use ERR_PTR and friends for smscore_register_device()
 *
 * - smscore_getbuffer should zero fields
 *
 * Fix stop command
 */

#include "smscoreapi.h"

#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/module.h>

#include "sms-cards.h"
#include "smsendian.h"

/* Registers */

#define SMSSDIO_DATA		0x00
#define SMSSDIO_INT		0x04
#define SMSSDIO_BLOCK_SIZE	128

static const struct sdio_device_id smssdio_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_STELLAR),
	 .driver_data = SMS1XXX_BOARD_SIANO_STELLAR},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_NOVA_A0),
	 .driver_data = SMS1XXX_BOARD_SIANO_NOVA_A},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_NOVA_B0),
	 .driver_data = SMS1XXX_BOARD_SIANO_NOVA_B},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_VEGA_A0),
	 .driver_data = SMS1XXX_BOARD_SIANO_VEGA},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_VENICE),
	 .driver_data = SMS1XXX_BOARD_SIANO_VEGA},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x302),
	.driver_data = SMS1XXX_BOARD_SIANO_MING},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x500),
	.driver_data = SMS1XXX_BOARD_SIANO_PELE},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x600),
	.driver_data = SMS1XXX_BOARD_SIANO_RIO},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x700),
	.driver_data = SMS1XXX_BOARD_SIANO_DENVER_2160},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x800),
	.driver_data = SMS1XXX_BOARD_SIANO_DENVER_1530},
	{ /* end: all zeroes */ },
};

MODULE_DEVICE_TABLE(sdio, smssdio_ids);

struct smssdio_device {
	struct sdio_func *func;

	struct smscore_device_t *coredev;

	struct smscore_buffer_t *split_cb;
};

/*******************************************************************/
/* Siano core callbacks                                            */
/*******************************************************************/

static int smssdio_sendrequest(void *context, void *buffer, size_t size)
{
	int ret = 0;
	struct smssdio_device *smsdev;

	smsdev = context;

	sdio_claim_host(smsdev->func);

	smsendian_handle_tx_message((struct sms_msg_data *) buffer);
	while (size >= smsdev->func->cur_blksize) {
		ret = sdio_memcpy_toio(smsdev->func, SMSSDIO_DATA,
					buffer, smsdev->func->cur_blksize);
		if (ret)
			goto out;

		buffer += smsdev->func->cur_blksize;
		size -= smsdev->func->cur_blksize;
	}

	if (size) {
		ret = sdio_memcpy_toio(smsdev->func, SMSSDIO_DATA,
					buffer, size);
	}

out:
	sdio_release_host(smsdev->func);

	return ret;
}

/*******************************************************************/
/* SDIO callbacks                                                  */
/*******************************************************************/

static void smssdio_interrupt(struct sdio_func *func)
{
	int ret;

	struct smssdio_device *smsdev;
	struct smscore_buffer_t *cb;
	struct sms_msg_hdr *hdr;
	size_t size;

	smsdev = sdio_get_drvdata(func);

	/*
	 * The interrupt register has no defined meaning. It is just
	 * a way of turning of the level triggered interrupt.
	 */
	(void)sdio_readb(func, SMSSDIO_INT, &ret);
	if (ret) {
		pr_err("Unable to read interrupt register!\n");
		return;
	}

	if (smsdev->split_cb == NULL) {
		cb = smscore_getbuffer(smsdev->coredev);
		if (!cb) {
			pr_err("Unable to allocate data buffer!\n");
			return;
		}

		ret = sdio_memcpy_fromio(smsdev->func,
					 cb->p,
					 SMSSDIO_DATA,
					 SMSSDIO_BLOCK_SIZE);
		if (ret) {
			pr_err("Error %d reading initial block!\n", ret);
			return;
		}

		hdr = cb->p;

		if (hdr->msg_flags & MSG_HDR_FLAG_SPLIT_MSG) {
			smsdev->split_cb = cb;
			return;
		}

		if (hdr->msg_length > smsdev->func->cur_blksize)
			size = hdr->msg_length - smsdev->func->cur_blksize;
		else
			size = 0;
	} else {
		cb = smsdev->split_cb;
		hdr = cb->p;

		size = hdr->msg_length - sizeof(struct sms_msg_hdr);

		smsdev->split_cb = NULL;
	}

	if (size) {
		void *buffer;

		buffer = cb->p + (hdr->msg_length - size);
		size = ALIGN(size, SMSSDIO_BLOCK_SIZE);

		BUG_ON(smsdev->func->cur_blksize != SMSSDIO_BLOCK_SIZE);

		/*
		 * First attempt to transfer all of it in one go...
		 */
		ret = sdio_memcpy_fromio(smsdev->func,
					 buffer,
					 SMSSDIO_DATA,
					 size);
		if (ret && ret != -EINVAL) {
			smscore_putbuffer(smsdev->coredev, cb);
			pr_err("Error %d reading data from card!\n", ret);
			return;
		}

		/*
		 * ..then fall back to one block at a time if that is
		 * not possible...
		 *
		 * (we have to do this manually because of the
		 * problem with the "increase address" bit)
		 */
		if (ret == -EINVAL) {
			while (size) {
				ret = sdio_memcpy_fromio(smsdev->func,
						  buffer, SMSSDIO_DATA,
						  smsdev->func->cur_blksize);
				if (ret) {
					smscore_putbuffer(smsdev->coredev, cb);
					pr_err("Error %d reading data from card!\n",
					       ret);
					return;
				}

				buffer += smsdev->func->cur_blksize;
				if (size > smsdev->func->cur_blksize)
					size -= smsdev->func->cur_blksize;
				else
					size = 0;
			}
		}
	}

	cb->size = hdr->msg_length;
	cb->offset = 0;

	smsendian_handle_rx_message((struct sms_msg_data *) cb->p);
	smscore_onresponse(smsdev->coredev, cb);
}

static int smssdio_probe(struct sdio_func *func,
			 const struct sdio_device_id *id)
{
	int ret;

	int board_id;
	struct smssdio_device *smsdev;
	struct smsdevice_params_t params;

	board_id = id->driver_data;

	smsdev = kzalloc(sizeof(struct smssdio_device), GFP_KERNEL);
	if (!smsdev)
		return -ENOMEM;

	smsdev->func = func;

	memset(&params, 0, sizeof(struct smsdevice_params_t));

	params.device = &func->dev;
	params.buffer_size = 0x5000;	/* ?? */
	params.num_buffers = 22;	/* ?? */
	params.context = smsdev;

	snprintf(params.devpath, sizeof(params.devpath),
		 "sdio\\%s", sdio_func_id(func));

	params.sendrequest_handler = smssdio_sendrequest;

	params.device_type = sms_get_board(board_id)->type;

	if (params.device_type != SMS_STELLAR)
		params.flags |= SMS_DEVICE_FAMILY2;
	else {
		/*
		 * FIXME: Stellar needs special handling...
		 */
		ret = -ENODEV;
		goto free;
	}

	ret = smscore_register_device(&params, &smsdev->coredev, GFP_DMA, NULL);
	if (ret < 0)
		goto free;

	smscore_set_board_id(smsdev->coredev, board_id);

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if (ret)
		goto release;

	ret = sdio_set_block_size(func, SMSSDIO_BLOCK_SIZE);
	if (ret)
		goto disable;

	ret = sdio_claim_irq(func, smssdio_interrupt);
	if (ret)
		goto disable;

	sdio_set_drvdata(func, smsdev);

	sdio_release_host(func);

	ret = smscore_start_device(smsdev->coredev);
	if (ret < 0)
		goto reclaim;

	return 0;

reclaim:
	sdio_claim_host(func);
	sdio_release_irq(func);
disable:
	sdio_disable_func(func);
release:
	sdio_release_host(func);
	smscore_unregister_device(smsdev->coredev);
free:
	kfree(smsdev);

	return ret;
}

static void smssdio_remove(struct sdio_func *func)
{
	struct smssdio_device *smsdev;

	smsdev = sdio_get_drvdata(func);

	/* FIXME: racy! */
	if (smsdev->split_cb)
		smscore_putbuffer(smsdev->coredev, smsdev->split_cb);

	smscore_unregister_device(smsdev->coredev);

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	kfree(smsdev);
}

static struct sdio_driver smssdio_driver = {
	.name = "smssdio",
	.id_table = smssdio_ids,
	.probe = smssdio_probe,
	.remove = smssdio_remove,
};

/*******************************************************************/
/* Module functions                                                */
/*******************************************************************/

static int __init smssdio_module_init(void)
{
	int ret = 0;

	printk(KERN_INFO "smssdio: Siano SMS1xxx SDIO driver\n");
	printk(KERN_INFO "smssdio: Copyright Pierre Ossman\n");

	ret = sdio_register_driver(&smssdio_driver);

	return ret;
}

static void __exit smssdio_module_exit(void)
{
	sdio_unregister_driver(&smssdio_driver);
}

module_init(smssdio_module_init);
module_exit(smssdio_module_exit);

MODULE_DESCRIPTION("Siano SMS1xxx SDIO driver");
MODULE_AUTHOR("Pierre Ossman");
MODULE_LICENSE("GPL");
