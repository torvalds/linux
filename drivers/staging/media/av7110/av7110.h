/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AV7110_H_
#define _AV7110_H_

#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/netdevice.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/time.h>

#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/ca.h>
#include <linux/dvb/osd.h>
#include <linux/dvb/net.h>
#include <linux/mutex.h>

#include <media/dvbdev.h>
#include <media/demux.h>
#include <media/dvb_demux.h>
#include <media/dmxdev.h>
#include "dvb_filter.h"
#include <media/dvb_net.h>
#include <media/dvb_ringbuffer.h>
#include <media/dvb_frontend.h>
#include "ves1820.h"
#include "ves1x93.h"
#include "stv0299.h"
#include "tda8083.h"
#include "sp8870.h"
#include "stv0297.h"
#include "l64781.h"

#include <media/drv-intf/saa7146_vv.h>

#define ANALOG_TUNER_VES1820 1
#define ANALOG_TUNER_STV0297 2

extern int av7110_debug;

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define dprintk(level, fmt, arg...) do {				\
	if ((level) & av7110_debug)					\
		pr_info("%s(): " fmt, __func__, ##arg);			\
} while (0)

#define MAXFILT 32

enum {AV_PES_STREAM, PS_STREAM, TS_STREAM, PES_STREAM};

enum av7110_video_mode {
	AV7110_VIDEO_MODE_PAL	= 0,
	AV7110_VIDEO_MODE_NTSC	= 1
};

struct av7110_p2t {
	u8		  pes[TS_SIZE];
	u8		  counter;
	long		  pos;
	int		  frags;
	struct dvb_demux_feed *feed;
};

/* video MPEG decoder events: */
/* (code copied from dvb_frontend.c, should maybe be factored out...) */
#define MAX_VIDEO_EVENT 8
struct dvb_video_events {
	struct video_event	  events[MAX_VIDEO_EVENT];
	int			  eventw;
	int			  eventr;
	int			  overflow;
	wait_queue_head_t	  wait_queue;
	spinlock_t		  lock;
};

struct av7110;

/* infrared remote control */
struct infrared {
	struct rc_dev		*rcdev;
	char			input_phys[32];
	u32			ir_config;
};

/* place to store all the necessary device information */
struct av7110 {
	/* devices */

	struct dvb_device	dvb_dev;
	struct dvb_net		dvb_net;

	struct video_device	v4l_dev;
	struct video_device	vbi_dev;

	struct saa7146_dev	*dev;

	struct i2c_adapter	i2c_adap;

	char			*card_name;

	/* support for analog module of dvb-c */
	int			analog_tuner_flags;
	int			current_input;
	u32			current_freq;

	struct tasklet_struct	debi_tasklet;
	struct tasklet_struct	gpio_tasklet;

	int adac_type;	       /* audio DAC type */
#define DVB_ADAC_TI	  0
#define DVB_ADAC_CRYSTAL  1
#define DVB_ADAC_MSP34x0  2
#define DVB_ADAC_MSP34x5  3
#define DVB_ADAC_NONE	 -1

	/* buffers */

	void		       *iobuf;	 /* memory for all buffers */
	struct dvb_ringbuffer	avout;   /* buffer for video or A/V mux */
#define AVOUTLEN (128 * 1024)
	struct dvb_ringbuffer	aout;    /* buffer for audio */
#define AOUTLEN (64 * 1024)
	void		       *bmpbuf;
#define BMPLEN (8 * 32768 + 1024)

	/* bitmap buffers and states */

	int			bmpp;
	int			bmplen;
	volatile int		bmp_state;
#define BMP_NONE     0
#define BMP_LOADING  1
#define BMP_LOADED   2
	wait_queue_head_t	bmpq;

	/* DEBI and polled command interface */

	spinlock_t		debilock;
	struct mutex		dcomlock;
	volatile int		debitype;
	volatile int		debilen;

	/* Recording and playback flags */

	int			rec_mode;
	int			playing;
#define RP_NONE  0
#define RP_VIDEO 1
#define RP_AUDIO 2
#define RP_AV	 3

	/* OSD */

	int			osdwin;      /* currently active window */
	u16			osdbpp[8];
	struct mutex		osd_mutex;

	/* CA */

	struct ca_slot_info	ci_slot[2];

	enum av7110_video_mode	vidmode;
	struct dmxdev		dmxdev;
	struct dvb_demux	demux;

	struct dmx_frontend	hw_frontend;
	struct dmx_frontend	mem_frontend;

	/* for budget mode demux1 */
	struct dmxdev		dmxdev1;
	struct dvb_demux	demux1;
	struct dvb_net		dvb_net1;
	spinlock_t		feedlock1;
	int			feeding1;
	u32			ttbp;
	unsigned char           *grabbing;
	struct saa7146_pgtable  pt;
	struct tasklet_struct   vpe_tasklet;
	bool			full_ts;

	int			fe_synced;
	struct mutex		pid_mutex;

	int			video_blank;
	struct video_status	videostate;
	u16			display_panscan;
	int			display_ar;
	int			trickmode;
#define TRICK_NONE   0
#define TRICK_FAST   1
#define TRICK_SLOW   2
#define TRICK_FREEZE 3
	struct audio_status	audiostate;

	struct dvb_demux_filter *handle2filter[32];
	struct av7110_p2t	 p2t_filter[MAXFILT];
	struct dvb_filter_pes2ts p2t[2];
	struct ipack		 ipack[2];
	u8			*kbuf[2];

	int sinfo;
	int feeding;

	int arm_errors;
	int registered;

	/* AV711X */

	u32		    arm_fw;
	u32		    arm_rtsl;
	u32		    arm_vid;
	u32		    arm_app;
	u32		    avtype;
	int		    arm_ready;
	struct task_struct *arm_thread;
	wait_queue_head_t   arm_wait;
	u16		    arm_loops;

	void		   *debi_virt;
	dma_addr_t	    debi_bus;

	u16		    pids[DMX_PES_OTHER];

	struct dvb_ringbuffer	 ci_rbuffer;
	struct dvb_ringbuffer	 ci_wbuffer;

	struct audio_mixer	mixer;

	struct dvb_adapter	 dvb_adapter;
	struct dvb_device	 *video_dev;
	struct dvb_device	 *audio_dev;
	struct dvb_device	 *ca_dev;
	struct dvb_device	 *osd_dev;

	struct dvb_video_events  video_events;
	video_size_t		 video_size;

	u16			wssMode;
	u16			wssData;

	struct infrared		ir;

	/* firmware stuff */
	unsigned char *bin_fw;
	unsigned long size_fw;

	unsigned char *bin_dpram;
	unsigned long size_dpram;

	unsigned char *bin_root;
	unsigned long size_root;

	struct dvb_frontend *fe;
	enum fe_status fe_status;

	struct mutex ioctl_mutex;

	/* crash recovery */
	void				(*recover)(struct av7110 *av7110);
	enum fe_sec_voltage		saved_voltage;
	enum fe_sec_tone_mode		saved_tone;
	struct dvb_diseqc_master_cmd	saved_master_cmd;
	enum fe_sec_mini_cmd		saved_minicmd;

	int (*fe_init)(struct dvb_frontend *fe);
	int (*fe_read_status)(struct dvb_frontend *fe, enum fe_status *status);
	int (*fe_diseqc_reset_overload)(struct dvb_frontend *fe);
	int (*fe_diseqc_send_master_cmd)(struct dvb_frontend *fe,
					 struct dvb_diseqc_master_cmd *cmd);
	int (*fe_diseqc_send_burst)(struct dvb_frontend *fe,
				    enum fe_sec_mini_cmd minicmd);
	int (*fe_set_tone)(struct dvb_frontend *fe,
			   enum fe_sec_tone_mode tone);
	int (*fe_set_voltage)(struct dvb_frontend *fe,
			      enum fe_sec_voltage voltage);
	int (*fe_dishnetwork_send_legacy_command)(struct dvb_frontend *fe,
						  unsigned long cmd);
	int (*fe_set_frontend)(struct dvb_frontend *fe);
};

int ChangePIDs(struct av7110 *av7110, u16 vpid, u16 apid, u16 ttpid,
	       u16 subpid, u16 pcrpid);

void av7110_ir_handler(struct av7110 *av7110, u32 ircom);
int av7110_set_ir_config(struct av7110 *av7110);
int av7110_ir_init(struct av7110 *av7110);
void av7110_ir_exit(struct av7110 *av7110);

/* msp3400 i2c subaddresses */
#define MSP_WR_DEM 0x10
#define MSP_RD_DEM 0x11
#define MSP_WR_DSP 0x12
#define MSP_RD_DSP 0x13

int i2c_writereg(struct av7110 *av7110, u8 id, u8 reg, u8 val);
u8 i2c_readreg(struct av7110 *av7110, u8 id, u8 reg);
int msp_writereg(struct av7110 *av7110, u8 dev, u16 reg, u16 val);

int av7110_init_analog_module(struct av7110 *av7110);
int av7110_init_v4l(struct av7110 *av7110);
int av7110_exit_v4l(struct av7110 *av7110);

#endif /* _AV7110_H_ */
