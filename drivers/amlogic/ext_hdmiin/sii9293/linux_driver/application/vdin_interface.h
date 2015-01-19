#ifndef __VDIN_INTERFACE_H__
#define __VDIN_INTERFACE_H__

int sii5293_register_tvin_frontend(struct tvin_frontend_s *frontend);

void sii5293_stop_vdin(sii5293_vdin *info);
void sii5293_start_vdin(sii5293_vdin *info, int width, int height, int frame_rate, int field_flag);

#endif
