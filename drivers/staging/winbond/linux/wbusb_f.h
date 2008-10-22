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

unsigned char WbUsb_initial(phw_data_t pHwData);
void WbUsb_destroy(phw_data_t pHwData);
unsigned char WbWLanInitialize(struct wb35_adapter *adapter);
#define	WbUsb_Stop( _A )

#define wb_usb_submit_urb(_A) usb_submit_urb(_A, GFP_ATOMIC)
#define wb_usb_alloc_urb(_A) usb_alloc_urb(_A, GFP_ATOMIC)

#define WbUsb_CheckForHang( _P )
#define WbUsb_DetectStart( _P, _I )





