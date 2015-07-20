#include "wilc_wfi_netdevice.h"

#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/host.h>



#if defined (NM73131_0_BOARD)
#define SDIO_MODALIAS "wilc_sdio"
#else
#define SDIO_MODALIAS "wilc1000_sdio"
#endif

#if defined (NM73131_0_BOARD)
 #define MAX_SPEED 50000000
#elif defined(CUSTOMER_PLATFORM)
/* TODO : User have to stable bus clock as user's environment. */
 #ifdef MAX_BUS_SPEED
 #define MAX_SPEED MAX_BUS_SPEED
 #else
 #define MAX_SPEED 50000000
 #endif
#else
 #define MAX_SPEED (6 * 1000000) /* Max 50M */
#endif


struct sdio_func *local_sdio_func;
extern linux_wlan_t *g_linux_wlan;
extern int wilc_netdev_init(void);
extern int sdio_clear_int(void);
extern void wilc_handle_isr(void);

static unsigned int sdio_default_speed;

#define SDIO_VENDOR_ID_WILC 0x0296
#define SDIO_DEVICE_ID_WILC 0x5347

static const struct sdio_device_id wilc_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_WILC, SDIO_DEVICE_ID_WILC) },
};


static void wilc_sdio_interrupt(struct sdio_func *func)
{
#ifndef WILC_SDIO_IRQ_GPIO
	sdio_release_host(func);
	wilc_handle_isr();
	sdio_claim_host(func);
#endif
}


int linux_sdio_cmd52(sdio_cmd52_t *cmd)
{
	struct sdio_func *func = g_linux_wlan->wilc_sdio_func;
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
		PRINT_ER("wilc_sdio_cmd52..failed, err(%d)\n", ret);
		return 0;
	}
	return 1;
}


int linux_sdio_cmd53(sdio_cmd53_t *cmd)
{
	struct sdio_func *func = g_linux_wlan->wilc_sdio_func;
	int size, ret;

	sdio_claim_host(func);

	func->num = cmd->function;
	func->cur_blksize = cmd->block_size;
	if (cmd->block_mode)
		size = cmd->count * cmd->block_size;
	else
		size = cmd->count;

	if (cmd->read_write) {  /* write */
		ret = sdio_memcpy_toio(func, cmd->address, (void *)cmd->buffer, size);
	} else {        /* read */
		ret = sdio_memcpy_fromio(func, (void *)cmd->buffer, cmd->address,  size);
	}

	sdio_release_host(func);


	if (ret < 0) {
		PRINT_ER("wilc_sdio_cmd53..failed, err(%d)\n", ret);
		return 0;
	}

	return 1;
}

volatile int probe; /* COMPLEMENT_BOOT */
static int linux_sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
	PRINT_D(INIT_DBG, "probe function\n");

#ifdef COMPLEMENT_BOOT
	if (local_sdio_func != NULL) {
		local_sdio_func = func;
		probe = 1;
		PRINT_D(INIT_DBG, "local_sdio_func isn't NULL\n");
		return 0;
	}
#endif
	PRINT_D(INIT_DBG, "Initializing netdev\n");
	local_sdio_func = func;
	if (wilc_netdev_init()) {
		PRINT_ER("Couldn't initialize netdev\n");
		return -1;
	}

	printk("Driver Initializing success\n");
	return 0;
}

static void linux_sdio_remove(struct sdio_func *func)
{
	/**
	 *      TODO
	 **/

}

struct sdio_driver wilc_bus = {
	.name		= SDIO_MODALIAS,
	.id_table	= wilc_sdio_ids,
	.probe		= linux_sdio_probe,
	.remove		= linux_sdio_remove,
};

int enable_sdio_interrupt(void)
{
	int ret = 0;
#ifndef WILC_SDIO_IRQ_GPIO

	sdio_claim_host(local_sdio_func);
	ret = sdio_claim_irq(local_sdio_func, wilc_sdio_interrupt);
	sdio_release_host(local_sdio_func);

	if (ret < 0) {
		PRINT_ER("can't claim sdio_irq, err(%d)\n", ret);
		ret = -EIO;
	}
#endif
	return ret;
}

void disable_sdio_interrupt(void)
{

#ifndef WILC_SDIO_IRQ_GPIO
	int ret;

	PRINT_D(INIT_DBG, "disable_sdio_interrupt IN\n");

	sdio_claim_host(local_sdio_func);
	ret = sdio_release_irq(local_sdio_func);
	if (ret < 0) {
		PRINT_ER("can't release sdio_irq, err(%d)\n", ret);
	}
	sdio_release_host(local_sdio_func);

	PRINT_D(INIT_DBG, "disable_sdio_interrupt OUT\n");
#endif
}

static int linux_sdio_set_speed(int speed)
{
	struct mmc_ios ios;
	sdio_claim_host(local_sdio_func);

	memcpy((void *)&ios, (void *)&local_sdio_func->card->host->ios, sizeof(struct mmc_ios));
	local_sdio_func->card->host->ios.clock = speed;
	ios.clock = speed;
	local_sdio_func->card->host->ops->set_ios(local_sdio_func->card->host, &ios);
	sdio_release_host(local_sdio_func);
	PRINT_INFO(INIT_DBG, "@@@@@@@@@@@@ change SDIO speed to %d @@@@@@@@@\n", speed);

	return 1;
}

static int linux_sdio_get_speed(void)
{
	return local_sdio_func->card->host->ios.clock;
}

int linux_sdio_init(void *pv)
{

	/**
	 *      TODO :
	 **/


	sdio_default_speed = linux_sdio_get_speed();
	return 1;
}

void linux_sdio_deinit(void *pv)
{

	/**
	 *      TODO :
	 **/


	sdio_unregister_driver(&wilc_bus);
}

int linux_sdio_set_max_speed(void)
{
	return linux_sdio_set_speed(MAX_SPEED);
}

int linux_sdio_set_default_speed(void)
{
	return linux_sdio_set_speed(sdio_default_speed);
}



