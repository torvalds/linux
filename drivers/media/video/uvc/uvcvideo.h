#ifndef _USB_VIDEO_H_
#define _USB_VIDEO_H_

#include <linux/kernel.h>
#include <linux/videodev2.h>

/*
 * Dynamic controls
 */

/* Data types for UVC control data */
#define UVC_CTRL_DATA_TYPE_RAW		0
#define UVC_CTRL_DATA_TYPE_SIGNED	1
#define UVC_CTRL_DATA_TYPE_UNSIGNED	2
#define UVC_CTRL_DATA_TYPE_BOOLEAN	3
#define UVC_CTRL_DATA_TYPE_ENUM		4
#define UVC_CTRL_DATA_TYPE_BITMASK	5

/* Control flags */
#define UVC_CONTROL_SET_CUR	(1 << 0)
#define UVC_CONTROL_GET_CUR	(1 << 1)
#define UVC_CONTROL_GET_MIN	(1 << 2)
#define UVC_CONTROL_GET_MAX	(1 << 3)
#define UVC_CONTROL_GET_RES	(1 << 4)
#define UVC_CONTROL_GET_DEF	(1 << 5)
/* Control should be saved at suspend and restored at resume. */
#define UVC_CONTROL_RESTORE	(1 << 6)
/* Control can be updated by the camera. */
#define UVC_CONTROL_AUTO_UPDATE	(1 << 7)

#define UVC_CONTROL_GET_RANGE	(UVC_CONTROL_GET_CUR | UVC_CONTROL_GET_MIN | \
				 UVC_CONTROL_GET_MAX | UVC_CONTROL_GET_RES | \
				 UVC_CONTROL_GET_DEF)

struct uvc_xu_control_info {
	__u8 entity[16];
	__u8 index;
	__u8 selector;
	__u16 size;
	__u32 flags;
};

struct uvc_xu_control_mapping {
	__u32 id;
	__u8 name[32];
	__u8 entity[16];
	__u8 selector;

	__u8 size;
	__u8 offset;
	enum v4l2_ctrl_type v4l2_type;
	__u32 data_type;
};

struct uvc_xu_control {
	__u8 unit;
	__u8 selector;
	__u16 size;
	__u8 __user *data;
};

#define UVCIOC_CTRL_ADD		_IOW('U', 1, struct uvc_xu_control_info)
#define UVCIOC_CTRL_MAP		_IOWR('U', 2, struct uvc_xu_control_mapping)
#define UVCIOC_CTRL_GET		_IOWR('U', 3, struct uvc_xu_control)
#define UVCIOC_CTRL_SET		_IOW('U', 4, struct uvc_xu_control)

#ifdef __KERNEL__

#include <linux/poll.h>

/* --------------------------------------------------------------------------
 * UVC constants
 */

#define SC_UNDEFINED			0x00
#define SC_VIDEOCONTROL			0x01
#define SC_VIDEOSTREAMING		0x02
#define SC_VIDEO_INTERFACE_COLLECTION	0x03

#define PC_PROTOCOL_UNDEFINED		0x00

#define CS_UNDEFINED			0x20
#define CS_DEVICE			0x21
#define CS_CONFIGURATION		0x22
#define CS_STRING			0x23
#define CS_INTERFACE			0x24
#define CS_ENDPOINT			0x25

/* VideoControl class specific interface descriptor */
#define VC_DESCRIPTOR_UNDEFINED		0x00
#define VC_HEADER			0x01
#define VC_INPUT_TERMINAL		0x02
#define VC_OUTPUT_TERMINAL		0x03
#define VC_SELECTOR_UNIT		0x04
#define VC_PROCESSING_UNIT		0x05
#define VC_EXTENSION_UNIT		0x06

/* VideoStreaming class specific interface descriptor */
#define VS_UNDEFINED			0x00
#define VS_INPUT_HEADER			0x01
#define VS_OUTPUT_HEADER		0x02
#define VS_STILL_IMAGE_FRAME		0x03
#define VS_FORMAT_UNCOMPRESSED		0x04
#define VS_FRAME_UNCOMPRESSED		0x05
#define VS_FORMAT_MJPEG			0x06
#define VS_FRAME_MJPEG			0x07
#define VS_FORMAT_MPEG2TS		0x0a
#define VS_FORMAT_DV			0x0c
#define VS_COLORFORMAT			0x0d
#define VS_FORMAT_FRAME_BASED		0x10
#define VS_FRAME_FRAME_BASED		0x11
#define VS_FORMAT_STREAM_BASED		0x12

/* Endpoint type */
#define EP_UNDEFINED			0x00
#define EP_GENERAL			0x01
#define EP_ENDPOINT			0x02
#define EP_INTERRUPT			0x03

/* Request codes */
#define RC_UNDEFINED			0x00
#define SET_CUR				0x01
#define GET_CUR				0x81
#define GET_MIN				0x82
#define GET_MAX				0x83
#define GET_RES				0x84
#define GET_LEN				0x85
#define GET_INFO			0x86
#define GET_DEF				0x87

/* VideoControl interface controls */
#define VC_CONTROL_UNDEFINED		0x00
#define VC_VIDEO_POWER_MODE_CONTROL	0x01
#define VC_REQUEST_ERROR_CODE_CONTROL	0x02

/* Terminal controls */
#define TE_CONTROL_UNDEFINED		0x00

/* Selector Unit controls */
#define SU_CONTROL_UNDEFINED		0x00
#define SU_INPUT_SELECT_CONTROL		0x01

/* Camera Terminal controls */
#define CT_CONTROL_UNDEFINED				0x00
#define CT_SCANNING_MODE_CONTROL			0x01
#define CT_AE_MODE_CONTROL				0x02
#define CT_AE_PRIORITY_CONTROL				0x03
#define CT_EXPOSURE_TIME_ABSOLUTE_CONTROL		0x04
#define CT_EXPOSURE_TIME_RELATIVE_CONTROL		0x05
#define CT_FOCUS_ABSOLUTE_CONTROL			0x06
#define CT_FOCUS_RELATIVE_CONTROL			0x07
#define CT_FOCUS_AUTO_CONTROL				0x08
#define CT_IRIS_ABSOLUTE_CONTROL			0x09
#define CT_IRIS_RELATIVE_CONTROL			0x0a
#define CT_ZOOM_ABSOLUTE_CONTROL			0x0b
#define CT_ZOOM_RELATIVE_CONTROL			0x0c
#define CT_PANTILT_ABSOLUTE_CONTROL			0x0d
#define CT_PANTILT_RELATIVE_CONTROL			0x0e
#define CT_ROLL_ABSOLUTE_CONTROL			0x0f
#define CT_ROLL_RELATIVE_CONTROL			0x10
#define CT_PRIVACY_CONTROL				0x11

/* Processing Unit controls */
#define PU_CONTROL_UNDEFINED				0x00
#define PU_BACKLIGHT_COMPENSATION_CONTROL		0x01
#define PU_BRIGHTNESS_CONTROL				0x02
#define PU_CONTRAST_CONTROL				0x03
#define PU_GAIN_CONTROL					0x04
#define PU_POWER_LINE_FREQUENCY_CONTROL			0x05
#define PU_HUE_CONTROL					0x06
#define PU_SATURATION_CONTROL				0x07
#define PU_SHARPNESS_CONTROL				0x08
#define PU_GAMMA_CONTROL				0x09
#define PU_WHITE_BALANCE_TEMPERATURE_CONTROL		0x0a
#define PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL	0x0b
#define PU_WHITE_BALANCE_COMPONENT_CONTROL		0x0c
#define PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL		0x0d
#define PU_DIGITAL_MULTIPLIER_CONTROL			0x0e
#define PU_DIGITAL_MULTIPLIER_LIMIT_CONTROL		0x0f
#define PU_HUE_AUTO_CONTROL				0x10
#define PU_ANALOG_VIDEO_STANDARD_CONTROL		0x11
#define PU_ANALOG_LOCK_STATUS_CONTROL			0x12

#define LXU_MOTOR_PANTILT_RELATIVE_CONTROL		0x01
#define LXU_MOTOR_PANTILT_RESET_CONTROL			0x02
#define LXU_MOTOR_FOCUS_MOTOR_CONTROL			0x03

/* VideoStreaming interface controls */
#define VS_CONTROL_UNDEFINED		0x00
#define VS_PROBE_CONTROL		0x01
#define VS_COMMIT_CONTROL		0x02
#define VS_STILL_PROBE_CONTROL		0x03
#define VS_STILL_COMMIT_CONTROL		0x04
#define VS_STILL_IMAGE_TRIGGER_CONTROL	0x05
#define VS_STREAM_ERROR_CODE_CONTROL	0x06
#define VS_GENERATE_KEY_FRAME_CONTROL	0x07
#define VS_UPDATE_FRAME_SEGMENT_CONTROL	0x08
#define VS_SYNC_DELAY_CONTROL		0x09

#define TT_VENDOR_SPECIFIC		0x0100
#define TT_STREAMING			0x0101

/* Input Terminal types */
#define ITT_VENDOR_SPECIFIC		0x0200
#define ITT_CAMERA			0x0201
#define ITT_MEDIA_TRANSPORT_INPUT	0x0202

/* Output Terminal types */
#define OTT_VENDOR_SPECIFIC		0x0300
#define OTT_DISPLAY			0x0301
#define OTT_MEDIA_TRANSPORT_OUTPUT	0x0302

/* External Terminal types */
#define EXTERNAL_VENDOR_SPECIFIC	0x0400
#define COMPOSITE_CONNECTOR		0x0401
#define SVIDEO_CONNECTOR		0x0402
#define COMPONENT_CONNECTOR		0x0403

#define UVC_TERM_INPUT			0x0000
#define UVC_TERM_OUTPUT			0x8000

#define UVC_ENTITY_TYPE(entity)		((entity)->type & 0x7fff)
#define UVC_ENTITY_IS_UNIT(entity)	(((entity)->type & 0xff00) == 0)
#define UVC_ENTITY_IS_TERM(entity)	(((entity)->type & 0xff00) != 0)
#define UVC_ENTITY_IS_ITERM(entity) \
	(((entity)->type & 0x8000) == UVC_TERM_INPUT)
#define UVC_ENTITY_IS_OTERM(entity) \
	(((entity)->type & 0x8000) == UVC_TERM_OUTPUT)

#define UVC_STATUS_TYPE_CONTROL		1
#define UVC_STATUS_TYPE_STREAMING	2

/* ------------------------------------------------------------------------
 * GUIDs
 */
#define UVC_GUID_UVC_CAMERA \
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}
#define UVC_GUID_UVC_OUTPUT \
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}
#define UVC_GUID_UVC_MEDIA_TRANSPORT_INPUT \
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03}
#define UVC_GUID_UVC_PROCESSING \
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}
#define UVC_GUID_UVC_SELECTOR \
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02}

#define UVC_GUID_LOGITECH_DEV_INFO \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	 0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x1e}
#define UVC_GUID_LOGITECH_USER_HW \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	 0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x1f}
#define UVC_GUID_LOGITECH_VIDEO \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	 0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x50}
#define UVC_GUID_LOGITECH_MOTOR \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	 0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x56}

#define UVC_GUID_FORMAT_MJPEG \
	{ 'M',  'J',  'P',  'G', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YUY2 \
	{ 'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_NV12 \
	{ 'N',  'V',  '1',  '2', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YV12 \
	{ 'Y',  'V',  '1',  '2', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_I420 \
	{ 'I',  '4',  '2',  '0', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_UYVY \
	{ 'U',  'Y',  'V',  'Y', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y800 \
	{ 'Y',  '8',  '0',  '0', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_BY8 \
	{ 'B',  'Y',  '8',  ' ', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}


/* ------------------------------------------------------------------------
 * Driver specific constants.
 */

#define DRIVER_VERSION_NUMBER	KERNEL_VERSION(0, 1, 0)

/* Number of isochronous URBs. */
#define UVC_URBS		5
/* Maximum number of packets per isochronous URB. */
#define UVC_MAX_ISO_PACKETS	40
/* Maximum frame size in bytes, for sanity checking. */
#define UVC_MAX_FRAME_SIZE	(16*1024*1024)
/* Maximum number of video buffers. */
#define UVC_MAX_VIDEO_BUFFERS	32
/* Maximum status buffer size in bytes of interrupt URB. */
#define UVC_MAX_STATUS_SIZE	16

#define UVC_CTRL_CONTROL_TIMEOUT	300
#define UVC_CTRL_STREAMING_TIMEOUT	1000

/* Devices quirks */
#define UVC_QUIRK_STATUS_INTERVAL	0x00000001
#define UVC_QUIRK_PROBE_MINMAX		0x00000002
#define UVC_QUIRK_PROBE_EXTRAFIELDS	0x00000004
#define UVC_QUIRK_BUILTIN_ISIGHT	0x00000008
#define UVC_QUIRK_STREAM_NO_FID		0x00000010
#define UVC_QUIRK_IGNORE_SELECTOR_UNIT	0x00000020
#define UVC_QUIRK_PRUNE_CONTROLS	0x00000040

/* Format flags */
#define UVC_FMT_FLAG_COMPRESSED		0x00000001
#define UVC_FMT_FLAG_STREAM		0x00000002

/* ------------------------------------------------------------------------
 * Structures.
 */

struct uvc_device;

/* TODO: Put the most frequently accessed fields at the beginning of
 * structures to maximize cache efficiency.
 */
struct uvc_streaming_control {
	__u16 bmHint;
	__u8  bFormatIndex;
	__u8  bFrameIndex;
	__u32 dwFrameInterval;
	__u16 wKeyFrameRate;
	__u16 wPFrameRate;
	__u16 wCompQuality;
	__u16 wCompWindowSize;
	__u16 wDelay;
	__u32 dwMaxVideoFrameSize;
	__u32 dwMaxPayloadTransferSize;
	__u32 dwClockFrequency;
	__u8  bmFramingInfo;
	__u8  bPreferedVersion;
	__u8  bMinVersion;
	__u8  bMaxVersion;
};

struct uvc_menu_info {
	__u32 value;
	__u8 name[32];
};

struct uvc_control_info {
	struct list_head list;
	struct list_head mappings;

	__u8 entity[16];
	__u8 index;
	__u8 selector;

	__u16 size;
	__u32 flags;
};

struct uvc_control_mapping {
	struct list_head list;

	struct uvc_control_info *ctrl;

	__u32 id;
	__u8 name[32];
	__u8 entity[16];
	__u8 selector;

	__u8 size;
	__u8 offset;
	enum v4l2_ctrl_type v4l2_type;
	__u32 data_type;

	struct uvc_menu_info *menu_info;
	__u32 menu_count;

	__s32 (*get) (struct uvc_control_mapping *mapping, __u8 query,
		      const __u8 *data);
	void (*set) (struct uvc_control_mapping *mapping, __s32 value,
		     __u8 *data);
};

struct uvc_control {
	struct uvc_entity *entity;
	struct uvc_control_info *info;

	__u8 index;	/* Used to match the uvc_control entry with a
			   uvc_control_info. */
	__u8 dirty : 1,
	     loaded : 1,
	     modified : 1;

	__u8 *data;
};

struct uvc_format_desc {
	char *name;
	__u8 guid[16];
	__u32 fcc;
};

/* The term 'entity' refers to both UVC units and UVC terminals.
 *
 * The type field is either the terminal type (wTerminalType in the terminal
 * descriptor), or the unit type (bDescriptorSubtype in the unit descriptor).
 * As the bDescriptorSubtype field is one byte long, the type value will
 * always have a null MSB for units. All terminal types defined by the UVC
 * specification have a non-null MSB, so it is safe to use the MSB to
 * differentiate between units and terminals as long as the descriptor parsing
 * code makes sure terminal types have a non-null MSB.
 *
 * For terminals, the type's most significant bit stores the terminal
 * direction (either UVC_TERM_INPUT or UVC_TERM_OUTPUT). The type field should
 * always be accessed with the UVC_ENTITY_* macros and never directly.
 */

struct uvc_entity {
	struct list_head list;		/* Entity as part of a UVC device. */
	struct list_head chain;		/* Entity as part of a video device
					 * chain. */
	__u8 id;
	__u16 type;
	char name[64];

	union {
		struct {
			__u16 wObjectiveFocalLengthMin;
			__u16 wObjectiveFocalLengthMax;
			__u16 wOcularFocalLength;
			__u8  bControlSize;
			__u8  *bmControls;
		} camera;

		struct {
			__u8  bControlSize;
			__u8  *bmControls;
			__u8  bTransportModeSize;
			__u8  *bmTransportModes;
		} media;

		struct {
			__u8  bSourceID;
		} output;

		struct {
			__u8  bSourceID;
			__u16 wMaxMultiplier;
			__u8  bControlSize;
			__u8  *bmControls;
			__u8  bmVideoStandards;
		} processing;

		struct {
			__u8  bNrInPins;
			__u8  *baSourceID;
		} selector;

		struct {
			__u8  guidExtensionCode[16];
			__u8  bNumControls;
			__u8  bNrInPins;
			__u8  *baSourceID;
			__u8  bControlSize;
			__u8  *bmControls;
			__u8  *bmControlsType;
		} extension;
	};

	unsigned int ncontrols;
	struct uvc_control *controls;
};

struct uvc_frame {
	__u8  bFrameIndex;
	__u8  bmCapabilities;
	__u16 wWidth;
	__u16 wHeight;
	__u32 dwMinBitRate;
	__u32 dwMaxBitRate;
	__u32 dwMaxVideoFrameBufferSize;
	__u8  bFrameIntervalType;
	__u32 dwDefaultFrameInterval;
	__u32 *dwFrameInterval;
};

struct uvc_format {
	__u8 type;
	__u8 index;
	__u8 bpp;
	__u8 colorspace;
	__u32 fcc;
	__u32 flags;

	char name[32];

	unsigned int nframes;
	struct uvc_frame *frame;
};

struct uvc_streaming_header {
	__u8 bNumFormats;
	__u8 bEndpointAddress;
	__u8 bTerminalLink;
	__u8 bControlSize;
	__u8 *bmaControls;
	/* The following fields are used by input headers only. */
	__u8 bmInfo;
	__u8 bStillCaptureMethod;
	__u8 bTriggerSupport;
	__u8 bTriggerUsage;
};

struct uvc_streaming {
	struct list_head list;

	struct usb_interface *intf;
	int intfnum;
	__u16 maxpsize;

	struct uvc_streaming_header header;
	enum v4l2_buf_type type;

	unsigned int nformats;
	struct uvc_format *format;

	struct uvc_streaming_control ctrl;
	struct uvc_format *cur_format;
	struct uvc_frame *cur_frame;

	struct mutex mutex;
};

enum uvc_buffer_state {
	UVC_BUF_STATE_IDLE	= 0,
	UVC_BUF_STATE_QUEUED	= 1,
	UVC_BUF_STATE_ACTIVE	= 2,
	UVC_BUF_STATE_DONE	= 3,
	UVC_BUF_STATE_ERROR	= 4,
};

struct uvc_buffer {
	unsigned long vma_use_count;
	struct list_head stream;

	/* Touched by interrupt handler. */
	struct v4l2_buffer buf;
	struct list_head queue;
	wait_queue_head_t wait;
	enum uvc_buffer_state state;
};

#define UVC_QUEUE_STREAMING		(1 << 0)
#define UVC_QUEUE_DISCONNECTED		(1 << 1)
#define UVC_QUEUE_DROP_INCOMPLETE	(1 << 2)

struct uvc_video_queue {
	enum v4l2_buf_type type;

	void *mem;
	unsigned int flags;
	__u32 sequence;

	unsigned int count;
	unsigned int buf_size;
	unsigned int buf_used;
	struct uvc_buffer buffer[UVC_MAX_VIDEO_BUFFERS];
	struct mutex mutex;	/* protects buffers and mainqueue */
	spinlock_t irqlock;	/* protects irqqueue */

	struct list_head mainqueue;
	struct list_head irqqueue;
};

struct uvc_video_device {
	struct uvc_device *dev;
	struct video_device *vdev;
	atomic_t active;
	unsigned int frozen : 1;

	struct list_head iterms;		/* Input terminals */
	struct uvc_entity *oterm;		/* Output terminal */
	struct uvc_entity *sterm;		/* USB streaming terminal */
	struct uvc_entity *processing;
	struct uvc_entity *selector;
	struct list_head extensions;
	struct mutex ctrl_mutex;

	struct uvc_video_queue queue;

	/* Video streaming object, must always be non-NULL. */
	struct uvc_streaming *streaming;

	void (*decode) (struct urb *urb, struct uvc_video_device *video,
			struct uvc_buffer *buf);

	/* Context data used by the bulk completion handler. */
	struct {
		__u8 header[256];
		unsigned int header_size;
		int skip_payload;
		__u32 payload_size;
		__u32 max_payload_size;
	} bulk;

	struct urb *urb[UVC_URBS];
	char *urb_buffer[UVC_URBS];
	dma_addr_t urb_dma[UVC_URBS];
	unsigned int urb_size;

	__u8 last_fid;
};

enum uvc_device_state {
	UVC_DEV_DISCONNECTED = 1,
};

struct uvc_device {
	struct usb_device *udev;
	struct usb_interface *intf;
	unsigned long warnings;
	__u32 quirks;
	int intfnum;
	char name[32];

	enum uvc_device_state state;
	struct kref kref;
	struct list_head list;

	/* Video control interface */
	__u16 uvc_version;
	__u32 clock_frequency;

	struct list_head entities;

	struct uvc_video_device video;

	/* Status Interrupt Endpoint */
	struct usb_host_endpoint *int_ep;
	struct urb *int_urb;
	__u8 *status;
	struct input_dev *input;

	/* Video Streaming interfaces */
	struct list_head streaming;
};

enum uvc_handle_state {
	UVC_HANDLE_PASSIVE	= 0,
	UVC_HANDLE_ACTIVE	= 1,
};

struct uvc_fh {
	struct uvc_video_device *device;
	enum uvc_handle_state state;
};

struct uvc_driver {
	struct usb_driver driver;

	struct mutex open_mutex;	/* protects from open/disconnect race */

	struct list_head devices;	/* struct uvc_device list */
	struct list_head controls;	/* struct uvc_control_info list */
	struct mutex ctrl_mutex;	/* protects controls and devices
					   lists */
};

/* ------------------------------------------------------------------------
 * Debugging, printing and logging
 */

#define UVC_TRACE_PROBE		(1 << 0)
#define UVC_TRACE_DESCR		(1 << 1)
#define UVC_TRACE_CONTROL	(1 << 2)
#define UVC_TRACE_FORMAT	(1 << 3)
#define UVC_TRACE_CAPTURE	(1 << 4)
#define UVC_TRACE_CALLS		(1 << 5)
#define UVC_TRACE_IOCTL		(1 << 6)
#define UVC_TRACE_FRAME		(1 << 7)
#define UVC_TRACE_SUSPEND	(1 << 8)
#define UVC_TRACE_STATUS	(1 << 9)

#define UVC_WARN_MINMAX		0
#define UVC_WARN_PROBE_DEF	1

extern unsigned int uvc_no_drop_param;
extern unsigned int uvc_trace_param;

#define uvc_trace(flag, msg...) \
	do { \
		if (uvc_trace_param & flag) \
			printk(KERN_DEBUG "uvcvideo: " msg); \
	} while (0)

#define uvc_warn_once(dev, warn, msg...) \
	do { \
		if (!test_and_set_bit(warn, &dev->warnings)) \
			printk(KERN_INFO "uvcvideo: " msg); \
	} while (0)

#define uvc_printk(level, msg...) \
	printk(level "uvcvideo: " msg)

#define UVC_GUID_FORMAT "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-" \
			"%02x%02x%02x%02x%02x%02x"
#define UVC_GUID_ARGS(guid) \
	(guid)[3],  (guid)[2],  (guid)[1],  (guid)[0], \
	(guid)[5],  (guid)[4], \
	(guid)[7],  (guid)[6], \
	(guid)[8],  (guid)[9], \
	(guid)[10], (guid)[11], (guid)[12], \
	(guid)[13], (guid)[14], (guid)[15]

/* --------------------------------------------------------------------------
 * Internal functions.
 */

/* Core driver */
extern struct uvc_driver uvc_driver;
extern void uvc_delete(struct kref *kref);

/* Video buffers queue management. */
extern void uvc_queue_init(struct uvc_video_queue *queue,
		enum v4l2_buf_type type);
extern int uvc_alloc_buffers(struct uvc_video_queue *queue,
		unsigned int nbuffers, unsigned int buflength);
extern int uvc_free_buffers(struct uvc_video_queue *queue);
extern int uvc_query_buffer(struct uvc_video_queue *queue,
		struct v4l2_buffer *v4l2_buf);
extern int uvc_queue_buffer(struct uvc_video_queue *queue,
		struct v4l2_buffer *v4l2_buf);
extern int uvc_dequeue_buffer(struct uvc_video_queue *queue,
		struct v4l2_buffer *v4l2_buf, int nonblocking);
extern int uvc_queue_enable(struct uvc_video_queue *queue, int enable);
extern void uvc_queue_cancel(struct uvc_video_queue *queue, int disconnect);
extern struct uvc_buffer *uvc_queue_next_buffer(struct uvc_video_queue *queue,
		struct uvc_buffer *buf);
extern unsigned int uvc_queue_poll(struct uvc_video_queue *queue,
		struct file *file, poll_table *wait);
static inline int uvc_queue_streaming(struct uvc_video_queue *queue)
{
	return queue->flags & UVC_QUEUE_STREAMING;
}

/* V4L2 interface */
extern const struct v4l2_file_operations uvc_fops;

/* Video */
extern int uvc_video_init(struct uvc_video_device *video);
extern int uvc_video_suspend(struct uvc_video_device *video);
extern int uvc_video_resume(struct uvc_video_device *video);
extern int uvc_video_enable(struct uvc_video_device *video, int enable);
extern int uvc_probe_video(struct uvc_video_device *video,
		struct uvc_streaming_control *probe);
extern int uvc_commit_video(struct uvc_video_device *video,
		struct uvc_streaming_control *ctrl);
extern int uvc_query_ctrl(struct uvc_device *dev, __u8 query, __u8 unit,
		__u8 intfnum, __u8 cs, void *data, __u16 size);

/* Status */
extern int uvc_status_init(struct uvc_device *dev);
extern void uvc_status_cleanup(struct uvc_device *dev);
extern int uvc_status_suspend(struct uvc_device *dev);
extern int uvc_status_resume(struct uvc_device *dev);

/* Controls */
extern struct uvc_control *uvc_find_control(struct uvc_video_device *video,
		__u32 v4l2_id, struct uvc_control_mapping **mapping);
extern int uvc_query_v4l2_ctrl(struct uvc_video_device *video,
		struct v4l2_queryctrl *v4l2_ctrl);

extern int uvc_ctrl_add_info(struct uvc_control_info *info);
extern int uvc_ctrl_add_mapping(struct uvc_control_mapping *mapping);
extern int uvc_ctrl_init_device(struct uvc_device *dev);
extern void uvc_ctrl_cleanup_device(struct uvc_device *dev);
extern int uvc_ctrl_resume_device(struct uvc_device *dev);
extern void uvc_ctrl_init(void);

extern int uvc_ctrl_begin(struct uvc_video_device *video);
extern int __uvc_ctrl_commit(struct uvc_video_device *video, int rollback);
static inline int uvc_ctrl_commit(struct uvc_video_device *video)
{
	return __uvc_ctrl_commit(video, 0);
}
static inline int uvc_ctrl_rollback(struct uvc_video_device *video)
{
	return __uvc_ctrl_commit(video, 1);
}

extern int uvc_ctrl_get(struct uvc_video_device *video,
		struct v4l2_ext_control *xctrl);
extern int uvc_ctrl_set(struct uvc_video_device *video,
		struct v4l2_ext_control *xctrl);

extern int uvc_xu_ctrl_query(struct uvc_video_device *video,
		struct uvc_xu_control *ctrl, int set);

/* Utility functions */
extern void uvc_simplify_fraction(uint32_t *numerator, uint32_t *denominator,
		unsigned int n_terms, unsigned int threshold);
extern uint32_t uvc_fraction_to_interval(uint32_t numerator,
		uint32_t denominator);
extern struct usb_host_endpoint *uvc_find_endpoint(
		struct usb_host_interface *alts, __u8 epaddr);

/* Quirks support */
void uvc_video_decode_isight(struct urb *urb, struct uvc_video_device *video,
		struct uvc_buffer *buf);

#endif /* __KERNEL__ */

#endif

