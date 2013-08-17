/*
 * Q40 master Chip Control
 * RTC stuff merged for compactnes..
*/

#ifndef _Q40_MASTER_H
#define _Q40_MASTER_H

#include <asm/raw_io.h>


#define q40_master_addr 0xff000000

#define IIRQ_REG            0x0       /* internal IRQ reg */
#define EIRQ_REG            0x4       /* external ... */
#define KEYCODE_REG         0x1c      /* value of received scancode  */
#define DISPLAY_CONTROL_REG 0x18
#define FRAME_CLEAR_REG     0x24
#define LED_REG             0x30

#define Q40_LED_ON()        master_outb(1,LED_REG)
#define Q40_LED_OFF()       master_outb(0,LED_REG)

#define INTERRUPT_REG       IIRQ_REG  /* "native" ints */
#define KEY_IRQ_ENABLE_REG  0x08      /**/
#define KEYBOARD_UNLOCK_REG 0x20      /* clear kb int */

#define SAMPLE_ENABLE_REG   0x14      /* generate SAMPLE ints */
#define SAMPLE_RATE_REG     0x2c
#define SAMPLE_CLEAR_REG    0x28
#define SAMPLE_LOW          0x00
#define SAMPLE_HIGH         0x01

#define FRAME_RATE_REG       0x38      /* generate FRAME ints at 200 HZ rate */

#if 0
#define SER_ENABLE_REG      0x0c      /* allow serial ints to be generated */
#endif
#define EXT_ENABLE_REG      0x10      /* ... rest of the ISA ints ... */


#define master_inb(_reg_)      in_8((unsigned char *)q40_master_addr+_reg_)
#define master_outb(_b_,_reg_)  out_8((unsigned char *)q40_master_addr+_reg_,_b_)

/* RTC defines */

#define Q40_RTC_BASE	    (0xff021ffc)

#define Q40_RTC_YEAR        (*(volatile unsigned char *)(Q40_RTC_BASE+0))
#define Q40_RTC_MNTH        (*(volatile unsigned char *)(Q40_RTC_BASE-4))
#define Q40_RTC_DATE        (*(volatile unsigned char *)(Q40_RTC_BASE-8))
#define Q40_RTC_DOW         (*(volatile unsigned char *)(Q40_RTC_BASE-12))
#define Q40_RTC_HOUR        (*(volatile unsigned char *)(Q40_RTC_BASE-16))
#define Q40_RTC_MINS        (*(volatile unsigned char *)(Q40_RTC_BASE-20))
#define Q40_RTC_SECS        (*(volatile unsigned char *)(Q40_RTC_BASE-24))
#define Q40_RTC_CTRL        (*(volatile unsigned char *)(Q40_RTC_BASE-28))

/* some control bits */
#define Q40_RTC_READ   64  /* prepare for reading */
#define Q40_RTC_WRITE  128

/* define some Q40 specific ints */
#include "q40ints.h"

/* misc defs */
#define DAC_LEFT  ((unsigned char *)0xff008000)
#define DAC_RIGHT ((unsigned char *)0xff008004)

#endif /* _Q40_MASTER_H */
