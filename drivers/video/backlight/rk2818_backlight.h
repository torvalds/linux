/*
 *  drivers/video/rk29_backlight.h
 *
 */

#ifndef __ASM_ARCH_RK2818_BACKLIGHT_H
#define __ASM_ARCH_RK2818_BACKLIGHT_H

///PWM_CTRL
#define PWM_DIV2            (0<<9)
#define PWM_DIV4            (1<<9)
#define PWM_DIV8            (2<<9)
#define PWM_DIV16           (3<<9)
#define PWM_DIV32           (4<<9)
#define PWM_DIV64           (5<<9)
#define PWM_DIV128          (6<<9)
#define PWM_DIV256          (7<<9)
#define PWM_DIV512          (8<<9)
#define PWM_DIV1024         (9<<9)

#define PWM_CAPTURE         (1<<8)
#define PWM_RESET           (1<<7)
#define PWM_INTCLR          (1<<6)
#define PWM_INTEN           (1<<5)
#define PWM_SINGLE          (1<<6)

#define PWM_ENABLE          (1<<3)
#define PWM_TIME_EN         (1)


#define PWM_REG_CNTR         0x00
#define PWM_REG_HRC          0x04 
#define PWM_REG_LRC          0x08
#define PWM_REG_CTRL         0x0c


#define PWM_DIV              PWM_DIV2
#define PWM_APB_PRE_DIV      1000
#define BL_STEP              255

#endif	/* __ASM_ARCH_RK2818_BACKLIGHT_H */
