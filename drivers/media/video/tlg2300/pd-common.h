#ifndef PD_COMMON_H
#define PD_COMMON_H

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/videodev2.h>
#include <linux/semaphore.h>
#include <linux/usb.h>
#include <linux/poll.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-device.h>

#include "dvb_frontend.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dmxdev.h"

#define SBUF_NUM	8
#define MAX_BUFFER_NUM	6
#define PK_PER_URB	32
#define ISO_PKT_SIZE	3072

#define POSEIDON_STATE_NONE		(0x0000)
#define POSEIDON_STATE_ANALOG		(0x0001)
#define POSEIDON_STATE_FM		(0x0002)
#define POSEIDON_STATE_DVBT		(0x0004)
#define POSEIDON_STATE_VBI		(0x0008)
#define POSEIDON_STATE_DISCONNECT	(0x0080)

#define PM_SUSPEND_DELAY	3

#define V4L_PAL_VBI_LINES	18
#define V4L_NTSC_VBI_LINES	12
#define V4L_PAL_VBI_FRAMESIZE	(V4L_PAL_VBI_LINES * 1440 * 2)
#define V4L_NTSC_VBI_FRAMESIZE	(V4L_NTSC_VBI_LINES * 1440 * 2)

#define TUNER_FREQ_MIN		(45000000)
#define TUNER_FREQ_MAX		(862000000)

struct vbi_data {
	struct video_device	*v_dev;
	struct video_data	*video;
	struct front_face	*front;

	unsigned int		copied;
	unsigned int		vbi_size; /* the whole size of two fields */
	int 			users;
};

/*
 * This is the running context of the video, it is useful for
 * resume()
 */
struct running_context {
	u32		freq;		/* VIDIOC_S_FREQUENCY */
	int		audio_idx;	/* VIDIOC_S_TUNER    */
	v4l2_std_id	tvnormid;	/* VIDIOC_S_STD     */
	int		sig_index;	/* VIDIOC_S_INPUT  */
	struct v4l2_pix_format pix;	/* VIDIOC_S_FMT   */
};

struct video_data {
	/* v4l2 video device */
	struct video_device	*v_dev;

	/* the working context */
	struct running_context	context;

	/* for data copy */
	int		field_count;

	char		*dst;
	int		lines_copied;
	int		prev_left;

	int		lines_per_field;
	int		lines_size;

	/* for communication */
	u8			endpoint_addr;
	struct urb 		*urb_array[SBUF_NUM];
	struct vbi_data		*vbi;
	struct poseidon 	*pd;
	struct front_face	*front;

	int			is_streaming;
	int			users;

	/* for bubble handler */
	struct work_struct	bubble_work;
};

enum pcm_stream_state {
	STREAM_OFF,
	STREAM_ON,
	STREAM_SUSPEND,
};

#define AUDIO_BUFS (3)
#define CAPTURE_STREAM_EN 1
struct poseidon_audio {
	struct urb		*urb_array[AUDIO_BUFS];
	unsigned int 		copied_position;
	struct snd_pcm_substream   *capture_pcm_substream;

	unsigned int 		rcv_position;
	struct	snd_card	*card;
	int 			card_close;

	int 			users;
	int			pm_state;
	enum pcm_stream_state 	capture_stream;
};

struct radio_data {
	__u32		fm_freq;
	int		users;
	unsigned int	is_radio_streaming;
	int		pre_emphasis;
	struct video_device *fm_dev;
};

#define DVB_SBUF_NUM		4
#define DVB_URB_BUF_SIZE	0x2000
struct pd_dvb_adapter {
	struct dvb_adapter	dvb_adap;
	struct dvb_frontend	dvb_fe;
	struct dmxdev		dmxdev;
	struct dvb_demux	demux;

	atomic_t		users;
	atomic_t		active_feed;

	/* data transfer */
	s32			is_streaming;
	struct urb		*urb_array[DVB_SBUF_NUM];
	struct poseidon		*pd_device;
	u8			ep_addr;
	u8			reserved[3];

	/* data for power resume*/
	struct dvb_frontend_parameters fe_param;

	/* for channel scanning */
	int		prev_freq;
	int		bandwidth;
	unsigned long	last_jiffies;
};

struct front_face {
	/* use this field to distinguish VIDEO and VBI */
	enum v4l2_buf_type	type;

	/* for host */
	struct videobuf_queue	q;

	/* the bridge for host and device */
	struct videobuf_buffer	*curr_frame;

	/* for device */
	spinlock_t		queue_lock;
	struct list_head	active;
	struct poseidon		*pd;
};

struct poseidon {
	struct list_head	device_list;

	struct mutex		lock;
	struct kref		kref;

	/* for V4L2 */
	struct v4l2_device	v4l2_dev;

	/* hardware info */
	struct usb_device	*udev;
	struct usb_interface	*interface;
	int 			cur_transfer_mode;

	struct video_data	video_data;	/* video */
	struct vbi_data		vbi_data;	/* vbi	 */
	struct poseidon_audio	audio;		/* audio (alsa) */
	struct radio_data	radio_data;	/* FM	 */
	struct pd_dvb_adapter	dvb_data;	/* DVB	 */

	u32			state;
	struct file		*file_for_stream; /* the active stream*/

#ifdef CONFIG_PM
	int (*pm_suspend)(struct poseidon *);
	int (*pm_resume)(struct poseidon *);
	pm_message_t		msg;

	struct work_struct	pm_work;
	u8			portnum;
#endif
};

struct poseidon_format {
	char 	*name;
	int	fourcc;		 /* video4linux 2	  */
	int	depth;		 /* bit/pixel		  */
	int	flags;
};

struct poseidon_tvnorm {
	v4l2_std_id	v4l2_id;
	char		name[12];
	u32		tlg_tvnorm;
};

/* video */
int pd_video_init(struct poseidon *);
void pd_video_exit(struct poseidon *);
int stop_all_video_stream(struct poseidon *);

/* alsa audio */
int poseidon_audio_init(struct poseidon *);
int poseidon_audio_free(struct poseidon *);
#ifdef CONFIG_PM
int pm_alsa_suspend(struct poseidon *);
int pm_alsa_resume(struct poseidon *);
#endif

/* dvb */
int pd_dvb_usb_device_init(struct poseidon *);
void pd_dvb_usb_device_exit(struct poseidon *);
void pd_dvb_usb_device_cleanup(struct poseidon *);
int pd_dvb_get_adapter_num(struct pd_dvb_adapter *);
void dvb_stop_streaming(struct pd_dvb_adapter *);

/* FM */
int poseidon_fm_init(struct poseidon *);
int poseidon_fm_exit(struct poseidon *);
struct video_device *vdev_init(struct poseidon *, struct video_device *);

/* vendor command ops */
int send_set_req(struct poseidon*, u8, s32, s32*);
int send_get_req(struct poseidon*, u8, s32, void*, s32*, s32);
s32 set_tuner_mode(struct poseidon*, unsigned char);

/* bulk urb alloc/free */
int alloc_bulk_urbs_generic(struct urb **urb_array, int num,
			struct usb_device *udev, u8 ep_addr,
			int buf_size, gfp_t gfp_flags,
			usb_complete_t complete_fn, void *context);
void free_all_urb_generic(struct urb **urb_array, int num);

/* misc */
void poseidon_delete(struct kref *kref);
void destroy_video_device(struct video_device **v_dev);
extern int debug_mode;
void set_debug_mode(struct video_device *vfd, int debug_mode);

#ifdef CONFIG_PM
#define in_hibernation(pd) (pd->msg.event == PM_EVENT_FREEZE)
#else
#define in_hibernation(pd) (0)
#endif
#define get_pm_count(p) (atomic_read(&(p)->interface->pm_usage_cnt))

#define log(a, ...) printk(KERN_DEBUG "\t[ %s : %.3d ] "a"\n", \
				__func__, __LINE__,  ## __VA_ARGS__)

/* for power management */
#define logpm(pd) do {\
			if (debug_mode & 0x10)\
				log();\
		} while (0)

#define logs(f) do { \
			if ((debug_mode & 0x4) && \
				(f)->type == V4L2_BUF_TYPE_VBI_CAPTURE) \
					log("type : VBI");\
								\
			if ((debug_mode & 0x8) && \
				(f)->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) \
					log("type : VIDEO");\
		} while (0)
#endif
