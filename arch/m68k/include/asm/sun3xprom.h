/* Useful PROM locations */

#ifndef SUN3X_PROM_H
#define SUN3X_PROM_H

extern void (*sun3x_putchar)(int);
extern int (*sun3x_getchar)(void);
extern int (*sun3x_mayget)(void);
extern int (*sun3x_mayput)(int);

void sun3x_reboot(void);
void sun3x_abort(void);
void sun3x_prom_init(void);
unsigned long sun3x_prom_ptov(unsigned long pa, unsigned long size);

/* interesting hardware locations */
#define SUN3X_IOMMU       0x60000000
#define SUN3X_ENAREG      0x61000000
#define SUN3X_INTREG      0x61001400
#define SUN3X_DIAGREG     0x61001800
#define SUN3X_ZS1         0x62000000
#define SUN3X_ZS2         0x62002000
#define SUN3X_LANCE       0x65002000
#define SUN3X_EEPROM      0x64000000
#define SUN3X_IDPROM      0x640007d8
#define SUN3X_VIDEO_BASE  0x50400000
#define SUN3X_VIDEO_REGS  0x50300000

/* vector table */
#define SUN3X_PROM_BASE   0xfefe0000
#define SUN3X_P_GETCHAR   (SUN3X_PROM_BASE + 20)
#define SUN3X_P_PUTCHAR   (SUN3X_PROM_BASE + 24)
#define SUN3X_P_MAYGET    (SUN3X_PROM_BASE + 28)
#define SUN3X_P_MAYPUT    (SUN3X_PROM_BASE + 32)
#define SUN3X_P_REBOOT    (SUN3X_PROM_BASE + 96)
#define SUN3X_P_SETLEDS   (SUN3X_PROM_BASE + 144)
#define SUN3X_P_ABORT     (SUN3X_PROM_BASE + 152)

/* mapped area */
#define SUN3X_MAP_START   0xfee00000
#define SUN3X_MAP_END     0xff000000

#endif
