
#ifndef DSP_IRQ_HEADER
#define  DSP_IRQ_HEADER
#include "audiodsp_module.h"
int audiodsp_init_mailbox(struct audiodsp_priv *priv) ;
int audiodsp_release_mailbox(struct audiodsp_priv *priv);
int dsp_mailbox_send(struct audiodsp_priv *priv,int overwrite,int num,int cmd,const char *data,int len);
int audiodsp_get_audioinfo(struct audiodsp_priv *priv);
#if 0
#define pre_read_mailbox(reg)	\
	dma_cache_inv((unsigned long)reg,sizeof(*reg))
#define after_change_mailbox(reg)	\
	dma_cache_wback((unsigned long)reg,sizeof(*reg))	
#else
#define pre_read_mailbox(reg)	
#define after_change_mailbox(reg)	
#endif
enum DSP_CMD
{
DSP_CMD_SET_EQ_PRESET=0,
DSP_CMD_SET_EQ_CUSTOMIZE,
DSP_CMD_GET_EQ_VALUE_SETS,
DSP_CMD_SET_SRS_SURROUND,
DSP_CMD_SET_SRS_TRUBASS,
DSP_CMD_SET_SRS_DIALOG_CLARITY,
DSP_CMD_SET_HDMI_SR,
DSP_CMD_SET_SRS_TRUBASS_GAIN,
DSP_CMD_SET_SRS_DIALOG_CLARITY_GAIN,
DSP_CMD_SET_REINIT_FILTER,
DSP_CMD_COUNT,
};

#endif
