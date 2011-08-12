/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2010 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
	Driver architecture
	*******************

	saa7164_core.c/buffer.c/cards.c/i2c.c/dvb.c
		|	: Standard Linux driver framework for creating
		|	: exposing and managing interfaces to the rest
		|	: of the kernel or userland. Also uses _fw.c to load
		|	: firmware direct into the PCIe bus, bypassing layers.
		V
	saa7164_api..()	: Translate kernel specific functions/features
		|	: into command buffers.
		V
	saa7164_cmd..()	: Manages the flow of command packets on/off,
		|	: the bus. Deal with bus errors, timeouts etc.
		V
	saa7164_bus..() : Manage a read/write memory ring buffer in the
		|	: PCIe Address space.
		|
		|		saa7164_fw...()	: Load any frimware
		|			|	: direct into the device
		V			V
	<- ----------------- PCIe address space -------------------- ->
*/

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/kdev_t.h>
#include <linux/mutex.h>
#include <linux/crc32.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <media/tuner.h>
#include <media/tveeprom.h>
#include <media/videobuf-dma-sg.h>
#include <media/videobuf-dvb.h>
#include <dvb_demux.h>
#include <dvb_frontend.h>
#include <dvb_net.h>
#include <dvbdev.h>
#include <dmxdev.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-chip-ident.h>

#include "saa7164-reg.h"
#include "saa7164-types.h"

#define SAA7164_MAXBOARDS 8

#define UNSET (-1U)
#define SAA7164_BOARD_NOAUTO			UNSET
#define SAA7164_BOARD_UNKNOWN			0
#define SAA7164_BOARD_UNKNOWN_REV2		1
#define SAA7164_BOARD_UNKNOWN_REV3		2
#define SAA7164_BOARD_HAUPPAUGE_HVR2250		3
#define SAA7164_BOARD_HAUPPAUGE_HVR2200		4
#define SAA7164_BOARD_HAUPPAUGE_HVR2200_2	5
#define SAA7164_BOARD_HAUPPAUGE_HVR2200_3	6
#define SAA7164_BOARD_HAUPPAUGE_HVR2250_2	7
#define SAA7164_BOARD_HAUPPAUGE_HVR2250_3	8
#define SAA7164_BOARD_HAUPPAUGE_HVR2200_4	9

#define SAA7164_MAX_UNITS		8
#define SAA7164_TS_NUMBER_OF_LINES	312
#define SAA7164_PS_NUMBER_OF_LINES	256
#define SAA7164_PT_ENTRIES		16 /* (312 * 188) / 4096 */
#define SAA7164_MAX_ENCODER_BUFFERS	64 /* max 5secs of latency at 6Mbps */
#define SAA7164_MAX_VBI_BUFFERS		64

/* Port related defines */
#define SAA7164_PORT_TS1	(0)
#define SAA7164_PORT_TS2	(SAA7164_PORT_TS1 + 1)
#define SAA7164_PORT_ENC1	(SAA7164_PORT_TS2 + 1)
#define SAA7164_PORT_ENC2	(SAA7164_PORT_ENC1 + 1)
#define SAA7164_PORT_VBI1	(SAA7164_PORT_ENC2 + 1)
#define SAA7164_PORT_VBI2	(SAA7164_PORT_VBI1 + 1)
#define SAA7164_MAX_PORTS	(SAA7164_PORT_VBI2 + 1)

#define DBGLVL_FW    4
#define DBGLVL_DVB   8
#define DBGLVL_I2C  16
#define DBGLVL_API  32
#define DBGLVL_CMD  64
#define DBGLVL_BUS 128
#define DBGLVL_IRQ 256
#define DBGLVL_BUF 512
#define DBGLVL_ENC 1024
#define DBGLVL_VBI 2048
#define DBGLVL_THR 4096
#define DBGLVL_CPU 8192

#define SAA7164_NORMS \
	(V4L2_STD_NTSC_M |  V4L2_STD_NTSC_M_JP |  V4L2_STD_NTSC_443)

enum port_t {
	SAA7164_MPEG_UNDEFINED = 0,
	SAA7164_MPEG_DVB,
	SAA7164_MPEG_ENCODER,
	SAA7164_MPEG_VBI,
};

enum saa7164_i2c_bus_nr {
	SAA7164_I2C_BUS_0 = 0,
	SAA7164_I2C_BUS_1,
	SAA7164_I2C_BUS_2,
};

enum saa7164_buffer_flags {
	SAA7164_BUFFER_UNDEFINED = 0,
	SAA7164_BUFFER_FREE,
	SAA7164_BUFFER_BUSY,
	SAA7164_BUFFER_FULL
};

enum saa7164_unit_type {
	SAA7164_UNIT_UNDEFINED = 0,
	SAA7164_UNIT_DIGITAL_DEMODULATOR,
	SAA7164_UNIT_ANALOG_DEMODULATOR,
	SAA7164_UNIT_TUNER,
	SAA7164_UNIT_EEPROM,
	SAA7164_UNIT_ZILOG_IRBLASTER,
	SAA7164_UNIT_ENCODER,
};

/* The PCIe bridge doesn't grant direct access to i2c.
 * Instead, you address i2c devices using a uniqely
 * allocated 'unitid' value via a messaging API. This
 * is a problem. The kernel and existing demod/tuner
 * drivers expect to talk 'i2c', so we have to maintain
 * a translation layer, and a series of functions to
 * convert i2c bus + device address into a unit id.
 */
struct saa7164_unit {
	enum saa7164_unit_type type;
	u8	id;
	char	*name;
	enum saa7164_i2c_bus_nr i2c_bus_nr;
	u8	i2c_bus_addr;
	u8	i2c_reg_len;
};

struct saa7164_board {
	char	*name;
	enum port_t porta, portb, portc,
		portd, porte, portf;
	enum {
		SAA7164_CHIP_UNDEFINED = 0,
		SAA7164_CHIP_REV2,
		SAA7164_CHIP_REV3,
	} chiprev;
	struct	saa7164_unit unit[SAA7164_MAX_UNITS];
};

struct saa7164_subid {
	u16     subvendor;
	u16     subdevice;
	u32     card;
};

struct saa7164_encoder_fh {
	struct saa7164_port *port;
	atomic_t v4l_reading;
};

struct saa7164_vbi_fh {
	struct saa7164_port *port;
	atomic_t v4l_reading;
};

struct saa7164_histogram_bucket {
	u32 val;
	u32 count;
	u64 update_time;
};

struct saa7164_histogram {
	char name[32];
	struct saa7164_histogram_bucket counter1[64];
};

struct saa7164_user_buffer {
	struct list_head list;

	/* Attributes */
	u8  *data;
	u32 pos;
	u32 actual_size;

	u32 crc;
};

struct saa7164_fw_status {

	/* RISC Core details */
	u32	status;
	u32	mode;
	u32	spec;
	u32	inst;
	u32	cpuload;
	u32	remainheap;

	/* Firmware version */
	u32	version;
	u32	major;
	u32	sub;
	u32	rel;
	u32	buildnr;
};

struct saa7164_dvb {
	struct mutex lock;
	struct dvb_adapter adapter;
	struct dvb_frontend *frontend;
	struct dvb_demux demux;
	struct dmxdev dmxdev;
	struct dmx_frontend fe_hw;
	struct dmx_frontend fe_mem;
	struct dvb_net net;
	int feeding;
};

struct saa7164_i2c {
	struct saa7164_dev		*dev;

	enum saa7164_i2c_bus_nr		nr;

	/* I2C I/O */
	struct i2c_adapter		i2c_adap;
	struct i2c_algo_bit_data	i2c_algo;
	struct i2c_client		i2c_client;
	u32				i2c_rc;
};

struct saa7164_ctrl {
	struct v4l2_queryctrl v;
};

struct saa7164_tvnorm {
	char		*name;
	v4l2_std_id	id;
};

struct saa7164_encoder_params {
	struct saa7164_tvnorm encodernorm;
	u32 height;
	u32 width;
	u32 is_50hz;
	u32 bitrate; /* bps */
	u32 bitrate_peak; /* bps */
	u32 bitrate_mode;
	u32 stream_type; /* V4L2_MPEG_STREAM_TYPE_MPEG2_TS */

	u32 audio_sampling_freq;
	u32 ctl_mute;
	u32 ctl_aspect;
	u32 refdist;
	u32 gop_size;
};

struct saa7164_vbi_params {
	struct saa7164_tvnorm encodernorm;
	u32 height;
	u32 width;
	u32 is_50hz;
	u32 bitrate; /* bps */
	u32 bitrate_peak; /* bps */
	u32 bitrate_mode;
	u32 stream_type; /* V4L2_MPEG_STREAM_TYPE_MPEG2_TS */

	u32 audio_sampling_freq;
	u32 ctl_mute;
	u32 ctl_aspect;
	u32 refdist;
	u32 gop_size;
};

struct saa7164_port;

struct saa7164_buffer {
	struct list_head list;

	/* Note of which h/w buffer list index position we occupy */
	int idx;

	struct saa7164_port *port;

	/* Hardware Specific */
	/* PCI Memory allocations */
	enum saa7164_buffer_flags flags; /* Free, Busy, Full */

	/* A block of page align PCI memory */
	u32 pci_size;	/* PCI allocation size in bytes */
	u64 __iomem *cpu;	/* Virtual address */
	dma_addr_t dma;	/* Physical address */
	u32 crc;	/* Checksum for the entire buffer data */

	/* A page table that splits the block into a number of entries */
	u32 pt_size;		/* PCI allocation size in bytes */
	u64 __iomem *pt_cpu;		/* Virtual address */
	dma_addr_t pt_dma;	/* Physical address */

	/* Encoder fops */
	u32 pos;
	u32 actual_size;
};

struct saa7164_port {

	struct saa7164_dev *dev;
	enum port_t type;
	int nr;

	/* --- Generic port attributes --- */

	/* HW stream parameters */
	struct tmHWStreamParameters hw_streamingparams;

	/* DMA configuration values, is seeded during initialization */
	struct tmComResDMATermDescrHeader hwcfg;

	/* hardware specific registers */
	u32 bufcounter;
	u32 pitch;
	u32 bufsize;
	u32 bufoffset;
	u32 bufptr32l;
	u32 bufptr32h;
	u64 bufptr64;

	u32 numpte;	/* Number of entries in array, only valid in head */

	struct mutex dmaqueue_lock;
	struct saa7164_buffer dmaqueue;

	u64 last_irq_msecs, last_svc_msecs;
	u64 last_irq_msecs_diff, last_svc_msecs_diff;
	u32 last_svc_wp;
	u32 last_svc_rp;
	u64 last_irq_svc_msecs_diff;
	u64 last_read_msecs, last_read_msecs_diff;
	u64 last_poll_msecs, last_poll_msecs_diff;

	struct saa7164_histogram irq_interval;
	struct saa7164_histogram svc_interval;
	struct saa7164_histogram irq_svc_interval;
	struct saa7164_histogram read_interval;
	struct saa7164_histogram poll_interval;

	/* --- DVB Transport Specific --- */
	struct saa7164_dvb dvb;

	/* --- Encoder/V4L related attributes --- */
	/* Encoder */
	/* Defaults established in saa7164-encoder.c */
	struct saa7164_tvnorm encodernorm;
	u32 height;
	u32 width;
	u32 freq;
	u32 ts_packet_size;
	u32 ts_packet_count;
	u8 mux_input;
	u8 encoder_profile;
	u8 video_format;
	u8 audio_format;
	u8 video_resolution;
	u16 ctl_brightness;
	u16 ctl_contrast;
	u16 ctl_hue;
	u16 ctl_saturation;
	u16 ctl_sharpness;
	s8 ctl_volume;

	struct tmComResAFeatureDescrHeader audfeat;
	struct tmComResEncoderDescrHeader encunit;
	struct tmComResProcDescrHeader vidproc;
	struct tmComResExtDevDescrHeader ifunit;
	struct tmComResTunerDescrHeader tunerunit;

	struct work_struct workenc;

	/* V4L Encoder Video */
	struct saa7164_encoder_params encoder_params;
	struct video_device *v4l_device;
	atomic_t v4l_reader_count;

	struct saa7164_buffer list_buf_used;
	struct saa7164_buffer list_buf_free;
	wait_queue_head_t wait_read;

	/* V4L VBI */
	struct tmComResVBIFormatDescrHeader vbi_fmt_ntsc;
	struct saa7164_vbi_params vbi_params;

	/* Debug */
	u32 sync_errors;
	u32 v_cc_errors;
	u32 a_cc_errors;
	u8 last_v_cc;
	u8 last_a_cc;
	u32 done_first_interrupt;
};

struct saa7164_dev {
	struct list_head	devlist;
	atomic_t		refcount;

	/* pci stuff */
	struct pci_dev	*pci;
	unsigned char	pci_rev, pci_lat;
	int		pci_bus, pci_slot;
	u32		__iomem *lmmio;
	u8		__iomem *bmmio;
	u32		__iomem *lmmio2;
	u8		__iomem *bmmio2;
	int		pci_irqmask;

	/* board details */
	int	nr;
	int	hwrevision;
	u32	board;
	char	name[16];

	/* firmware status */
	struct saa7164_fw_status	fw_status;
	u32				firmwareloaded;

	struct tmComResHWDescr		hwdesc;
	struct tmComResInterfaceDescr	intfdesc;
	struct tmComResBusDescr		busdesc;

	struct tmComResBusInfo		bus;

	/* Interrupt status and ack registers */
	u32 int_status;
	u32 int_ack;

	struct cmd			cmds[SAA_CMD_MAX_MSG_UNITS];
	struct mutex			lock;

	/* I2c related */
	struct saa7164_i2c i2c_bus[3];

	/* Transport related */
	struct saa7164_port ports[SAA7164_MAX_PORTS];

	/* Deferred command/api interrupts handling */
	struct work_struct workcmd;

	/* A kernel thread to monitor the firmware log, used
	 * only in debug mode.
	 */
	struct task_struct *kthread;

};

extern struct list_head saa7164_devlist;
extern unsigned int waitsecs;
extern unsigned int encoder_buffers;
extern unsigned int vbi_buffers;

/* ----------------------------------------------------------- */
/* saa7164-core.c                                              */
void saa7164_dumpregs(struct saa7164_dev *dev, u32 addr);
void saa7164_dumphex16(struct saa7164_dev *dev, u8 *buf, int len);
void saa7164_getfirmwarestatus(struct saa7164_dev *dev);
u32 saa7164_getcurrentfirmwareversion(struct saa7164_dev *dev);
void saa7164_histogram_update(struct saa7164_histogram *hg, u32 val);

/* ----------------------------------------------------------- */
/* saa7164-fw.c                                                */
int saa7164_downloadfirmware(struct saa7164_dev *dev);

/* ----------------------------------------------------------- */
/* saa7164-i2c.c                                               */
extern int saa7164_i2c_register(struct saa7164_i2c *bus);
extern int saa7164_i2c_unregister(struct saa7164_i2c *bus);
extern void saa7164_call_i2c_clients(struct saa7164_i2c *bus,
	unsigned int cmd, void *arg);

/* ----------------------------------------------------------- */
/* saa7164-bus.c                                               */
int saa7164_bus_setup(struct saa7164_dev *dev);
void saa7164_bus_dump(struct saa7164_dev *dev);
int saa7164_bus_set(struct saa7164_dev *dev, struct tmComResInfo* msg,
	void *buf);
int saa7164_bus_get(struct saa7164_dev *dev, struct tmComResInfo* msg,
	void *buf, int peekonly);

/* ----------------------------------------------------------- */
/* saa7164-cmd.c                                               */
int saa7164_cmd_send(struct saa7164_dev *dev,
	u8 id, enum tmComResCmd command, u16 controlselector,
	u16 size, void *buf);
void saa7164_cmd_signal(struct saa7164_dev *dev, u8 seqno);
int saa7164_irq_dequeue(struct saa7164_dev *dev);

/* ----------------------------------------------------------- */
/* saa7164-api.c                                               */
int saa7164_api_get_fw_version(struct saa7164_dev *dev, u32 *version);
int saa7164_api_enum_subdevs(struct saa7164_dev *dev);
int saa7164_api_i2c_read(struct saa7164_i2c *bus, u8 addr, u32 reglen, u8 *reg,
	u32 datalen, u8 *data);
int saa7164_api_i2c_write(struct saa7164_i2c *bus, u8 addr,
	u32 datalen, u8 *data);
int saa7164_api_dif_write(struct saa7164_i2c *bus, u8 addr,
	u32 datalen, u8 *data);
int saa7164_api_read_eeprom(struct saa7164_dev *dev, u8 *buf, int buflen);
int saa7164_api_set_gpiobit(struct saa7164_dev *dev, u8 unitid, u8 pin);
int saa7164_api_clear_gpiobit(struct saa7164_dev *dev, u8 unitid, u8 pin);
int saa7164_api_transition_port(struct saa7164_port *port, u8 mode);
int saa7164_api_initialize_dif(struct saa7164_port *port);
int saa7164_api_configure_dif(struct saa7164_port *port, u32 std);
int saa7164_api_set_encoder(struct saa7164_port *port);
int saa7164_api_get_encoder(struct saa7164_port *port);
int saa7164_api_set_aspect_ratio(struct saa7164_port *port);
int saa7164_api_set_usercontrol(struct saa7164_port *port, u8 ctl);
int saa7164_api_get_usercontrol(struct saa7164_port *port, u8 ctl);
int saa7164_api_set_videomux(struct saa7164_port *port);
int saa7164_api_audio_mute(struct saa7164_port *port, int mute);
int saa7164_api_set_audio_volume(struct saa7164_port *port, s8 level);
int saa7164_api_set_audio_std(struct saa7164_port *port);
int saa7164_api_set_audio_detection(struct saa7164_port *port, int autodetect);
int saa7164_api_get_videomux(struct saa7164_port *port);
int saa7164_api_set_vbi_format(struct saa7164_port *port);
int saa7164_api_set_debug(struct saa7164_dev *dev, u8 level);
int saa7164_api_collect_debug(struct saa7164_dev *dev);
int saa7164_api_get_load_info(struct saa7164_dev *dev,
	struct tmFwInfoStruct *i);

/* ----------------------------------------------------------- */
/* saa7164-cards.c                                             */
extern struct saa7164_board saa7164_boards[];
extern const unsigned int saa7164_bcount;

extern struct saa7164_subid saa7164_subids[];
extern const unsigned int saa7164_idcount;

extern void saa7164_card_list(struct saa7164_dev *dev);
extern void saa7164_gpio_setup(struct saa7164_dev *dev);
extern void saa7164_card_setup(struct saa7164_dev *dev);

extern int saa7164_i2caddr_to_reglen(struct saa7164_i2c *bus, int addr);
extern int saa7164_i2caddr_to_unitid(struct saa7164_i2c *bus, int addr);
extern char *saa7164_unitid_name(struct saa7164_dev *dev, u8 unitid);

/* ----------------------------------------------------------- */
/* saa7164-dvb.c                                               */
extern int saa7164_dvb_register(struct saa7164_port *port);
extern int saa7164_dvb_unregister(struct saa7164_port *port);

/* ----------------------------------------------------------- */
/* saa7164-buffer.c                                            */
extern struct saa7164_buffer *saa7164_buffer_alloc(
	struct saa7164_port *port, u32 len);
extern int saa7164_buffer_dealloc(struct saa7164_buffer *buf);
extern void saa7164_buffer_display(struct saa7164_buffer *buf);
extern int saa7164_buffer_activate(struct saa7164_buffer *buf, int i);
extern int saa7164_buffer_cfg_port(struct saa7164_port *port);
extern struct saa7164_user_buffer *saa7164_buffer_alloc_user(
	struct saa7164_dev *dev, u32 len);
extern void saa7164_buffer_dealloc_user(struct saa7164_user_buffer *buf);
extern int saa7164_buffer_zero_offsets(struct saa7164_port *port, int i);

/* ----------------------------------------------------------- */
/* saa7164-encoder.c                                            */
int saa7164_encoder_register(struct saa7164_port *port);
void saa7164_encoder_unregister(struct saa7164_port *port);

/* ----------------------------------------------------------- */
/* saa7164-vbi.c                                            */
int saa7164_vbi_register(struct saa7164_port *port);
void saa7164_vbi_unregister(struct saa7164_port *port);

/* ----------------------------------------------------------- */

extern unsigned int crc_checking;

extern unsigned int saa_debug;
#define dprintk(level, fmt, arg...)\
	do { if (saa_debug & level)\
		printk(KERN_DEBUG "%s: " fmt, dev->name, ## arg);\
	} while (0)

#define log_warn(fmt, arg...)\
	do { \
		printk(KERN_WARNING "%s: " fmt, dev->name, ## arg);\
	} while (0)

#define log_err(fmt, arg...)\
	do { \
		printk(KERN_ERROR "%s: " fmt, dev->name, ## arg);\
	} while (0)

#define saa7164_readl(reg) readl(dev->lmmio + ((reg) >> 2))
#define saa7164_writel(reg, value) writel((value), dev->lmmio + ((reg) >> 2))

#define saa7164_readb(reg)             readl(dev->bmmio + (reg))
#define saa7164_writeb(reg, value)     writel((value), dev->bmmio + (reg))

