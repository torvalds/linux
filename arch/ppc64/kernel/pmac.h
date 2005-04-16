#ifndef __PMAC_H__
#define __PMAC_H__

#include <linux/pci.h>
#include <linux/ide.h>

/*
 * Declaration for the various functions exported by the
 * pmac_* files. Mostly for use by pmac_setup
 */

extern void pmac_get_boot_time(struct rtc_time *tm);
extern void pmac_get_rtc_time(struct rtc_time *tm);
extern int  pmac_set_rtc_time(struct rtc_time *tm);
extern void pmac_read_rtc_time(void);
extern void pmac_calibrate_decr(void);

extern void pmac_pcibios_fixup(void);
extern void pmac_pci_init(void);
extern void pmac_setup_pci_dma(void);
extern void pmac_check_ht_link(void);

extern void pmac_setup_smp(void);

extern unsigned long pmac_ide_get_base(int index);
extern void pmac_ide_init_hwif_ports(hw_regs_t *hw,
	unsigned long data_port, unsigned long ctrl_port, int *irq);

extern void pmac_nvram_init(void);

#endif /* __PMAC_H__ */
