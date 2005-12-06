#ifndef __PMAC_H__
#define __PMAC_H__

#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/irq.h>

/*
 * Declaration for the various functions exported by the
 * pmac_* files. Mostly for use by pmac_setup
 */

struct rtc_time;

extern long pmac_time_init(void);
extern unsigned long pmac_get_boot_time(void);
extern void pmac_get_rtc_time(struct rtc_time *);
extern int pmac_set_rtc_time(struct rtc_time *);
extern void pmac_read_rtc_time(void);
extern void pmac_calibrate_decr(void);
extern void pmac_pcibios_fixup(void);
extern void pmac_pci_init(void);
extern unsigned long pmac_ide_get_base(int index);
extern void pmac_ide_init_hwif_ports(hw_regs_t *hw,
	unsigned long data_port, unsigned long ctrl_port, int *irq);

extern void pmac_nvram_update(void);
extern unsigned char pmac_nvram_read_byte(int addr);
extern void pmac_nvram_write_byte(int addr, unsigned char val);
extern int pmac_pci_enable_device_hook(struct pci_dev *dev, int initial);
extern void pmac_pcibios_after_init(void);
extern int of_show_percpuinfo(struct seq_file *m, int i);

extern void pmac_pci_init(void);
extern void pmac_setup_pci_dma(void);
extern void pmac_check_ht_link(void);

extern void pmac_setup_smp(void);

extern unsigned long pmac_ide_get_base(int index);
extern void pmac_ide_init_hwif_ports(hw_regs_t *hw,
	unsigned long data_port, unsigned long ctrl_port, int *irq);

extern int pmac_nvram_init(void);

extern struct hw_interrupt_type pmac_pic;

void pmac_pic_init(void);
int pmac_get_irq(struct pt_regs *regs);

#endif /* __PMAC_H__ */
