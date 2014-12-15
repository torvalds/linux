#ifndef __MACH_MESON3_GPIO_H_
#define __MACH_MESON3_GPIO_H_
#include <linux/types.h>
#include <mach/pinmux.h>



/**
 * =================================================================================================
 */
/**
 * GPIO operation function
 */
typedef pinmux_item_t gpio_item_t;
typedef gpio_item_t gpio_set_t;
/**
 * @return 0, success , 
 * 		   SOMEPIN IS LOCKED, some pin is locked to the specail feature . You can not change it
 * 		   NOTAVAILABLE, not available .
 */
#define gpio_status_in    true
#define gpio_status_out   false
int32_t gpio_set_status(uint32_t pin,bool gpio_in);
/**
 * GPIO out function
 */
int32_t gpio_out(uint32_t pin,bool high);
static inline int32_t gpio_out_high(uint32_t pin)
{
	return gpio_out(pin ,true);
}
static inline int32_t gpio_out_low(uint32_t pin)
{
	return gpio_out(pin ,false);
}

	/**
	 * Multi pin operation
	 * @return 0, success , 
	 * 		   SOMEPIN IS LOCKED, some pin is locked to the specail feature . You can not change it
	 * 		   NOTAVAILABLE, not available .
	 * 
	 */

gpio_set_t * gpio_out_group_cacl(uint32_t pin,uint32_t bits, ... );
int32_t gpio_out_group_set(gpio_set_t*,uint32_t high_low );
/**
 * GPIO in function .ls
 */
int32_t gpio_in_get(uint32_t pin); ///one bit operation
	/**
	 * Multi pin operation
	 */
	/**
	 * Multi pin operation
	 * @return 0, success , 
	 * 		   SOMEPIN IS LOCKED, some pin is locked to the specail feature . You can not change it
	 * 		   NOTAVAILABLE, not available .
	 * 
	 */
gpio_set_t * gpio_in_group_cacl(uint32_t pin,uint32_t bits, ... );
typedef int64_t gpio_in_t;
typedef int64_t gpio_in_set_t;
gpio_in_t gpio_in_group(gpio_in_set_t *);

/**
 * GPIO interrupt interface
 */
#define  GPIO_IRQ(irq,type)  ((((irq)&7)<<2)|((type)&3))
enum {
	GPIO_IRQ_HIGH,
	GPIO_IRQ_LOW,
	GPIO_IRQ_RISING,
	GPIO_IRQ_FALLING
};

#define  GPIO_IRQ_DEFAULT_FILTER -1
typedef struct gpio_irq_s{
    int8_t    filter;
    uint8_t    irq;///
    uint16_t   pad;
}gpio_irq_t;
int32_t gpio_irq_set_lock(int32_t pad, uint32_t irq/*GPIO_IRQ(irq,type)*/,int32_t filter,bool lock);
void gpio_irq_enable(uint32_t irq);
static inline int32_t gpio_irq_set(int32_t pad, uint32_t irq/*GPIO_IRQ(irq,type)*/)
{
    int32_t ret;
    ret=gpio_irq_set_lock(pad,irq,-1,false);
    if(ret==0)
        gpio_irq_enable(irq);
    return ret;
}



#endif
// add by leo for cardreader mode
//#ifdef CONFIG_CARDREADER
#ifndef __MESON_GPIO_H__
#define  __MESON_GPIO_H__

#ifdef CONFIG_TCA6424
#include <linux/tca6424.h>
#define CONFIG_EXGPIO
#endif

#ifdef CONFIG_SN7325
#include <linux/sn7325.h>
#ifndef CONFIG_EXGPIO
#define CONFIG_EXGPIO
#endif
#endif

typedef enum gpio_bank {
    PREG_PAD_GPIO0 = 0,
    PREG_PAD_GPIO1,
    PREG_PAD_GPIO2,
    PREG_PAD_GPIO3,
    PREG_PAD_GPIO4,
    PREG_PAD_GPIO5,
	PREG_PAD_GPIOAO,
#ifdef CONFIG_EXGPIO
    EXGPIO_BANK0,
    EXGPIO_BANK1,
    EXGPIO_BANK2,
    EXGPIO_BANK3
#endif
} gpio_bank_t;


typedef enum gpio_mode {
    GPIO_OUTPUT_MODE,
    GPIO_INPUT_MODE,
} gpio_mode_t;

int set_gpio_mode(gpio_bank_t bank, int bit, gpio_mode_t mode);
gpio_mode_t get_gpio_mode(gpio_bank_t bank, int bit);

int set_gpio_val(gpio_bank_t bank, int bit, unsigned long val);
unsigned long  get_gpio_val(gpio_bank_t bank, int bit);

#define GPIOA_bank_bit0_27(bit)     (PREG_PAD_GPIO0)
#define GPIOA_bit_bit0_27(bit)      (bit)

#define GPIOB_bank_bit0_23(bit)     (PREG_PAD_GPIO1)
#define GPIOB_bit_bit0_23(bit)      (bit)

#define GPIOC_bank_bit0_15(bit)     (PREG_PAD_GPIO2)
#define GPIOC_bit_bit0_15(bit)      (bit)

#define GPIOAO_bank_bit0_11(bit)    (PREG_PAD_GPIOAO)
#define GPIOAO_bit_bit0_11(bit)     (bit)

#define GPIOD_bank_bit0_9(bit)      (PREG_PAD_GPIO2)
#define GPIOD_bit_bit0_9(bit)       (bit+16)

#define GPIOCARD_bank_bit0_8(bit)   (PREG_PAD_GPIO5)
#define GPIOCARD_bit_bit0_8(bit)    (bit+23)

#define GPIOBOOT_bank_bit0_17(bit)  (PREG_PAD_GPIO3)
#define GPIOBOOT_bit_bit0_17(bit)   (bit)

#define GPIOX_bank_bit0_31(bit)     (PREG_PAD_GPIO4)
#define GPIOX_bit_bit0_31(bit)      (bit)

#define GPIOX_bank_bit32_35(bit)    (PREG_PAD_GPIO3)
#define GPIOX_bit_bit32_35(bit)     (bit- 32 + 20)

#define GPIOY_bank_bit0_22(bit)     (PREG_PAD_GPIO5)
#define GPIOY_bit_bit0_22(bit)      (bit)

enum {
    GPIOY_IDX = 0,
    GPIOX_IDX = 23,
    GPIO_BOOT_IDX = 59,
    GPIOD_IDX = 77,
    GPIOC_IDX = 87,
    GPIO_CARD_IDX = 103,
    GPIOB_IDX = 112,
    GPIOA_IDX = 154,
    GPIOAO_IDX = 164,
};

extern int gpio_to_idx(unsigned gpio);

/**
 * enable gpio edge interrupt
 *
 * @param [in] pin  index number of the chip, start with 0 up to 255
 * @param [in] flag rising(0) or falling(1) edge
 * @param [in] group  this interrupt belong to which interrupt group  from 0 to 7
 */
extern void gpio_enable_edge_int(int pin , int flag, int group);
/**
 * enable gpio level interrupt
 *
 * @param [in] pin  index number of the chip, start with 0 up to 255
 * @param [in] flag high(0) or low(1) level
 * @param [in] group  this interrupt belong to which interrupt group  from 0 to 7
 */
extern void gpio_enable_level_int(int pin , int flag, int group);

/**
 * enable gpio interrupt filter
 *
 * @param [in] filter from 0~7(*222ns)
 * @param [in] group  this interrupt belong to which interrupt group  from 0 to 7
 */
int gpio_enable_irq(unsigned gpio, int irq, int irq_flag);
extern void gpio_enable_int_filter(int filter, int group);

extern int gpio_is_valid(int number);
extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);
extern int gpio_direction_input(unsigned gpio);
extern int gpio_direction_output(unsigned gpio, int value);
extern void gpio_set_value(unsigned gpio, int value);
extern int gpio_get_value(unsigned gpio);


#ifdef CONFIG_EXGPIO
#define MAX_EXGPIO_BANK 4

static inline unsigned char get_exgpio_port(gpio_bank_t bank)
{
    unsigned char port = bank - EXGPIO_BANK0;

    return (port >= MAX_EXGPIO_BANK) ? MAX_EXGPIO_BANK : port;
}

static inline int set_exgpio_mode(unsigned char port, int bit, gpio_mode_t mode)
{
    int bank_mode ;
    if (port == MAX_EXGPIO_BANK) {
        return -1;
    }

    bank_mode = get_configIO(port);

    bank_mode &= ~(1 << bit);
    bank_mode |= mode << bit;
    configIO(port, bank_mode);
    return 0;
}

static inline gpio_mode_t get_exgpio_mode(unsigned char port, int bit)
{
    if (port == MAX_EXGPIO_BANK) {
        return -1;
    }
    return (get_configIO(port) >> bit) & 1;
}

static inline int set_exgpio_val(unsigned char port, int bit, unsigned long val)
{
    if (port == MAX_EXGPIO_BANK) {
        return -1;
    }

#ifdef CONFIG_TCA6424
    int bank_val = getIO_level(port);

    bank_val &= ~(1 << bit);
    bank_val |= val << bit;
    setIO_level(port, bank_val);
#endif

#ifdef CONFIG_SN7325
    setIO_level(port, val, bit);
#endif
    return 0;
}

static inline unsigned long  get_exgpio_val(unsigned char port, int bit)
{
    if (port == MAX_EXGPIO_BANK) {
        return -1;
    }
#ifdef CONFIG_TCA6424
    return (getIO_level(port) >> bit) & 1;
#endif

#ifdef CONFIG_SN7325
    return getIObit_level(port, bit);
#endif
}
#endif

//#endif


#endif
