#ifndef __INTERFACE_MISC_H
#define __INTERFACE_MISC_H

INT
InterfaceRDM(struct bcm_interface_adapter *psIntfAdapter,
			UINT addr,
			PVOID buff,
			INT len);

INT
InterfaceWRM(struct bcm_interface_adapter *psIntfAdapter,
			UINT addr,
			PVOID buff,
			INT len);


int InterfaceFileDownload(PVOID psIntfAdapter,
			struct file *flp,
			unsigned int on_chip_loc);

int InterfaceFileReadbackFromChip(PVOID psIntfAdapter,
			struct file *flp,
			unsigned int on_chip_loc);


int BcmRDM(PVOID arg,
			UINT addr,
			PVOID buff,
			INT len);

int BcmWRM(PVOID arg,
			UINT addr,
			PVOID buff,
			INT len);

INT Bcm_clear_halt_of_endpoints(struct bcm_mini_adapter *Adapter);

VOID Bcm_kill_all_URBs(struct bcm_interface_adapter *psIntfAdapter);

#define DISABLE_USB_ZERO_LEN_INT 0x0F011878

#endif // __INTERFACE_MISC_H
