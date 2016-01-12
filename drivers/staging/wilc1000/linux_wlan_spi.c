#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/spi/spi.h>

#include "linux_wlan_common.h"
#include "linux_wlan_spi.h"

#define USE_SPI_DMA     0       /* johnny add */

#ifdef WILC_ASIC_A0
 #if defined(PLAT_PANDA_ES_OMAP4460)
  #define MIN_SPEED 12000000
  #define MAX_SPEED 24000000
 #elif defined(PLAT_WMS8304)
  #define MIN_SPEED 12000000
  #define MAX_SPEED 24000000 /* 4000000 */
 #elif defined(CUSTOMER_PLATFORM)
/*
  TODO : define Clock speed under 48M.
 *
 * ex)
 * #define MIN_SPEED 24000000
 * #define MAX_SPEED 48000000
 */
 #else
  #define MIN_SPEED 24000000
  #define MAX_SPEED 48000000
 #endif
#else /* WILC_ASIC_A0 */
/* Limit clk to 6MHz on FPGA. */
 #define MIN_SPEED 6000000
 #define MAX_SPEED 6000000
#endif /* WILC_ASIC_A0 */

static u32 SPEED = MIN_SPEED;

struct spi_device *wilc_spi_dev;
void linux_spi_deinit(void *vp);

static int __init wilc_bus_probe(struct spi_device *spi)
{

	PRINT_D(BUS_DBG, "spiModalias: %s\n", spi->modalias);
	PRINT_D(BUS_DBG, "spiMax-Speed: %d\n", spi->max_speed_hz);
	wilc_spi_dev = spi;

	printk("Driver Initializing success\n");
	return 0;
}

static int __exit wilc_bus_remove(struct spi_device *spi)
{

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id wilc1000_of_match[] = {
	{ .compatible = "atmel,wilc_spi", },
	{}
};
MODULE_DEVICE_TABLE(of, wilc1000_of_match);
#endif

struct spi_driver wilc_bus __refdata = {
	.driver = {
		.name = MODALIAS,
#ifdef CONFIG_OF
		.of_match_table = wilc1000_of_match,
#endif
	},
	.probe =  wilc_bus_probe,
	.remove = __exit_p(wilc_bus_remove),
};


void linux_spi_deinit(void *vp)
{

	spi_unregister_driver(&wilc_bus);

	SPEED = MIN_SPEED;
	PRINT_ER("@@@@@@@@@@@@ restore SPI speed to %d @@@@@@@@@\n", SPEED);

}



int linux_spi_init(void *vp)
{
	int ret = 1;
	static int called;


	if (called == 0) {
		called++;
		ret = spi_register_driver(&wilc_bus);
	}

	/* change return value to match WILC interface */
	(ret < 0) ? (ret = 0) : (ret = 1);

	return ret;
}

#if defined(PLAT_WMS8304)
#define TXRX_PHASE_SIZE (4096)
#endif

#if defined(TXRX_PHASE_SIZE)

int linux_spi_write(u8 *b, u32 len)
{
	int ret;

	if (len > 0 && b != NULL) {
		int i = 0;
		int blk = len / TXRX_PHASE_SIZE;
		int remainder = len % TXRX_PHASE_SIZE;

		char *r_buffer = kzalloc(TXRX_PHASE_SIZE, GFP_KERNEL);
		if (!r_buffer)
			return -ENOMEM;

		if (blk) {
			while (i < blk)	{
				struct spi_message msg;
				struct spi_transfer tr = {
					.tx_buf = b + (i * TXRX_PHASE_SIZE),
					.len = TXRX_PHASE_SIZE,
					.speed_hz = SPEED,
					.bits_per_word = 8,
					.delay_usecs = 0,
				};

				tr.rx_buf = r_buffer;

				memset(&msg, 0, sizeof(msg));
				spi_message_init(&msg);
				msg.spi = wilc_spi_dev;
				msg.is_dma_mapped = USE_SPI_DMA;

				spi_message_add_tail(&tr, &msg);
				ret = spi_sync(wilc_spi_dev, &msg);
				if (ret < 0) {
					PRINT_ER("SPI transaction failed\n");
				}
				i++;

			}
		}
		if (remainder) {
			struct spi_message msg;
			struct spi_transfer tr = {
				.tx_buf = b + (blk * TXRX_PHASE_SIZE),
				.len = remainder,
				.speed_hz = SPEED,
				.bits_per_word = 8,
				.delay_usecs = 0,
			};
			tr.rx_buf = r_buffer;

			memset(&msg, 0, sizeof(msg));
			spi_message_init(&msg);
			msg.spi = wilc_spi_dev;
			msg.is_dma_mapped = USE_SPI_DMA;                                /* rachel */

			spi_message_add_tail(&tr, &msg);
			ret = spi_sync(wilc_spi_dev, &msg);
			if (ret < 0) {
				PRINT_ER("SPI transaction failed\n");
			}
		}
		kfree(r_buffer);
	} else {
		PRINT_ER("can't write data with the following length: %d\n", len);
		PRINT_ER("FAILED due to NULL buffer or ZERO length check the following length: %d\n", len);
		ret = -1;
	}

	/* change return value to match WILC interface */
	(ret < 0) ? (ret = 0) : (ret = 1);

	return ret;

}

#else
int linux_spi_write(u8 *b, u32 len)
{

	int ret;
	struct spi_message msg;

	if (len > 0 && b != NULL) {
		struct spi_transfer tr = {
			.tx_buf = b,
			.len = len,
			.speed_hz = SPEED,
			.delay_usecs = 0,
		};
		char *r_buffer = kzalloc(len, GFP_KERNEL);
		if (!r_buffer)
			return -ENOMEM;

		tr.rx_buf = r_buffer;
		PRINT_D(BUS_DBG, "Request writing %d bytes\n", len);

		memset(&msg, 0, sizeof(msg));
		spi_message_init(&msg);
/* [[johnny add */
		msg.spi = wilc_spi_dev;
		msg.is_dma_mapped = USE_SPI_DMA;
/* ]] */
		spi_message_add_tail(&tr, &msg);

		ret = spi_sync(wilc_spi_dev, &msg);
		if (ret < 0) {
			PRINT_ER("SPI transaction failed\n");
		}

		kfree(r_buffer);
	} else {
		PRINT_ER("can't write data with the following length: %d\n", len);
		PRINT_ER("FAILED due to NULL buffer or ZERO length check the following length: %d\n", len);
		ret = -1;
	}

	/* change return value to match WILC interface */
	(ret < 0) ? (ret = 0) : (ret = 1);


	return ret;
}

#endif

#if defined(TXRX_PHASE_SIZE)

int linux_spi_read(u8 *rb, u32 rlen)
{
	int ret;

	if (rlen > 0) {
		int i = 0;

		int blk = rlen / TXRX_PHASE_SIZE;
		int remainder = rlen % TXRX_PHASE_SIZE;

		char *t_buffer = kzalloc(TXRX_PHASE_SIZE, GFP_KERNEL);
		if (!t_buffer)
			return -ENOMEM;

		if (blk) {
			while (i < blk)	{
				struct spi_message msg;
				struct spi_transfer tr = {
					.rx_buf = rb + (i * TXRX_PHASE_SIZE),
					.len = TXRX_PHASE_SIZE,
					.speed_hz = SPEED,
					.bits_per_word = 8,
					.delay_usecs = 0,
				};
				tr.tx_buf = t_buffer;

				memset(&msg, 0, sizeof(msg));
				spi_message_init(&msg);
				msg.spi = wilc_spi_dev;
				msg.is_dma_mapped = USE_SPI_DMA;

				spi_message_add_tail(&tr, &msg);
				ret = spi_sync(wilc_spi_dev, &msg);
				if (ret < 0) {
					PRINT_ER("SPI transaction failed\n");
				}
				i++;
			}
		}
		if (remainder) {
			struct spi_message msg;
			struct spi_transfer tr = {
				.rx_buf = rb + (blk * TXRX_PHASE_SIZE),
				.len = remainder,
				.speed_hz = SPEED,
				.bits_per_word = 8,
				.delay_usecs = 0,
			};
			tr.tx_buf = t_buffer;

			memset(&msg, 0, sizeof(msg));
			spi_message_init(&msg);
			msg.spi = wilc_spi_dev;
			msg.is_dma_mapped = USE_SPI_DMA;                                /* rachel */

			spi_message_add_tail(&tr, &msg);
			ret = spi_sync(wilc_spi_dev, &msg);
			if (ret < 0) {
				PRINT_ER("SPI transaction failed\n");
			}
		}

		kfree(t_buffer);
	} else {
		PRINT_ER("can't read data with the following length: %u\n", rlen);
		ret = -1;
	}
	/* change return value to match WILC interface */
	(ret < 0) ? (ret = 0) : (ret = 1);

	return ret;
}

#else
int linux_spi_read(u8 *rb, u32 rlen)
{

	int ret;

	if (rlen > 0) {
		struct spi_message msg;
		struct spi_transfer tr = {
			.rx_buf = rb,
			.len = rlen,
			.speed_hz = SPEED,
			.delay_usecs = 0,

		};
		char *t_buffer = kzalloc(rlen, GFP_KERNEL);
		if (!t_buffer)
			return -ENOMEM;

		tr.tx_buf = t_buffer;

		memset(&msg, 0, sizeof(msg));
		spi_message_init(&msg);
/* [[ johnny add */
		msg.spi = wilc_spi_dev;
		msg.is_dma_mapped = USE_SPI_DMA;
/* ]] */
		spi_message_add_tail(&tr, &msg);

		ret = spi_sync(wilc_spi_dev, &msg);
		if (ret < 0) {
			PRINT_ER("SPI transaction failed\n");
		}
		kfree(t_buffer);
	} else {
		PRINT_ER("can't read data with the following length: %u\n", rlen);
		ret = -1;
	}
	/* change return value to match WILC interface */
	(ret < 0) ? (ret = 0) : (ret = 1);

	return ret;
}

#endif

int linux_spi_write_read(u8 *wb, u8 *rb, u32 rlen)
{

	int ret;

	if (rlen > 0) {
		struct spi_message msg;
		struct spi_transfer tr = {
			.rx_buf = rb,
			.tx_buf = wb,
			.len = rlen,
			.speed_hz = SPEED,
			.bits_per_word = 8,
			.delay_usecs = 0,

		};

		memset(&msg, 0, sizeof(msg));
		spi_message_init(&msg);
		msg.spi = wilc_spi_dev;
		msg.is_dma_mapped = USE_SPI_DMA;

		spi_message_add_tail(&tr, &msg);
		ret = spi_sync(wilc_spi_dev, &msg);
		if (ret < 0) {
			PRINT_ER("SPI transaction failed\n");
		}
	} else {
		PRINT_ER("can't read data with the following length: %u\n", rlen);
		ret = -1;
	}
	/* change return value to match WILC interface */
	(ret < 0) ? (ret = 0) : (ret = 1);

	return ret;
}

int linux_spi_set_max_speed(void)
{
	SPEED = MAX_SPEED;

	PRINT_INFO(BUS_DBG, "@@@@@@@@@@@@ change SPI speed to %d @@@@@@@@@\n", SPEED);
	return 1;
}
