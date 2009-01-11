/* USB defines for older kernels */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)

/**
 * usb_endpoint_dir_out - check if the endpoint has OUT direction
 * @epd: endpoint to be checked
 *
 * Returns true if the endpoint is of type OUT, otherwise it returns false.
 */

static inline int usb_endpoint_dir_out(const struct usb_endpoint_descriptor *epd)
{
       return ((epd->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT);
}

static inline int usb_endpoint_dir_in(const struct usb_endpoint_descriptor *epd)
{
       return ((epd->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN);
}


/**
 * usb_endpoint_xfer_int - check if the endpoint has interrupt transfer type
 * @epd: endpoint to be checked
 *
 * Returns true if the endpoint is of type interrupt, otherwise it returns
 * false.
 */
static inline int usb_endpoint_xfer_int(const struct usb_endpoint_descriptor *epd)
{
       return ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
               USB_ENDPOINT_XFER_INT);
}


/**
 * usb_endpoint_is_int_in - check if the endpoint is interrupt IN
 * @epd: endpoint to be checked
 *
 * Returns true if the endpoint has interrupt transfer type and IN direction,
 * otherwise it returns false.
 */

static inline int usb_endpoint_is_int_in(const struct usb_endpoint_descriptor *epd)
{
       return (usb_endpoint_xfer_int(epd) && usb_endpoint_dir_in(epd));
}

/**
 * usb_endpoint_is_int_out - check if the endpoint is interrupt OUT
 * @epd: endpoint to be checked
 *
 * Returns true if the endpoint has interrupt transfer type and OUT direction,
 * otherwise it returns false.
 */

static inline int usb_endpoint_is_int_out(const struct usb_endpoint_descriptor *epd)
{
       return (usb_endpoint_xfer_int(epd) && usb_endpoint_dir_out(epd));
}

#endif /* older kernel versions */
