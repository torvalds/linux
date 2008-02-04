#ifndef __PVRUSB2_DVB_H__
#define __PVRUSB2_DVB_H__

#include "dvb_frontend.h"
#include "dvb_demux.h"
#include "dvb_net.h"
#include "dmxdev.h"
#include "pvrusb2-context.h"

struct pvr2_dvb_adapter {
	struct pvr2_context	*pvr;

	struct dvb_adapter	dvb_adap;
	struct dmxdev		dmxdev;
	struct dvb_demux	demux;
	struct dvb_net		dvb_net;
	struct dvb_frontend	*fe;

	int			feedcount;
	int			max_feed_count;

	unsigned int		digital_up:1;
};

struct pvr2_dvb_props {
	int (*frontend_attach) (struct pvr2_dvb_adapter *);
	int (*tuner_attach) (struct pvr2_dvb_adapter *);
};

int pvr2_dvb_init(struct pvr2_context *pvr);
int pvr2_dvb_exit(struct pvr2_context *pvr);

#endif /* __PVRUSB2_DVB_H__ */
