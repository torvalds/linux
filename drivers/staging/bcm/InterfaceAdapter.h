#ifndef _INTERFACE_ADAPTER_H
#define _INTERFACE_ADAPTER_H

typedef struct _BULK_ENDP_IN {
	char	*bulk_in_buffer;
	size_t	bulk_in_size;
	unsigned char	bulk_in_endpointAddr;
	unsigned int	bulk_in_pipe;
} BULK_ENDP_IN, *PBULK_ENDP_IN;


typedef struct _BULK_ENDP_OUT {
	unsigned char	bulk_out_buffer;
	size_t	bulk_out_size;
	unsigned char	bulk_out_endpointAddr;
	unsigned int	bulk_out_pipe;
	/* this is used when int out endpoint is used as bulk out end point */
	unsigned char	int_out_interval;
} BULK_ENDP_OUT, *PBULK_ENDP_OUT;

typedef struct _INTR_ENDP_IN {
	char	*int_in_buffer;
	size_t	int_in_size;
	unsigned char	int_in_endpointAddr;
	unsigned char	int_in_interval;
	unsigned int	int_in_pipe;
} INTR_ENDP_IN, *PINTR_ENDP_IN;

typedef struct _INTR_ENDP_OUT {
	char	*int_out_buffer;
	size_t	int_out_size;
	unsigned char	int_out_endpointAddr;
	unsigned char	int_out_interval;
	unsigned int	int_out_pipe;
} INTR_ENDP_OUT, *PINTR_ENDP_OUT;

typedef struct _USB_TCB {
	struct urb *urb;
	void *psIntfAdapter;
	bool bUsed;
} USB_TCB, *PUSB_TCB;

struct bcm_usb_rcb {
	struct urb *urb;
	void *psIntfAdapter;
	bool bUsed;
};

/*
 * This is the interface specific Sub-Adapter
 * Structure.
 */
struct bcm_interface_adapter {
	struct usb_device *udev;
	struct usb_interface *interface;
	/* Bulk endpoint in info */
	BULK_ENDP_IN	sBulkIn;
	/* Bulk endpoint out info */
	BULK_ENDP_OUT	sBulkOut;
	/* Interrupt endpoint in info */
	INTR_ENDP_IN	sIntrIn;
	/* Interrupt endpoint out info */
	INTR_ENDP_OUT	sIntrOut;
	ULONG		ulInterruptData[2];
	struct urb *psInterruptUrb;
	USB_TCB		asUsbTcb[MAXIMUM_USB_TCB];
	struct bcm_usb_rcb	asUsbRcb[MAXIMUM_USB_RCB];
	atomic_t	uNumTcbUsed;
	atomic_t	uCurrTcb;
	atomic_t	uNumRcbUsed;
	atomic_t	uCurrRcb;
	struct bcm_mini_adapter *psAdapter;
	bool		bFlashBoot;
	bool		bHighSpeedDevice;
	bool		bSuspended;
	bool		bPreparingForBusSuspend;
	struct work_struct usbSuspendWork;
};

#endif
