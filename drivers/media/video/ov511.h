#ifndef __LINUX_OV511_H
#define __LINUX_OV511_H

#include <asm/uaccess.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#include <linux/usb.h>
#include <linux/mutex.h>

#define OV511_DEBUG	/* Turn on debug messages */

#ifdef OV511_DEBUG
	#define PDEBUG(level, fmt, args...) \
		if (debug >= (level)) info("[%s:%d] " fmt, \
		__FUNCTION__, __LINE__ , ## args)
#else
	#define PDEBUG(level, fmt, args...) do {} while(0)
#endif

/* This macro restricts an int variable to an inclusive range */
#define RESTRICT_TO_RANGE(v,mi,ma) { \
	if ((v) < (mi)) (v) = (mi); \
	else if ((v) > (ma)) (v) = (ma); \
}

/* --------------------------------- */
/* DEFINES FOR OV511 AND OTHER CHIPS */
/* --------------------------------- */

/* USB IDs */
#define VEND_OMNIVISION	0x05A9
#define PROD_OV511	0x0511
#define PROD_OV511PLUS	0xA511
#define PROD_OV518	0x0518
#define PROD_OV518PLUS	0xA518

#define VEND_MATTEL	0x0813
#define PROD_ME2CAM	0x0002

/* --------------------------------- */
/*     OV51x REGISTER MNEMONICS      */
/* --------------------------------- */

/* Camera interface register numbers */
#define R511_CAM_DELAY		0x10
#define R511_CAM_EDGE		0x11
#define R511_CAM_PXCNT		0x12
#define R511_CAM_LNCNT		0x13
#define R511_CAM_PXDIV		0x14
#define R511_CAM_LNDIV		0x15
#define R511_CAM_UV_EN		0x16
#define R511_CAM_LINE_MODE	0x17
#define R511_CAM_OPTS		0x18

/* Snapshot mode camera interface register numbers */
#define R511_SNAP_FRAME		0x19
#define R511_SNAP_PXCNT		0x1A
#define R511_SNAP_LNCNT		0x1B
#define R511_SNAP_PXDIV		0x1C
#define R511_SNAP_LNDIV		0x1D
#define R511_SNAP_UV_EN		0x1E
#define R511_SNAP_OPTS		0x1F

/* DRAM register numbers */
#define R511_DRAM_FLOW_CTL	0x20
#define R511_DRAM_ARCP		0x21
#define R511_DRAM_MRC		0x22
#define R511_DRAM_RFC		0x23

/* ISO FIFO register numbers */
#define R51x_FIFO_PSIZE		0x30	/* 2 bytes wide w/ OV518(+) */
#define R511_FIFO_OPTS		0x31

/* Parallel IO register numbers */
#define R511_PIO_OPTS		0x38
#define R511_PIO_DATA		0x39
#define R511_PIO_BIST		0x3E
#define R518_GPIO_IN		0x55	/* OV518(+) only */
#define R518_GPIO_OUT		0x56	/* OV518(+) only */
#define R518_GPIO_CTL		0x57	/* OV518(+) only */
#define R518_GPIO_PULSE_IN	0x58	/* OV518(+) only */
#define R518_GPIO_PULSE_CLEAR	0x59	/* OV518(+) only */
#define R518_GPIO_PULSE_POL	0x5a	/* OV518(+) only */
#define R518_GPIO_PULSE_EN	0x5b	/* OV518(+) only */
#define R518_GPIO_RESET		0x5c	/* OV518(+) only */

/* I2C registers */
#define R511_I2C_CTL		0x40
#define R518_I2C_CTL		0x47	/* OV518(+) only */
#define R51x_I2C_W_SID		0x41
#define R51x_I2C_SADDR_3	0x42
#define R51x_I2C_SADDR_2	0x43
#define R51x_I2C_R_SID		0x44
#define R51x_I2C_DATA		0x45
#define R51x_I2C_CLOCK		0x46
#define R51x_I2C_TIMEOUT	0x47

/* I2C snapshot registers */
#define R511_SI2C_SADDR_3	0x48
#define R511_SI2C_DATA		0x49

/* System control registers */
#define R51x_SYS_RESET		0x50
		/* Reset type definitions */
#define 	OV511_RESET_UDC		0x01
#define 	OV511_RESET_I2C		0x02
#define 	OV511_RESET_FIFO	0x04
#define 	OV511_RESET_OMNICE	0x08
#define 	OV511_RESET_DRAM	0x10
#define 	OV511_RESET_CAM_INT	0x20
#define 	OV511_RESET_OV511	0x40
#define 	OV511_RESET_NOREGS	0x3F /* All but OV511 & regs */
#define 	OV511_RESET_ALL		0x7F

#define R511_SYS_CLOCK_DIV	0x51
#define R51x_SYS_SNAP		0x52
#define R51x_SYS_INIT         	0x53
#define R511_SYS_PWR_CLK	0x54 /* OV511+/OV518(+) only */
#define R511_SYS_LED_CTL	0x55 /* OV511+ only */
#define R511_SYS_USER		0x5E
#define R511_SYS_CUST_ID	0x5F

/* OmniCE (compression) registers */
#define R511_COMP_PHY		0x70
#define R511_COMP_PHUV		0x71
#define R511_COMP_PVY		0x72
#define R511_COMP_PVUV		0x73
#define R511_COMP_QHY		0x74
#define R511_COMP_QHUV		0x75
#define R511_COMP_QVY		0x76
#define R511_COMP_QVUV		0x77
#define R511_COMP_EN		0x78
#define R511_COMP_LUT_EN	0x79
#define R511_COMP_LUT_BEGIN	0x80

/* --------------------------------- */
/*         ALTERNATE NUMBERS         */
/* --------------------------------- */

/* Alternate numbers for various max packet sizes (OV511 only) */
#define OV511_ALT_SIZE_992	0
#define OV511_ALT_SIZE_993	1
#define OV511_ALT_SIZE_768	2
#define OV511_ALT_SIZE_769	3
#define OV511_ALT_SIZE_512	4
#define OV511_ALT_SIZE_513	5
#define OV511_ALT_SIZE_257	6
#define OV511_ALT_SIZE_0	7

/* Alternate numbers for various max packet sizes (OV511+ only) */
#define OV511PLUS_ALT_SIZE_0	0
#define OV511PLUS_ALT_SIZE_33	1
#define OV511PLUS_ALT_SIZE_129	2
#define OV511PLUS_ALT_SIZE_257	3
#define OV511PLUS_ALT_SIZE_385	4
#define OV511PLUS_ALT_SIZE_513	5
#define OV511PLUS_ALT_SIZE_769	6
#define OV511PLUS_ALT_SIZE_961	7

/* Alternate numbers for various max packet sizes (OV518(+) only) */
#define OV518_ALT_SIZE_0	0
#define OV518_ALT_SIZE_128	1
#define OV518_ALT_SIZE_256	2
#define OV518_ALT_SIZE_384	3
#define OV518_ALT_SIZE_512	4
#define OV518_ALT_SIZE_640	5
#define OV518_ALT_SIZE_768	6
#define OV518_ALT_SIZE_896	7

/* --------------------------------- */
/*     OV7610 REGISTER MNEMONICS     */
/* --------------------------------- */

/* OV7610 registers */
#define OV7610_REG_GAIN          0x00	/* gain setting (5:0) */
#define OV7610_REG_BLUE          0x01	/* blue channel balance */
#define OV7610_REG_RED           0x02	/* red channel balance */
#define OV7610_REG_SAT           0x03	/* saturation */
					/* 04 reserved */
#define OV7610_REG_CNT           0x05	/* Y contrast */
#define OV7610_REG_BRT           0x06	/* Y brightness */
					/* 08-0b reserved */
#define OV7610_REG_BLUE_BIAS     0x0C	/* blue channel bias (5:0) */
#define OV7610_REG_RED_BIAS      0x0D	/* read channel bias (5:0) */
#define OV7610_REG_GAMMA_COEFF   0x0E	/* gamma settings */
#define OV7610_REG_WB_RANGE      0x0F	/* AEC/ALC/S-AWB settings */
#define OV7610_REG_EXP           0x10	/* manual exposure setting */
#define OV7610_REG_CLOCK         0x11	/* polarity/clock prescaler */
#define OV7610_REG_COM_A         0x12	/* misc common regs */
#define OV7610_REG_COM_B         0x13	/* misc common regs */
#define OV7610_REG_COM_C         0x14	/* misc common regs */
#define OV7610_REG_COM_D         0x15	/* misc common regs */
#define OV7610_REG_FIELD_DIVIDE  0x16	/* field interval/mode settings */
#define OV7610_REG_HWIN_START    0x17	/* horizontal window start */
#define OV7610_REG_HWIN_END      0x18	/* horizontal window end */
#define OV7610_REG_VWIN_START    0x19	/* vertical window start */
#define OV7610_REG_VWIN_END      0x1A	/* vertical window end */
#define OV7610_REG_PIXEL_SHIFT   0x1B	/* pixel shift */
#define OV7610_REG_ID_HIGH       0x1C	/* manufacturer ID MSB */
#define OV7610_REG_ID_LOW        0x1D	/* manufacturer ID LSB */
					/* 0e-0f reserved */
#define OV7610_REG_COM_E         0x20	/* misc common regs */
#define OV7610_REG_YOFFSET       0x21	/* Y channel offset */
#define OV7610_REG_UOFFSET       0x22	/* U channel offset */
					/* 23 reserved */
#define OV7610_REG_ECW           0x24	/* Exposure white level for AEC */
#define OV7610_REG_ECB           0x25	/* Exposure black level for AEC */
#define OV7610_REG_COM_F         0x26	/* misc settings */
#define OV7610_REG_COM_G         0x27	/* misc settings */
#define OV7610_REG_COM_H         0x28	/* misc settings */
#define OV7610_REG_COM_I         0x29	/* misc settings */
#define OV7610_REG_FRAMERATE_H   0x2A	/* frame rate MSB + misc */
#define OV7610_REG_FRAMERATE_L   0x2B	/* frame rate LSB */
#define OV7610_REG_ALC           0x2C	/* Auto Level Control settings */
#define OV7610_REG_COM_J         0x2D	/* misc settings */
#define OV7610_REG_VOFFSET       0x2E	/* V channel offset adjustment */
#define OV7610_REG_ARRAY_BIAS	 0x2F	/* Array bias -- don't change */
					/* 30-32 reserved */
#define OV7610_REG_YGAMMA        0x33	/* misc gamma settings (7:6) */
#define OV7610_REG_BIAS_ADJUST   0x34	/* misc bias settings */
#define OV7610_REG_COM_L         0x35	/* misc settings */
					/* 36-37 reserved */
#define OV7610_REG_COM_K         0x38	/* misc registers */

/* --------------------------------- */
/*           I2C ADDRESSES           */
/* --------------------------------- */

#define OV7xx0_SID   0x42
#define OV6xx0_SID   0xC0
#define OV8xx0_SID   0xA0
#define KS0127_SID   0xD8
#define SAA7111A_SID 0x48

/* --------------------------------- */
/*       MISCELLANEOUS DEFINES       */
/* --------------------------------- */

#define I2C_CLOCK_PRESCALER 	0x03

#define FRAMES_PER_DESC		10	/* FIXME - What should this be? */
#define MAX_FRAME_SIZE_PER_DESC	993	/* For statically allocated stuff */
#define PIXELS_PER_SEG		256	/* Pixels per segment */

#define OV511_ENDPOINT_ADDRESS	1	/* Isoc endpoint number */

#define OV511_NUMFRAMES	2
#if OV511_NUMFRAMES > VIDEO_MAX_FRAME
	#error "OV511_NUMFRAMES is too high"
#endif

#define OV511_NUMSBUF		2

/* Control transfers use up to 4 bytes */
#define OV511_CBUF_SIZE		4

/* Size of usb_make_path() buffer */
#define OV511_USB_PATH_LEN	64

/* Bridge types */
enum {
	BRG_UNKNOWN,
	BRG_OV511,
	BRG_OV511PLUS,
	BRG_OV518,
	BRG_OV518PLUS,
};

/* Bridge classes */
enum {
	BCL_UNKNOWN,
	BCL_OV511,
	BCL_OV518,
};

/* Sensor types */
enum {
	SEN_UNKNOWN,
	SEN_OV76BE,
	SEN_OV7610,
	SEN_OV7620,
	SEN_OV7620AE,
	SEN_OV6620,
	SEN_OV6630,
	SEN_OV6630AE,
	SEN_OV6630AF,
	SEN_OV8600,
	SEN_KS0127,
	SEN_KS0127B,
	SEN_SAA7111A,
};

enum {
	STATE_SCANNING,		/* Scanning for start */
	STATE_HEADER,		/* Parsing header */
	STATE_LINES,		/* Parsing lines */
};

/* Buffer states */
enum {
	BUF_NOT_ALLOCATED,
	BUF_ALLOCATED,
};

/* --------- Definition of ioctl interface --------- */

#define OV511_INTERFACE_VER 101

/* LED options */
enum {
	LED_OFF,
	LED_ON,
	LED_AUTO,
};

/* Raw frame formats */
enum {
	RAWFMT_INVALID,
	RAWFMT_YUV400,
	RAWFMT_YUV420,
	RAWFMT_YUV422,
	RAWFMT_GBR422,
};

struct ov511_i2c_struct {
	unsigned char slave; /* Write slave ID (read ID - 1) */
	unsigned char reg;   /* Index of register */
	unsigned char value; /* User sets this w/ write, driver does w/ read */
	unsigned char mask;  /* Bits to be changed. Not used with read ops */
};

/* ioctls */
#define OV511IOC_WI2C     _IOW('v', BASE_VIDIOCPRIVATE + 5, \
			       struct ov511_i2c_struct)
#define OV511IOC_RI2C    _IOWR('v', BASE_VIDIOCPRIVATE + 6, \
			       struct ov511_i2c_struct)
/* ------------- End IOCTL interface -------------- */

struct usb_ov511;		/* Forward declaration */

struct ov511_sbuf {
	struct usb_ov511 *ov;
	unsigned char *data;
	struct urb *urb;
	spinlock_t lock;
	int n;
};

enum {
	FRAME_UNUSED,		/* Unused (no MCAPTURE) */
	FRAME_READY,		/* Ready to start grabbing */
	FRAME_GRABBING,		/* In the process of being grabbed into */
	FRAME_DONE,		/* Finished grabbing, but not been synced yet */
	FRAME_ERROR,		/* Something bad happened while processing */
};

struct ov511_regvals {
	enum {
		OV511_DONE_BUS,
		OV511_REG_BUS,
		OV511_I2C_BUS,
	} bus;
	unsigned char reg;
	unsigned char val;
};

struct ov511_frame {
	int framenum;		/* Index of this frame */
	unsigned char *data;	/* Frame buffer */
	unsigned char *tempdata; /* Temp buffer for multi-stage conversions */
	unsigned char *rawdata;	/* Raw camera data buffer */
	unsigned char *compbuf;	/* Temp buffer for decompressor */

	int depth;		/* Bytes per pixel */
	int width;		/* Width application is expecting */
	int height;		/* Height application is expecting */

	int rawwidth;		/* Actual width of frame sent from camera */
	int rawheight;		/* Actual height of frame sent from camera */

	int sub_flag;		/* Sub-capture mode for this frame? */
	unsigned int format;	/* Format for this frame */
	int compressed;		/* Is frame compressed? */

	volatile int grabstate;	/* State of grabbing */
	int scanstate;		/* State of scanning */

	int bytes_recvd;	/* Number of image bytes received from camera */

	long bytes_read;	/* Amount that has been read() */

	wait_queue_head_t wq;	/* Processes waiting */

	int snapshot;		/* True if frame was a snapshot */
};

#define DECOMP_INTERFACE_VER 4

/* Compression module operations */
struct ov51x_decomp_ops {
	int (*decomp_400)(unsigned char *, unsigned char *, unsigned char *,
			  int, int, int);
	int (*decomp_420)(unsigned char *, unsigned char *, unsigned char *,
			  int, int, int);
	int (*decomp_422)(unsigned char *, unsigned char *, unsigned char *,
			  int, int, int);
	struct module *owner;
};

struct usb_ov511 {
	struct video_device *vdev;
	struct usb_device *dev;

	int customid;
	char *desc;
	unsigned char iface;
	char usb_path[OV511_USB_PATH_LEN];

	/* Determined by sensor type */
	int maxwidth;
	int maxheight;
	int minwidth;
	int minheight;

	int brightness;
	int colour;
	int contrast;
	int hue;
	int whiteness;
	int exposure;
	int auto_brt;		/* Auto brightness enabled flag */
	int auto_gain;		/* Auto gain control enabled flag */
	int auto_exp;		/* Auto exposure enabled flag */
	int backlight;		/* Backlight exposure algorithm flag */
	int mirror;		/* Image is reversed horizontally */

	int led_policy;		/* LED: off|on|auto; OV511+ only */

	struct mutex lock;	/* Serializes user-accessible operations */
	int user;		/* user count for exclusive use */

	int streaming;		/* Are we streaming Isochronous? */
	int grabbing;		/* Are we grabbing? */

	int compress;		/* Should the next frame be compressed? */
	int compress_inited;	/* Are compression params uploaded? */

	int lightfreq;		/* Power (lighting) frequency */
	int bandfilt;		/* Banding filter enabled flag */

	unsigned char *fbuf;	/* Videodev buffer area */
	unsigned char *tempfbuf; /* Temporary (intermediate) buffer area */
	unsigned char *rawfbuf;	/* Raw camera data buffer area */

	int sub_flag;		/* Pix Array subcapture on flag */
	int subx;		/* Pix Array subcapture x offset */
	int suby;		/* Pix Array subcapture y offset */
	int subw;		/* Pix Array subcapture width */
	int subh;		/* Pix Array subcapture height */

	int curframe;		/* Current receiving sbuf */
	struct ov511_frame frame[OV511_NUMFRAMES];

	struct ov511_sbuf sbuf[OV511_NUMSBUF];

	wait_queue_head_t wq;	/* Processes waiting */

	int snap_enabled;	/* Snapshot mode enabled */

	int bridge;		/* Type of bridge (BRG_*) */
	int bclass;		/* Class of bridge (BCL_*) */
	int sensor;		/* Type of image sensor chip (SEN_*) */

	int packet_size;	/* Frame size per isoc desc */
	int packet_numbering;	/* Is ISO frame numbering enabled? */

	/* Framebuffer/sbuf management */
	int buf_state;
	struct mutex buf_lock;

	struct ov51x_decomp_ops *decomp_ops;

	/* Stop streaming while changing picture settings */
	int stop_during_set;

	int stopped;		/* Streaming is temporarily paused */

	/* Video decoder stuff */
	int input;		/* Composite, S-VIDEO, etc... */
	int num_inputs;		/* Number of inputs */
	int norm; 		/* NTSC / PAL / SECAM */
	int has_decoder;	/* Device has a video decoder */
	int pal;		/* Device is designed for PAL resolution */

	/* I2C interface */
	struct mutex i2c_lock;	  /* Protect I2C controller regs */
	unsigned char primary_i2c_slave;  /* I2C write id of sensor */

	/* Control transaction stuff */
	unsigned char *cbuf;		/* Buffer for payload */
	struct mutex cbuf_lock;
};

/* Used to represent a list of values and their respective symbolic names */
struct symbolic_list {
	int num;
	char *name;
};

#define NOT_DEFINED_STR "Unknown"

/* Returns the name of the matching element in the symbolic_list array. The
 * end of the list must be marked with an element that has a NULL name.
 */
static inline char *
symbolic(struct symbolic_list list[], int num)
{
	int i;

	for (i = 0; list[i].name != NULL; i++)
			if (list[i].num == num)
				return (list[i].name);

	return (NOT_DEFINED_STR);
}

/* Compression stuff */

#define OV511_QUANTABLESIZE	64
#define OV518_QUANTABLESIZE	32

#define OV511_YQUANTABLE { \
	0, 1, 1, 2, 2, 3, 3, 4, \
	1, 1, 1, 2, 2, 3, 4, 4, \
	1, 1, 2, 2, 3, 4, 4, 4, \
	2, 2, 2, 3, 4, 4, 4, 4, \
	2, 2, 3, 4, 4, 5, 5, 5, \
	3, 3, 4, 4, 5, 5, 5, 5, \
	3, 4, 4, 4, 5, 5, 5, 5, \
	4, 4, 4, 4, 5, 5, 5, 5  \
}

#define OV511_UVQUANTABLE { \
	0, 2, 2, 3, 4, 4, 4, 4, \
	2, 2, 2, 4, 4, 4, 4, 4, \
	2, 2, 3, 4, 4, 4, 4, 4, \
	3, 4, 4, 4, 4, 4, 4, 4, \
	4, 4, 4, 4, 4, 4, 4, 4, \
	4, 4, 4, 4, 4, 4, 4, 4, \
	4, 4, 4, 4, 4, 4, 4, 4, \
	4, 4, 4, 4, 4, 4, 4, 4  \
}

#define OV518_YQUANTABLE { \
	5, 4, 5, 6, 6, 7, 7, 7, \
	5, 5, 5, 5, 6, 7, 7, 7, \
	6, 6, 6, 6, 7, 7, 7, 8, \
	7, 7, 6, 7, 7, 7, 8, 8  \
}

#define OV518_UVQUANTABLE { \
	6, 6, 6, 7, 7, 7, 7, 7, \
	6, 6, 6, 7, 7, 7, 7, 7, \
	6, 6, 6, 7, 7, 7, 7, 8, \
	7, 7, 7, 7, 7, 7, 8, 8  \
}

#endif
