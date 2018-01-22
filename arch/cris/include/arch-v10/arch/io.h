/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARCH_CRIS_IO_H
#define _ASM_ARCH_CRIS_IO_H

/* Etrax shadow registers - which live in arch/cris/kernel/shadows.c */

extern unsigned long gen_config_ii_shadow;
extern unsigned long port_g_data_shadow;
extern unsigned char port_pa_dir_shadow;
extern unsigned char port_pa_data_shadow;
extern unsigned char port_pb_i2c_shadow;
extern unsigned char port_pb_config_shadow;
extern unsigned char port_pb_dir_shadow;
extern unsigned char port_pb_data_shadow;
extern unsigned long r_timer_ctrl_shadow;

extern unsigned long port_cse1_shadow;
extern unsigned long port_csp0_shadow;
extern unsigned long port_csp4_shadow;

extern volatile unsigned long *port_cse1_addr;
extern volatile unsigned long *port_csp0_addr;
extern volatile unsigned long *port_csp4_addr;

/* macro for setting regs through a shadow -
 * r = register name (like R_PORT_PA_DATA)
 * s = shadow name (like port_pa_data_shadow)
 * b = bit number
 * v = value (0 or 1)
 */

#define REG_SHADOW_SET(r,s,b,v) *r = s = (s & ~(1 << (b))) | ((v) << (b))

/* The LED's on various Etrax-based products are set differently. */

#if defined(CONFIG_ETRAX_NO_LEDS)
#undef CONFIG_ETRAX_PA_LEDS
#undef CONFIG_ETRAX_PB_LEDS
#undef CONFIG_ETRAX_CSP0_LEDS
#define CRIS_LED_NETWORK_SET_G(x)
#define CRIS_LED_NETWORK_SET_R(x)
#define CRIS_LED_ACTIVE_SET_G(x)
#define CRIS_LED_ACTIVE_SET_R(x)
#define CRIS_LED_DISK_WRITE(x)
#define CRIS_LED_DISK_READ(x)
#endif

#if !defined(CONFIG_ETRAX_CSP0_LEDS)
#define CRIS_LED_BIT_SET(x)
#define CRIS_LED_BIT_CLR(x)
#endif

#define CRIS_LED_OFF    0x00
#define CRIS_LED_GREEN  0x01
#define CRIS_LED_RED    0x02
#define CRIS_LED_ORANGE (CRIS_LED_GREEN | CRIS_LED_RED)

#if defined(CONFIG_ETRAX_NO_LEDS)
#define CRIS_LED_NETWORK_SET(x)
#else
#if CONFIG_ETRAX_LED1G == CONFIG_ETRAX_LED1R
#define CRIS_LED_NETWORK_SET(x)                          \
	do {                                        \
		CRIS_LED_NETWORK_SET_G((x) & CRIS_LED_GREEN); \
	} while (0)
#else
#define CRIS_LED_NETWORK_SET(x)                          \
	do {                                        \
		CRIS_LED_NETWORK_SET_G((x) & CRIS_LED_GREEN); \
		CRIS_LED_NETWORK_SET_R((x) & CRIS_LED_RED);   \
	} while (0)
#endif
#if CONFIG_ETRAX_LED2G == CONFIG_ETRAX_LED2R
#define CRIS_LED_ACTIVE_SET(x)                           \
	do {                                        \
		CRIS_LED_ACTIVE_SET_G((x) & CRIS_LED_GREEN);  \
	} while (0)
#else
#define CRIS_LED_ACTIVE_SET(x)                           \
	do {                                        \
		CRIS_LED_ACTIVE_SET_G((x) & CRIS_LED_GREEN);  \
		CRIS_LED_ACTIVE_SET_R((x) & CRIS_LED_RED);    \
	} while (0)
#endif
#endif

#ifdef CONFIG_ETRAX_PA_LEDS
#define CRIS_LED_NETWORK_SET_G(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED1G, !(x))
#define CRIS_LED_NETWORK_SET_R(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED1R, !(x))
#define CRIS_LED_ACTIVE_SET_G(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED2G, !(x))
#define CRIS_LED_ACTIVE_SET_R(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED2R, !(x))
#define CRIS_LED_DISK_WRITE(x) \
         do{\
                REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED3G, !(x));\
                REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED3R, !(x));\
        }while(0)
#define CRIS_LED_DISK_READ(x) \
	REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, \
		CONFIG_ETRAX_LED3G, !(x))
#endif

#ifdef CONFIG_ETRAX_PB_LEDS
#define CRIS_LED_NETWORK_SET_G(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED1G, !(x))
#define CRIS_LED_NETWORK_SET_R(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED1R, !(x))
#define CRIS_LED_ACTIVE_SET_G(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED2G, !(x))
#define CRIS_LED_ACTIVE_SET_R(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED2R, !(x))
#define CRIS_LED_DISK_WRITE(x) \
        do{\
                REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED3G, !(x));\
                REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED3R, !(x));\
        }while(0)
#define CRIS_LED_DISK_READ(x) \
	REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, \
		CONFIG_ETRAX_LED3G, !(x))
#endif

#ifdef CONFIG_ETRAX_CSP0_LEDS
#define CONFIGURABLE_LEDS\
        ((1 << CONFIG_ETRAX_LED1G ) | (1 << CONFIG_ETRAX_LED1R ) |\
         (1 << CONFIG_ETRAX_LED2G ) | (1 << CONFIG_ETRAX_LED2R ) |\
         (1 << CONFIG_ETRAX_LED3G ) | (1 << CONFIG_ETRAX_LED3R ) |\
         (1 << CONFIG_ETRAX_LED4G ) | (1 << CONFIG_ETRAX_LED4R ) |\
         (1 << CONFIG_ETRAX_LED5G ) | (1 << CONFIG_ETRAX_LED5R ) |\
         (1 << CONFIG_ETRAX_LED6G ) | (1 << CONFIG_ETRAX_LED6R ) |\
         (1 << CONFIG_ETRAX_LED7G ) | (1 << CONFIG_ETRAX_LED7R ) |\
         (1 << CONFIG_ETRAX_LED8Y ) | (1 << CONFIG_ETRAX_LED9Y ) |\
         (1 << CONFIG_ETRAX_LED10Y ) |(1 << CONFIG_ETRAX_LED11Y )|\
         (1 << CONFIG_ETRAX_LED12R ))

#define CRIS_LED_NETWORK_SET_G(x) \
         REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, CONFIG_ETRAX_LED1G, !(x))
#define CRIS_LED_NETWORK_SET_R(x) \
         REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, CONFIG_ETRAX_LED1R, !(x))
#define CRIS_LED_ACTIVE_SET_G(x) \
         REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, CONFIG_ETRAX_LED2G, !(x))
#define CRIS_LED_ACTIVE_SET_R(x) \
         REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, CONFIG_ETRAX_LED2R, !(x))
#define CRIS_LED_DISK_WRITE(x) \
        do{\
                REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, CONFIG_ETRAX_LED3G, !(x));\
                REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, CONFIG_ETRAX_LED3R, !(x));\
        }while(0)
#define CRIS_LED_DISK_READ(x) \
         REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, CONFIG_ETRAX_LED3G, !(x))
#define CRIS_LED_BIT_SET(x)\
        do{\
                if((( 1 << x) & CONFIGURABLE_LEDS)  != 0)\
                       REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, x, 1);\
        }while(0)
#define CRIS_LED_BIT_CLR(x)\
        do{\
                if((( 1 << x) & CONFIGURABLE_LEDS)  != 0)\
                       REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, x, 0);\
        }while(0)
#endif

#
#ifdef CONFIG_ETRAX_SOFT_SHUTDOWN
#define SOFT_SHUTDOWN() \
          REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, CONFIG_ETRAX_SHUTDOWN_BIT, 1)
#else
#define SOFT_SHUTDOWN()
#endif

#endif
