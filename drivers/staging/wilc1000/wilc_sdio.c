// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <linux/mmc/sdio_func.h>
#include <linux/mmc/host.h>

#include "wilc_wfi_netdevice.h"

#define SDIO_MODALIAS "wilc1000_sdio"

#define SDIO_VENDOR_ID_WILC 0x0296
#define SDIO_DEVICE_ID_WILC 0x5347

static const struct sdio_device_id wilc_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_WILC, SDIO_DEVICE_ID_WILC) },
	{ },
};

#define WILC_SDIO_BLOCK_SIZE 512

struct wilc_sdio {
	bool irq_gpio;
	u32 block_size;
	int nint;
/* Max num interrupts allowed in registers 0xf7, 0xf8 */
#define MAX_NUN_INT_THRPT_ENH2 (5)
	int has_thrpt_enh3;
};

struct sdio_cmd52 {
	u32 read_write:		1;
	u32 function:		3;
	u32 raw:		1;
	u32 address:		17;
	u32 data:		8;
};

struct sdio_cmd53 {
	u32 read_write:		1;
	u32 function:		3;
	u32 block_mode:		1;
	u32 increment:		1;
	u32 address:		17;
	u32 count:		9;
	u8 *buffer;
	u32 block_size;
};

static const struct wilc_hif_func wilc_hif_sdio;

static int sdio_write_reg(struct wilc *wilc, u32 addr, u32 data);
static int sdio_read_reg(struct wilc *wilc, u32 addr, u32 *data);
static int sdio_init(struct wilc *wilc, bool resume);

static void wilc_sdio_interrupt(struct sdio_func *func)
{
	sdio_release_host(func);
	wilc_handle_isr(sdio_get_drvdata(func));
	sdio_claim_host(func);
}

static int wilc_sdio_cmd52(struct wilc *wilc, struct sdio_cmd52 *cmd)
{
	struct sdio_func *func = container_of(wilc->dev, struct sdio_func, dev);
	int ret;
	u8 data;

	sdio_claim_host(func);

	func->num = cmd->function;
	if (cmd->read_write) {  /* write */
		if (cmd->raw) {
			sdio_writeb(func, cmd->data, cmd->address, &ret);
			data = sdio_readb(func, cmd->address, &ret);
			cmd->data = data;
		} else {
			sdio_writeb(func, cmd->data, cmd->address, &ret);
		}
	} else {        /* read */
		data = sdio_readb(func, cmd->address, &ret);
		cmd->data = data;
	}

	sdio_release_host(func);

	if (ret)
		dev_err(&func->dev, "%s..failed, err(%d)\n", __func__, ret);
	return ret;
}

static int wilc_sdio_cmd53(struct wilc *wilc, struct sdio_cmd53 *cmd)
{
	struct sdio_func *func = container_of(wilc->dev, struct sdio_func, dev);
	int size, ret;

	sdio_claim_host(func);

	func->num = cmd->function;
	func->cur_blksize = cmd->block_size;
	if (cmd->block_mode)
		size = cmd->count * cmd->block_size;
	else
		size = cmd->count;

	if (cmd->read_write) {  /* write */
		ret = sdio_memcpy_toio(func, cmd->address,
				       (void *)cmd->buffer, size);
	} else {        /* read */
		ret = sdio_memcpy_fromio(func, (void *)cmd->buffer,
					 cmd->address,  size);
	}

	sdio_release_host(func);

	if (ret)
		dev_err(&func->dev, "%s..failed, err(%d)\n", __func__,  ret);

	return ret;
}

static int linux_sdio_probe(struct sdio_func *func,
			    const struct sdio_device_id *id)
{
	struct wilc *wilc;
	int ret;
	struct gpio_desc *gpio = NULL;
	struct wilc_sdio *sdio_priv;

	sdio_priv = kzalloc(sizeof(*sdio_priv), GFP_KERNEL);
	if (!sdio_priv)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_WILC1000_HW_OOB_INTR)) {
		gpio = gpiod_get(&func->dev, "irq", GPIOD_IN);
		if (IS_ERR(gpio)) {
			/* get the GPIO descriptor from hardcode GPIO number */
			gpio = gpio_to_desc(GPIO_NUM);
			if (!gpio)
				dev_err(&func->dev, "failed to get irq gpio\n");
		}
	}

	dev_dbg(&func->dev, "Initializing netdev\n");
	ret = wilc_netdev_init(&wilc, &func->dev, WILC_HIF_SDIO,
			       &wilc_hif_sdio);
	if (ret) {
		dev_err(&func->dev, "Couldn't initialize netdev\n");
		kfree(sdio_priv);
		return ret;
	}
	sdio_set_drvdata(func, wilc);
	wilc->bus_data = sdio_priv;
	wilc->dev = &func->dev;
	wilc->gpio_irq = gpio;

	dev_info(&func->dev, "Driver Initializing success\n");
	return 0;
}

static void linux_sdio_remove(struct sdio_func *func)
{
	struct wilc *wilc = sdio_get_drvdata(func);

	/* free the GPIO in module remove */
	if (wilc->gpio_irq)
		gpiod_put(wilc->gpio_irq);
	wilc_netdev_cleanup(wilc);
}

static int sdio_reset(struct wilc *wilc)
{
	struct sdio_cmd52 cmd;
	int ret;
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);

	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 0;
	cmd.address = 0x6;
	cmd.data = 0x8;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Fail cmd 52, reset cmd ...\n");
		return ret;
	}
	return 0;
}

static int wilc_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct wilc *wilc = sdio_get_drvdata(func);
	int ret;

	dev_info(dev, "sdio suspend\n");
	chip_wakeup(wilc);

	if (!wilc->suspend_event) {
		wilc_chip_sleep_manually(wilc);
	} else {
		host_sleep_notify(wilc);
		chip_allow_sleep(wilc);
	}

	ret = sdio_reset(wilc);
	if (ret) {
		dev_err(&func->dev, "Fail reset sdio\n");
		return ret;
	}
	sdio_claim_host(func);

	return 0;
}

static int wilc_sdio_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct wilc *wilc = sdio_get_drvdata(func);

	dev_info(dev, "sdio resume\n");
	sdio_release_host(func);
	chip_wakeup(wilc);
	sdio_init(wilc, true);

	if (wilc->suspend_event)
		host_wakeup_notify(wilc);

	chip_allow_sleep(wilc);

	return 0;
}

static const struct of_device_id wilc_of_match[] = {
	{ .compatible = "microchip,wilc1000-sdio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wilc_of_match);

static const struct dev_pm_ops wilc_sdio_pm_ops = {
	.suspend = wilc_sdio_suspend,
	.resume = wilc_sdio_resume,
};

static struct sdio_driver wilc_sdio_driver = {
	.name		= SDIO_MODALIAS,
	.id_table	= wilc_sdio_ids,
	.probe		= linux_sdio_probe,
	.remove		= linux_sdio_remove,
	.drv = {
		.pm = &wilc_sdio_pm_ops,
		.of_match_table = wilc_of_match,
	}
};
module_driver(wilc_sdio_driver,
	      sdio_register_driver,
	      sdio_unregister_driver);
MODULE_LICENSE("GPL");

static int wilc_sdio_enable_interrupt(struct wilc *dev)
{
	struct sdio_func *func = container_of(dev->dev, struct sdio_func, dev);
	int ret = 0;

	sdio_claim_host(func);
	ret = sdio_claim_irq(func, wilc_sdio_interrupt);
	sdio_release_host(func);

	if (ret < 0) {
		dev_err(&func->dev, "can't claim sdio_irq, err(%d)\n", ret);
		ret = -EIO;
	}
	return ret;
}

static void wilc_sdio_disable_interrupt(struct wilc *dev)
{
	struct sdio_func *func = container_of(dev->dev, struct sdio_func, dev);
	int ret;

	sdio_claim_host(func);
	ret = sdio_release_irq(func);
	if (ret < 0)
		dev_err(&func->dev, "can't release sdio_irq, err(%d)\n", ret);
	sdio_release_host(func);
}

/********************************************
 *
 *      Function 0
 *
 ********************************************/

static int sdio_set_func0_csa_address(struct wilc *wilc, u32 adr)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct sdio_cmd52 cmd;
	int ret;

	/**
	 *      Review: BIG ENDIAN
	 **/
	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 0;
	cmd.address = 0x10c;
	cmd.data = (u8)adr;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set 0x10c data...\n");
		goto fail;
	}

	cmd.address = 0x10d;
	cmd.data = (u8)(adr >> 8);
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set 0x10d data...\n");
		goto fail;
	}

	cmd.address = 0x10e;
	cmd.data = (u8)(adr >> 16);
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set 0x10e data...\n");
		goto fail;
	}

	return 1;
fail:
	return 0;
}

static int sdio_set_func0_block_size(struct wilc *wilc, u32 block_size)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct sdio_cmd52 cmd;
	int ret;

	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 0;
	cmd.address = 0x10;
	cmd.data = (u8)block_size;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set 0x10 data...\n");
		goto fail;
	}

	cmd.address = 0x11;
	cmd.data = (u8)(block_size >> 8);
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set 0x11 data...\n");
		goto fail;
	}

	return 1;
fail:
	return 0;
}

/********************************************
 *
 *      Function 1
 *
 ********************************************/

static int sdio_set_func1_block_size(struct wilc *wilc, u32 block_size)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct sdio_cmd52 cmd;
	int ret;

	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 0;
	cmd.address = 0x110;
	cmd.data = (u8)block_size;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set 0x110 data...\n");
		goto fail;
	}
	cmd.address = 0x111;
	cmd.data = (u8)(block_size >> 8);
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set 0x111 data...\n");
		goto fail;
	}

	return 1;
fail:
	return 0;
}

/********************************************
 *
 *      Sdio interfaces
 *
 ********************************************/
static int sdio_write_reg(struct wilc *wilc, u32 addr, u32 data)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	int ret;

	cpu_to_le32s(&data);

	if (addr >= 0xf0 && addr <= 0xff) {
		struct sdio_cmd52 cmd;

		cmd.read_write = 1;
		cmd.function = 0;
		cmd.raw = 0;
		cmd.address = addr;
		cmd.data = data;
		ret = wilc_sdio_cmd52(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd 52, read reg (%08x) ...\n", addr);
			goto fail;
		}
	} else {
		struct sdio_cmd53 cmd;

		/**
		 *      set the AHB address
		 **/
		if (!sdio_set_func0_csa_address(wilc, addr))
			goto fail;

		cmd.read_write = 1;
		cmd.function = 0;
		cmd.address = 0x10f;
		cmd.block_mode = 0;
		cmd.increment = 1;
		cmd.count = 4;
		cmd.buffer = (u8 *)&data;
		cmd.block_size = sdio_priv->block_size;
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53, write reg (%08x)...\n", addr);
			goto fail;
		}
	}

	return 1;

fail:

	return 0;
}

static int sdio_write(struct wilc *wilc, u32 addr, u8 *buf, u32 size)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	u32 block_size = sdio_priv->block_size;
	struct sdio_cmd53 cmd;
	int nblk, nleft, ret;

	cmd.read_write = 1;
	if (addr > 0) {
		/**
		 *      has to be word aligned...
		 **/
		if (size & 0x3) {
			size += 4;
			size &= ~0x3;
		}

		/**
		 *      func 0 access
		 **/
		cmd.function = 0;
		cmd.address = 0x10f;
	} else {
		/**
		 *      has to be word aligned...
		 **/
		if (size & 0x3) {
			size += 4;
			size &= ~0x3;
		}

		/**
		 *      func 1 access
		 **/
		cmd.function = 1;
		cmd.address = 0;
	}

	nblk = size / block_size;
	nleft = size % block_size;

	if (nblk > 0) {
		cmd.block_mode = 1;
		cmd.increment = 1;
		cmd.count = nblk;
		cmd.buffer = buf;
		cmd.block_size = block_size;
		if (addr > 0) {
			if (!sdio_set_func0_csa_address(wilc, addr))
				goto fail;
		}
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53 [%x], block send...\n", addr);
			goto fail;
		}
		if (addr > 0)
			addr += nblk * block_size;
		buf += nblk * block_size;
	}

	if (nleft > 0) {
		cmd.block_mode = 0;
		cmd.increment = 1;
		cmd.count = nleft;
		cmd.buffer = buf;

		cmd.block_size = block_size;

		if (addr > 0) {
			if (!sdio_set_func0_csa_address(wilc, addr))
				goto fail;
		}
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53 [%x], bytes send...\n", addr);
			goto fail;
		}
	}

	return 1;

fail:

	return 0;
}

static int sdio_read_reg(struct wilc *wilc, u32 addr, u32 *data)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	int ret;

	if (addr >= 0xf0 && addr <= 0xff) {
		struct sdio_cmd52 cmd;

		cmd.read_write = 0;
		cmd.function = 0;
		cmd.raw = 0;
		cmd.address = addr;
		ret = wilc_sdio_cmd52(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd 52, read reg (%08x) ...\n", addr);
			goto fail;
		}
		*data = cmd.data;
	} else {
		struct sdio_cmd53 cmd;

		if (!sdio_set_func0_csa_address(wilc, addr))
			goto fail;

		cmd.read_write = 0;
		cmd.function = 0;
		cmd.address = 0x10f;
		cmd.block_mode = 0;
		cmd.increment = 1;
		cmd.count = 4;
		cmd.buffer = (u8 *)data;

		cmd.block_size = sdio_priv->block_size;
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53, read reg (%08x)...\n", addr);
			goto fail;
		}
	}

	le32_to_cpus(data);

	return 1;

fail:

	return 0;
}

static int sdio_read(struct wilc *wilc, u32 addr, u8 *buf, u32 size)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	u32 block_size = sdio_priv->block_size;
	struct sdio_cmd53 cmd;
	int nblk, nleft, ret;

	cmd.read_write = 0;
	if (addr > 0) {
		/**
		 *      has to be word aligned...
		 **/
		if (size & 0x3) {
			size += 4;
			size &= ~0x3;
		}

		/**
		 *      func 0 access
		 **/
		cmd.function = 0;
		cmd.address = 0x10f;
	} else {
		/**
		 *      has to be word aligned...
		 **/
		if (size & 0x3) {
			size += 4;
			size &= ~0x3;
		}

		/**
		 *      func 1 access
		 **/
		cmd.function = 1;
		cmd.address = 0;
	}

	nblk = size / block_size;
	nleft = size % block_size;

	if (nblk > 0) {
		cmd.block_mode = 1;
		cmd.increment = 1;
		cmd.count = nblk;
		cmd.buffer = buf;
		cmd.block_size = block_size;
		if (addr > 0) {
			if (!sdio_set_func0_csa_address(wilc, addr))
				goto fail;
		}
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53 [%x], block read...\n", addr);
			goto fail;
		}
		if (addr > 0)
			addr += nblk * block_size;
		buf += nblk * block_size;
	}       /* if (nblk > 0) */

	if (nleft > 0) {
		cmd.block_mode = 0;
		cmd.increment = 1;
		cmd.count = nleft;
		cmd.buffer = buf;

		cmd.block_size = block_size;

		if (addr > 0) {
			if (!sdio_set_func0_csa_address(wilc, addr))
				goto fail;
		}
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53 [%x], bytes read...\n", addr);
			goto fail;
		}
	}

	return 1;

fail:

	return 0;
}

/********************************************
 *
 *      Bus interfaces
 *
 ********************************************/

static int sdio_deinit(struct wilc *wilc)
{
	return 1;
}

static int sdio_init(struct wilc *wilc, bool resume)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	struct sdio_cmd52 cmd;
	int loop, ret;
	u32 chipid;

	if (!resume)
		sdio_priv->irq_gpio = wilc->dev_irq_num;

	/**
	 *      function 0 csa enable
	 **/
	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 1;
	cmd.address = 0x100;
	cmd.data = 0x80;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Fail cmd 52, enable csa...\n");
		goto fail;
	}

	/**
	 *      function 0 block size
	 **/
	if (!sdio_set_func0_block_size(wilc, WILC_SDIO_BLOCK_SIZE)) {
		dev_err(&func->dev, "Fail cmd 52, set func 0 block size...\n");
		goto fail;
	}
	sdio_priv->block_size = WILC_SDIO_BLOCK_SIZE;

	/**
	 *      enable func1 IO
	 **/
	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 1;
	cmd.address = 0x2;
	cmd.data = 0x2;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev,
			"Fail cmd 52, set IOE register...\n");
		goto fail;
	}

	/**
	 *      make sure func 1 is up
	 **/
	cmd.read_write = 0;
	cmd.function = 0;
	cmd.raw = 0;
	cmd.address = 0x3;
	loop = 3;
	do {
		cmd.data = 0;
		ret = wilc_sdio_cmd52(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Fail cmd 52, get IOR register...\n");
			goto fail;
		}
		if (cmd.data == 0x2)
			break;
	} while (loop--);

	if (loop <= 0) {
		dev_err(&func->dev, "Fail func 1 is not ready...\n");
		goto fail;
	}

	/**
	 *      func 1 is ready, set func 1 block size
	 **/
	if (!sdio_set_func1_block_size(wilc, WILC_SDIO_BLOCK_SIZE)) {
		dev_err(&func->dev, "Fail set func 1 block size...\n");
		goto fail;
	}

	/**
	 *      func 1 interrupt enable
	 **/
	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 1;
	cmd.address = 0x4;
	cmd.data = 0x3;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Fail cmd 52, set IEN register...\n");
		goto fail;
	}

	/**
	 *      make sure can read back chip id correctly
	 **/
	if (!resume) {
		if (!sdio_read_reg(wilc, 0x1000, &chipid)) {
			dev_err(&func->dev, "Fail cmd read chip id...\n");
			goto fail;
		}
		dev_err(&func->dev, "chipid (%08x)\n", chipid);
		if ((chipid & 0xfff) > 0x2a0)
			sdio_priv->has_thrpt_enh3 = 1;
		else
			sdio_priv->has_thrpt_enh3 = 0;
		dev_info(&func->dev, "has_thrpt_enh3 = %d...\n",
			 sdio_priv->has_thrpt_enh3);
	}

	return 1;

fail:

	return 0;
}

static int sdio_read_size(struct wilc *wilc, u32 *size)
{
	u32 tmp;
	struct sdio_cmd52 cmd;

	/**
	 *      Read DMA count in words
	 **/
	cmd.read_write = 0;
	cmd.function = 0;
	cmd.raw = 0;
	cmd.address = 0xf2;
	cmd.data = 0;
	wilc_sdio_cmd52(wilc, &cmd);
	tmp = cmd.data;

	cmd.address = 0xf3;
	cmd.data = 0;
	wilc_sdio_cmd52(wilc, &cmd);
	tmp |= (cmd.data << 8);

	*size = tmp;
	return 1;
}

static int sdio_read_int(struct wilc *wilc, u32 *int_status)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	u32 tmp;
	struct sdio_cmd52 cmd;

	sdio_read_size(wilc, &tmp);

	/**
	 *      Read IRQ flags
	 **/
	if (!sdio_priv->irq_gpio) {
		int i;

		cmd.read_write = 0;
		cmd.function = 1;
		cmd.address = 0x04;
		cmd.data = 0;
		wilc_sdio_cmd52(wilc, &cmd);

		if (cmd.data & BIT(0))
			tmp |= INT_0;
		if (cmd.data & BIT(2))
			tmp |= INT_1;
		if (cmd.data & BIT(3))
			tmp |= INT_2;
		if (cmd.data & BIT(4))
			tmp |= INT_3;
		if (cmd.data & BIT(5))
			tmp |= INT_4;
		if (cmd.data & BIT(6))
			tmp |= INT_5;
		for (i = sdio_priv->nint; i < MAX_NUM_INT; i++) {
			if ((tmp >> (IRG_FLAGS_OFFSET + i)) & 0x1) {
				dev_err(&func->dev,
					"Unexpected interrupt (1) : tmp=%x, data=%x\n",
					tmp, cmd.data);
				break;
			}
		}
	} else {
		u32 irq_flags;

		cmd.read_write = 0;
		cmd.function = 0;
		cmd.raw = 0;
		cmd.address = 0xf7;
		cmd.data = 0;
		wilc_sdio_cmd52(wilc, &cmd);
		irq_flags = cmd.data & 0x1f;
		tmp |= ((irq_flags >> 0) << IRG_FLAGS_OFFSET);
	}

	*int_status = tmp;

	return 1;
}

static int sdio_clear_int_ext(struct wilc *wilc, u32 val)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	int ret;
	int vmm_ctl;

	if (sdio_priv->has_thrpt_enh3) {
		u32 reg;

		if (sdio_priv->irq_gpio) {
			u32 flags;

			flags = val & (BIT(MAX_NUN_INT_THRPT_ENH2) - 1);
			reg = flags;
		} else {
			reg = 0;
		}
		/* select VMM table 0 */
		if (val & SEL_VMM_TBL0)
			reg |= BIT(5);
		/* select VMM table 1 */
		if (val & SEL_VMM_TBL1)
			reg |= BIT(6);
		/* enable VMM */
		if (val & EN_VMM)
			reg |= BIT(7);
		if (reg) {
			struct sdio_cmd52 cmd;

			cmd.read_write = 1;
			cmd.function = 0;
			cmd.raw = 0;
			cmd.address = 0xf8;
			cmd.data = reg;

			ret = wilc_sdio_cmd52(wilc, &cmd);
			if (ret) {
				dev_err(&func->dev,
					"Failed cmd52, set 0xf8 data (%d) ...\n",
					__LINE__);
				goto fail;
			}
		}
		return 1;
	}
	if (sdio_priv->irq_gpio) {
		/* has_thrpt_enh2 uses register 0xf8 to clear interrupts. */
		/*
		 * Cannot clear multiple interrupts.
		 * Must clear each interrupt individually.
		 */
		u32 flags;

		flags = val & (BIT(MAX_NUM_INT) - 1);
		if (flags) {
			int i;

			ret = 1;
			for (i = 0; i < sdio_priv->nint; i++) {
				if (flags & 1) {
					struct sdio_cmd52 cmd;

					cmd.read_write = 1;
					cmd.function = 0;
					cmd.raw = 0;
					cmd.address = 0xf8;
					cmd.data = BIT(i);

					ret = wilc_sdio_cmd52(wilc, &cmd);
					if (ret) {
						dev_err(&func->dev,
							"Failed cmd52, set 0xf8 data (%d) ...\n",
							__LINE__);
						goto fail;
					}
				}
				if (!ret)
					break;
				flags >>= 1;
			}
			if (!ret)
				goto fail;
			for (i = sdio_priv->nint; i < MAX_NUM_INT; i++) {
				if (flags & 1)
					dev_err(&func->dev,
						"Unexpected interrupt cleared %d...\n",
						i);
				flags >>= 1;
			}
		}
	}

	vmm_ctl = 0;
	/* select VMM table 0 */
	if (val & SEL_VMM_TBL0)
		vmm_ctl |= BIT(0);
	/* select VMM table 1 */
	if (val & SEL_VMM_TBL1)
		vmm_ctl |= BIT(1);
	/* enable VMM */
	if (val & EN_VMM)
		vmm_ctl |= BIT(2);

	if (vmm_ctl) {
		struct sdio_cmd52 cmd;

		cmd.read_write = 1;
		cmd.function = 0;
		cmd.raw = 0;
		cmd.address = 0xf6;
		cmd.data = vmm_ctl;
		ret = wilc_sdio_cmd52(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd52, set 0xf6 data (%d) ...\n",
				__LINE__);
			goto fail;
		}
	}
	return 1;
fail:
	return 0;
}

static int sdio_sync_ext(struct wilc *wilc, int nint)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	u32 reg;

	if (nint > MAX_NUM_INT) {
		dev_err(&func->dev, "Too many interrupts (%d)...\n", nint);
		return 0;
	}
	if (nint > MAX_NUN_INT_THRPT_ENH2) {
		dev_err(&func->dev,
			"Cannot support more than 5 interrupts when has_thrpt_enh2=1.\n");
		return 0;
	}

	sdio_priv->nint = nint;

	/**
	 *      Disable power sequencer
	 **/
	if (!sdio_read_reg(wilc, WILC_MISC, &reg)) {
		dev_err(&func->dev, "Failed read misc reg...\n");
		return 0;
	}

	reg &= ~BIT(8);
	if (!sdio_write_reg(wilc, WILC_MISC, reg)) {
		dev_err(&func->dev, "Failed write misc reg...\n");
		return 0;
	}

	if (sdio_priv->irq_gpio) {
		u32 reg;
		int ret, i;

		/**
		 *      interrupt pin mux select
		 **/
		ret = sdio_read_reg(wilc, WILC_PIN_MUX_0, &reg);
		if (!ret) {
			dev_err(&func->dev, "Failed read reg (%08x)...\n",
				WILC_PIN_MUX_0);
			return 0;
		}
		reg |= BIT(8);
		ret = sdio_write_reg(wilc, WILC_PIN_MUX_0, reg);
		if (!ret) {
			dev_err(&func->dev, "Failed write reg (%08x)...\n",
				WILC_PIN_MUX_0);
			return 0;
		}

		/**
		 *      interrupt enable
		 **/
		ret = sdio_read_reg(wilc, WILC_INTR_ENABLE, &reg);
		if (!ret) {
			dev_err(&func->dev, "Failed read reg (%08x)...\n",
				WILC_INTR_ENABLE);
			return 0;
		}

		for (i = 0; (i < 5) && (nint > 0); i++, nint--)
			reg |= BIT((27 + i));
		ret = sdio_write_reg(wilc, WILC_INTR_ENABLE, reg);
		if (!ret) {
			dev_err(&func->dev, "Failed write reg (%08x)...\n",
				WILC_INTR_ENABLE);
			return 0;
		}
		if (nint) {
			ret = sdio_read_reg(wilc, WILC_INTR2_ENABLE, &reg);
			if (!ret) {
				dev_err(&func->dev,
					"Failed read reg (%08x)...\n",
					WILC_INTR2_ENABLE);
				return 0;
			}

			for (i = 0; (i < 3) && (nint > 0); i++, nint--)
				reg |= BIT(i);

			ret = sdio_read_reg(wilc, WILC_INTR2_ENABLE, &reg);
			if (!ret) {
				dev_err(&func->dev,
					"Failed write reg (%08x)...\n",
					WILC_INTR2_ENABLE);
				return 0;
			}
		}
	}
	return 1;
}

/* Global sdio HIF function table */
static const struct wilc_hif_func wilc_hif_sdio = {
	.hif_init = sdio_init,
	.hif_deinit = sdio_deinit,
	.hif_read_reg = sdio_read_reg,
	.hif_write_reg = sdio_write_reg,
	.hif_block_rx = sdio_read,
	.hif_block_tx = sdio_write,
	.hif_read_int = sdio_read_int,
	.hif_clear_int_ext = sdio_clear_int_ext,
	.hif_read_size = sdio_read_size,
	.hif_block_tx_ext = sdio_write,
	.hif_block_rx_ext = sdio_read,
	.hif_sync_ext = sdio_sync_ext,
	.enable_interrupt = wilc_sdio_enable_interrupt,
	.disable_interrupt = wilc_sdio_disable_interrupt,
};

