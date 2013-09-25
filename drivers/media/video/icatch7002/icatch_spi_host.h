#ifndef ICATCH_SPI_HOST_H
#define ICATCH_SPI_HOST_H
#include "../../../spi/rk29_spim.h"
#include <linux/spi/spi.h>

static struct rk29xx_spi_chip spi_icatch = {
	//.poll_mode = 1,
	.enable_dma = 0,
};
//user must define this struct according to hardware config	
static struct spi_board_info board_spi_icatch_devices[] = {	
	{
		.modalias  = "spi_icatch",
		.bus_num = 0,	//0 or 1
		.max_speed_hz  = 24*1000*1000,
		.chip_select   = 0, 
		.mode = SPI_MODE_0,
		.controller_data = &spi_icatch,
	},	
	
};
#endif

