/*
 * wifi_power.c for MID_AIGO_E700.
 *
 * Power control for WIFI module.
 *
 * There are Power supply and Power Up/Down controls for WIFI typically.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

#include "wifi_power.h"

#if (WIFI_GPIO_POWER_CONTROL == 1)

/*
 * GPIO to control LDO/DCDC.
 *
 * 用于控制WIFI的电源，通常是3.3V和1.8V，可能1.2V也在其中。
 *
 * 如果是扩展IO，请参考下面的例子:
 *   POWER_USE_EXT_GPIO, 0, 0, 0, PCA9554_Pin1, GPIO_HIGH
 */
struct wifi_power power_gpio = 
{
	POWER_USE_GPIO, POWER_GPIO_IOMUX, GPIOB1_SMCS1_MMC0PCA_NAME,
    IOMUXA_GPIO0_B1, GPIOPortB_Pin1, GPIO_HIGH
};

/*
 * GPIO to control WIFI PowerDOWN/RESET.
 *
 * 控制WIFI的PowerDown脚。有些模组PowerDown脚是和Reset脚短接在一起。
 */
struct wifi_power power_save_gpio = 
{
	POWER_USE_GPIO, POWER_GPIO_IOMUX, GPIOG0_UART0_MMC1DET_NAME,
	IOMUXA_GPIO1_C0, GPIOPortG_Pin0, GPIO_HIGH
};

/*
 * GPIO to reset WIFI. Keep this as NULL normally.
 *
 * 控制WIFI的Reset脚，通常WiFi模组没有用到这个引脚。
 */
struct wifi_power power_reset_gpio = 
{
	0, 0, 0, 0, 0, 0
};

/*
 * If external GPIO chip such as PCA9554 is being used, please
 * implement the following 2 function.
 *
 * id:   is GPIO identifier, such as GPIOPortF_Pin0, or external 
 *       name defined in struct wifi_power.
 * sens: the value should be set to GPIO, usually is GPIO_HIGH or GPIO_LOW.
 *
 * 如果有用扩展GPIO来控制WIFI，请实现下面的函数:
 * 函数的功能是：控制指定的IO口id，使其状态切换为要求的sens状态。
 * id  : 是IO的标识号，以整数的形式标识。
 * sens: 是要求的IO状态，为高或低。
 */
void wifi_extgpio_operation(u8 id, u8 sens)
{
	//pca955x_gpio_direction_output(id, sens);
}

/*
 * 在系统中如果要调用WIFI的IO控制，将WIFI下电，可以调用如下接口：
 *   void rockchip_wifi_shutdown();
 * 但注意需要在宏WIFI_GPIO_POWER_CONTROL的控制下。
 */

#endif /* WIFI_GPIO_POWER_CONTROL */

