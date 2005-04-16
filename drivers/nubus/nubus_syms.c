/* Exported symbols for NuBus services

   (c) 1999 David Huggins-Daines <dhd@debian.org> */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/nubus.h>

#ifdef CONFIG_PROC_FS
EXPORT_SYMBOL(nubus_proc_attach_device);
EXPORT_SYMBOL(nubus_proc_detach_device);
#endif

MODULE_LICENSE("GPL");

EXPORT_SYMBOL(nubus_find_device);
EXPORT_SYMBOL(nubus_find_type);
EXPORT_SYMBOL(nubus_find_slot);
EXPORT_SYMBOL(nubus_get_root_dir);
EXPORT_SYMBOL(nubus_get_board_dir);
EXPORT_SYMBOL(nubus_get_func_dir);
EXPORT_SYMBOL(nubus_readdir);
EXPORT_SYMBOL(nubus_find_rsrc);
EXPORT_SYMBOL(nubus_rewinddir);
EXPORT_SYMBOL(nubus_get_subdir);
EXPORT_SYMBOL(nubus_get_rsrc_mem);
EXPORT_SYMBOL(nubus_get_rsrc_str);

