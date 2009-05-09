#ifndef __PPC_PLATFORMS_PMAC_PIC_H
#define __PPC_PLATFORMS_PMAC_PIC_H

#include <linux/irq.h>

extern struct irq_chip pmac_pic;

extern void pmac_pic_init(void);
extern int pmac_get_irq(void);

#endif /* __PPC_PLATFORMS_PMAC_PIC_H */
