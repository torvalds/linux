#ifndef LINUX_WLAN_COMMON_H
#define LINUX_WLAN_COMMON_H

enum debug_region {
	Init_debug = 0,
	COMP = 0xFFFFFFFF,
};

#define INIT_DBG                (1 << Init_debug)

#if defined(WILC_DEBUGFS)
extern atomic_t WILC_REGION;
extern atomic_t WILC_DEBUG_LEVEL;

#define DEBUG           BIT(0)
#define INFO            BIT(1)
#define WRN             BIT(2)
#define ERR             BIT(3)

#define PRINT_D(region, ...)						\
	do {								\
		if ((atomic_read(&WILC_DEBUG_LEVEL) & DEBUG) &&	\
		   ((atomic_read(&WILC_REGION)) & (region))) {	\
			printk("DBG [%s: %d]", __func__, __LINE__);	\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_INFO(region, ...)						\
	do {								\
		if ((atomic_read(&WILC_DEBUG_LEVEL) & INFO) &&	\
		   ((atomic_read(&WILC_REGION)) & (region))) {	\
			printk("INFO [%s]", __func__);			\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_WRN(region, ...)						\
	do {								\
		if ((atomic_read(&WILC_DEBUG_LEVEL) & WRN) &&	\
		   ((atomic_read(&WILC_REGION)) & (region))) {	\
			printk("WRN [%s: %d]", __func__, __LINE__);	\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_ER(...)							\
	do {								\
		if ((atomic_read(&WILC_DEBUG_LEVEL) & ERR)) {	\
			printk("ERR [%s: %d]", __func__, __LINE__);	\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#else

#define REGION  (INIT_DBG | GENERIC_DBG | CFG80211_DBG | FIRM_DBG | HOSTAPD_DBG)

#define DEBUG       1
#define INFO        0
#define WRN         0

#define PRINT_D(region, ...)						\
	do {								\
		if (DEBUG == 1 && ((REGION)&(region))) {		\
			printk("DBG [%s: %d]", __func__, __LINE__);	\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_INFO(region, ...)						\
	do {								\
		if (INFO == 1 && ((REGION)&(region))) {			\
			printk("INFO [%s]", __func__);			\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_WRN(region, ...)						\
	do {								\
		if (WRN == 1 && ((REGION)&(region))) {			\
			printk("WRN [%s: %d]", __func__, __LINE__);	\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_ER(...)							\
	do {								\
		printk("ERR [%s: %d]", __func__, __LINE__);		\
		printk(__VA_ARGS__);					\
	} while (0)

#endif

#define LINUX_RX_SIZE	(96 * 1024)
#define LINUX_TX_SIZE	(64 * 1024)


#define WILC_MULTICAST_TABLE_SIZE	8

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
	#define MODALIAS	"WILC_SPI"
	#define GPIO_NUM	0x44
#endif
#endif
