#ifndef __RK_EXT_FSHLED_H__
#define __RK_EXT_FSHLED_H__
#include "../camsys_internal.h"
//register flash dev
extern int camsys_init_ext_fsh_module(void);
extern int camsys_deinit_ext_fsh_module(void);
extern void* camsys_register_ext_fsh_dev(camsys_flash_info_t *fsh_info);
extern int camsys_deregister_ext_fsh_dev(void* dev);
extern int camsys_ext_fsh_ctrl(void* dev,int mode,unsigned int on);
#endif
