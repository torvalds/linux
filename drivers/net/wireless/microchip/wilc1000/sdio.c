// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <linux/clk.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio.h>
#include <linux/of_irq.h>

#include "netdev.h"
#include "cfg80211.h"

#define SDIO_MODALIAS "wilc1000_sdio"

static const struct sdio_device_id wilc_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MICROCHIP_WILC, SDIO_DEVICE_ID_MICROCHIP_WILC1000) },
	{ },
};
MODULE_DEVICE_TABLE(sdio, wilc_sdio_ids);

#define WILC_SDIO_BLOCK_SIZE 512

static int wilc_sdio_init(struct wilc *wilc, bool resume);
static int wilc_sdio_deinit(struct wilc *wilc);

struct wilc_sdio {
	bool irq_gpio;
	u32 block_size;
	bool isinit;
	u8 *cmd53_buf;
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
	bool use_global_buf;
};

static const struct wilc_hif_func wilc_hif_sdio;

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
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	u8 *buf = cmd->buffer;

	sdio_claim_host(func);

	func->num = cmd->function;
	func->cur_blksize = cmd->block_size;
	if (cmd->block_mode)
		size = cmd->count * cmd->block_size;
	else
		size = cmd->count;

	if (cmd->use_global_buf) {
		if (size > sizeof(u32)) {
			ret = -EINVAL;
			goto out;
		}
		buf = sdio_priv->cmd53_buf;
	}

	if (cmd->read_write) {  /* write */
		if (cmd->use_global_buf)
			memcpy(buf, cmd->buffer, size);

		ret = sdio_memcpy_toio(func, cmd->address, buf, size);
	} else {        /* read */
		ret = sdio_memcpy_fromio(func, buf, cmd->address, size);

		if (cmd->use_global_buf)
			memcpy(cmd->buffer, buf, size);
	}
out:
	sdio_release_host(func);

	if (ret)
		dev_err(&func->dev, "%s..failed, err(%d)\n", __func__,  ret);

	return ret;
}

static int wilc_sdio_probe(struct sdio_func *func,
			   const struct sdio_device_id *id)
{
	struct wilc_sdio *sdio_priv;
	struct wilc_vif *vif;
	struct wilc *wilc;
	int ret;


	sdio_priv = kzalloc(sizeof(*sdio_priv), GFP_KERNEL);
	if (!sdio_priv)
		return -ENOMEM;

	sdio_priv->cmd53_buf = kzalloc(sizeof(u32), GFP_KERNEL);
	if (!sdio_priv->cmd53_buf) {
		ret = -ENOMEM;
		goto free;
	}

	ret = wilc_cfg80211_init(&wilc, &func->dev, WILC_HIF_SDIO,
				 &wilc_hif_sdio);
	if (ret)
		goto free;

	if (IS_ENABLED(CONFIG_WILC1000_HW_OOB_INTR)) {
		struct device_node *np = func->card->dev.of_node;
		int irq_num = of_irq_get(np, 0);

		if (irq_num > 0) {
			wilc->dev_irq_num = irq_num;
			sdio_priv->irq_gpio = true;
		}
	}

	sdio_set_drvdata(func, wilc);
	wilc->bus_data = sdio_priv;
	wilc->dev = &func->dev;

	wilc->rtc_clk = devm_clk_get_optional(&func->card->dev, "rtc");
	if (IS_ERR(wilc->rtc_clk)) {
		ret = PTR_ERR(wilc->rtc_clk);
		goto dispose_irq;
	}
	clk_prepare_enable(wilc->rtc_clk);

	wilc_sdio_init(wilc, false);

	ret = wilc_load_mac_from_nv(wilc);
	if (ret) {
		pr_err("Can not retrieve MAC address from chip\n");
		goto clk_disable_unprepare;
	}

	wilc_sdio_deinit(wilc);

	vif = wilc_netdev_ifc_init(wilc, "wlan%d", WILC_STATION_MODE,
				   NL80211_IFTYPE_STATION, false);
	if (IS_ERR(vif)) {
		ret = PTR_ERR(vif);
		goto clk_disable_unprepare;
	}

	dev_info(&func->dev, "Driver Initializing success\n");
	return 0;

clk_disable_unprepare:
	clk_disable_unprepare(wilc->rtc_clk);
dispose_irq:
	irq_dispose_mapping(wilc->dev_irq_num);
	wilc_netdev_cleanup(wilc);
free:
	kfree(sdio_priv->cmd53_buf);
	kfree(sdio_priv);
	return ret;
}

static void wilc_sdio_remove(struct sdio_func *func)
{
	struct wilc *wilc = sdio_get_drvdata(func);
	struct wilc_sdio *sdio_priv = wilc->bus_data;

	clk_disable_unprepare(wilc->rtc_clk);
	wilc_netdev_cleanup(wilc);
	kfree(sdio_priv->cmd53_buf);
	kfree(sdio_priv);
}

static int wilc_sdio_reset(struct wilc *wilc)
{
	struct sdio_cmd52 cmd;
	int ret;
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);

	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 0;
	cmd.address = SDIO_CCCR_ABORT;
	cmd.data = WILC_SDIO_CCCR_ABORT_RESET;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Fail cmd 52, reset cmd ...\n");
		return ret;
	}
	return 0;
}

static bool wilc_sdio_is_init(struct wilc *wilc)
{
	struct wilc_sdio *sdio_priv = wilc->bus_data;

	return sdio_priv->isinit;
}

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

static int wilc_sdio_set_func0_csa_address(struct wilc *wilc, u32 adr)
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
	cmd.address = WILC_SDIO_FBR_CSA_REG;
	cmd.data = (u8)adr;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set %04x data...\n",
			cmd.address);
		return ret;
	}

	cmd.address = WILC_SDIO_FBR_CSA_REG + 1;
	cmd.data = (u8)(adr >> 8);
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set %04x data...\n",
			cmd.address);
		return ret;
	}

	cmd.address = WILC_SDIO_FBR_CSA_REG + 2;
	cmd.data = (u8)(adr >> 16);
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set %04x data...\n",
			cmd.address);
		return ret;
	}

	return 0;
}

static int wilc_sdio_set_block_size(struct wilc *wilc, u8 func_num,
				    u32 block_size)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct sdio_cmd52 cmd;
	int ret;

	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 0;
	cmd.address = SDIO_FBR_BASE(func_num) + SDIO_CCCR_BLKSIZE;
	cmd.data = (u8)block_size;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set %04x data...\n",
			cmd.address);
		return ret;
	}

	cmd.address = SDIO_FBR_BASE(func_num) + SDIO_CCCR_BLKSIZE +  1;
	cmd.data = (u8)(block_size >> 8);
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed cmd52, set %04x data...\n",
			cmd.address);
		return ret;
	}

	return 0;
}

/********************************************
 *
 *      Sdio interfaces
 *
 ********************************************/
static int wilc_sdio_write_reg(struct wilc *wilc, u32 addr, u32 data)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	int ret;

	cpu_to_le32s(&data);

	if (addr >= 0xf0 && addr <= 0xff) { /* only vendor specific registers */
		struct sdio_cmd52 cmd;

		cmd.read_write = 1;
		cmd.function = 0;
		cmd.raw = 0;
		cmd.address = addr;
		cmd.data = data;
		ret = wilc_sdio_cmd52(wilc, &cmd);
		if (ret)
			dev_err(&func->dev,
				"Failed cmd 52, read reg (%08x) ...\n", addr);
	} else {
		struct sdio_cmd53 cmd;

		/**
		 *      set the AHB address
		 **/
		ret = wilc_sdio_set_func0_csa_address(wilc, addr);
		if (ret)
			return ret;

		cmd.read_write = 1;
		cmd.function = 0;
		cmd.address = WILC_SDIO_FBR_DATA_REG;
		cmd.block_mode = 0;
		cmd.increment = 1;
		cmd.count = sizeof(u32);
		cmd.buffer = (u8 *)&data;
		cmd.use_global_buf = true;
		cmd.block_size = sdio_priv->block_size;
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret)
			dev_err(&func->dev,
				"Failed cmd53, write reg (%08x)...\n", addr);
	}

	return ret;
}

static int wilc_sdio_write(struct wilc *wilc, u32 addr, u8 *buf, u32 size)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	u32 block_size = sdio_priv->block_size;
	struct sdio_cmd53 cmd;
	int nblk, nleft, ret;

	cmd.read_write = 1;
	if (addr > 0) {
		/**
		 *      func 0 access
		 **/
		cmd.function = 0;
		cmd.address = WILC_SDIO_FBR_DATA_REG;
	} else {
		/**
		 *      func 1 access
		 **/
		cmd.function = 1;
		cmd.address = WILC_SDIO_F1_DATA_REG;
	}

	size = ALIGN(size, 4);
	nblk = size / block_size;
	nleft = size % block_size;

	cmd.use_global_buf = false;
	if (nblk > 0) {
		cmd.block_mode = 1;
		cmd.increment = 1;
		cmd.count = nblk;
		cmd.buffer = buf;
		cmd.block_size = block_size;
		if (addr > 0) {
			ret = wilc_sdio_set_func0_csa_address(wilc, addr);
			if (ret)
				return ret;
		}
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53 [%x], block send...\n", addr);
			return ret;
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
			ret = wilc_sdio_set_func0_csa_address(wilc, addr);
			if (ret)
				return ret;
		}
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53 [%x], bytes send...\n", addr);
			return ret;
		}
	}

	return 0;
}

static int wilc_sdio_read_reg(struct wilc *wilc, u32 addr, u32 *data)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	int ret;

	if (addr >= 0xf0 && addr <= 0xff) { /* only vendor specific registers */
		struct sdio_cmd52 cmd;

		cmd.read_write = 0;
		cmd.function = 0;
		cmd.raw = 0;
		cmd.address = addr;
		ret = wilc_sdio_cmd52(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd 52, read reg (%08x) ...\n", addr);
			return ret;
		}
		*data = cmd.data;
	} else {
		struct sdio_cmd53 cmd;

		ret = wilc_sdio_set_func0_csa_address(wilc, addr);
		if (ret)
			return ret;

		cmd.read_write = 0;
		cmd.function = 0;
		cmd.address = WILC_SDIO_FBR_DATA_REG;
		cmd.block_mode = 0;
		cmd.increment = 1;
		cmd.count = sizeof(u32);
		cmd.buffer = (u8 *)data;
		cmd.use_global_buf = true;

		cmd.block_size = sdio_priv->block_size;
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53, read reg (%08x)...\n", addr);
			return ret;
		}
	}

	le32_to_cpus(data);
	return 0;
}

static int wilc_sdio_read(struct wilc *wilc, u32 addr, u8 *buf, u32 size)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	u32 block_size = sdio_priv->block_size;
	struct sdio_cmd53 cmd;
	int nblk, nleft, ret;

	cmd.read_write = 0;
	if (addr > 0) {
		/**
		 *      func 0 access
		 **/
		cmd.function = 0;
		cmd.address = WILC_SDIO_FBR_DATA_REG;
	} else {
		/**
		 *      func 1 access
		 **/
		cmd.function = 1;
		cmd.address = WILC_SDIO_F1_DATA_REG;
	}

	size = ALIGN(size, 4);
	nblk = size / block_size;
	nleft = size % block_size;

	cmd.use_global_buf = false;
	if (nblk > 0) {
		cmd.block_mode = 1;
		cmd.increment = 1;
		cmd.count = nblk;
		cmd.buffer = buf;
		cmd.block_size = block_size;
		if (addr > 0) {
			ret = wilc_sdio_set_func0_csa_address(wilc, addr);
			if (ret)
				return ret;
		}
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53 [%x], block read...\n", addr);
			return ret;
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
			ret = wilc_sdio_set_func0_csa_address(wilc, addr);
			if (ret)
				return ret;
		}
		ret = wilc_sdio_cmd53(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd53 [%x], bytes read...\n", addr);
			return ret;
		}
	}

	return 0;
}

/********************************************
 *
 *      Bus interfaces
 *
 ********************************************/

static int wilc_sdio_deinit(struct wilc *wilc)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	struct sdio_cmd52 cmd;
	int ret;

	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 1;

	/* Disable all functions interrupts */
	cmd.address = SDIO_CCCR_IENx;
	cmd.data = 0;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Failed to disable functions interrupts\n");
		return ret;
	}

	/* Disable all functions */
	cmd.address = SDIO_CCCR_IOEx;
	cmd.data = 0;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev,
			"Failed to reset all functions\n");
		return ret;
	}

	/* Disable CSA */
	cmd.read_write = 0;
	cmd.address = SDIO_FBR_BASE(1);
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev,
			"Failed to read CSA for function 1\n");
		return ret;
	}
	cmd.read_write = 1;
	cmd.address = SDIO_FBR_BASE(1);
	cmd.data &= ~SDIO_FBR_ENABLE_CSA;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev,
			"Failed to disable CSA for function 1\n");
		return ret;
	}

	sdio_priv->isinit = false;
	return 0;
}

static int wilc_sdio_init(struct wilc *wilc, bool resume)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	struct sdio_cmd52 cmd;
	int loop, ret;
	u32 chipid;

	/**
	 *      function 0 csa enable
	 **/
	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 1;
	cmd.address = SDIO_FBR_BASE(1);
	cmd.data = SDIO_FBR_ENABLE_CSA;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Fail cmd 52, enable csa...\n");
		return ret;
	}

	/**
	 *      function 0 block size
	 **/
	ret = wilc_sdio_set_block_size(wilc, 0, WILC_SDIO_BLOCK_SIZE);
	if (ret) {
		dev_err(&func->dev, "Fail cmd 52, set func 0 block size...\n");
		return ret;
	}
	sdio_priv->block_size = WILC_SDIO_BLOCK_SIZE;

	/**
	 *      enable func1 IO
	 **/
	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 1;
	cmd.address = SDIO_CCCR_IOEx;
	cmd.data = WILC_SDIO_CCCR_IO_EN_FUNC1;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev,
			"Fail cmd 52, set IOE register...\n");
		return ret;
	}

	/**
	 *      make sure func 1 is up
	 **/
	cmd.read_write = 0;
	cmd.function = 0;
	cmd.raw = 0;
	cmd.address = SDIO_CCCR_IORx;
	loop = 3;
	do {
		cmd.data = 0;
		ret = wilc_sdio_cmd52(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Fail cmd 52, get IOR register...\n");
			return ret;
		}
		if (cmd.data == WILC_SDIO_CCCR_IO_EN_FUNC1)
			break;
	} while (loop--);

	if (loop <= 0) {
		dev_err(&func->dev, "Fail func 1 is not ready...\n");
		return -EINVAL;
	}

	/**
	 *      func 1 is ready, set func 1 block size
	 **/
	ret = wilc_sdio_set_block_size(wilc, 1, WILC_SDIO_BLOCK_SIZE);
	if (ret) {
		dev_err(&func->dev, "Fail set func 1 block size...\n");
		return ret;
	}

	/**
	 *      func 1 interrupt enable
	 **/
	cmd.read_write = 1;
	cmd.function = 0;
	cmd.raw = 1;
	cmd.address = SDIO_CCCR_IENx;
	cmd.data = WILC_SDIO_CCCR_IEN_MASTER | WILC_SDIO_CCCR_IEN_FUNC1;
	ret = wilc_sdio_cmd52(wilc, &cmd);
	if (ret) {
		dev_err(&func->dev, "Fail cmd 52, set IEN register...\n");
		return ret;
	}

	/**
	 *      make sure can read back chip id correctly
	 **/
	if (!resume) {
		ret = wilc_sdio_read_reg(wilc, WILC_CHIPID, &chipid);
		if (ret) {
			dev_err(&func->dev, "Fail cmd read chip id...\n");
			return ret;
		}
		dev_err(&func->dev, "chipid (%08x)\n", chipid);
	}

	sdio_priv->isinit = true;
	return 0;
}

static int wilc_sdio_read_size(struct wilc *wilc, u32 *size)
{
	u32 tmp;
	struct sdio_cmd52 cmd;

	/**
	 *      Read DMA count in words
	 **/
	cmd.read_write = 0;
	cmd.function = 0;
	cmd.raw = 0;
	cmd.address = WILC_SDIO_INTERRUPT_DATA_SZ_REG;
	cmd.data = 0;
	wilc_sdio_cmd52(wilc, &cmd);
	tmp = cmd.data;

	cmd.address = WILC_SDIO_INTERRUPT_DATA_SZ_REG + 1;
	cmd.data = 0;
	wilc_sdio_cmd52(wilc, &cmd);
	tmp |= (cmd.data << 8);

	*size = tmp;
	return 0;
}

static int wilc_sdio_read_int(struct wilc *wilc, u32 *int_status)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	u32 tmp;
	u8 irq_flags;
	struct sdio_cmd52 cmd;

	wilc_sdio_read_size(wilc, &tmp);

	/**
	 *      Read IRQ flags
	 **/
	if (!sdio_priv->irq_gpio) {
		cmd.function = 1;
		cmd.address = WILC_SDIO_EXT_IRQ_FLAG_REG;
	} else {
		cmd.function = 0;
		cmd.address = WILC_SDIO_IRQ_FLAG_REG;
	}
	cmd.raw = 0;
	cmd.read_write = 0;
	cmd.data = 0;
	wilc_sdio_cmd52(wilc, &cmd);
	irq_flags = cmd.data;
	tmp |= FIELD_PREP(IRG_FLAGS_MASK, cmd.data);

	if (FIELD_GET(UNHANDLED_IRQ_MASK, irq_flags))
		dev_err(&func->dev, "Unexpected interrupt (1) int=%lx\n",
			FIELD_GET(UNHANDLED_IRQ_MASK, irq_flags));

	*int_status = tmp;

	return 0;
}

static int wilc_sdio_clear_int_ext(struct wilc *wilc, u32 val)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;
	int ret;
	u32 reg = 0;

	if (sdio_priv->irq_gpio)
		reg = val & (BIT(MAX_NUM_INT) - 1);

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
		cmd.address = WILC_SDIO_IRQ_CLEAR_FLAG_REG;
		cmd.data = reg;

		ret = wilc_sdio_cmd52(wilc, &cmd);
		if (ret) {
			dev_err(&func->dev,
				"Failed cmd52, set (%02x) data (%d) ...\n",
				cmd.address, __LINE__);
			return ret;
		}
	}
	return 0;
}

static int wilc_sdio_sync_ext(struct wilc *wilc, int nint)
{
	struct sdio_func *func = dev_to_sdio_func(wilc->dev);
	struct wilc_sdio *sdio_priv = wilc->bus_data;

	if (nint > MAX_NUM_INT) {
		dev_err(&func->dev, "Too many interrupts (%d)...\n", nint);
		return -EINVAL;
	}

	if (sdio_priv->irq_gpio) {
		u32 reg;
		int ret, i;

		/**
		 *      interrupt pin mux select
		 **/
		ret = wilc_sdio_read_reg(wilc, WILC_PIN_MUX_0, &reg);
		if (ret) {
			dev_err(&func->dev, "Failed read reg (%08x)...\n",
				WILC_PIN_MUX_0);
			return ret;
		}
		reg |= BIT(8);
		ret = wilc_sdio_write_reg(wilc, WILC_PIN_MUX_0, reg);
		if (ret) {
			dev_err(&func->dev, "Failed write reg (%08x)...\n",
				WILC_PIN_MUX_0);
			return ret;
		}

		/**
		 *      interrupt enable
		 **/
		ret = wilc_sdio_read_reg(wilc, WILC_INTR_ENABLE, &reg);
		if (ret) {
			dev_err(&func->dev, "Failed read reg (%08x)...\n",
				WILC_INTR_ENABLE);
			return ret;
		}

		for (i = 0; (i < 5) && (nint > 0); i++, nint--)
			reg |= BIT((27 + i));
		ret = wilc_sdio_write_reg(wilc, WILC_INTR_ENABLE, reg);
		if (ret) {
			dev_err(&func->dev, "Failed write reg (%08x)...\n",
				WILC_INTR_ENABLE);
			return ret;
		}
		if (nint) {
			ret = wilc_sdio_read_reg(wilc, WILC_INTR2_ENABLE, &reg);
			if (ret) {
				dev_err(&func->dev,
					"Failed read reg (%08x)...\n",
					WILC_INTR2_ENABLE);
				return ret;
			}

			for (i = 0; (i < 3) && (nint > 0); i++, nint--)
				reg |= BIT(i);

			ret = wilc_sdio_write_reg(wilc, WILC_INTR2_ENABLE, reg);
			if (ret) {
				dev_err(&func->dev,
					"Failed write reg (%08x)...\n",
					WILC_INTR2_ENABLE);
				return ret;
			}
		}
	}
	return 0;
}

/* Global sdio HIF function table */
static const struct wilc_hif_func wilc_hif_sdio = {
	.hif_init = wilc_sdio_init,
	.hif_deinit = wilc_sdio_deinit,
	.hif_read_reg = wilc_sdio_read_reg,
	.hif_write_reg = wilc_sdio_write_reg,
	.hif_block_rx = wilc_sdio_read,
	.hif_block_tx = wilc_sdio_write,
	.hif_read_int = wilc_sdio_read_int,
	.hif_clear_int_ext = wilc_sdio_clear_int_ext,
	.hif_read_size = wilc_sdio_read_size,
	.hif_block_tx_ext = wilc_sdio_write,
	.hif_block_rx_ext = wilc_sdio_read,
	.hif_sync_ext = wilc_sdio_sync_ext,
	.enable_interrupt = wilc_sdio_enable_interrupt,
	.disable_interrupt = wilc_sdio_disable_interrupt,
	.hif_reset = wilc_sdio_reset,
	.hif_is_init = wilc_sdio_is_init,
};

static int wilc_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct wilc *wilc = sdio_get_drvdata(func);
	int ret;

	dev_info(dev, "sdio suspend\n");

	if (!IS_ERR(wilc->rtc_clk))
		clk_disable_unprepare(wilc->rtc_clk);

	host_sleep_notify(wilc);

	wilc_sdio_disable_interrupt(wilc);

	ret = wilc_sdio_reset(wilc);
	if (ret) {
		dev_err(&func->dev, "Fail reset sdio\n");
		return ret;
	}

	return 0;
}

static int wilc_sdio_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct wilc *wilc = sdio_get_drvdata(func);

	dev_info(dev, "sdio resume\n");
	wilc_sdio_init(wilc, true);
	wilc_sdio_enable_interrupt(wilc);

	host_wakeup_notify(wilc);

	return 0;
}

static const struct of_device_id wilc_of_match[] = {
	{ .compatible = "microchip,wilc1000", },
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
	.probe		= wilc_sdio_probe,
	.remove		= wilc_sdio_remove,
	.drv = {
		.pm = &wilc_sdio_pm_ops,
		.of_match_table = wilc_of_match,
	}
};
module_sdio_driver(wilc_sdio_driver);

MODULE_DESCRIPTION("Atmel WILC1000 SDIO wireless driver");
MODULE_LICENSE("GPL");
