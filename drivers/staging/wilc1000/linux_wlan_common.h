#ifndef LINUX_WLAN_COMMON_H
#define LINUX_WLAN_COMMON_H

#if defined(BEAGLE_BOARD)
	#define SPI_CHANNEL	4

	#if SPI_CHANNEL == 4
		#define MODALIAS	"wilc_spi4"
		#define GPIO_NUM	162
	#else
		#define MODALIAS	"wilc_spi3"
		#define GPIO_NUM	133
	#endif
#elif defined(PLAT_WMS8304)             /* rachel */
	#define MODALIAS	"wilc_spi"
	#define GPIO_NUM	139
#elif defined(PLAT_RKXXXX)
 #define MODALIAS	"WILC_IRQ"
 #define GPIO_NUM	RK30_PIN3_PD2 /* RK30_PIN3_PA1 */
/* RK30_PIN3_PD2 */
/* RK2928_PIN1_PA7 */

#elif defined(CUSTOMER_PLATFORM)
/*
 TODO : specify MODALIAS name and GPIO number. This is certainly necessary for SPI interface.
 *
 * ex)
 * #define MODALIAS  "WILC_SPI"
 * #define GPIO_NUM  139
 */

#else
/* base on SAMA5D3_Xplained Board */
#endif
#endif
