#ifndef _VBINIT_
#define _VBINIT_
unsigned char XGIInitNew(struct pci_dev *pdev);
void XGIRegInit(struct vb_device_info *XGI_Pr, unsigned long BaseAddr);
#endif
