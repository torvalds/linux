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

#include <media/v4l2-common.h>
#include <media/tuner.h>
#include <media/tveeprom.h>
#include <media/video-buf.h>
#include <media/cx2341x.h>
#include <media/audiochip.h>
#include <media/video-buf-dvb.h>

#include "btcx-risc.h"
#include "cx88-reg.h"

#include <linux/version.h>
#include <linux/mutex.h>
#define CX88_VERSION_CODE KERNEL_VERSION(0,0,6)

#define UNSET (-1U)

#define CX88_MAXBOARDS 8

/* Max number of inputs by card */
#define MAX_CX88_INPUT 8

/* ----------------------------------------------------------- */
/* defines and enums                                           */

#define FORMAT_FLAGS_PACKED       0x01
#define FORMAT_FLAGS_PLANAR       0x02

#define VBI_LINE_COUNT              17
#define VBI_LINE_LENGTH           2048

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

struct cx88_tvnorm {
	char                   *name;
	v4l2_std_id            id;
	u32                    cxiformat;
	u32                    cxoformat;
};

static unsigned int inline norm_maxw(struct cx88_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 768 : 640;
}


static unsigned int inline norm_maxh(struct cx88_tvnorm *norm)
{
	return (norm->id & V4L2_STD_625_50) ? 576 : 480;
}

/* ----------------------------------------------------------- */
/* static data                                                 */

struct cx8800_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
	int   flags;
	u32   cxformat;
};

struct cx88_ctrl {
	struct v4l2_queryctrl  v;
	u32                    off;
	u32                    reg;
	u32                    sreg;
	u32                    mask;
	u32                    shift;
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
/* more */

struct sram_channel {
	char *name;
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
extern struct sram_channel cx88_sram_channels[];

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
	unsigned int    vmux;
	u32             gpio0, gpio1, gpio2, gpio3;
	unsigned int    extadc:1;
};

struct cx88_board {
	char                    *name;
	unsigned int            tuner_type;
	unsigned int		radio_type;
	unsigned char		tuner_addr;
	unsigned char		radio_addr;
	int                     tda9887_conf;
	struct cx88_input       input[MAX_CX88_INPUT];
	struct cx88_input       radio;
	enum cx88_board_type    mpeg;
	enum audiochip          audio_chip;
};

struct cx88_subid {
	u16     subvendor;
	u16     subdevice;
	u32     card;
};

#define INPUT(nr) (&cx88_boards[core->board].input[nr])

/* ----------------------------------------------------------- */
/* device / file handle status                                 */

#define RESOURCE_OVERLAY       1
#define RESOURCE_VIDEO         2
#define RESOURCE_VBI           4

#define BUFFER_TIMEOUT     (HZ/2)  /* 0.5 seconds */

/* buffer for one video frame */
struct cx88_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	/* cx88 specific */
	unsigned int           bpl;
	struct btcx_riscmem    risc;
	struct cx8800_fmt      *fmt;
	u32                    count;
};

struct cx88_dmaqueue {
	struct list_head       active;
	struct list_head       queued;
	struct timer_list      timeout;
	struct btcx_riscmem    stopper;
	u32                    count;
};

struct cx88_core {
	struct list_head           devlist;
	atomic_t                   refcount;

	/* board name */
	int                        nr;
	char                       name[32];

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
	unsigned int               board;
	unsigned int               tuner_type;
	unsigned int               radio_type;
	unsigned char              tuner_addr;
	unsigned char              radio_addr;
	unsigned int               tda9887_conf;
	unsigned int               has_radio;

	/* Supported V4L _STD_ tuner formats */
	unsigned int               tuner_formats;

	/* config info -- dvb */
	struct dvb_pll_desc        *pll_desc;
	unsigned int               pll_addr;
	int 			   (*prev_set_voltage)(struct dvb_frontend* fe, fe_sec_voltage_t voltage);

	/* state info */
	struct task_struct         *kthread;
	struct cx88_tvnorm         *tvnorm;
	u32                        tvaudio;
	u32                        audiomode_manual;
	u32                        audiomode_current;
	u32                        input;
	u32                        astat;
	u32			   use_nicam;

	/* IR remote control state */
	struct cx88_IR             *ir;

	struct mutex               lock;
	/* various v4l controls */
	u32                        freq;

	/* cx88-video needs to access cx8802 for hybrid tuner pll access. */
	struct cx8802_dev          *dvbdev;
	enum cx88_board_type       active_type_id;
};

struct cx8800_dev;
struct cx8802_dev;

/* ----------------------------------------------------------- */
/* function 0: video stuff                                     */

struct cx8800_fh {
	struct cx8800_dev          *dev;
	enum v4l2_buf_type         type;
	int                        radio;
	unsigned int               resources;

	/* video overlay */
	struct v4l2_window         win;
	struct v4l2_clip           *clips;
	unsigned int               nclips;

	/* video capture */
	struct cx8800_fmt          *fmt;
	unsigned int               width,height;
	struct videobuf_queue      vidq;

	/* vbi capture */
	struct videobuf_queue      vbiq;
};

struct cx8800_suspend_state {
	int                        disabled;
};

struct cx8800_dev {
	struct cx88_core           *core;
	struct list_head           devlist;
	spinlock_t                 slock;

	/* various device info */
	unsigned int               resources;
	struct video_device        *video_dev;
	struct video_device        *vbi_dev;
	struct video_device        *radio_dev;

	/* pci i/o */
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;


	/* capture queues */
	struct cx88_dmaqueue       vidq;
	struct cx88_dmaqueue       vbiq;

	/* various v4l controls */

	/* other global state info */
	struct cx8800_suspend_state state;
};

/* ----------------------------------------------------------- */
/* function 1: audio/alsa stuff                                */
/* =============> moved to cx88-alsa.c <====================== */


/* ----------------------------------------------------------- */
/* function 2: mpeg stuff                                      */

struct cx8802_fh {
	struct cx8802_dev          *dev;
	struct videobuf_queue      mpegq;
};

struct cx8802_suspend_state {
	int                        disabled;
};

struct cx8802_driver {
	struct cx88_core *core;
	struct list_head devlist;

	/* Type of driver and access required */
	enum cx88_board_type type_id;
	enum cx8802_board_access hw_access;

	/* MPEG 8802 internal only */
	int (*suspend)(struct pci_dev *pci_dev, pm_message_t state);
	int (*resume)(struct pci_dev *pci_dev);

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

	/* dma queues */
	struct cx88_dmaqueue       mpegq;
	u32                        ts_packet_size;
	u32                        ts_packet_count;

	/* other global state info */
	struct cx8802_suspend_state state;

	/* for blackbird only */
	struct list_head           devlist;
	struct video_device        *mpeg_dev;
	u32                        mailbox;
	int                        width;
	int                        height;

	/* for dvb only */
	struct videobuf_dvb        dvb;
	void*                      fe_handle;
	int                        (*fe_release)(void *handle);

	void			   *card_priv;
	/* for switching modulation types */
	unsigned char              ts_gen_cntrl;

	/* mpeg params */
	struct cx2341x_mpeg_params params;

	/* List of attached drivers */
	struct cx8802_driver       drvlist;
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

extern void cx88_print_irqbits(char *name, char *tag, char **strings,
			       u32 bits, u32 mask);

extern int cx88_core_irq(struct cx88_core *core, u32 status);
extern void cx88_wakeup(struct cx88_core *core,
			struct cx88_dmaqueue *q, u32 count);
extern void cx88_shutdown(struct cx88_core *core);
extern int cx88_reset(struct cx88_core *core);

extern int
cx88_risc_buffer(struct pci_dev *pci, struct btcx_riscmem *risc,
		 struct scatterlist *sglist,
		 unsigned int top_offset, unsigned int bottom_offset,
		 unsigned int bpl, unsigned int padding, unsigned int lines);
extern int
cx88_risc_databuffer(struct pci_dev *pci, struct btcx_riscmem *risc,
		     struct scatterlist *sglist, unsigned int bpl,
		     unsigned int lines);
extern int
cx88_risc_stopper(struct pci_dev *pci, struct btcx_riscmem *risc,
		  u32 reg, u32 mask, u32 value);
extern void
cx88_free_buffer(struct videobuf_queue *q, struct cx88_buffer *buf);

extern void cx88_risc_disasm(struct cx88_core *core,
			     struct btcx_riscmem *risc);
extern int cx88_sram_channel_setup(struct cx88_core *core,
				   struct sram_channel *ch,
				   unsigned int bpl, u32 risc);
extern void cx88_sram_channel_dump(struct cx88_core *core,
				   struct sram_channel *ch);

extern int cx88_set_scale(struct cx88_core *core, unsigned int width,
			  unsigned int height, enum v4l2_field field);
extern int cx88_set_tvnorm(struct cx88_core *core, struct cx88_tvnorm *norm);

extern struct video_device *cx88_vdev_init(struct cx88_core *core,
					   struct pci_dev *pci,
					   struct video_device *template,
					   char *type);
extern struct cx88_core* cx88_core_get(struct pci_dev *pci);
extern void cx88_core_put(struct cx88_core *core,
			  struct pci_dev *pci);

extern int cx88_start_audio_dma(struct cx88_core *core);
extern int cx88_stop_audio_dma(struct cx88_core *core);


/* ----------------------------------------------------------- */
/* cx88-vbi.c                                                  */

void cx8800_vbi_fmt(struct cx8800_dev *dev, struct v4l2_format *f);
/*
int cx8800_start_vbi_dma(struct cx8800_dev    *dev,
			 struct cx88_dmaqueue *q,
			 struct cx88_buffer   *buf);
*/
int cx8800_stop_vbi_dma(struct cx8800_dev *dev);
int cx8800_restart_vbi_queue(struct cx8800_dev    *dev,
			     struct cx88_dmaqueue *q);
void cx8800_vbi_timeout(unsigned long data);

extern struct videobuf_queue_ops cx8800_vbi_qops;

/* ----------------------------------------------------------- */
/* cx88-i2c.c                                                  */

extern int cx88_i2c_init(struct cx88_core *core, struct pci_dev *pci);
extern void cx88_call_i2c_clients(struct cx88_core *core,
				  unsigned int cmd, void *arg);


/* ----------------------------------------------------------- */
/* cx88-cards.c                                                */

extern struct cx88_board cx88_boards[];
extern const unsigned int cx88_bcount;

extern struct cx88_subid cx88_subids[];
extern const unsigned int cx88_idcount;

extern void cx88_card_list(struct cx88_core *core, struct pci_dev *pci);
extern void cx88_card_setup(struct cx88_core *core);
extern void cx88_card_setup_pre_i2c(struct cx88_core *core);

/* ----------------------------------------------------------- */
/* cx88-tvaudio.c                                              */

#define WW_NONE		 1
#define WW_BTSC		 2
#define WW_BG		 3
#define WW_DK		 4
#define WW_I		 5
#define WW_L		 6
#define WW_EIAJ		 7
#define WW_I2SPT	 8
#define WW_FM		 9

void cx88_set_tvaudio(struct cx88_core *core);
void cx88_newstation(struct cx88_core *core);
void cx88_get_stereo(struct cx88_core *core, struct v4l2_tuner *t);
void cx88_set_stereo(struct cx88_core *core, u32 mode, int manual);
int cx88_audio_thread(void *data);

int cx8802_register_driver(struct cx8802_driver *drv);
int cx8802_unregister_driver(struct cx8802_driver *drv);
struct cx8802_dev * cx8802_get_device(struct inode *inode);
struct cx8802_driver * cx8802_get_driver(struct cx8802_dev *dev, enum cx88_board_type btype);

/* ----------------------------------------------------------- */
/* cx88-input.c                                                */

int cx88_ir_init(struct cx88_core *core, struct pci_dev *pci);
int cx88_ir_fini(struct cx88_core *core);
void cx88_ir_irq(struct cx88_core *core);

/* ----------------------------------------------------------- */
/* cx88-mpeg.c                                                 */

int cx8802_buf_prepare(struct videobuf_queue *q,struct cx8802_dev *dev,
			struct cx88_buffer *buf, enum v4l2_field field);
void cx8802_buf_queue(struct cx8802_dev *dev, struct cx88_buffer *buf);
void cx8802_cancel_buffers(struct cx8802_dev *dev);

int cx8802_init_common(struct cx8802_dev *dev);
void cx8802_fini_common(struct cx8802_dev *dev);

int cx8802_suspend_common(struct pci_dev *pci_dev, pm_message_t state);
int cx8802_resume_common(struct pci_dev *pci_dev);

/* ----------------------------------------------------------- */
/* cx88-video.c                                                */
extern int cx88_do_ioctl(struct inode *inode, struct file *file, int radio,
				struct cx88_core *core, unsigned int cmd,
				void *arg, v4l2_kioctl driver_ioctl);
extern const u32 cx88_user_ctrls[];
extern int cx8800_ctrl_query(struct v4l2_queryctrl *qctrl);

/* ----------------------------------------------------------- */
/* cx88-blackbird.c                                            */
/* used by cx88-ivtv ioctl emulation layer                     */
extern int (*cx88_ioctl_hook)(struct inode *inode, struct file *file,
			      unsigned int cmd, void *arg);
extern unsigned int (*cx88_ioctl_translator)(unsigned int cmd);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 * kate: eol "unix"; indent-width 3; remove-trailing-space on; replace-trailing-space-save on; tab-width 8; replace-tabs off; space-indent off; mixed-indent off
 */
