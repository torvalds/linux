#ifndef _INTERFACE_INIT_H
#define _INTERFACE_INIT_H

#define BCM_USB_VENDOR_ID_T3	0x198f
#define BCM_USB_VENDOR_ID_FOXCONN	0x0489
#define BCM_USB_VENDOR_ID_ZTE	0x19d2

#define BCM_USB_PRODUCT_ID_T3	0x0300
#define BCM_USB_PRODUCT_ID_T3B	0x0210
#define BCM_USB_PRODUCT_ID_T3L	0x0220
#define BCM_USB_PRODUCT_ID_SM250	0xbccd
#define BCM_USB_PRODUCT_ID_SYM	0x15E
#define BCM_USB_PRODUCT_ID_1901	0xe017
#define BCM_USB_PRODUCT_ID_226	0x0132 /* not sure if this is valid */
#define BCM_USB_PRODUCT_ID_ZTE_226 0x172
#define BCM_USB_PRODUCT_ID_ZTE_TU25	0x0007

#define BCM_USB_MINOR_BASE	192

int InterfaceInitialize(void);

int InterfaceExit(void);

int usbbcm_worker_thread(struct bcm_interface_adapter *psIntfAdapter);

#endif
