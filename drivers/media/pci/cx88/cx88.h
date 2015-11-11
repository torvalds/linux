/*
 *
 * v4l2 device driver for cx2388x based TV cards
 *
 * (c) 2003,04 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev2.h>
#include <linux/kdev_t.h>

#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/tuner.h>
#include <media/tveeprom.h>
#include <media/videobuf2-dma-sg.h>
#include <media/cx2341x.h>
#include <media/videobuf2-dvb.h>
#include <media/ir-kbd-i2c.h>
#include <media/wm8775.h>

#include "cx88-reg.h"
#include "tuner-xc2028.h"

#include <linux/mutex.h>

#define CX88_VERSION "1.0.0"

#define UNSET (-1U)

#define CX88_MAXBOARDS 8

/* Max number of inputs by card */
#define MAX_CX88_INPUT 8

/* ----------------------------------------------------------- */
/* defines and enums                                           */

/* Currently unsupported by the driver: PAL/H, NTSC/Kr, SECAM/LC */
#define CX88_NORMS (V4L2_STD_ALL 		\
		    & ~V4L2_STD_PAL_H		\
		    & ~V4L2_STD_NTSC_M_KR	\
		    & ~V4L2_STD_SECAM_LC)

#define FORMAT_FLAGS_PACKED       0x01
#define FORMAT_FLAGS_PLANAR       0x02

#define VBI_LINE_PAL_COUNT              18
#define VBI_LINE_NTSC_COUNT             12
#define VBI_LINE_LENGTH           2048

#define AUD_RDS_LINES		     4

/* need "shadow" registers for some write-only ones ... */
#define SHADOW_AUD_VOL_CTL           1
#define SHADOW_AUD_BAL_CTL           2
#define SHADOW_MAX                   3

/* FM Radio deemphasis type */
enum cx88_deemph_type {
	FM_NO_DEEMPH = 0,
	FM_DEEMPH_50,
	FM_DEEMPH_75
};

enum cx88_board_type {
	CX88_BOARD_NONE = 0,
	CX88_MPEG_DVB,
	CX88_MPEG_BLACKBIRD
};

enum cx8802_board_access {
	CX8802_DRVCTL_SHARED    = 1,
	CX8802_DRVCTL_EXCLUSIVE = 2,
};

/* ----------------------------------------------------------- */
/* tv norms                                                    */

static inline unsigned int norm_maxw(v4l2_std_id norm)
{
	return 720;
}


static inline unsigned int norm_maxh(v4l2_std_id norm)
{
	return (norm & V4L2_STD_525_60) ? 480 : 576;
}

/* ----------------------------------------------------------- */
/* static data                                                 */

struct cx8800_fmt {
	const char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
	int   flags;
	u32   cxformat;
};

/* ----------------------------------------------------------- */
/* SRAM memory management data (see cx88-core.c)               */

#define SRAM_CH21 0   /* video */
#define SRAM_CH22 1
#define SRAM_CH23 2
#define SRAM_CH24 3   /* vbi   */
#define SRAM_CH25 4   /* audio */
#define SRAM_CH26 5
#define SRAM_CH28 6   /* mpeg */
#define SRAM_CH27 7   /* audio rds */
/* more */

struct sram_channel {
	const char *name;
	u32  cmds_start;
	u32  ctrl_start;
	u32  cdt;
	u32  fifo_start;
	u32  fifo_size;
	u32  ptr1_reg;
	u32  ptr2_reg;
	u32  cnt1_reg;
	u32  cnt2_reg;
};
extern const struct sram_channel cx88_sram_channels[];

/* ----------------------------------------------------------- */
/* card configuration                                          */

#define CX88_BOARD_NOAUTO               UNSET
#define CX88_BOARD_UNKNOWN                  0
#define CX88_BOARD_HAUPPAUGE                1
#define CX88_BOARD_GDI                      2
#define CX88_BOARD_PIXELVIEW                3
#define CX88_BOARD_ATI_WONDER_PRO           4
#define CX88_BOARD_WINFAST2000XP_EXPERT     5
#define CX88_BOARD_AVERTV_STUDIO_303        6
#define CX88_BOARD_MSI_TVANYWHERE_MASTER    7
#define CX88_BOARD_WINFAST_DV2000           8
#define CX88_BOARD_LEADTEK_PVR2000          9
#define CX88_BOARD_IODATA_GVVCP3PCI        10
#define CX88_BOARD_PROLINK_PLAYTVPVR       11
#define CX88_BOARD_ASUS_PVR_416            12
#define CX88_BOARD_MSI_TVANYWHERE          13
#define CX88_BOARD_KWORLD_DVB_T            14
#define CX88_BOARD_DVICO_FUSIONHDTV_DVB_T1 15
#define CX88_BOARD_KWORLD_LTV883           16
#define CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_Q  17
#define CX88_BOARD_HAUPPAUGE_DVB_T1        18
#define CX88_BOARD_CONEXANT_DVB_T1         19
#define CX88_BOARD_PROVIDEO_PV259          20
#define CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PLUS 21
#define CX88_BOARD_PCHDTV_HD3000           22
#define CX88_BOARD_DNTV_LIVE_DVB_T         23
#define CX88_BOARD_HAUPPAUGE_ROSLYN        24
#define CX88_BOARD_DIGITALLOGIC_MEC        25
#define CX88_BOARD_IODATA_GVBCTV7E         26
#define CX88_BOARD_PIXELVIEW_PLAYTV_ULTRA_PRO 27
#define CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_T  28
#define CX88_BOARD_ADSTECH_DVB_T_PCI          29
#define CX88_BOARD_TERRATEC_CINERGY_1400_DVB_T1  30
#define CX88_BOARD_DVICO_FUSIONHDTV_5_GOLD 31
#define CX88_BOARD_AVERMEDIA_ULTRATV_MC_550 32
#define CX88_BOARD_KWORLD_VSTREAM_EXPERT_DVD 33
#define CX88_BOARD_ATI_HDTVWONDER          34
#define CX88_BOARD_WINFAST_DTV1000         35
#define CX88_BOARD_AVERTV_303              36
#define CX88_BOARD_HAUPPAUGE_NOVASPLUS_S1  37
#define CX88_BOARD_HAUPPAUGE_NOVASE2_S1    38
#define CX88_BOARD_KWORLD_DVBS_100         39
#define CX88_BOARD_HAUPPAUGE_HVR1100       40
#define CX88_BOARD_HAUPPAUGE_HVR1100LP     41
#define CX88_BOARD_DNTV_LIVE_DVB_T_PRO     42
#define CX88_BOARD_KWORLD_DVB_T_CX22702    43
#define CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_DUAL 44
#define CX88_BOARD_KWORLD_HARDWARE_MPEG_TV_XPERT 45
#define CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_HYBRID 46
#define CX88_BOARD_PCHDTV_HD5500           47
#define CX88_BOARD_KWORLD_MCE200_DELUXE    48
#define CX88_BOARD_PIXELVIEW_PLAYTV_P7000  49
#define CX88_BOARD_NPGTECH_REALTV_TOP10FM  50
#define CX88_BOARD_WINFAST_DTV2000H        51
#define CX88_BOARD_GENIATECH_DVBS          52
#define CX88_BOARD_HAUPPAUGE_HVR3000       53
#define CX88_BOARD_NORWOOD_MICRO           54
#define CX88_BOARD_TE_DTV_250_OEM_SWANN    55
#define CX88_BOARD_HAUPPAUGE_HVR1300       56
#define CX88_BOARD_ADSTECH_PTV_390         57
#define CX88_BOARD_PINNACLE_PCTV_HD_800i   58
#define CX88_BOARD_DVICO_FUSIONHDTV_5_PCI_NANO 59
#define CX88_BOARD_PINNACLE_HYBRID_PCTV    60
#define CX88_BOARD_WINFAST_TV2000_XP_GLOBAL 61
#define CX88_BOARD_POWERCOLOR_REAL_ANGEL   62
#define CX88_BOARD_GENIATECH_X8000_MT      63
#define CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PRO 64
#define CX88_BOARD_DVICO_FUSIONHDTV_7_GOLD 65
#define CX88_BOARD_PROLINK_PV_8000GT       66
#define CX88_BOARD_KWORLD_ATSC_120         67
#define CX88_BOARD_HAUPPAUGE_HVR4000       68
#define CX88_BOARD_HAUPPAUGE_HVR4000LITE   69
#define CX88_BOARD_TEVII_S460              70
#define CX88_BOARD_OMICOM_SS4_PCI          71
#define CX88_BOARD_TBS_8920                72
#define CX88_BOARD_TEVII_S420              73
#define CX88_BOARD_PROLINK_PV_GLOBAL_XTREME 74
#define CX88_BOARD_PROF_7300               75
#define CX88_BOARD_SATTRADE_ST4200         76
#define CX88_BOARD_TBS_8910                77
#define CX88_BOARD_PROF_6200               78
#define CX88_BOARD_TERRATEC_CINERGY_HT_PCI_MKII 79
#define CX88_BOARD_HAUPPAUGE_IRONLY        80
#define CX88_BOARD_WINFAST_DTV1800H        81
#define CX88_BOARD_WINFAST_DTV2000H_J      82
#define CX88_BOARD_PROF_7301               83
#define CX88_BOARD_SAMSUNG_SMT_7020        84
#define CX88_BOARD_TWINHAN_VP1027_DVBS     85
#define CX88_BOARD_TEVII_S464              86
#define CX88_BOARD_WINFAST_DTV2000H_PLUS   87
#define CX88_BOARD_WINFAST_DTV1800H_XC4000 88
#define CX88_BOARD_WINFAST_TV2000_XP_GLOBAL_6F36 89
#define CX88_BOARD_WINFAST_TV2000_XP_GLOBAL_6F43 90

enum cx88_itype {
	CX88_VMUX_COMPOSITE1 = 1,
	CX88_VMUX_COMPOSITE2,
	CX88_VMUX_COMPOSITE3,
	CX88_VMUX_COMPOSITE4,
	CX88_VMUX_SVIDEO,
	CX88_VMUX_TELEVISION,
	CX88_VMUX_CABLE,
	CX88_VMUX_DVB,
	CX88_VMUX_DEBUG,
	CX88_RADIO,
};

struct cx88_input {
	enum cx88_itype type;
	u32             gpio0, gpio1, gpio2, gpio3;
	unsigned int    vmux:2;
	unsigned int    audioroute:4;
};

enum cx88_audio_chip {
	CX88_AUDIO_WM8775 = 1,
	CX88_AUDIO_TVAUDIO,
};

struct cx88_board {
	const char              *name;
	unsigned int            tuner_type;
	unsigned int		radio_type;
	unsigned char		tuner_addr;
	unsigned char		radio_addr;
	int                     tda9887_conf;
	struct cx88_input       input[MAX_CX88_INPUT];
	struct cx88_input       radio;
	enum cx88_board_type    mpeg;
	enum cx88_audio_chip	audio_chip;
	int			num_frontends;

	/* Used for I2S devices */
	int			i2sinputcntl;
};

struct cx88_subid {
	u16     subvendor;
	u16     subdevice;
	u32     card;
};

enum cx88_tvaudio {
	WW_NONE = 1,
	WW_BTSC,
	WW_BG,
	WW_DK,
	WW_I,
	WW_L,
	WW_EIAJ,
	WW_I2SPT,
	WW_FM,
	WW_I2SADC,
	WW_M
};

#define INPUT(nr) (core->board.input[nr])

/* ----------------------------------------------------------- */
/* device / file handle status                                 */

#define RESOURCE_OVERLAY       1
#define RESOURCE_VIDEO         2
#define RESOURCE_VBI           4

#define BUFFER_TIMEOUT     msecs_to_jiffies(2000)

struct cx88_riscmem {
	unsigned int   size;
	__le32         *cpu;
	__le32         *jmp;
	dma_addr_t     dma;
};

/* buffer for one video frame */
struct cx88_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer vb;
	struct list_head       list;

	/* cx88 specific */
	unsigned int           bpl;
	struct cx88_riscmem    risc;
};

struct cx88_dmaqueue {
	struct list_head       active;
	u32                    count;
};

struct cx8800_dev;
struct cx8802_dev;

struct cx88_core {
	struct list_head           devlist;
	atomic_t                   refcount;

	/* board name */
	int                        nr;
	char                       name[32];
	u32			   model;

	/* pci stuff */
	int                        pci_bus;
	int                        pci_slot;
	u32                        __iomem *lmmio;
	u8                         __iomem *bmmio;
	u32                        shadow[SHADOW_MAX];
	int                        pci_irqmask;

	/* i2c i/o */
	struct i2c_adapter         i2c_adap;
	struct i2c_algo_bit_data   i2c_algo;
	struct i2c_client          i2c_client;
	u32                        i2c_state, i2c_rc;

	/* config info -- analog */
	struct v4l2_device 	   v4l2_dev;
	struct v4l2_ctrl_handler   video_hdl;
	struct v4l2_ctrl	   *chroma_agc;
	struct v4l2_ctrl_handler   audio_hdl;
	struct v4l2_subdev	   *sd_wm8775;
	struct i2c_client 	   *i2c_rtc;
	unsigned int               boardnr;
	struct cx88_board	   board;

	/* Supported V4L _STD_ tuner formats */
	unsigned int               tuner_formats;

	/* config info -- dvb */
#if IS_ENABLED(CONFIG_VIDEO_CX88_DVB)
	int	(*prev_set_voltage)(struct dvb_frontend *fe,
				    enum fe_sec_voltage voltage);
#endif
	void	(*gate_ctrl)(struct cx88_core *core, int open);

	/* state info */
	struct task_struct         *kthread;
	v4l2_std_id                tvnorm;
	unsigned		   width, height;
	unsigned		   field;
	enum cx88_tvaudio          tvaudio;
	u32                        audiomode_manual;
	u32                        audiomode_current;
	u32                        input;
	u32                        last_analog_input;
	u32                        astat;
	u32			   use_nicam;
	unsigned long		   last_change;

	/* IR remote control state */
	struct cx88_IR             *ir;

	/* I2C remote data */
	struct IR_i2c_init_data    init_data;
	struct wm8775_platform_data wm8775_data;

	struct mutex               lock;
	/* various v4l controls */
	u32                        freq;

	/*
	 * cx88-video needs to access cx8802 for hybrid tuner pll access and
	 * for vb2_is_busy() checks.
	 */
	struct cx8802_dev          *dvbdev;
	/* cx88-blackbird needs to access cx8800 for vb2_is_busy() checks */
	struct cx8800_dev          *v4ldev;
	enum cx88_board_type       active_type_id;
	int			   active_ref;
	int			   active_fe_id;
};

static inline struct cx88_core *to_core(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct cx88_core, v4l2_dev);
}

#define call_hw(core, grpid, o, f, args...) \
	do {							\
		if (!core->i2c_rc) {				\
			if (core->gate_ctrl)			\
				core->gate_ctrl(core, 1);	\
			v4l2_device_call_all(&core->v4l2_dev, grpid, o, f, ##args); \
			if (core->gate_ctrl)			\
				core->gate_ctrl(core, 0);	\
		}						\
	} while (0)

#define call_all(core, o, f, args...) call_hw(core, 0, o, f, ##args)

#define WM8775_GID      (1 << 0)

#define wm8775_s_ctrl(core, id, val) \
	do {									\
		struct v4l2_ctrl *ctrl_ =					\
			v4l2_ctrl_find(core->sd_wm8775->ctrl_handler, id);	\
		if (ctrl_ && !core->i2c_rc) {					\
			if (core->gate_ctrl)					\
				core->gate_ctrl(core, 1);			\
			v4l2_ctrl_s_ctrl(ctrl_, val);				\
			if (core->gate_ctrl)					\
				core->gate_ctrl(core, 0);			\
		}								\
	} while (0)

#define wm8775_g_ctrl(core, id) \
	({									\
		struct v4l2_ctrl *ctrl_ =					\
			v4l2_ctrl_find(core->sd_wm8775->ctrl_handler, id);	\
		s32 val = 0;							\
		if (ctrl_ && !core->i2c_rc) {					\
			if (core->gate_ctrl)					\
				core->gate_ctrl(core, 1);			\
			val = v4l2_ctrl_g_ctrl(ctrl_);				\
			if (core->gate_ctrl)					\
				core->gate_ctrl(core, 0);			\
		}								\
		val;								\
	})

/* ----------------------------------------------------------- */
/* function 0: video stuff                                     */

struct cx8800_suspend_state {
	int                        disabled;
};

struct cx8800_dev {
	struct cx88_core           *core;
	spinlock_t                 slock;

	/* various device info */
	unsigned int               resources;
	struct video_device        video_dev;
	struct video_device        vbi_dev;
	struct video_device        radio_dev;

	/* pci i/o */
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;
	void			   *alloc_ctx;

	const struct cx8800_fmt    *fmt;

	/* capture queues */
	struct cx88_dmaqueue       vidq;
	struct vb2_queue           vb2_vidq;
	struct cx88_dmaqueue       vbiq;
	struct vb2_queue           vb2_vbiq;

	/* various v4l controls */

	/* other global state info */
	struct cx8800_suspend_state state;
};

/* ----------------------------------------------------------- */
/* function 1: audio/alsa stuff                                */
/* =============> moved to cx88-alsa.c <====================== */


/* ----------------------------------------------------------- */
/* function 2: mpeg stuff                                      */

struct cx8802_suspend_state {
	int                        disabled;
};

struct cx8802_driver {
	struct cx88_core *core;

	/* List of drivers attached to device */
	struct list_head drvlist;

	/* Type of driver and access required */
	enum cx88_board_type type_id;
	enum cx8802_board_access hw_access;

	/* MPEG 8802 internal only */
	int (*suspend)(struct pci_dev *pci_dev, pm_message_t state);
	int (*resume)(struct pci_dev *pci_dev);

	/* Callers to the following functions must hold core->lock */

	/* MPEG 8802 -> mini driver - Driver probe and configuration */
	int (*probe)(struct cx8802_driver *drv);
	int (*remove)(struct cx8802_driver *drv);

	/* MPEG 8802 -> mini driver - Access for hardware control */
	int (*advise_acquire)(struct cx8802_driver *drv);
	int (*advise_release)(struct cx8802_driver *drv);

	/* MPEG 8802 <- mini driver - Access for hardware control */
	int (*request_acquire)(struct cx8802_driver *drv);
	int (*request_release)(struct cx8802_driver *drv);
};

struct cx8802_dev {
	struct cx88_core           *core;
	spinlock_t                 slock;

	/* pci i/o */
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;
	void			   *alloc_ctx;

	/* dma queues */
	struct cx88_dmaqueue       mpegq;
	struct vb2_queue           vb2_mpegq;
	u32                        ts_packet_size;
	u32                        ts_packet_count;

	/* other global state info */
	struct cx8802_suspend_state state;

	/* for blackbird only */
	struct list_head           devlist;
#if IS_ENABLED(CONFIG_VIDEO_CX88_BLACKBIRD)
	struct video_device        mpeg_dev;
	u32                        mailbox;

	/* mpeg params */
	struct cx2341x_handler     cxhdl;
#endif

#if IS_ENABLED(CONFIG_VIDEO_CX88_DVB)
	/* for dvb only */
	struct vb2_dvb_frontends frontends;
#endif

#if IS_ENABLED(CONFIG_VIDEO_CX88_VP3054)
	/* For VP3045 secondary I2C bus support */
	struct vp3054_i2c_state	   *vp3054;
#endif
	/* for switching modulation types */
	unsigned char              ts_gen_cntrl;

	/* List of attached drivers; must hold core->lock to access */
	struct list_head	   drvlist;

	struct work_struct	   request_module_wk;
};

/* ----------------------------------------------------------- */

#define cx_read(reg)             readl(core->lmmio + ((reg)>>2))
#define cx_write(reg,value)      writel((value), core->lmmio + ((reg)>>2))
#define cx_writeb(reg,value)     writeb((value), core->bmmio + (reg))

#define cx_andor(reg,mask,value) \
  writel((readl(core->lmmio+((reg)>>2)) & ~(mask)) |\
  ((value) & (mask)), core->lmmio+((reg)>>2))
#define cx_set(reg,bit)          cx_andor((reg),(bit),(bit))
#define cx_clear(reg,bit)        cx_andor((reg),(bit),0)

#define cx_wait(d) { if (need_resched()) schedule(); else udelay(d); }

/* shadow registers */
#define cx_sread(sreg)		    (core->shadow[sreg])
#define cx_swrite(sreg,reg,value) \
  (core->shadow[sreg] = value, \
   writel(core->shadow[sreg], core->lmmio + ((reg)>>2)))
#define cx_sandor(sreg,reg,mask,value) \
  (core->shadow[sreg] = (core->shadow[sreg] & ~(mask)) | ((value) & (mask)), \
   writel(core->shadow[sreg], core->lmmio + ((reg)>>2)))

/* ----------------------------------------------------------- */
/* cx88-core.c                                                 */

extern unsigned int cx88_core_debug;

extern void cx88_print_irqbits(const char *name, const char *tag, const char *strings[],
			       int len, u32 bits, u32 mask);

extern int cx88_core_irq(struct cx88_core *core, u32 status);
extern void cx88_wakeup(struct cx88_core *core,
			struct cx88_dmaqueue *q, u32 count);
extern void cx88_shutdown(struct cx88_core *core);
extern int cx88_reset(struct cx88_core *core);

extern int
cx88_risc_buffer(struct pci_dev *pci, struct cx88_riscmem *risc,
		 struct scatterlist *sglist,
		 unsigned int top_offset, unsigned int bottom_offset,
		 unsigned int bpl, unsigned int padding, unsigned int lines);
extern int
cx88_risc_databuffer(struct pci_dev *pci, struct cx88_riscmem *risc,
		     struct scatterlist *sglist, unsigned int bpl,
		     unsigned int lines, unsigned int lpi);

extern void cx88_risc_disasm(struct cx88_core *core,
			     struct cx88_riscmem *risc);
extern int cx88_sram_channel_setup(struct cx88_core *core,
				   const struct sram_channel *ch,
				   unsigned int bpl, u32 risc);
extern void cx88_sram_channel_dump(struct cx88_core *core,
				   const struct sram_channel *ch);

extern int cx88_set_scale(struct cx88_core *core, unsigned int width,
			  unsigned int height, enum v4l2_field field);
extern int cx88_set_tvnorm(struct cx88_core *core, v4l2_std_id norm);

extern void cx88_vdev_init(struct cx88_core *core,
			   struct pci_dev *pci,
			   struct video_device *vfd,
			   const struct video_device *template_,
			   const char *type);
extern struct cx88_core *cx88_core_get(struct pci_dev *pci);
extern void cx88_core_put(struct cx88_core *core,
			  struct pci_dev *pci);

extern int cx88_start_audio_dma(struct cx88_core *core);
extern int cx88_stop_audio_dma(struct cx88_core *core);


/* ----------------------------------------------------------- */
/* cx88-vbi.c                                                  */

/* Can be used as g_vbi_fmt, try_vbi_fmt and s_vbi_fmt */
int cx8800_vbi_fmt (struct file *file, void *priv,
					struct v4l2_format *f);

/*
int cx8800_start_vbi_dma(struct cx8800_dev    *dev,
			 struct cx88_dmaqueue *q,
			 struct cx88_buffer   *buf);
*/
void cx8800_stop_vbi_dma(struct cx8800_dev *dev);
int cx8800_restart_vbi_queue(struct cx8800_dev *dev, struct cx88_dmaqueue *q);

extern const struct vb2_ops cx8800_vbi_qops;

/* ----------------------------------------------------------- */
/* cx88-i2c.c                                                  */

extern int cx88_i2c_init(struct cx88_core *core, struct pci_dev *pci);


/* ----------------------------------------------------------- */
/* cx88-cards.c                                                */

extern int cx88_tuner_callback(void *dev, int component, int command, int arg);
extern int cx88_get_resources(const struct cx88_core *core,
			      struct pci_dev *pci);
extern struct cx88_core *cx88_core_create(struct pci_dev *pci, int nr);
extern void cx88_setup_xc3028(struct cx88_core *core, struct xc2028_ctrl *ctl);

/* ----------------------------------------------------------- */
/* cx88-tvaudio.c                                              */

void cx88_set_tvaudio(struct cx88_core *core);
void cx88_newstation(struct cx88_core *core);
void cx88_get_stereo(struct cx88_core *core, struct v4l2_tuner *t);
void cx88_set_stereo(struct cx88_core *core, u32 mode, int manual);
int cx88_audio_thread(void *data);

int cx8802_register_driver(struct cx8802_driver *drv);
int cx8802_unregister_driver(struct cx8802_driver *drv);

/* Caller must hold core->lock */
struct cx8802_driver * cx8802_get_driver(struct cx8802_dev *dev, enum cx88_board_type btype);

/* ----------------------------------------------------------- */
/* cx88-dsp.c                                                  */

s32 cx88_dsp_detect_stereo_sap(struct cx88_core *core);

/* ----------------------------------------------------------- */
/* cx88-input.c                                                */

int cx88_ir_init(struct cx88_core *core, struct pci_dev *pci);
int cx88_ir_fini(struct cx88_core *core);
void cx88_ir_irq(struct cx88_core *core);
int cx88_ir_start(struct cx88_core *core);
void cx88_ir_stop(struct cx88_core *core);
extern void cx88_i2c_init_ir(struct cx88_core *core);

/* ----------------------------------------------------------- */
/* cx88-mpeg.c                                                 */

int cx8802_buf_prepare(struct vb2_queue *q, struct cx8802_dev *dev,
			struct cx88_buffer *buf);
void cx8802_buf_queue(struct cx8802_dev *dev, struct cx88_buffer *buf);
void cx8802_cancel_buffers(struct cx8802_dev *dev);
int cx8802_start_dma(struct cx8802_dev    *dev,
			    struct cx88_dmaqueue *q,
			    struct cx88_buffer   *buf);

/* ----------------------------------------------------------- */
/* cx88-video.c*/
int cx88_enum_input(struct cx88_core *core, struct v4l2_input *i);
int cx88_set_freq(struct cx88_core  *core, const struct v4l2_frequency *f);
int cx88_video_mux(struct cx88_core *core, unsigned int input);
void cx88_querycap(struct file *file, struct cx88_core *core,
		struct v4l2_capability *cap);
