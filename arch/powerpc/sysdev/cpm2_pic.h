#ifndef _PPC_KERNEL_CPM2_H
#define _PPC_KERNEL_CPM2_H

extern unsigned int cpm2_get_irq(void);

extern void cpm2_pic_init(struct device_node*);

#endif /* _PPC_KERNEL_CPM2_H */
