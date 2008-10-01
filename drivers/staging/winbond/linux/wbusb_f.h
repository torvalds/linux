//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Copyright (c) 1996-2004 Winbond Electronic Corporation
//
//  Module Name:
//    wbusb_f.h
//
//  Abstract:
//    Linux driver.
//
//  Author:
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

int wb35_open(struct net_device *netdev);
int wb35_close(struct net_device *netdev);
unsigned char WbUsb_initial(phw_data_t pHwData);
void WbUsb_destroy(phw_data_t pHwData);
unsigned char WbWLanInitialize(PADAPTER Adapter);
#define	WbUsb_Stop( _A )

int wb35_probe(struct usb_interface *intf,const struct usb_device_id *id_table);
void wb35_disconnect(struct usb_interface *intf);


#define wb_usb_submit_urb(_A) usb_submit_urb(_A, GFP_ATOMIC)
#define wb_usb_alloc_urb(_A) usb_alloc_urb(_A, GFP_ATOMIC)

#define WbUsb_CheckForHang( _P )
#define WbUsb_DetectStart( _P, _I )





