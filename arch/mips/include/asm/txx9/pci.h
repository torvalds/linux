/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_TXX9_PCI_H
#define __ASM_TXX9_PCI_H

#include <linux/pci.h>

extern struct pci_controller txx9_primary_pcic;
struct pci_controller *
txx9_alloc_pci_controller(struct pci_controller *pcic,
			  unsigned long mem_base, unsigned long mem_size,
			  unsigned long io_base, unsigned long io_size);

int txx9_pci66_check(struct pci_controller *hose, int top_bus,
			int current_bus);
extern int txx9_pci_mem_high __initdata;

extern int txx9_pci_option;
#define TXX9_PCI_OPT_PICMG	0x0002
#define TXX9_PCI_OPT_CLK_33	0x0008
#define TXX9_PCI_OPT_CLK_66	0x0010
#define TXX9_PCI_OPT_CLK_MASK	\
	(TXX9_PCI_OPT_CLK_33 | TXX9_PCI_OPT_CLK_66)
#define TXX9_PCI_OPT_CLK_AUTO	TXX9_PCI_OPT_CLK_MASK

enum txx9_pci_err_action {
	TXX9_PCI_ERR_REPORT,
	TXX9_PCI_ERR_IGNORE,
	TXX9_PCI_ERR_PANIC,
};
extern enum txx9_pci_err_action txx9_pci_err_action;

extern char * (*txx9_board_pcibios_setup)(char *str);
char *txx9_pcibios_setup(char *str);

#endif /* __ASM_TXX9_PCI_H */
