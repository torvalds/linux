#ifndef _USB_VIDEO_H_
#define _USB_VIDEO_H_

#ifndef __KERNEL__
#error "The uvcvideo.h header is deprecated, use linux/uvcvideo.h instead."
#endif /* __KERNEL__ */

#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/usb.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#include <linux/videodev2.h>
#include <media/media-device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-core.h>

/* --------------------------------------------------------------------------
 * UVC constants
 */

#define UVC_TERM_INPUT			0x0000
#define UVC_TERM_OUTPUT			0x8000
#define UVC_TERM_DIRECTION(term)	((term)->type & 0x8000)

#define UVC_ENTITY_TYPE(entity)		((entity)->type & 0x7fff)
#define UVC_ENTITY_IS_UNIT(entity)	(((entity)->type & 0xff00) == 0)
#define UVC_ENTITY_IS_TERM(entity)	(((entity)->type & 0xff00) != 0)
#define UVC_ENTITY_IS_ITERM(entity) \
	(UVC_ENTITY_IS_TERM(entity) && \
	((entity)->type & 0x8000) == UVC_TERM_INPUT)
#define UVC_ENTITY_IS_OTERM(entity) \
	(UVC_ENTITY_IS_TERM(entity) && \
	((entity)->type & 0x8000) == UVC_TERM_OUTPUT)


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

#define UVC_GUID_FORMAT_MJPEG \
	{ 'M',  'J',  'P',  'G', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YUY2 \
	{ 'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YUY2_ISIGHT \
	{ 'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0x00, 0x00, 0x38, 0x9b, 0x71}
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
#define UVC_GUID_FORMAT_Y8 \
	{ 'Y',  '8',  ' ',  ' ', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y10 \
	{ 'Y',  '1',  '0',  ' ', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y12 \
	{ 'Y',  '1',  '2',  ' ', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y16 \
	{ 'Y',  '1',  '6',  ' ', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_BY8 \
	{ 'B',  'Y',  '8',  ' ', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_BA81 \
	{ 'B',  'A',  '8',  '1', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_GBRG \
	{ 'G',  'B',  'R',  'G', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_GRBG \
	{ 'G',  'R',  'B',  'G', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_RGGB \
	{ 'R',  'G',  'G',  'B', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_RGBP \
	{ 'R',  'G',  'B',  'P', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_M420 \
	{ 'M',  '4',  '2',  '0', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}

#define UVC_GUID_FORMAT_H264 \
	{ 'H',  '2',  '6',  '4', 0x00, 0x00, 0x10, 0x00, \
	 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}

/* ------------------------------------------------------------------------
 * Driver specific constants.
 */

#define DRIVER_VERSION		"1.1.1"

/* Number of isochronous URBs. */
#define UVC_URBS		5
/* Maximum number of packets per URB. */
#define UVC_MAX_PACKETS		32
/* Maximum status buffer size in bytes of interrupt URB. */
#define UVC_MAX_STATUS_SIZE	16

#define UVC_CTRL_CONTROL_TIMEOUT	300
#define UVC_CTRL_STREAMING_TIMEOUT	5000

/* Maximum allowed number of control mappings per device */
#define UVC_MAX_CONTROL_MAPPINGS	1024
#define UVC_MAX_CONTROL_MENU_ENTRIES	32

/* Devices quirks */
#define UVC_QUIRK_STATUS_INTERVAL	0x00000001
#define UVC_QUIRK_PROBE_MINMAX		0x00000002
#define UVC_QUIRK_PROBE_EXTRAFIELDS	0x00000004
#define UVC_QUIRK_BUILTIN_ISIGHT	0x00000008
#define UVC_QUIRK_STREAM_NO_FID		0x00000010
#define UVC_QUIRK_IGNORE_SELECTOR_UNIT	0x00000020
#define UVC_QUIRK_FIX_BANDWIDTH		0x00000080
#define UVC_QUIRK_PROBE_DEF		0x00000100
#define UVC_QUIRK_RESTRICT_FRAME_RATE	0x00000200
#define UVC_QUIRK_RESTORE_CTRLS_ON_INIT	0x00000400
#define UVC_QUIRK_FORCE_Y8		0x00000800

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
struct uvc_control_info {
	struct list_head mappings;

	__u8 entity[16];
	__u8 index;	/* Bit index in bmControls */
	__u8 selector;

	__u16 size;
	__u32 flags;
};

struct uvc_control_mapping {
	struct list_head list;
	struct list_head ev_subs;

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

	__u32 master_id;
	__s32 master_manual;
	__u32 slave_ids[2];

	__s32 (*get) (struct uvc_control_mapping *mapping, __u8 query,
		      const __u8 *data);
	void (*set) (struct uvc_control_mapping *mapping, __s32 value,
		     __u8 *data);
};

struct uvc_control {
	struct uvc_entity *entity;
	struct uvc_control_info info;

	__u8 index;	/* Used to match the uvc_control entry with a
			   uvc_control_info. */
	__u8 dirty:1,
	     loaded:1,
	     modified:1,
	     cached:1,
	     initialized:1;

	__u8 *uvc_data;
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

#define UVC_ENTITY_FLAG_DEFAULT		(1 << 0)

struct uvc_entity {
	struct list_head list;		/* Entity as part of a UVC device. */
	struct list_head chain;		/* Entity as part of a video device
					 * chain. */
	unsigned int flags;

	__u8 id;
	__u16 type;
	char name[64];

	/* Media controller-related fields. */
	struct video_device *vdev;
	struct v4l2_subdev subdev;
	unsigned int num_pads;
	unsigned int num_links;
	struct media_pad *pads;

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
		} output;

		struct {
			__u16 wMaxMultiplier;
			__u8  bControlSize;
			__u8  *bmControls;
			__u8  bmVideoStandards;
		} processing;

		struct {
		} selector;

		struct {
			__u8  guidExtensionCode[16];
			__u8  bNumControls;
			__u8  bControlSize;
			__u8  *bmControls;
			__u8  *bmControlsType;
		} extension;
	};

	__u8 bNrInPins;
	__u8 *baSourceID;

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

enum uvc_buffer_state {
	UVC_BUF_STATE_IDLE	= 0,
	UVC_BUF_STATE_QUEUED	= 1,
	UVC_BUF_STATE_ACTIVE	= 2,
	UVC_BUF_STATE_READY	= 3,
	UVC_BUF_STATE_DONE	= 4,
	UVC_BUF_STATE_ERROR	= 5,
};

struct uvc_buffer {
	struct vb2_buffer buf;
	struct list_head queue;

	enum uvc_buffer_state state;
	unsigned int error;

	void *mem;
	unsigned int length;
	unsigned int bytesused;

	u32 pts;
};

#define UVC_QUEUE_DISCONNECTED		(1 << 0)
#define UVC_QUEUE_DROP_CORRUPTED	(1 << 1)

struct uvc_video_queue {
	struct vb2_queue queue;
	struct mutex mutex;			/* Protects queue */

	unsigned int flags;
	unsigned int buf_used;

	spinlock_t irqlock;			/* Protects irqqueue */
	struct list_head irqqueue;
};

struct uvc_video_chain {
	struct uvc_device *dev;
	struct list_head list;

	struct list_head entities;		/* All entities */
	struct uvc_entity *processing;		/* Processing unit */
	struct uvc_entity *selector;		/* Selector unit */

	struct mutex ctrl_mutex;		/* Protects ctrl.info */

	struct v4l2_prio_state prio;		/* V4L2 priority state */
	u32 caps;				/* V4L2 chain-wide caps */
};

struct uvc_stats_frame {
	unsigned int size;		/* Number of bytes captured */
	unsigned int first_data;	/* Index of the first non-empty packet */

	unsigned int nb_packets;	/* Number of packets */
	unsigned int nb_empty;		/* Number of empty packets */
	unsigned int nb_invalid;	/* Number of packets with an invalid header */
	unsigned int nb_errors;		/* Number of packets with the error bit set */

	unsigned int nb_pts;		/* Number of packets with a PTS timestamp */
	unsigned int nb_pts_diffs;	/* Number of PTS differences inside a frame */
	unsigned int last_pts_diff;	/* Index of the last PTS difference */
	bool has_initial_pts;		/* Whether the first non-empty packet has a PTS */
	bool has_early_pts;		/* Whether a PTS is present before the first non-empty packet */
	u32 pts;			/* PTS of the last packet */

	unsigned int nb_scr;		/* Number of packets with a SCR timestamp */
	unsigned int nb_scr_diffs;	/* Number of SCR.STC differences inside a frame */
	u16 scr_sof;			/* SCR.SOF of the last packet */
	u32 scr_stc;			/* SCR.STC of the last packet */
};

struct uvc_stats_stream {
	struct timespec start_ts;	/* Stream start timestamp */
	struct timespec stop_ts;	/* Stream stop timestamp */

	unsigned int nb_frames;		/* Number of frames */

	unsigned int nb_packets;	/* Number of packets */
	unsigned int nb_empty;		/* Number of empty packets */
	unsigned int nb_invalid;	/* Number of packets with an invalid header */
	unsigned int nb_errors;		/* Number of packets with the error bit set */

	unsigned int nb_pts_constant;	/* Number of frames with constant PTS */
	unsigned int nb_pts_early;	/* Number of frames with early PTS */
	unsigned int nb_pts_initial;	/* Number of frames with initial PTS */

	unsigned int nb_scr_count_ok;	/* Number of frames with at least one SCR per non empty packet */
	unsigned int nb_scr_diffs_ok;	/* Number of frames with varying SCR.STC */
	unsigned int scr_sof_count;	/* STC.SOF counter accumulated since stream start */
	unsigned int scr_sof;		/* STC.SOF of the last packet */
	unsigned int min_sof;		/* Minimum STC.SOF value */
	unsigned int max_sof;		/* Maximum STC.SOF value */
};

struct uvc_streaming {
	struct list_head list;
	struct uvc_device *dev;
	struct video_device *vdev;
	struct uvc_video_chain *chain;
	atomic_t active;

	struct usb_interface *intf;
	int intfnum;
	__u16 maxpsize;

	struct uvc_streaming_header header;
	enum v4l2_buf_type type;

	unsigned int nformats;
	struct uvc_format *format;

	struct uvc_streaming_control ctrl;
	struct uvc_format *def_format;
	struct uvc_format *cur_format;
	struct uvc_frame *cur_frame;

	/* Protect access to ctrl, cur_format, cur_frame and hardware video
	 * probe control.
	 */
	struct mutex mutex;

	/* Buffers queue. */
	unsigned int frozen : 1;
	struct uvc_video_queue queue;
	void (*decode) (struct urb *urb, struct uvc_streaming *video,
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

	__u32 sequence;
	__u8 last_fid;

	/* debugfs */
	struct dentry *debugfs_dir;
	struct {
		struct uvc_stats_frame frame;
		struct uvc_stats_stream stream;
	} stats;

	/* Timestamps support. */
	struct uvc_clock {
		struct uvc_clock_sample {
			u32 dev_stc;
			u16 dev_sof;
			struct timespec host_ts;
			u16 host_sof;
		} *samples;

		unsigned int head;
		unsigned int count;
		unsigned int size;

		u16 last_sof;
		u16 sof_offset;

		spinlock_t lock;
	} clock;
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
	struct mutex lock;		/* Protects users */
	unsigned int users;
	atomic_t nmappings;

	/* Video control interface */
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device mdev;
#endif
	struct v4l2_device vdev;
	__u16 uvc_version;
	__u32 clock_frequency;

	struct list_head entities;
	struct list_head chains;

	/* Video Streaming interfaces */
	struct list_head streams;
	atomic_t nstreams;

	/* Status Interrupt Endpoint */
	struct usb_host_endpoint *int_ep;
	struct urb *int_urb;
	__u8 *status;
	struct input_dev *input;
	char input_phys[64];
};

enum uvc_handle_state {
	UVC_HANDLE_PASSIVE	= 0,
	UVC_HANDLE_ACTIVE	= 1,
};

struct uvc_fh {
	struct v4l2_fh vfh;
	struct uvc_video_chain *chain;
	struct uvc_streaming *stream;
	enum uvc_handle_state state;
};

struct uvc_driver {
	struct usb_driver driver;
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
#define UVC_TRACE_FRAME		(1 << 7)
#define UVC_TRACE_SUSPEND	(1 << 8)
#define UVC_TRACE_STATUS	(1 << 9)
#define UVC_TRACE_VIDEO		(1 << 10)
#define UVC_TRACE_STATS		(1 << 11)
#define UVC_TRACE_CLOCK		(1 << 12)

#define UVC_WARN_MINMAX		0
#define UVC_WARN_PROBE_DEF	1
#define UVC_WARN_XU_GET_RES	2

extern unsigned int uvc_clock_param;
extern unsigned int uvc_no_drop_param;
extern unsigned int uvc_trace_param;
extern unsigned int uvc_timeout_param;

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

/* --------------------------------------------------------------------------
 * Internal functions.
 */

/* Core driver */
extern struct uvc_driver uvc_driver;

extern struct uvc_entity *uvc_entity_by_id(struct uvc_device *dev, int id);

/* Video buffers queue management. */
extern int uvc_queue_init(struct uvc_video_queue *queue,
		enum v4l2_buf_type type, int drop_corrupted);
extern void uvc_queue_release(struct uvc_video_queue *queue);
extern int uvc_request_buffers(struct uvc_video_queue *queue,
		struct v4l2_requestbuffers *rb);
extern int uvc_query_buffer(struct uvc_video_queue *queue,
		struct v4l2_buffer *v4l2_buf);
extern int uvc_create_buffers(struct uvc_video_queue *queue,
		struct v4l2_create_buffers *v4l2_cb);
extern int uvc_queue_buffer(struct uvc_video_queue *queue,
		struct v4l2_buffer *v4l2_buf);
extern int uvc_dequeue_buffer(struct uvc_video_queue *queue,
		struct v4l2_buffer *v4l2_buf, int nonblocking);
extern int uvc_queue_streamon(struct uvc_video_queue *queue,
			      enum v4l2_buf_type type);
extern int uvc_queue_streamoff(struct uvc_video_queue *queue,
			       enum v4l2_buf_type type);
extern void uvc_queue_cancel(struct uvc_video_queue *queue, int disconnect);
extern struct uvc_buffer *uvc_queue_next_buffer(struct uvc_video_queue *queue,
		struct uvc_buffer *buf);
extern int uvc_queue_mmap(struct uvc_video_queue *queue,
		struct vm_area_struct *vma);
extern unsigned int uvc_queue_poll(struct uvc_video_queue *queue,
		struct file *file, poll_table *wait);
#ifndef CONFIG_MMU
extern unsigned long uvc_queue_get_unmapped_area(struct uvc_video_queue *queue,
		unsigned long pgoff);
#endif
extern int uvc_queue_allocated(struct uvc_video_queue *queue);
static inline int uvc_queue_streaming(struct uvc_video_queue *queue)
{
	return vb2_is_streaming(&queue->queue);
}

/* V4L2 interface */
extern const struct v4l2_ioctl_ops uvc_ioctl_ops;
extern const struct v4l2_file_operations uvc_fops;

/* Media controller */
extern int uvc_mc_register_entities(struct uvc_video_chain *chain);
extern void uvc_mc_cleanup_entity(struct uvc_entity *entity);

/* Video */
extern int uvc_video_init(struct uvc_streaming *stream);
extern int uvc_video_suspend(struct uvc_streaming *stream);
extern int uvc_video_resume(struct uvc_streaming *stream, int reset);
extern int uvc_video_enable(struct uvc_streaming *stream, int enable);
extern int uvc_probe_video(struct uvc_streaming *stream,
		struct uvc_streaming_control *probe);
extern int uvc_query_ctrl(struct uvc_device *dev, __u8 query, __u8 unit,
		__u8 intfnum, __u8 cs, void *data, __u16 size);
void uvc_video_clock_update(struct uvc_streaming *stream,
			    struct v4l2_buffer *v4l2_buf,
			    struct uvc_buffer *buf);

/* Status */
extern int uvc_status_init(struct uvc_device *dev);
extern void uvc_status_cleanup(struct uvc_device *dev);
extern int uvc_status_start(struct uvc_device *dev, gfp_t flags);
extern void uvc_status_stop(struct uvc_device *dev);

/* Controls */
extern const struct v4l2_subscribed_event_ops uvc_ctrl_sub_ev_ops;

extern int uvc_query_v4l2_ctrl(struct uvc_video_chain *chain,
		struct v4l2_queryctrl *v4l2_ctrl);
extern int uvc_query_v4l2_menu(struct uvc_video_chain *chain,
		struct v4l2_querymenu *query_menu);

extern int uvc_ctrl_add_mapping(struct uvc_video_chain *chain,
		const struct uvc_control_mapping *mapping);
extern int uvc_ctrl_init_device(struct uvc_device *dev);
extern void uvc_ctrl_cleanup_device(struct uvc_device *dev);
extern int uvc_ctrl_restore_values(struct uvc_device *dev);

extern int uvc_ctrl_begin(struct uvc_video_chain *chain);
extern int __uvc_ctrl_commit(struct uvc_fh *handle, int rollback,
			const struct v4l2_ext_control *xctrls,
			unsigned int xctrls_count);
static inline int uvc_ctrl_commit(struct uvc_fh *handle,
			const struct v4l2_ext_control *xctrls,
			unsigned int xctrls_count)
{
	return __uvc_ctrl_commit(handle, 0, xctrls, xctrls_count);
}
static inline int uvc_ctrl_rollback(struct uvc_fh *handle)
{
	return __uvc_ctrl_commit(handle, 1, NULL, 0);
}

extern int uvc_ctrl_get(struct uvc_video_chain *chain,
		struct v4l2_ext_control *xctrl);
extern int uvc_ctrl_set(struct uvc_video_chain *chain,
		struct v4l2_ext_control *xctrl);

extern int uvc_xu_ctrl_query(struct uvc_video_chain *chain,
		struct uvc_xu_control_query *xqry);

/* Utility functions */
extern void uvc_simplify_fraction(uint32_t *numerator, uint32_t *denominator,
		unsigned int n_terms, unsigned int threshold);
extern uint32_t uvc_fraction_to_interval(uint32_t numerator,
		uint32_t denominator);
extern struct usb_host_endpoint *uvc_find_endpoint(
		struct usb_host_interface *alts, __u8 epaddr);

/* Quirks support */
void uvc_video_decode_isight(struct urb *urb, struct uvc_streaming *stream,
		struct uvc_buffer *buf);

/* debugfs and statistics */
int uvc_debugfs_init(void);
void uvc_debugfs_cleanup(void);
int uvc_debugfs_init_stream(struct uvc_streaming *stream);
void uvc_debugfs_cleanup_stream(struct uvc_streaming *stream);

size_t uvc_video_stats_dump(struct uvc_streaming *stream, char *buf,
			    size_t size);

#endif
