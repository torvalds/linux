#ifndef __PLAT_MTU_H
#define __PLAT_MTU_H

/* should be set by the platform code */
extern void __iomem *mtu_base;

void nmdk_clkevt_reset(void);
void nmdk_clksrc_reset(void);

#endif /* __PLAT_MTU_H */

