#ifndef __ECRNX_DEBUGFS_CUSTOM_H_
#define __ECRNX_DEBUGFS_CUSTOM_H_

#include <linux/init.h>
#include <linux/module.h>
#include <linux/seq_file.h>

#include "lmac_types.h"
#include "ecrnx_debugfs_func.h"
#include "ecrnx_compat.h"


int ecrnx_debugfs_init(void     *private_data);
void ecrnx_debugfs_exit(void);
void ecrnx_debugfs_sta_in_ap_init(void *private_data);
void ecrnx_debugfs_sta_in_ap_del(u8 sta_idx);

#endif


