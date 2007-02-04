#ifndef _PASEMI_PASEMI_H
#define _PASEMI_PASEMI_H

extern unsigned long pas_get_boot_time(void);
extern void pas_pci_init(void);

extern void __init pasemi_idle_init(void);

/* Power savings modes, implemented in asm */
extern void idle_spin(void);
extern void idle_doze(void);


#endif /* _PASEMI_PASEMI_H */
