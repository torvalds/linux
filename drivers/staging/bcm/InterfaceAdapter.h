#ifndef _INTERFACE_ADAPTER_H
#define _INTERFACE_ADAPTER_H

struct bcm_bulk_endpoint_in {
	char	*bulk_in_buffer;
	size_t	bulk_in_size;
	unsigned char	bulk_in_endpointAddr;
	unsigned int	bulk_in_pipe;
};

struct bcm_bulk_endpoint_out {
	unsigned char	bulk_out_buffer;
	size_t	bulk_out_size;
	unsigned char	bulk_out_endpointAddr;
	unsigned int	bulk_out_pipe;
	/* this is used when int out endpoint is used as bulk out end point */
	unsigned char	int_out_interval;
};

struct bcm_intr_endpoint_in {
	char	*int_in_buffer;
	size_t	int_in_size;
	unsigned char	int_in_endpointAddr;
	unsigned char	int_in_interval;
	unsigned int	int_in_pipe;
};

struct bcm_intr_endpoint_out {
	char	*int_out_buffer;
	size_t	int_out_size;
	unsigned char	int_out_endpointAddr;
	unsigned char	int_out_interval;
	unsigned int	int_out_pipe;
};

struct bcm_usb_tcb {
	struct urb *urb;
	void *psIntfAdapter;
	bool bUsed;
};

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
	struct bcm_bulk_endpoint_in	sBulkIn;
	/* Bulk endpoint out info */
	struct bcm_bulk_endpoint_out	sBulkOut;
	/* Interrupt endpoint in info */
	struct bcm_intr_endpoint_in	sIntrIn;
	/* Interrupt endpoint out info */
	struct bcm_intr_endpoint_out	sIntrOut;
	unsigned long		ulInterruptData[2];
	struct urb *psInterruptUrb;
	struct bcm_usb_tcb	asUsbTcb[MAXIMUM_USB_TCB];
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
