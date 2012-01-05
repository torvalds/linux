#ifndef __WSP_H
#define __WSP_H

#include <asm/wsp.h>

/* Devtree compatible strings for major devices */
#define PCIE_COMPATIBLE     "ibm,wsp-pciex"

extern void wsp_setup_pci(void);
extern void scom_init_wsp(void);

extern void a2_setup_smp(void);
extern int a2_scom_startup_cpu(unsigned int lcpu, int thr_idx,
			       struct device_node *np);
int smp_a2_cpu_bootable(unsigned int nr);
int __devinit smp_a2_kick_cpu(int nr);

void opb_pic_init(void);

#endif /*  __WSP_H */
