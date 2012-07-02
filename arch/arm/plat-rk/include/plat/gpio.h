#ifndef __PLAT_GPIO_H
#define __PLAT_GPIO_H

typedef enum eGPIOPinLevel
{
	GPIO_LOW=0,
	GPIO_HIGH
}eGPIOPinLevel_t;

typedef enum eGPIOPinDirection
{
	GPIO_IN=0,
	GPIO_OUT
}eGPIOPinDirection_t;

typedef enum GPIOPullType {
	PullDisable = 0,
	PullEnable,
	GPIONormal,  //PullEnable, please do not use it
	GPIOPullUp,	//PullEnable, please do not use it
	GPIOPullDown,//PullEnable, please do not use it
	GPIONOInit,//PullEnable, please do not use it
}eGPIOPullType_t;

typedef enum GPIOIntType {
	GPIOLevelLow=0,
	GPIOLevelHigh,	 
	GPIOEdgelFalling,
	GPIOEdgelRising
}eGPIOIntType_t;

#ifndef __ASSEMBLY__                                         

#include <asm/errno.h>
#include <asm-generic/gpio.h>		/* cansleep wrappers */

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep

#endif	/* __ASSEMBLY__ */

#endif
