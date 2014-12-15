
#ifndef DSP_MONITOR_HEADER_H
#define DSP_MONITOR_HEADER_H
#include "audiodsp_module.h"

void start_audiodsp_monitor(struct audiodsp_priv *priv);
void stop_audiodsp_monitor(struct audiodsp_priv *priv);
void init_audiodsp_monitor(struct audiodsp_priv *priv);
void release_audiodsp_monitor(struct audiodsp_priv *priv);

#endif
