#ifndef _INTERFACE_ADAPTER_H
#define _INTERFACE_ADAPTER_H

typedef struct _BULK_ENDP_IN
{
    PCHAR		    bulk_in_buffer;
    size_t          bulk_in_size;
    UCHAR			bulk_in_endpointAddr;
    UINT bulk_in_pipe;
}BULK_ENDP_IN, *PBULK_ENDP_IN;


typedef struct _BULK_ENDP_OUT
{
    UCHAR			bulk_out_buffer;
    size_t          bulk_out_size;
    UCHAR			bulk_out_endpointAddr;
    UINT bulk_out_pipe;
    //this is used when int out endpoint is used as bulk out end point
	UCHAR			int_out_interval;
}BULK_ENDP_OUT, *PBULK_ENDP_OUT;

typedef struct _INTR_ENDP_IN
{
    PCHAR		    int_in_buffer;
    size_t          int_in_size;
    UCHAR			int_in_endpointAddr;
    UCHAR			int_in_interval;
    UINT int_in_pipe;
}INTR_ENDP_IN, *PINTR_ENDP_IN;

typedef struct _INTR_ENDP_OUT
{
    PCHAR		    int_out_buffer;
    size_t          int_out_size;
    UCHAR			int_out_endpointAddr;
    UCHAR			int_out_interval;
    UINT int_out_pipe;
}INTR_ENDP_OUT, *PINTR_ENDP_OUT;


typedef struct _USB_TCB
{
	struct urb *urb;
	PVOID psIntfAdapter;
	BOOLEAN bUsed;
}USB_TCB, *PUSB_TCB;


typedef struct _USB_RCB
{
	struct urb *urb;
	PVOID psIntfAdapter;
	BOOLEAN bUsed;
}USB_RCB, *PUSB_RCB;

/*
//This is the interface specific Sub-Adapter
//Structure.
*/
typedef struct _S_INTERFACE_ADAPTER
{
    struct usb_device * udev;
    struct usb_interface *  interface;

	/* Bulk endpoint in info */
	BULK_ENDP_IN	sBulkIn;
	/* Bulk endpoint out info */
	BULK_ENDP_OUT	sBulkOut;
	/* Interrupt endpoint in info */
	INTR_ENDP_IN	sIntrIn;
	/* Interrupt endpoint out info */
	INTR_ENDP_OUT	sIntrOut;



	ULONG ulInterruptData[2];

	struct urb *psInterruptUrb;

	USB_TCB			asUsbTcb[MAXIMUM_USB_TCB];
	USB_RCB			asUsbRcb[MAXIMUM_USB_RCB];
	atomic_t	  	uNumTcbUsed;
	atomic_t		uCurrTcb;
	atomic_t		uNumRcbUsed;
	atomic_t		uCurrRcb;

	PMINI_ADAPTER	psAdapter;
	BOOLEAN                 bFlashBoot;
	BOOLEAN 		bHighSpeedDevice ;

	BOOLEAN 		bSuspended;
	BOOLEAN 		bPreparingForBusSuspend;
	struct work_struct usbSuspendWork;
}S_INTERFACE_ADAPTER,*PS_INTERFACE_ADAPTER;

#endif
