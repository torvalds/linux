#ifndef _AML_DVB_H_
#define _AML_DVB_H_

#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/netdevice.h>
#include <linux/i2c.h>

#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/ca.h>
#include <linux/dvb/osd.h>
#include <linux/dvb/net.h>
#include <linux/dvb/frontend.h>

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif


#include "drivers/media/dvb-core/dvbdev.h"
#include "drivers/media/dvb-core/demux.h"
#include "drivers/media/dvb-core/dvb_demux.h"
#include "drivers/media/dvb-core/dmxdev.h"
#include "drivers/media/dvb-core/dvb_filter.h"
#include "drivers/media/dvb-core/dvb_net.h"
#include "drivers/media/dvb-core/dvb_ringbuffer.h"
#include "drivers/media/dvb-core/dvb_frontend.h"

#include <linux/of.h>
#include <linux/pinctrl/consumer.h>

#include <mach/c_stb_define.h>

#define TS_IN_COUNT       3
#define S2P_COUNT         2

#define DMX_DEV_COUNT     3
#define FE_DEV_COUNT      2
#define CHANNEL_COUNT     31
#define FILTER_COUNT      31
#define FILTER_LEN        15
#define DSC_COUNT         8
#define SEC_BUF_GRP_COUNT 4
#define SEC_BUF_BUSY_SIZE 4
#define SEC_BUF_COUNT     (SEC_BUF_GRP_COUNT*8)
#define ASYNCFIFO_COUNT 2

typedef enum{
	AM_DMX_0=0,
	AM_DMX_1,
	AM_DMX_2,
	AM_DMX_MAX,
}aml_dmx_id_t;

typedef enum {
	AM_TS_SRC_TS0,
	AM_TS_SRC_TS1,
	AM_TS_SRC_TS2,
	AM_TS_SRC_S_TS0,
	AM_TS_SRC_S_TS1,
	AM_TS_SRC_S_TS2,
	AM_TS_SRC_HIU,
	AM_TS_SRC_DMX0,
	AM_TS_SRC_DMX1,
	AM_TS_SRC_DMX2
} aml_ts_source_t;

struct aml_sec_buf {
	unsigned long        addr;
	int                  len;
};

struct aml_channel {
	int                  type;
	dmx_ts_pes_t     pes_type;
	int                  pid;
	int                  used;
	int                  filter_count;
	struct dvb_demux_feed     *feed;
	struct dvb_demux_feed     *dvr_feed;
};

struct aml_filter {
	int                  chan_id;
	int                  used;
	struct dmx_section_filter *filter;
	u8                   value[FILTER_LEN];
	u8                   maskandmode[FILTER_LEN];
	u8                   maskandnotmode[FILTER_LEN];
	u8                   neq;
};

struct aml_dsc {
	int                  pid;
	u8                   even[8];
	u8                   odd[8];
	int                  used;
	int                  set;
	int                  id;
	struct aml_dvb      *dvb;
};

struct aml_dmx {
	struct dvb_demux     demux;
	struct dmxdev        dmxdev;
	int                  id;
	int                  feed_count;
	int                  chan_count;
	aml_ts_source_t      source;
	int                  init;
	int                  record;
	struct dmx_frontend  hw_fe[DMX_DEV_COUNT];
	struct dmx_frontend  mem_fe;
	struct dvb_net       dvb_net;
	int                  dmx_irq;
	int                  dvr_irq;
	struct tasklet_struct     dmx_tasklet;
	struct tasklet_struct     dvr_tasklet;
	unsigned long        sec_pages;
	unsigned long        sec_pages_map;
	int                  sec_total_len;
	struct aml_sec_buf   sec_buf[SEC_BUF_COUNT];
	unsigned long        pes_pages;
	unsigned long        pes_pages_map;
	int                  pes_buf_len;
	unsigned long        sub_pages;
	unsigned long        sub_pages_map;
	int                  sub_buf_len;
	struct aml_channel   channel[CHANNEL_COUNT];
	struct aml_filter    filter[FILTER_COUNT];
	irq_handler_t        irq_handler;
	void                *irq_data;
	int                  aud_chan;
	int                  vid_chan;
	int                  sub_chan;
	int                  pcr_chan;
	u32                  section_busy[SEC_BUF_BUSY_SIZE];
	struct dvb_frontend *fe;
	int                  int_check_count;
	u32                  int_check_time;
	int                  in_tune;
	int                  error_check;
	int                  dump_ts_select;
	int                  sec_buf_watchdog_count[SEC_BUF_COUNT];

	int                  demux_filter_user;
};

struct aml_asyncfifo {
	int	id;
	int	init;
	int	asyncfifo_irq;
	aml_dmx_id_t	source;
	unsigned long	pages;
	unsigned long   pages_map;
	int	buf_len;
	int	buf_toggle;
	int buf_read;
	int flush_size;
	struct tasklet_struct     asyncfifo_tasklet;
	struct aml_dvb *dvb;
};

enum{
	AM_TS_DISABLE,
	AM_TS_PARALLEL,
	AM_TS_SERIAL
};

struct aml_ts_input {
	int                  mode;
	struct pinctrl      *pinctrl;
	int                  control;
	int                  s2p_id;
};

struct aml_s2p {
	int    invert;
};

struct aml_dvb {
	struct dvb_device    dvb_dev;
	struct aml_ts_input  ts[TS_IN_COUNT];
	struct aml_s2p       s2p[S2P_COUNT];
	struct aml_dmx       dmx[DMX_DEV_COUNT];
	struct aml_dsc       dsc[DSC_COUNT];
	struct aml_asyncfifo asyncfifo[ASYNCFIFO_COUNT];
	struct dvb_device   *dsc_dev;
	struct dvb_adapter   dvb_adapter;
	struct device       *dev;
	struct platform_device *pdev;
	aml_ts_source_t      stb_source;
	aml_ts_source_t      dsc_source;
	aml_ts_source_t      tso_source;
	int                  dmx_init;
	int                  reset_flag;
	spinlock_t           slock;
	struct timer_list    watchdog_timer;
};


/*AMLogic demux interface*/
extern int aml_dmx_hw_init(struct aml_dmx *dmx);
extern int aml_dmx_hw_deinit(struct aml_dmx *dmx);
extern int aml_dmx_hw_start_feed(struct dvb_demux_feed *dvbdmxfeed);
extern int aml_dmx_hw_stop_feed(struct dvb_demux_feed *dvbdmxfeed);
extern int aml_dmx_hw_set_source(struct dmx_demux* demux, dmx_source_t src);
extern int aml_stb_hw_set_source(struct aml_dvb *dvb, dmx_source_t src);
extern int aml_dsc_hw_set_source(struct aml_dvb *dvb, aml_ts_source_t src);
extern int aml_tso_hw_set_source(struct aml_dvb *dvb, dmx_source_t src);
extern int aml_dmx_set_skipbyte(struct aml_dvb *dvb, int skipbyte);
extern int aml_dmx_set_demux(struct aml_dvb *dvb, int id);
extern int aml_dmx_hw_set_dump_ts_select(struct dmx_demux* demux, int dump_ts_select);

extern int  dmx_alloc_chan(struct aml_dmx *dmx, int type, int pes_type, int pid);
extern void dmx_free_chan(struct aml_dmx *dmx, int cid);

extern int dmx_get_ts_serial(aml_ts_source_t src);

/*AMLogic dsc interface*/
extern int dsc_set_pid(struct aml_dsc *dsc, int pid);
extern int dsc_set_key(struct aml_dsc *dsc, int type, u8 *key);
extern int dsc_release(struct aml_dsc *dsc);

/*AMLogic ASYNC FIFO interface*/
extern int aml_asyncfifo_hw_init(struct aml_asyncfifo *afifo);
extern int aml_asyncfifo_hw_deinit(struct aml_asyncfifo *afifo);
extern int aml_asyncfifo_hw_set_source(struct aml_asyncfifo *afifo, aml_dmx_id_t src);
extern int aml_asyncfifo_hw_reset(struct aml_asyncfifo *afifo);

/*Get the Audio & Video PTS*/
extern u32 aml_dmx_get_video_pts(struct aml_dvb *dvb);
extern u32 aml_dmx_get_audio_pts(struct aml_dvb *dvb);
extern u32 aml_dmx_get_first_video_pts(struct aml_dvb *dvb);
extern u32 aml_dmx_get_first_audio_pts(struct aml_dvb *dvb);

/*Get the DVB device*/
extern struct aml_dvb* aml_get_dvb_device(void);

/*Demod interface*/
extern void aml_dmx_register_frontend(aml_ts_source_t src, struct dvb_frontend *fe);
extern void aml_dmx_before_retune(aml_ts_source_t src, struct dvb_frontend *fe);
extern void aml_dmx_after_retune(aml_ts_source_t src, struct dvb_frontend *fe);
extern void aml_dmx_start_error_check(aml_ts_source_t src, struct dvb_frontend *fe);
extern int  aml_dmx_stop_error_check(aml_ts_source_t src, struct dvb_frontend *fe);

struct devio_aml_platform_data {
	int (*io_setup)(void*);
	int (*io_cleanup)(void*);
	int (*io_power)(void *, int enable);
	int (*io_reset)(void *, int enable);
};


#endif

