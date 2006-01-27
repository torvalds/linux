#ifndef	_ISERIES_IRQ_H
#define	_ISERIES_IRQ_H

extern void iSeries_init_IRQ(void);
extern int  iSeries_allocate_IRQ(HvBusNumber, HvSubBusNumber, HvAgentId);
extern void iSeries_activate_IRQs(void);
extern int iSeries_get_irq(struct pt_regs *);

#endif /* _ISERIES_IRQ_H */
