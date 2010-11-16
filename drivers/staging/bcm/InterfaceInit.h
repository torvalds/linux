#ifndef _INTERFACE_INIT_H
#define _INTERFACE_INIT_H

#define BCM_USB_VENDOR_ID_T3 	0x198f
#define BCM_USB_VENDOR_ID_FOXCONN       0x0489
#define BCM_USB_VENDOR_ID_ZTE   0x19d2

#define BCM_USB_PRODUCT_ID_T3 	0x0300
#define BCM_USB_PRODUCT_ID_T3B 	0x0210
#define BCM_USB_PRODUCT_ID_T3L 	0x0220
#define BCM_USB_PRODUCT_ID_SYM  0x15E
#define BCM_USB_PRODUCT_ID_1901 0xe017
#define BCM_USB_PRODUCT_ID_226  0x0132

#define BCM_USB_MINOR_BASE 		192


INT InterfaceInitialize(void);

INT InterfaceExit(void);

#ifndef BCM_SHM_INTERFACE
INT InterfaceAdapterInit(PS_INTERFACE_ADAPTER Adapter);

INT usbbcm_worker_thread(PS_INTERFACE_ADAPTER psIntfAdapter);

VOID InterfaceAdapterFree(PS_INTERFACE_ADAPTER psIntfAdapter);

#else
INT InterfaceAdapterInit(PMINI_ADAPTER Adapter);
#endif


#if 0

ULONG InterfaceClaimAdapter(PMINI_ADAPTER Adapter);

VOID InterfaceDDRControllerInit(PMINI_ADAPTER Adapter);

ULONG InterfaceReset(PMINI_ADAPTER Adapter);

ULONG InterfaceRegisterResources(PMINI_ADAPTER Adapter);

VOID InterfaceUnRegisterResources(PMINI_ADAPTER Adapter);

ULONG InterfaceFirmwareDownload(PMINI_ADAPTER Adapter);

#endif

#endif

