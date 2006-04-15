#ifndef __LINUX_USB_PCI_QUIRKS_H
#define __LINUX_USB_PCI_QUIRKS_H

void uhci_reset_hc(struct pci_dev *pdev, unsigned long base);
int uhci_check_and_reset_hc(struct pci_dev *pdev, unsigned long base);

#endif  /*  __LINUX_USB_PCI_QUIRKS_H  */
