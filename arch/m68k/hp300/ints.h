extern void hp300_init_IRQ(void);
extern void (*hp300_handlers[8])(int, void *, struct pt_regs *);
extern void hp300_free_irq(unsigned int irq, void *dev_id);
extern int hp300_request_irq(unsigned int irq,
		irqreturn_t (*handler) (int, void *, struct pt_regs *),
		unsigned long flags, const char *devname, void *dev_id);

/* number of interrupts, includes 0 (what's that?) */
#define HP300_NUM_IRQS 8
