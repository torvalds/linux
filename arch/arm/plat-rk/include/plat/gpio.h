#ifndef __PLAT_GPIO_H
#define __PLAT_GPIO_H

/*
 * tp_int = <bank><goff><off><driving force><wake_en><irq_flags><reserve>
 * tp_rst = <bank><goff><off><driving force><active_low><pull_mode><reserve>
 * gpio = RKXX_PIN(bank)_P(goff)(off)
 * e.g.  bank=2, goff=A, off=3 ==>gpio is RKXX_PIN2_PA3
 */
enum {
        PULL_MODE_NONE = 0,
        PULL_MODE_DISABLE,
        PULL_MODE_ENABLE,
};
struct irq_config{
        unsigned int off:4,  //bit[3:0]
                     goff:4,
                     bank:4,
                     driving_force:4, 
                     wake_en:4,
                     irq_flags:4,
                     reserve:8;
};
struct gpio_config{
        unsigned int off:4, //bit[3:0]
                     goff:4,
                     bank:4,
                     driving_force:4,
                     active_low:4,
                     pull_mode:4, 
                     reserve:8;
};
struct port_config {
        union{
                struct irq_config irq;
                struct gpio_config io;
                unsigned int v;
        };
        int gpio;
};
static inline struct port_config get_port_config(unsigned int value)
{
        struct port_config port;

        port.v = value;
        port.gpio = PIN_BASE + port.io.bank * 32 + (port.io.goff - 0x0A) * 8 + port.io.off;

        return port;
}
void gpio_set_iomux(int gpio);
int port_output_init(unsigned int value, int on, char *name);
void port_output_on(unsigned int value);
void port_output_off(unsigned int value);
int port_input_init(unsigned int value, char *name);
int port_get_value(unsigned int value);
void port_deinit(unsigned int value);

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
