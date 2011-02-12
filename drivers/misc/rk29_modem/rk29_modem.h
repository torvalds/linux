#ifndef _rk29_MODEM_H
#define _rk29_MODEM_H

/* Modem states */
#define MODEM_DISABLE       0
#define MODEM_ENABLE        1
#define MODEM_SLEEP         2
#define MODEM_MAX_STATUS    3

/*===================lintao@rock-chips====================*/
#define G3_POWER_ON					RK29_PIN6_PB1//GPIOPortB_Pin0
//#define G3_POWER_ON_IOMUX_NAME		GPIOB0_SPI0CSN1_MMC1PCA_NAME
//#define G3_POWER_ON_IOMUX_MODE		IOMUXA_GPIO0_B0
#define G3_POWER_ENABLE				GPIO_HIGH
#define G3_POWER_DISABLE				GPIO_LOW
/*===================================================*/
//#define G3_RADIO_ON_OFF                           		GPIOPortG_Pin0
//#define G3_RADIO_ON_OFF_IOMUX_NAME       GPIOG0_UART0_MMC1DET_NAME
//#define G3_RADIO_ON_OFF_IOMUX_MODE	IOMUXA_GPIO1_C0
//#define G3_RADIO_ENABLE				GPIO_HIGH
//#define G3_RADIO_DISABLE				GPIO_LOW
/*====================================================*/
//#define G3_RESET						GPIOPortG_Pin1
//#define G3_RESET_IOMUX_NAME			GPIOG1_UART0_MMC1WPT_NAME
//#define G3_RESET_IOMUX_MODE			IOMUXA_GPIO1_C1
//#define G3_RESET_ENABLE				GPIO_LOW
//#define G3_RESET_DISABLE				GPIO_HIGH
/*====================================================*/
struct rk29_modem_t {
	char *name;
	int cur_mode;
	int (*enable)(void);
	int (*disable)(void);
	int (*sleep)(void);
	int (*init)(void);
};

int rk29_modem_register(struct rk29_modem_t *rk29_modem);
void rk29_modem_unregister(struct rk29_modem_t *rk29_modem);

#endif
