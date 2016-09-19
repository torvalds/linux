/* ES1 AP Bridge Chip USB descriptor definitions */

static const u8 es1_dev_descriptor[] = {
	0x12,		/* __u8   bLength */
	0x01,		/* __u8   bDescriptorType; Device */
	0x00, 0x02	/* __le16 bcdUSB v2.0 */
	0x00,		/* __u8   bDeviceClass */
	0x00,		/* __u8   bDeviceClass */
	0x00,		/* __u8   bDeviceSubClass; */
	0x00,		/* __u8   bDeviceProtocol; */
	0x40,		/* __u8   bMaxPacketSize0; 2^64 = 512 Bytes */

	0xff, 0xff,	/* __le16 idVendor; 0xffff  made up for now */
	0x01, 0x00,	/* __le16 idProduct; 0x0001  made up for now */
	0x01, 0x00,	/* __le16 bcdDevice; ES1 */

	0x03,		/* __u8  iManufacturer; */
	0x02,		/* __u8  iProduct; */
	0x01,		/* __u8  iSerialNumber; */
	0x01		/* __u8  bNumConfigurations; */
};

static const u8 es1_config_descriptor[] = {
	/* one configuration */
	0x09,		/*  __u8   bLength; */
	0x02,		/*  __u8   bDescriptorType; Configuration */
	0x19, 0x00,	/*  __le16 wTotalLength; */
	0x01,		/*  __u8   bNumInterfaces; (1) */
	0x01,		/*  __u8   bConfigurationValue; */
	0x00,		/*  __u8   iConfiguration; */
	0xc0,		/*  __u8   bmAttributes;
				 Bit 7: must be set,
				     6: Self-powered,
				     5: Remote wakeup,
				     4..0: resvd */
	0x00,		/*  __u8  MaxPower; */

	/* one interface */
	0x09,		/*  __u8  if_bLength; */
	0x04,		/*  __u8  if_bDescriptorType; Interface */
	0x00,		/*  __u8  if_bInterfaceNumber; */
	0x00,		/*  __u8  if_bAlternateSetting; */
	0x03,		/*  __u8  if_bNumEndpoints; */
	0xff,		/*  __u8  if_bInterfaceClass; Vendor-specific */
	0xff,		/*  __u8  if_bInterfaceSubClass; Vendor-specific */
	0xff,		/*  __u8  if_bInterfaceProtocol; Vendor-specific */
	0x00,		/*  __u8  if_iInterface; */

	/* three endpoints */
	0x07,		/*  __u8   ep_bLength; */
	0x05,		/*  __u8   ep_bDescriptorType; Endpoint */
	0x81,		/*  __u8   ep_bEndpointAddress; IN Endpoint 1 */
	0x03,		/*  __u8   ep_bmAttributes; Interrupt */
	0x00, 0x04,	/*  __le16 ep_wMaxPacketSize; 1024 */
	0x40,		/*  __u8   ep_bInterval; 64ms */

	0x07,		/*  __u8   ep_bLength; */
	0x05,		/*  __u8   ep_bDescriptorType; Endpoint */
	0x82,		/*  __u8   ep_bEndpointAddress; IN Endpoint 2 */
	0x02,		/*  __u8   ep_bmAttributes; Bulk */
	0x00, 0x04,	/*  __le16 ep_wMaxPacketSize; 1024 */
	0x40		/*  __u8   ep_bInterval; */

	0x07,		/*  __u8   ep_bLength; */
	0x05,		/*  __u8   ep_bDescriptorType; Endpoint */
	0x02,		/*  __u8   ep_bEndpointAddress; Out Endpoint 2 */
	0x02,		/*  __u8   ep_bmAttributes; Bulk */
	0x00, 0x04,	/*  __le16 ep_wMaxPacketSize; 1024 */
	0x40		/*  __u8   ep_bInterval; */
};
