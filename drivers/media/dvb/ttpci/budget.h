#ifndef __BUDGET_DVB__
#define __BUDGET_DVB__

#include "dvb_frontend.h"
#include "dvbdev.h"
#include "demux.h"
#include "dvb_demux.h"
#include "dmxdev.h"
#include "dvb_filter.h"
#include "dvb_net.h"

#include <linux/module.h>
#include <linux/mutex.h>

#include <media/saa7146.h>

extern int budget_debug;

#ifdef dprintk
#undef dprintk
#endif

#define dprintk(level,args...) \
	    do { if ((budget_debug & level)) { printk("%s: %s(): ", KBUILD_MODNAME, __FUNCTION__); printk(args); } } while (0)

struct budget_info {
	char *name;
	int type;
};

/* place to store all the necessary device information */
struct budget {

	/* devices */
	struct dvb_device dvb_dev;
	struct dvb_net dvb_net;

	struct saa7146_dev *dev;

	struct i2c_adapter i2c_adap;
	struct budget_info *card;

	unsigned char *grabbing;
	struct saa7146_pgtable pt;

	struct tasklet_struct fidb_tasklet;
	struct tasklet_struct vpe_tasklet;

	struct dmxdev dmxdev;
	struct dvb_demux demux;

	struct dmx_frontend hw_frontend;
	struct dmx_frontend mem_frontend;

	int ci_present;
	int video_port;

	u32 buffer_width;
	u32 buffer_height;
	u32 buffer_size;
	u32 buffer_warning_threshold;
	u32 buffer_warnings;
	unsigned long buffer_warning_time;

	u32 ttbp;
	int feeding;

	spinlock_t feedlock;

	spinlock_t debilock;

	struct dvb_adapter dvb_adapter;
	struct dvb_frontend *dvb_frontend;
	int (*read_fe_status)(struct dvb_frontend *fe, fe_status_t *status);
	int fe_synced;

	void *priv;
};

#define MAKE_BUDGET_INFO(x_var,x_name,x_type) \
static struct budget_info x_var ## _info = { \
	.name=x_name,	\
	.type=x_type };	\
static struct saa7146_pci_extension_data x_var = { \
	.ext_priv = &x_var ## _info, \
	.ext = &budget_extension };

#define BUDGET_TT		   0
#define BUDGET_TT_HW_DISEQC	   1
#define BUDGET_PATCH		   3
#define BUDGET_FS_ACTIVY	   4
#define BUDGET_CIN1200S		   5
#define BUDGET_CIN1200C		   6
#define BUDGET_CIN1200T		   7
#define BUDGET_KNC1S		   8
#define BUDGET_KNC1C		   9
#define BUDGET_KNC1T		   10
#define BUDGET_KNC1SP		   11
#define BUDGET_KNC1CP		   12
#define BUDGET_KNC1TP		   13
#define BUDGET_TVSTAR		   14

#define BUDGET_VIDEO_PORTA         0
#define BUDGET_VIDEO_PORTB         1

extern int ttpci_budget_init(struct budget *budget, struct saa7146_dev *dev,
			     struct saa7146_pci_extension_data *info,
			     struct module *owner);
extern void ttpci_budget_init_hooks(struct budget *budget);
extern int ttpci_budget_deinit(struct budget *budget);
extern void ttpci_budget_irq10_handler(struct saa7146_dev *dev, u32 * isr);
extern void ttpci_budget_set_video_port(struct saa7146_dev *dev, int video_port);
extern int ttpci_budget_debiread(struct budget *budget, u32 config, int addr, int count,
				 int uselocks, int nobusyloop);
extern int ttpci_budget_debiwrite(struct budget *budget, u32 config, int addr, int count, u32 value,
				  int uselocks, int nobusyloop);

#endif
