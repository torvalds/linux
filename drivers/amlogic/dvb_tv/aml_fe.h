#ifndef _AML_FE_H_
#define _AML_FE_H_

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
#include "aml_dvb.h"
#include "linux/videodev2.h"

#include <linux/amlogic/aml_gpio_consumer.h>


#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/pinctrl/consumer.h>



typedef enum{
	AM_FE_UNKNOWN = 0,
	AM_FE_QPSK = 1,
	AM_FE_QAM  = 2,
	AM_FE_OFDM = 4,
	AM_FE_ATSC = 8,
	AM_FE_ANALOG = 16,
	AM_FE_DTMB = 32,
	AM_FE_ISDBT = 64
}aml_fe_mode_t;

#define AM_FE_DTV_MASK (AM_FE_QPSK|AM_FE_QAM|AM_FE_OFDM|AM_FE_ATSC|AM_FE_DTMB|AM_FE_ISDBT)

typedef enum{
	AM_TUNER_SI2176     = 1,
        AM_TUNER_SI2196     =2,
	AM_TUNER_FQ1216 = 3,
	AM_TUNER_HTM = 4,
	AM_TUNER_CTC703=5,
	AM_TUNER_SI2177 = 6
}aml_tuner_type_t;

typedef enum{
	AM_ATV_DEMOD_SI2176 = 1,
    AM_ATV_DEMOD_SI2196 =2,
	AM_ATV_DEMOD_FQ1216 =3,
	AM_ATV_DEMOD_HTM = 4,
	AM_ATV_DEMOD_CTC703 = 5,
	AM_ATV_DEMOD_SI2177 =6
}aml_atv_demod_type_t;

typedef enum{
	AM_DTV_DEMOD_M1     = 0,
	AM_DTV_DEMOD_SI2176 = 1,
	AM_DTV_DEMOD_MXL101 = 2,
	AM_DTV_DEMOD_SI2196 = 3,
	AM_DTV_DEMOD_AVL6211 = 4,
	AM_DTV_DEMOD_SI2168 = 5,
	AM_DTV_DEMOD_ITE9133 = 6,
	AM_DTV_DEMOD_ITE9173 = 7,
	AM_DTV_DEMOD_DIB8096 = 8,
	AM_DTV_DEMOD_ATBM8869 = 9
}aml_dtv_demod_type_t;

typedef enum{
	AM_DEV_TUNER,
	AM_DEV_ATV_DEMOD,
	AM_DEV_DTV_DEMOD
}aml_fe_dev_type_t;

struct aml_fe_dev;
struct aml_fe;
struct aml_fe_drv{
	struct module        *owner;
	struct aml_fe_drv    *next;
	aml_tuner_type_t      id;
	char    *name;
	int      capability;
	int (*init)(struct aml_fe_dev *dev);
	int (*release)(struct aml_fe_dev *dev);
	int (*resume)(struct aml_fe_dev *dev);
	int (*suspend)(struct aml_fe_dev *dev);
	int (*get_ops)(struct aml_fe_dev *dev, int mode, void *ops);
	int (*enter_mode)(struct aml_fe *fe, int mode);
	int (*leave_mode)(struct aml_fe *fe, int mode);
	int      ref;
};

struct aml_fe_dev{
	/*point to parent aml_fe*/
	struct aml_fe *fe;
	int      i2c_adap_id;
	int      i2c_addr;
	struct i2c_adapter *i2c_adap;
	int      reset_gpio;
	int      reset_value;
	struct aml_fe_drv *drv;
	wait_queue_head_t  lock_wq;
	void    *priv_data;

	/*for tuner power control*/
	int      tuner_power_gpio;
	/*for dtv dvbsx lnb power control*/
	int      lnb_power_gpio;
	/*for ant overload control, it possible in dtv dvbsx and depond on fe hw*/
	int      antoverload_gpio;

	/*for mem reserved*/
	int      mem_start;
	int      mem_end;
};

struct aml_fe{
	struct dvb_frontend *fe;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend es;
#endif /*CONFIG_HAS_EARLYSUSPEND*/
	spinlock_t slock;
	int      init;
	int      mode;
	int      dev_id;
	int      capability;
	aml_ts_source_t    ts;
	struct aml_fe_dev *tuner;
	struct aml_fe_dev *atv_demod;
	struct aml_fe_dev *dtv_demod;
	//struct dvb_frontend_parameters params;
	struct dtv_frontend_properties params;
};

struct aml_fe_man{
	struct aml_fe       fe[FE_DEV_COUNT];
	struct aml_fe_dev   tuner[FE_DEV_COUNT];
	struct aml_fe_dev   atv_demod[FE_DEV_COUNT];
	struct aml_fe_dev   dtv_demod[FE_DEV_COUNT];
	struct dvb_frontend dev[FE_DEV_COUNT];
	struct pinctrl     *pinctrl;
	struct platform_device *pdev;
};

extern int aml_register_fe_drv(aml_fe_dev_type_t type, struct aml_fe_drv *drv);

extern int aml_unregister_fe_drv(aml_fe_dev_type_t type, struct aml_fe_drv *drv);

extern const char* soundsys_to_str(unsigned short soundsys);
extern const char* audmode_to_str(unsigned short soundsys);
extern const char* v4l2_std_to_str(v4l2_std_id std);
extern const char* fe_type_to_str(fe_type_t type);
#endif /*_AML_FE_H_*/
