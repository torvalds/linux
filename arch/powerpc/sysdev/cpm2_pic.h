#ifndef _PPC_KERNEL_CPM2_H
#define _PPC_KERNEL_CPM2_H

extern intctl_cpm2_t *cpm2_intctl;

extern unsigned int cpm2_get_irq(struct pt_regs *regs);

extern void cpm2_pic_init(struct device_node*);

#endif /* _PPC_KERNEL_CPM2_H */
