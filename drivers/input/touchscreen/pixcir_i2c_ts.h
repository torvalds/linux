#ifndef __DRIVERS_TOUCHSCREEN_PIXCIR_TS_H
#define __DRIVERS_TOUCHSCREEN_PIXCIR_TS_H

// #include <mach/gpio.h>

static int attb_read_val(void);
static void tangoC_init(void);

#define X_MAX 480
#define Y_MAX 800
#define MAX_SUPPORT_POINT 5

#define IOMUX_NAME_SIZE 48
struct pixcir_platform_data {

	u16		model;			/*. */
	bool	swap_xy;		/* swap x and y axes */
	u16		x_min, x_max;
	u16		y_min, y_max;
    int 	gpio_reset;
    int     gpio_reset_active_low;
	int		gpio_pendown;		/* the GPIO used to decide the pendown */

	char	pendown_iomux_name[IOMUX_NAME_SIZE];
	char	resetpin_iomux_name[IOMUX_NAME_SIZE];
	int		pendown_iomux_mode;
	int		resetpin_iomux_mode;
	
	uint8_t                     virtual_key_num;
	uint16_t                   virtual_key_code[4];

	int	    (*get_pendown_state)(void);
};

//Platform gpio define
//#define	S5PC1XX

#ifdef S5PC1XX
	#include <plat/gpio-bank-e1.h> //reset pin GPE1_5
	#include <plat/gpio-bank-h1.h> //attb pin GPH1_3
	#include <mach/gpio.h>
	#include <plat/gpio-cfg.h>

	#define ATTB		S5PC1XX_GPH1(3)
	#define get_attb_value	gpio_get_value
	#define	RESETPIN_CFG	s3c_gpio_cfgpin(S5PC1XX_GPE1(5),S3C_GPIO_OUTPUT)
	#define	RESETPIN_SET0 	gpio_direction_output(S5PC1XX_GPE1(5),0)
	#define	RESETPIN_SET1	gpio_direction_output(S5PC1XX_GPE1(5),1)

#else	//mini6410

//	#include <plat/gpio-cfg.h>
//	#include <mach/gpio-bank-e.h>
//	#include <mach/gpio-bank-n.h>
//	#include <mach/gpio.h>

	#define ATTB		RK29_PIN4_PD5
	#define get_attb_value	gpio_get_value
	#define	RESETPIN_CFG	//s3c_gpio_cfgpin(RK29_PIN4_PD5,S3C_GPIO_OUTPUT)
	//rk29_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE_GPIO);
	#define	RESETPIN_SET0 	gpio_direction_output(RK29_PIN4_PD5,0)
	#define	RESETPIN_SET1	gpio_direction_output(RK29_PIN4_PD5,1)
#endif

static int attb_read_val(void)
{
	return gpio_get_value(RK29_PIN4_PD5);
}

/*static void tangoC_init(void)
{
	RESETPIN_SET0;
}*/
#endif
