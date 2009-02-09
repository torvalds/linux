/*
 * contains some Q40 related interrupt definitions
 */

#define Q40_IRQ_MAX      (34)

#define Q40_IRQ_SAMPLE    (34)
#define Q40_IRQ_KEYBOARD (32)
#define Q40_IRQ_FRAME    (33)


/* masks for interrupt regiosters*/
/* internal, IIRQ_REG */
#define Q40_IRQ_KEYB_MASK    (2)
#define Q40_IRQ_SER_MASK     (1<<2)
#define Q40_IRQ_FRAME_MASK   (1<<3)
#define Q40_IRQ_EXT_MASK     (1<<4)    /* is a EIRQ */
/* eirq, EIRQ_REG */
#define Q40_IRQ3_MASK        (1)
#define Q40_IRQ4_MASK        (1<<1)
#define Q40_IRQ5_MASK        (1<<2)
#define Q40_IRQ6_MASK        (1<<3)
#define Q40_IRQ7_MASK        (1<<4)
#define Q40_IRQ10_MASK       (1<<5)
#define Q40_IRQ14_MASK       (1<<6)
#define Q40_IRQ15_MASK       (1<<7)

extern unsigned long q40_probe_irq_on (void);
extern int q40_probe_irq_off (unsigned long irqs);
