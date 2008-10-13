#ifndef _H8300_GPIO_H
#define _H8300_GPIO_H

#define H8300_GPIO_P1 0
#define H8300_GPIO_P2 1
#define H8300_GPIO_P3 2
#define H8300_GPIO_P4 3
#define H8300_GPIO_P5 4
#define H8300_GPIO_P6 5
#define H8300_GPIO_P7 6
#define H8300_GPIO_P8 7
#define H8300_GPIO_P9 8
#define H8300_GPIO_PA 9
#define H8300_GPIO_PB 10
#define H8300_GPIO_PC 11
#define H8300_GPIO_PD 12
#define H8300_GPIO_PE 13
#define H8300_GPIO_PF 14
#define H8300_GPIO_PG 15
#define H8300_GPIO_PH 16

#define H8300_GPIO_B7 0x80
#define H8300_GPIO_B6 0x40
#define H8300_GPIO_B5 0x20
#define H8300_GPIO_B4 0x10
#define H8300_GPIO_B3 0x08
#define H8300_GPIO_B2 0x04
#define H8300_GPIO_B1 0x02
#define H8300_GPIO_B0 0x01

#define H8300_GPIO_INPUT 0
#define H8300_GPIO_OUTPUT 1

#define H8300_GPIO_RESERVE(port, bits) \
        h8300_reserved_gpio(port, bits)

#define H8300_GPIO_FREE(port, bits) \
        h8300_free_gpio(port, bits)

#define H8300_GPIO_DDR(port, bit, dir) \
        h8300_set_gpio_dir(((port) << 8) | (bit), dir)

#define H8300_GPIO_GETDIR(port, bit) \
        h8300_get_gpio_dir(((port) << 8) | (bit))

extern int h8300_reserved_gpio(int port, int bits);
extern int h8300_free_gpio(int port, int bits);
extern int h8300_set_gpio_dir(int port_bit, int dir);
extern int h8300_get_gpio_dir(int port_bit);
extern int h8300_init_gpio(void);

#endif
