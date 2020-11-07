/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * v4l2 device driver for philips saa7134 based TV cards
 *
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org>
 */

#define SAA7134_VERSION "0, 2, 17"

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/kdev_t.h>
#include <linux/input.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>

#include <asm/io.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ctrls.h>
#include <media/tuner.h>
#include <media/rc-core.h>
#include <media/i2c/ir-kbd-i2c.h>
#include <media/videobuf2-dma-sg.h>
#include <sound/core.h>
#include <sound/pcm.h>
#if IS_ENABLED(CONFIG_VIDEO_SAA7134_DVB)
#include <media/videobuf2-dvb.h>
#endif
#include "tda8290.h"

#define UNSET (-1U)

/* ----------------------------------------------------------- */
/* enums                                                       */

enum saa7134_tvaudio_mode {
	TVAUDIO_FM_MONO       = 1,
	TVAUDIO_FM_BG_STEREO  = 2,
	TVAUDIO_FM_SAT_STEREO = 3,
	TVAUDIO_FM_K_STEREO   = 4,
	TVAUDIO_NICAM_AM      = 5,
	TVAUDIO_NICAM_FM      = 6,
};

enum saa7134_audio_in {
	TV    = 1,
	LINE1 = 2,
	LINE2 = 3,
	LINE2_LEFT,
};

enum saa7134_video_out {
	CCIR656 = 1,
};

/* ----------------------------------------------------------- */
/* static data                                                 */

struct saa7134_tvnorm {
	char          *name;
	v4l2_std_id   id;

	/* video decoder */
	unsigned int  sync_control;
	unsigned int  luma_control;
	unsigned int  chroma_ctrl1;
	unsigned int  chroma_gain;
	unsigned int  chroma_ctrl2;
	unsigned int  vgate_misc;

	/* video scaler */
	unsigned int  h_start;
	unsigned int  h_stop;
	unsigned int  video_v_start;
	unsigned int  video_v_stop;
	unsigned int  vbi_v_start_0;
	unsigned int  vbi_v_stop_0;
	unsigned int  src_timing;
	unsigned int  vbi_v_start_1;
};

struct saa7134_tvaudio {
	char         *name;
	v4l2_std_id  std;
	enum         saa7134_tvaudio_mode mode;
	int          carr1;
	int          carr2;
};

struct saa7134_format {
	unsigned int   fourcc;
	unsigned int   depth;
	unsigned int   pm;
	unsigned int   vshift;   /* vertical downsampling (for planar yuv) */
	unsigned int   hshift;   /* horizontal downsampling (for planar yuv) */
	unsigned int   bswap:1;
	unsigned int   wswap:1;
	unsigned int   yuv:1;
	unsigned int   planar:1;
	unsigned int   uvswap:1;
};

struct saa7134_card_ir {
	struct rc_dev		*dev;

	char                    phys[32];

	u32			polling;
	u32			last_gpio;
	u32			mask_keycode, mask_keydown, mask_keyup;

	bool                    running;

	struct timer_list       timer;

	/* IR core raw decoding */
	u32                     raw_decode;
};

/* ----------------------------------------------------------- */
/* card configuration                                          */

#define SAA7134_BOARD_NOAUTO        UNSET
#define SAA7134_BOARD_UNKNOWN           0
#define SAA7134_BOARD_PROTEUS_PRO       1
#define SAA7134_BOARD_FLYVIDEO3000      2
#define SAA7134_BOARD_FLYVIDEO2000      3
#define SAA7134_BOARD_EMPRESS           4
#define SAA7134_BOARD_MONSTERTV         5
#define SAA7134_BOARD_MD9717            6
#define SAA7134_BOARD_TVSTATION_RDS     7
#define SAA7134_BOARD_CINERGY400	8
#define SAA7134_BOARD_MD5044		9
#define SAA7134_BOARD_KWORLD           10
#define SAA7134_BOARD_CINERGY600       11
#define SAA7134_BOARD_MD7134           12
#define SAA7134_BOARD_TYPHOON_90031    13
#define SAA7134_BOARD_ELSA             14
#define SAA7134_BOARD_ELSA_500TV       15
#define SAA7134_BOARD_ASUSTeK_TVFM7134 16
#define SAA7134_BOARD_VA1000POWER      17
#define SAA7134_BOARD_BMK_MPEX_NOTUNER 18
#define SAA7134_BOARD_VIDEOMATE_TV     19
#define SAA7134_BOARD_CRONOS_PLUS      20
#define SAA7134_BOARD_10MOONSTVMASTER  21
#define SAA7134_BOARD_MD2819           22
#define SAA7134_BOARD_BMK_MPEX_TUNER   23
#define SAA7134_BOARD_TVSTATION_DVR    24
#define SAA7134_BOARD_ASUSTEK_TVFM7133	25
#define SAA7134_BOARD_PINNACLE_PCTV_STEREO 26
#define SAA7134_BOARD_MANLI_MTV002     27
#define SAA7134_BOARD_MANLI_MTV001     28
#define SAA7134_BOARD_TG3000TV         29
#define SAA7134_BOARD_ECS_TVP3XP       30
#define SAA7134_BOARD_ECS_TVP3XP_4CB5  31
#define SAA7134_BOARD_AVACSSMARTTV     32
#define SAA7134_BOARD_AVERMEDIA_DVD_EZMAKER 33
#define SAA7134_BOARD_NOVAC_PRIMETV7133 34
#define SAA7134_BOARD_AVERMEDIA_STUDIO_305 35
#define SAA7134_BOARD_UPMOST_PURPLE_TV 36
#define SAA7134_BOARD_ITEMS_MTV005     37
#define SAA7134_BOARD_CINERGY200       38
#define SAA7134_BOARD_FLYTVPLATINUM_MINI 39
#define SAA7134_BOARD_VIDEOMATE_TV_PVR 40
#define SAA7134_BOARD_VIDEOMATE_TV_GOLD_PLUS 41
#define SAA7134_BOARD_SABRENT_SBTTVFM  42
#define SAA7134_BOARD_ZOLID_XPERT_TV7134 43
#define SAA7134_BOARD_EMPIRE_PCI_TV_RADIO_LE 44
#define SAA7134_BOARD_AVERMEDIA_STUDIO_307    45
#define SAA7134_BOARD_AVERMEDIA_CARDBUS 46
#define SAA7134_BOARD_CINERGY400_CARDBUS 47
#define SAA7134_BOARD_CINERGY600_MK3   48
#define SAA7134_BOARD_VIDEOMATE_GOLD_PLUS 49
#define SAA7134_BOARD_PINNACLE_300I_DVBT_PAL 50
#define SAA7134_BOARD_PROVIDEO_PV952   51
#define SAA7134_BOARD_AVERMEDIA_305    52
#define SAA7134_BOARD_ASUSTeK_TVFM7135 53
#define SAA7134_BOARD_FLYTVPLATINUM_FM 54
#define SAA7134_BOARD_FLYDVBTDUO 55
#define SAA7134_BOARD_AVERMEDIA_307    56
#define SAA7134_BOARD_AVERMEDIA_GO_007_FM 57
#define SAA7134_BOARD_ADS_INSTANT_TV 58
#define SAA7134_BOARD_KWORLD_VSTREAM_XPERT 59
#define SAA7134_BOARD_FLYDVBT_DUO_CARDBUS 60
#define SAA7134_BOARD_PHILIPS_TOUGH 61
#define SAA7134_BOARD_VIDEOMATE_TV_GOLD_PLUSII 62
#define SAA7134_BOARD_KWORLD_XPERT 63
#define SAA7134_BOARD_FLYTV_DIGIMATRIX 64
#define SAA7134_BOARD_KWORLD_TERMINATOR 65
#define SAA7134_BOARD_YUAN_TUN900 66
#define SAA7134_BOARD_BEHOLD_409FM 67
#define SAA7134_BOARD_GOTVIEW_7135 68
#define SAA7134_BOARD_PHILIPS_EUROPA  69
#define SAA7134_BOARD_VIDEOMATE_DVBT_300 70
#define SAA7134_BOARD_VIDEOMATE_DVBT_200 71
#define SAA7134_BOARD_RTD_VFG7350 72
#define SAA7134_BOARD_RTD_VFG7330 73
#define SAA7134_BOARD_FLYTVPLATINUM_MINI2 74
#define SAA7134_BOARD_AVERMEDIA_AVERTVHD_A180 75
#define SAA7134_BOARD_MONSTERTV_MOBILE 76
#define SAA7134_BOARD_PINNACLE_PCTV_110i 77
#define SAA7134_BOARD_ASUSTeK_P7131_DUAL 78
#define SAA7134_BOARD_SEDNA_PC_TV_CARDBUS     79
#define SAA7134_BOARD_ASUSTEK_DIGIMATRIX_TV 80
#define SAA7134_BOARD_PHILIPS_TIGER  81
#define SAA7134_BOARD_MSI_TVATANYWHERE_PLUS  82
#define SAA7134_BOARD_CINERGY250PCI 83
#define SAA7134_BOARD_FLYDVB_TRIO 84
#define SAA7134_BOARD_AVERMEDIA_777 85
#define SAA7134_BOARD_FLYDVBT_LR301 86
#define SAA7134_BOARD_ADS_DUO_CARDBUS_PTV331 87
#define SAA7134_BOARD_TEVION_DVBT_220RF 88
#define SAA7134_BOARD_ELSA_700TV       89
#define SAA7134_BOARD_KWORLD_ATSC110   90
#define SAA7134_BOARD_AVERMEDIA_A169_B 91
#define SAA7134_BOARD_AVERMEDIA_A169_B1 92
#define SAA7134_BOARD_MD7134_BRIDGE_2     93
#define SAA7134_BOARD_FLYDVBT_HYBRID_CARDBUS 94
#define SAA7134_BOARD_FLYVIDEO3000_NTSC 95
#define SAA7134_BOARD_MEDION_MD8800_QUADRO 96
#define SAA7134_BOARD_FLYDVBS_LR300 97
#define SAA7134_BOARD_PROTEUS_2309 98
#define SAA7134_BOARD_AVERMEDIA_A16AR   99
#define SAA7134_BOARD_ASUS_EUROPA2_HYBRID 100
#define SAA7134_BOARD_PINNACLE_PCTV_310i  101
#define SAA7134_BOARD_AVERMEDIA_STUDIO_507 102
#define SAA7134_BOARD_VIDEOMATE_DVBT_200A  103
#define SAA7134_BOARD_HAUPPAUGE_HVR1110    104
#define SAA7134_BOARD_CINERGY_HT_PCMCIA    105
#define SAA7134_BOARD_ENCORE_ENLTV         106
#define SAA7134_BOARD_ENCORE_ENLTV_FM      107
#define SAA7134_BOARD_CINERGY_HT_PCI       108
#define SAA7134_BOARD_PHILIPS_TIGER_S      109
#define SAA7134_BOARD_AVERMEDIA_M102	   110
#define SAA7134_BOARD_ASUS_P7131_4871	   111
#define SAA7134_BOARD_ASUSTeK_P7131_HYBRID_LNA 112
#define SAA7134_BOARD_ECS_TVP3XP_4CB6  113
#define SAA7134_BOARD_KWORLD_DVBT_210 114
#define SAA7134_BOARD_SABRENT_TV_PCB05     115
#define SAA7134_BOARD_10MOONSTVMASTER3     116
#define SAA7134_BOARD_AVERMEDIA_SUPER_007  117
#define SAA7134_BOARD_BEHOLD_401	118
#define SAA7134_BOARD_BEHOLD_403	119
#define SAA7134_BOARD_BEHOLD_403FM	120
#define SAA7134_BOARD_BEHOLD_405	121
#define SAA7134_BOARD_BEHOLD_405FM	122
#define SAA7134_BOARD_BEHOLD_407	123
#define SAA7134_BOARD_BEHOLD_407FM	124
#define SAA7134_BOARD_BEHOLD_409	125
#define SAA7134_BOARD_BEHOLD_505FM	126
#define SAA7134_BOARD_BEHOLD_507_9FM	127
#define SAA7134_BOARD_BEHOLD_COLUMBUS_TVFM 128
#define SAA7134_BOARD_BEHOLD_607FM_MK3	129
#define SAA7134_BOARD_BEHOLD_M6		130
#define SAA7134_BOARD_TWINHAN_DTV_DVB_3056 131
#define SAA7134_BOARD_GENIUS_TVGO_A11MCE   132
#define SAA7134_BOARD_PHILIPS_SNAKE        133
#define SAA7134_BOARD_CREATIX_CTX953       134
#define SAA7134_BOARD_MSI_TVANYWHERE_AD11  135
#define SAA7134_BOARD_AVERMEDIA_CARDBUS_506 136
#define SAA7134_BOARD_AVERMEDIA_A16D       137
#define SAA7134_BOARD_AVERMEDIA_M115       138
#define SAA7134_BOARD_VIDEOMATE_T750       139
#define SAA7134_BOARD_AVERMEDIA_A700_PRO    140
#define SAA7134_BOARD_AVERMEDIA_A700_HYBRID 141
#define SAA7134_BOARD_BEHOLD_H6      142
#define SAA7134_BOARD_BEHOLD_M63      143
#define SAA7134_BOARD_BEHOLD_M6_EXTRA    144
#define SAA7134_BOARD_AVERMEDIA_M103    145
#define SAA7134_BOARD_ASUSTeK_P7131_ANALOG 146
#define SAA7134_BOARD_ASUSTeK_TIGER_3IN1   147
#define SAA7134_BOARD_ENCORE_ENLTV_FM53 148
#define SAA7134_BOARD_AVERMEDIA_M135A    149
#define SAA7134_BOARD_REAL_ANGEL_220     150
#define SAA7134_BOARD_ADS_INSTANT_HDTV_PCI  151
#define SAA7134_BOARD_ASUSTeK_TIGER         152
#define SAA7134_BOARD_KWORLD_PLUS_TV_ANALOG 153
#define SAA7134_BOARD_AVERMEDIA_GO_007_FM_PLUS 154
#define SAA7134_BOARD_HAUPPAUGE_HVR1150     155
#define SAA7134_BOARD_HAUPPAUGE_HVR1120   156
#define SAA7134_BOARD_AVERMEDIA_STUDIO_507UA 157
#define SAA7134_BOARD_AVERMEDIA_CARDBUS_501 158
#define SAA7134_BOARD_BEHOLD_505RDS_MK5     159
#define SAA7134_BOARD_BEHOLD_507RDS_MK3     160
#define SAA7134_BOARD_BEHOLD_507RDS_MK5     161
#define SAA7134_BOARD_BEHOLD_607FM_MK5      162
#define SAA7134_BOARD_BEHOLD_609FM_MK3      163
#define SAA7134_BOARD_BEHOLD_609FM_MK5      164
#define SAA7134_BOARD_BEHOLD_607RDS_MK3     165
#define SAA7134_BOARD_BEHOLD_607RDS_MK5     166
#define SAA7134_BOARD_BEHOLD_609RDS_MK3     167
#define SAA7134_BOARD_BEHOLD_609RDS_MK5     168
#define SAA7134_BOARD_VIDEOMATE_S350        169
#define SAA7134_BOARD_AVERMEDIA_STUDIO_505  170
#define SAA7134_BOARD_BEHOLD_X7             171
#define SAA7134_BOARD_ROVERMEDIA_LINK_PRO_FM 172
#define SAA7134_BOARD_ZOLID_HYBRID_PCI		173
#define SAA7134_BOARD_ASUS_EUROPA_HYBRID	174
#define SAA7134_BOARD_LEADTEK_WINFAST_DTV1000S 175
#define SAA7134_BOARD_BEHOLD_505RDS_MK3     176
#define SAA7134_BOARD_HAWELL_HW_404M7		177
#define SAA7134_BOARD_BEHOLD_H7             178
#define SAA7134_BOARD_BEHOLD_A7             179
#define SAA7134_BOARD_AVERMEDIA_M733A       180
#define SAA7134_BOARD_TECHNOTREND_BUDGET_T3000 181
#define SAA7134_BOARD_KWORLD_PCI_SBTVD_FULLSEG 182
#define SAA7134_BOARD_VIDEOMATE_M1F         183
#define SAA7134_BOARD_ENCORE_ENLTV_FM3      184
#define SAA7134_BOARD_MAGICPRO_PROHDTV_PRO2 185
#define SAA7134_BOARD_BEHOLD_501            186
#define SAA7134_BOARD_BEHOLD_503FM          187
#define SAA7134_BOARD_SENSORAY811_911       188
#define SAA7134_BOARD_KWORLD_PC150U         189
#define SAA7134_BOARD_ASUSTeK_PS3_100      190
#define SAA7134_BOARD_HAWELL_HW_9004V1      191
#define SAA7134_BOARD_AVERMEDIA_A706		192
#define SAA7134_BOARD_WIS_VOYAGER           193
#define SAA7134_BOARD_AVERMEDIA_505         194
#define SAA7134_BOARD_LEADTEK_WINFAST_TV2100_FM 195
#define SAA7134_BOARD_SNAZIO_TVPVR_PRO      196

#define SAA7134_MAXBOARDS 32
#define SAA7134_INPUT_MAX 8

/* ----------------------------------------------------------- */
/* Since we support 2 remote types, lets tell them apart       */

#define SAA7134_REMOTE_GPIO  1
#define SAA7134_REMOTE_I2C   2

/* ----------------------------------------------------------- */
/* Video Output Port Register Initialization Options           */

#define SET_T_CODE_POLARITY_NON_INVERTED	(1 << 0)
#define SET_CLOCK_NOT_DELAYED			(1 << 1)
#define SET_CLOCK_INVERTED			(1 << 2)
#define SET_VSYNC_OFF				(1 << 3)

enum saa7134_input_types {
	SAA7134_NO_INPUT = 0,
	SAA7134_INPUT_MUTE,
	SAA7134_INPUT_RADIO,
	SAA7134_INPUT_TV,
	SAA7134_INPUT_TV_MONO,
	SAA7134_INPUT_COMPOSITE,
	SAA7134_INPUT_COMPOSITE0,
	SAA7134_INPUT_COMPOSITE1,
	SAA7134_INPUT_COMPOSITE2,
	SAA7134_INPUT_COMPOSITE3,
	SAA7134_INPUT_COMPOSITE4,
	SAA7134_INPUT_SVIDEO,
	SAA7134_INPUT_SVIDEO0,
	SAA7134_INPUT_SVIDEO1,
	SAA7134_INPUT_COMPOSITE_OVER_SVIDEO,
};

struct saa7134_input {
	enum saa7134_input_types type;
	unsigned int             vmux;
	enum saa7134_audio_in    amux;
	unsigned int             gpio;
};

enum saa7134_mpeg_type {
	SAA7134_MPEG_UNUSED,
	SAA7134_MPEG_EMPRESS,
	SAA7134_MPEG_DVB,
	SAA7134_MPEG_GO7007,
};

enum saa7134_mpeg_ts_type {
	SAA7134_MPEG_TS_PARALLEL = 0,
	SAA7134_MPEG_TS_SERIAL,
};

struct saa7134_board {
	char                    *name;
	unsigned int            audio_clock;

	/* input switching */
	unsigned int            gpiomask;
	struct saa7134_input    inputs[SAA7134_INPUT_MAX];
	struct saa7134_input    radio;
	struct saa7134_input    mute;

	/* i2c chip info */
	unsigned int            tuner_type;
	unsigned int		radio_type;
	unsigned char		tuner_addr;
	unsigned char		radio_addr;
	unsigned char		empress_addr;
	unsigned char		rds_addr;

	unsigned int            tda9887_conf;
	struct tda829x_config   tda829x_conf;

	/* peripheral I/O */
	enum saa7134_video_out  video_out;
	enum saa7134_mpeg_type  mpeg;
	enum saa7134_mpeg_ts_type ts_type;
	unsigned int            vid_port_opts;
	unsigned int            ts_force_val:1;
};

#define card_has_radio(dev)   (SAA7134_NO_INPUT != saa7134_boards[dev->board].radio.type)
#define card_is_empress(dev)  (SAA7134_MPEG_EMPRESS == saa7134_boards[dev->board].mpeg)
#define card_is_dvb(dev)      (SAA7134_MPEG_DVB     == saa7134_boards[dev->board].mpeg)
#define card_is_go7007(dev)   (SAA7134_MPEG_GO7007  == saa7134_boards[dev->board].mpeg)
#define card_has_mpeg(dev)    (SAA7134_MPEG_UNUSED  != saa7134_boards[dev->board].mpeg)
#define card(dev)             (saa7134_boards[dev->board])
#define card_in(dev,n)        (saa7134_boards[dev->board].inputs[n])

#define V4L2_CID_PRIVATE_INVERT      (V4L2_CID_USER_SAA7134_BASE + 0)
#define V4L2_CID_PRIVATE_Y_ODD       (V4L2_CID_USER_SAA7134_BASE + 1)
#define V4L2_CID_PRIVATE_Y_EVEN      (V4L2_CID_USER_SAA7134_BASE + 2)
#define V4L2_CID_PRIVATE_AUTOMUTE    (V4L2_CID_USER_SAA7134_BASE + 3)

/* ----------------------------------------------------------- */
/* device / file handle status                                 */

#define RESOURCE_OVERLAY       1
#define RESOURCE_VIDEO         2
#define RESOURCE_VBI           4
#define RESOURCE_EMPRESS       8

#define INTERLACE_AUTO         0
#define INTERLACE_ON           1
#define INTERLACE_OFF          2

#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */
#define TS_BUFFER_TIMEOUT  msecs_to_jiffies(1000)  /* 1 second */

struct saa7134_dev;
struct saa7134_dma;

/* saa7134 page table */
struct saa7134_pgtable {
	unsigned int               size;
	__le32                     *cpu;
	dma_addr_t                 dma;
};

/* tvaudio thread status */
struct saa7134_thread {
	struct task_struct         *thread;
	unsigned int               scan1;
	unsigned int               scan2;
	unsigned int               mode;
	unsigned int		   stopped;
};

/* buffer for one video/vbi/ts frame */
struct saa7134_buf {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer vb2;

	/* saa7134 specific */
	unsigned int            top_seen;
	int (*activate)(struct saa7134_dev *dev,
			struct saa7134_buf *buf,
			struct saa7134_buf *next);

	struct list_head	entry;
};

struct saa7134_dmaqueue {
	struct saa7134_dev         *dev;
	struct saa7134_buf         *curr;
	struct list_head           queue;
	struct timer_list          timeout;
	unsigned int               need_two;
	unsigned int               seq_nr;
	struct saa7134_pgtable     pt;
};

/* dmasound dsp status */
struct saa7134_dmasound {
	struct mutex               lock;
	int                        minor_mixer;
	int                        minor_dsp;
	unsigned int               users_dsp;

	/* mixer */
	enum saa7134_audio_in      input;
	unsigned int               count;
	unsigned int               line1;
	unsigned int               line2;

	/* dsp */
	unsigned int               afmt;
	unsigned int               rate;
	unsigned int               channels;
	unsigned int               recording_on;
	unsigned int               dma_running;
	unsigned int               blocks;
	unsigned int               blksize;
	unsigned int               bufsize;
	struct saa7134_pgtable     pt;
	void			   *vaddr;
	struct scatterlist	   *sglist;
	int                        sglen;
	unsigned long              nr_pages;
	unsigned int               dma_blk;
	unsigned int               read_offset;
	unsigned int               read_count;
	void *			   priv_data;
	struct snd_pcm_substream   *substream;
};

/* ts/mpeg status */
struct saa7134_ts {
	/* TS capture */
	int                        nr_packets;
	int                        nr_bufs;
};

/* ts/mpeg ops */
struct saa7134_mpeg_ops {
	enum saa7134_mpeg_type     type;
	struct list_head           next;
	int                        (*init)(struct saa7134_dev *dev);
	int                        (*fini)(struct saa7134_dev *dev);
	void                       (*signal_change)(struct saa7134_dev *dev);
	void                       (*irq_ts_done)(struct saa7134_dev *dev,
						  unsigned long status);
};

enum saa7134_pads {
	SAA7134_PAD_IF_INPUT,
	SAA7134_PAD_VID_OUT,
	SAA7134_NUM_PADS
};

/* global device status */
struct saa7134_dev {
	struct list_head           devlist;
	struct mutex               lock;
	spinlock_t                 slock;
	struct v4l2_device         v4l2_dev;
	/* workstruct for loading modules */
	struct work_struct request_module_wk;

	/* insmod option/autodetected */
	int                        autodetected;

	/* various device info */
	unsigned int               resources;
	struct video_device        *video_dev;
	struct video_device        *radio_dev;
	struct video_device        *vbi_dev;
	struct saa7134_dmasound    dmasound;

	/* infrared remote */
	int                        has_remote;
	struct saa7134_card_ir     *remote;

	/* pci i/o */
	char                       name[32];
	int                        nr;
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;
	__u32                      __iomem *lmmio;
	__u8                       __iomem *bmmio;

	/* config info */
	unsigned int               board;
	unsigned int               tuner_type;
	unsigned int		   radio_type;
	unsigned char		   tuner_addr;
	unsigned char		   radio_addr;

	unsigned int               tda9887_conf;
	unsigned int               gpio_value;

	/* i2c i/o */
	struct i2c_adapter         i2c_adap;
	struct i2c_client          i2c_client;
	unsigned char              eedata[256];
	int			   has_rds;

	/* video overlay */
	struct v4l2_framebuffer    ovbuf;
	struct saa7134_format      *ovfmt;
	unsigned int               ovenable;
	enum v4l2_field            ovfield;
	struct v4l2_window         win;
	struct v4l2_clip           clips[8];
	unsigned int               nclips;
	struct v4l2_fh		   *overlay_owner;


	/* video+ts+vbi capture */
	struct saa7134_dmaqueue    video_q;
	struct vb2_queue           video_vbq;
	struct saa7134_dmaqueue    vbi_q;
	struct vb2_queue           vbi_vbq;
	enum v4l2_field		   field;
	struct saa7134_format      *fmt;
	unsigned int               width, height;
	unsigned int               vbi_hlen, vbi_vlen;
	struct pm_qos_request	   qos_request;

	/* SAA7134_MPEG_* */
	struct saa7134_ts          ts;
	struct saa7134_dmaqueue    ts_q;
	enum v4l2_field		   ts_field;
	int                        ts_started;
	struct saa7134_mpeg_ops    *mops;

	/* SAA7134_MPEG_EMPRESS only */
	struct video_device        *empress_dev;
	struct v4l2_subdev	   *empress_sd;
	struct vb2_queue           empress_vbq;
	struct work_struct         empress_workqueue;
	int                        empress_started;
	struct v4l2_ctrl_handler   empress_ctrl_handler;

	/* various v4l controls */
	struct saa7134_tvnorm      *tvnorm;              /* video */
	struct saa7134_tvaudio     *tvaudio;
	struct v4l2_ctrl_handler   ctrl_handler;
	unsigned int               ctl_input;
	int                        ctl_bright;
	int                        ctl_contrast;
	int                        ctl_hue;
	int                        ctl_saturation;
	int                        ctl_mute;             /* audio */
	int                        ctl_volume;
	int                        ctl_invert;           /* private */
	int                        ctl_mirror;
	int                        ctl_y_odd;
	int                        ctl_y_even;
	int                        ctl_automute;

	/* crop */
	struct v4l2_rect           crop_bounds;
	struct v4l2_rect           crop_defrect;
	struct v4l2_rect           crop_current;

	/* other global state info */
	unsigned int               automute;
	struct saa7134_thread      thread;
	struct saa7134_input       *input;
	struct saa7134_input       *hw_input;
	unsigned int               hw_mute;
	int                        last_carrier;
	int                        nosignal;
	unsigned int               insuspend;
	struct v4l2_ctrl_handler   radio_ctrl_handler;

	/* I2C keyboard data */
	struct IR_i2c_init_data    init_data;

#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device *media_dev;

	struct media_entity input_ent[SAA7134_INPUT_MAX + 1];
	struct media_pad input_pad[SAA7134_INPUT_MAX + 1];

	struct media_entity demod;
	struct media_pad demod_pad[SAA7134_NUM_PADS];

	struct media_pad video_pad, vbi_pad;
	struct media_entity *decoder;
#endif

#if IS_ENABLED(CONFIG_VIDEO_SAA7134_DVB)
	/* SAA7134_MPEG_DVB only */
	struct vb2_dvb_frontends frontends;
	int (*original_demod_sleep)(struct dvb_frontend *fe);
	int (*original_set_voltage)(struct dvb_frontend *fe,
				    enum fe_sec_voltage voltage);
	int (*original_set_high_voltage)(struct dvb_frontend *fe, long arg);
#endif
	void (*gate_ctrl)(struct saa7134_dev *dev, int open);
};

/* ----------------------------------------------------------- */

#define saa_readl(reg)             readl(dev->lmmio + (reg))
#define saa_writel(reg,value)      writel((value), dev->lmmio + (reg));
#define saa_andorl(reg,mask,value) \
  writel((readl(dev->lmmio+(reg)) & ~(mask)) |\
  ((value) & (mask)), dev->lmmio+(reg))
#define saa_setl(reg,bit)          saa_andorl((reg),(bit),(bit))
#define saa_clearl(reg,bit)        saa_andorl((reg),(bit),0)

#define saa_readb(reg)             readb(dev->bmmio + (reg))
#define saa_writeb(reg,value)      writeb((value), dev->bmmio + (reg));
#define saa_andorb(reg,mask,value) \
  writeb((readb(dev->bmmio+(reg)) & ~(mask)) |\
  ((value) & (mask)), dev->bmmio+(reg))
#define saa_setb(reg,bit)          saa_andorb((reg),(bit),(bit))
#define saa_clearb(reg,bit)        saa_andorb((reg),(bit),0)

#define saa_wait(us) { udelay(us); }

#define SAA7134_NORMS	(\
		V4L2_STD_PAL    | V4L2_STD_PAL_N | \
		V4L2_STD_PAL_Nc | V4L2_STD_SECAM | \
		V4L2_STD_NTSC   | V4L2_STD_PAL_M | \
		V4L2_STD_PAL_60)

#define GRP_EMPRESS (1)
#define saa_call_all(dev, o, f, args...) do {				\
	if (dev->gate_ctrl)						\
		dev->gate_ctrl(dev, 1);					\
	v4l2_device_call_all(&(dev)->v4l2_dev, 0, o, f , ##args);	\
	if (dev->gate_ctrl)						\
		dev->gate_ctrl(dev, 0);					\
} while (0)

#define saa_call_empress(dev, o, f, args...) ({				\
	long _rc;							\
	if (dev->gate_ctrl)						\
		dev->gate_ctrl(dev, 1);					\
	_rc = v4l2_device_call_until_err(&(dev)->v4l2_dev,		\
					 GRP_EMPRESS, o, f , ##args);	\
	if (dev->gate_ctrl)						\
		dev->gate_ctrl(dev, 0);					\
	_rc;								\
})

static inline bool is_empress(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct saa7134_dev *dev = video_get_drvdata(vdev);

	return vdev->queue == &dev->empress_vbq;
}

/* ----------------------------------------------------------- */
/* saa7134-core.c                                              */

extern struct list_head  saa7134_devlist;
extern struct mutex saa7134_devlist_lock;
extern int saa7134_no_overlay;
extern bool saa7134_userptr;

void saa7134_track_gpio(struct saa7134_dev *dev, const char *msg);
void saa7134_set_gpio(struct saa7134_dev *dev, int bit_no, int value);

#define SAA7134_PGTABLE_SIZE 4096

int saa7134_pgtable_alloc(struct pci_dev *pci, struct saa7134_pgtable *pt);
int  saa7134_pgtable_build(struct pci_dev *pci, struct saa7134_pgtable *pt,
			   struct scatterlist *list, unsigned int length,
			   unsigned int startpage);
void saa7134_pgtable_free(struct pci_dev *pci, struct saa7134_pgtable *pt);

int saa7134_buffer_count(unsigned int size, unsigned int count);
int saa7134_buffer_startpage(struct saa7134_buf *buf);
unsigned long saa7134_buffer_base(struct saa7134_buf *buf);

int saa7134_buffer_queue(struct saa7134_dev *dev, struct saa7134_dmaqueue *q,
			 struct saa7134_buf *buf);
void saa7134_buffer_finish(struct saa7134_dev *dev, struct saa7134_dmaqueue *q,
			   unsigned int state);
void saa7134_buffer_next(struct saa7134_dev *dev, struct saa7134_dmaqueue *q);
void saa7134_buffer_timeout(struct timer_list *t);
void saa7134_stop_streaming(struct saa7134_dev *dev, struct saa7134_dmaqueue *q);

int saa7134_set_dmabits(struct saa7134_dev *dev);

extern int (*saa7134_dmasound_init)(struct saa7134_dev *dev);
extern int (*saa7134_dmasound_exit)(struct saa7134_dev *dev);


/* ----------------------------------------------------------- */
/* saa7134-cards.c                                             */

extern struct saa7134_board saa7134_boards[];
extern const char * const saa7134_input_name[];
extern const unsigned int saa7134_bcount;
extern struct pci_device_id saa7134_pci_tbl[];

extern int saa7134_board_init1(struct saa7134_dev *dev);
extern int saa7134_board_init2(struct saa7134_dev *dev);
int saa7134_tuner_callback(void *priv, int component, int command, int arg);


/* ----------------------------------------------------------- */
/* saa7134-i2c.c                                               */

int saa7134_i2c_register(struct saa7134_dev *dev);
int saa7134_i2c_unregister(struct saa7134_dev *dev);


/* ----------------------------------------------------------- */
/* saa7134-video.c                                             */

extern unsigned int video_debug;
extern struct video_device saa7134_video_template;
extern struct video_device saa7134_radio_template;

void saa7134_vb2_buffer_queue(struct vb2_buffer *vb);
int saa7134_vb2_start_streaming(struct vb2_queue *vq, unsigned int count);
void saa7134_vb2_stop_streaming(struct vb2_queue *vq);

int saa7134_s_std(struct file *file, void *priv, v4l2_std_id id);
int saa7134_g_std(struct file *file, void *priv, v4l2_std_id *id);
int saa7134_querystd(struct file *file, void *priv, v4l2_std_id *std);
int saa7134_enum_input(struct file *file, void *priv, struct v4l2_input *i);
int saa7134_g_input(struct file *file, void *priv, unsigned int *i);
int saa7134_s_input(struct file *file, void *priv, unsigned int i);
int saa7134_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap);
int saa7134_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *t);
int saa7134_s_tuner(struct file *file, void *priv,
					const struct v4l2_tuner *t);
int saa7134_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f);
int saa7134_s_frequency(struct file *file, void *priv,
					const struct v4l2_frequency *f);

int saa7134_videoport_init(struct saa7134_dev *dev);
void saa7134_set_tvnorm_hw(struct saa7134_dev *dev);

int saa7134_video_init1(struct saa7134_dev *dev);
int saa7134_video_init2(struct saa7134_dev *dev);
void saa7134_irq_video_signalchange(struct saa7134_dev *dev);
void saa7134_irq_video_done(struct saa7134_dev *dev, unsigned long status);
void saa7134_video_fini(struct saa7134_dev *dev);


/* ----------------------------------------------------------- */
/* saa7134-ts.c                                                */

#define TS_PACKET_SIZE 188 /* TS packets 188 bytes */

int saa7134_ts_buffer_init(struct vb2_buffer *vb2);
int saa7134_ts_buffer_prepare(struct vb2_buffer *vb2);
int saa7134_ts_queue_setup(struct vb2_queue *q,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[]);
int saa7134_ts_start_streaming(struct vb2_queue *vq, unsigned int count);
void saa7134_ts_stop_streaming(struct vb2_queue *vq);

extern struct vb2_ops saa7134_ts_qops;

int saa7134_ts_init1(struct saa7134_dev *dev);
int saa7134_ts_fini(struct saa7134_dev *dev);
void saa7134_irq_ts_done(struct saa7134_dev *dev, unsigned long status);

int saa7134_ts_register(struct saa7134_mpeg_ops *ops);
void saa7134_ts_unregister(struct saa7134_mpeg_ops *ops);

int saa7134_ts_init_hw(struct saa7134_dev *dev);

int saa7134_ts_start(struct saa7134_dev *dev);
int saa7134_ts_stop(struct saa7134_dev *dev);

/* ----------------------------------------------------------- */
/* saa7134-vbi.c                                               */

extern const struct vb2_ops saa7134_vbi_qops;
extern struct video_device saa7134_vbi_template;

int saa7134_vbi_init1(struct saa7134_dev *dev);
int saa7134_vbi_fini(struct saa7134_dev *dev);
void saa7134_irq_vbi_done(struct saa7134_dev *dev, unsigned long status);


/* ----------------------------------------------------------- */
/* saa7134-tvaudio.c                                           */

int saa7134_tvaudio_rx2mode(u32 rx);

void saa7134_tvaudio_setmute(struct saa7134_dev *dev);
void saa7134_tvaudio_setinput(struct saa7134_dev *dev,
			      struct saa7134_input *in);
void saa7134_tvaudio_setvolume(struct saa7134_dev *dev, int level);
int saa7134_tvaudio_getstereo(struct saa7134_dev *dev);

void saa7134_tvaudio_init(struct saa7134_dev *dev);
int saa7134_tvaudio_init2(struct saa7134_dev *dev);
int saa7134_tvaudio_fini(struct saa7134_dev *dev);
int saa7134_tvaudio_do_scan(struct saa7134_dev *dev);
int saa7134_tvaudio_close(struct saa7134_dev *dev);

int saa_dsp_writel(struct saa7134_dev *dev, int reg, u32 value);

void saa7134_enable_i2s(struct saa7134_dev *dev);

/* ----------------------------------------------------------- */
/* saa7134-oss.c                                               */

extern const struct file_operations saa7134_dsp_fops;
extern const struct file_operations saa7134_mixer_fops;

int saa7134_oss_init1(struct saa7134_dev *dev);
int saa7134_oss_fini(struct saa7134_dev *dev);
void saa7134_irq_oss_done(struct saa7134_dev *dev, unsigned long status);

/* ----------------------------------------------------------- */
/* saa7134-input.c                                             */

#if defined(CONFIG_VIDEO_SAA7134_RC)
int  saa7134_input_init1(struct saa7134_dev *dev);
void saa7134_input_fini(struct saa7134_dev *dev);
void saa7134_input_irq(struct saa7134_dev *dev);
void saa7134_probe_i2c_ir(struct saa7134_dev *dev);
int saa7134_ir_open(struct rc_dev *dev);
void saa7134_ir_close(struct rc_dev *dev);
#else
#define saa7134_input_init1(dev)	((void)0)
#define saa7134_input_fini(dev)		((void)0)
#define saa7134_input_irq(dev)		((void)0)
#define saa7134_probe_i2c_ir(dev)	((void)0)
#define saa7134_ir_open(dev)		((void)0)
#define saa7134_ir_close(dev)		((void)0)
#endif
