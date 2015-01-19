#ifndef __VDIN_INTERFACE_H__
#define __VDIN_INTERFACE_H__

void sii9233a_stop_vdin(sii9233a_info_t *info);
void sii9233a_start_vdin(sii9233a_info_t *info, int width, int height, int frame_rate, int field_flag);

#endif
