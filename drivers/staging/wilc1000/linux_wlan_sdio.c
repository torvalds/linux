#include "wilc_wfi_netdevice.h"

#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/host.h>
#include <linux/of_gpio.h>

#include "linux_wlan_sdio.h"

#define SDIO_MODALIAS "wilc1000_sdio"

#define SDIO_VENDOR_ID_WILC 0x0296
#define SDIO_DEVICE_ID_WILC 0x5347

static const struct sdio_device_id wilc_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_WILC, SDIO_DEVICE_ID_WILC) },
	{ },
};


static void wilc_sdio_interrupt(struct sdio_func *func)
{
	sdio_release_host(func);
	wilc_handle_isr(sdio_get_drvdata(func));
	sdio_claim_host(func);
}

int wilc_sdio_cmd52(struct wilc *wilc, sdio_cmd52_t *cmd)
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

	if (ret < 0) {
		dev_err(&func->dev, "wilc_sdio_cmd52..failed, err(%d)\n", ret);
		return 0;
	}
	return 1;
}


int wilc_sdio_cmd53(struct wilc *wilc, sdio_cmd53_t *cmd)
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


	if (ret < 0) {
		dev_err(&func->dev, "wilc_sdio_cmd53..failed, err(%d)\n", ret);
		return 0;
	}

	return 1;
}

static int linux_sdio_probe(struct sdio_func *func,
			    const struct sdio_device_id *id)
{
	struct wilc *wilc;
	int gpio;

	gpio = -1;
	if (IS_ENABLED(CONFIG_WILC1000_HW_OOB_INTR)) {
		gpio = of_get_gpio(func->dev.of_node, 0);
		if (gpio < 0)
			gpio = GPIO_NUM;
	}

	dev_dbg(&func->dev, "Initializing netdev\n");
	if (wilc_netdev_init(&wilc, &func->dev, HIF_SDIO, gpio,
			     &wilc_hif_sdio)) {
		dev_err(&func->dev, "Couldn't initialize netdev\n");
		return -1;
	}
	sdio_set_drvdata(func, wilc);
	wilc->dev = &func->dev;

	dev_info(&func->dev, "Driver Initializing success\n");
	return 0;
}

static void linux_sdio_remove(struct sdio_func *func)
{
	wilc_netdev_cleanup(sdio_get_drvdata(func));
}

static struct sdio_driver wilc1000_sdio_driver = {
	.name		= SDIO_MODALIAS,
	.id_table	= wilc_sdio_ids,
	.probe		= linux_sdio_probe,
	.remove		= linux_sdio_remove,
};
module_driver(wilc1000_sdio_driver,
	      sdio_register_driver,
	      sdio_unregister_driver);
MODULE_LICENSE("GPL");

int wilc_sdio_enable_interrupt(struct wilc *dev)
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

void wilc_sdio_disable_interrupt(struct wilc *dev)
{
	struct sdio_func *func = container_of(dev->dev, struct sdio_func, dev);
	int ret;

	dev_dbg(&func->dev, "wilc_sdio_disable_interrupt IN\n");

	sdio_claim_host(func);
	ret = sdio_release_irq(func);
	if (ret < 0) {
		dev_err(&func->dev, "can't release sdio_irq, err(%d)\n", ret);
	}
	sdio_release_host(func);

	dev_info(&func->dev, "wilc_sdio_disable_interrupt OUT\n");
}

int wilc_sdio_init(void)
{
	return 1;
}

MODULE_LICENSE("GPL");
