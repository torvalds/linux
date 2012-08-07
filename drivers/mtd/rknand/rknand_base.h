/*
 *  linux/drivers/mtd/rknand/rknand_base.c
 *
 *  Copyright (C) 2005-2009 Fuzhou Rockchip Electronics
 *  ZYF <zyf@rock-chips.com>
 *
 *   
 */
#ifndef _RKNAND_BASE_H
#define _RKNAND_BASE_H
//#include "api_flash.h"

#define DRIVER_NAME	"rk29xxnand"

#define NAND_DEBUG_LEVEL0 0
#define NAND_DEBUG_LEVEL1 1
#define NAND_DEBUG_LEVEL2 2
#define NAND_DEBUG_LEVEL3 3
//#define PAGE_REMAP

#ifndef CONFIG_RKFTL_PAGECACHE_SIZE
#define CONFIG_RKFTL_PAGECACHE_SIZE  64 //定义page映射区大小，单位为MB,mount 在/data/data下。
#endif

extern unsigned long SysImageWriteEndAdd;
extern int g_num_partitions;

/*
 * rknand_state_t - chip states
 * Enumeration for Rknand flash chip state
 */
typedef enum {
    FL_READY,
    FL_READING,
    FL_WRITING,
    FL_ERASING,
    FL_SYNCING,
    FL_UNVALID,
} rknand_state_t;

struct rknand_chip {
    wait_queue_head_t	wq;
    rknand_state_t		state;
    int rknand_schedule_enable;//1 enable ,0 disable
    void (*pFlashCallBack)(void);//call back funtion
};

struct rknand_info {
    int enable;
    char *pbuf;
    int bufSize;
    unsigned int SysImageWriteEndAdd;
    unsigned int nandCapacity;
    struct rknand_chip	rknand;
    int (*ftl_cache_en)(int en);  
    int (*ftl_read) (int Index, int nSec, void *buf);  
    int (*ftl_write) (int Index, int nSec, void *buf ,int mode);
    int (*ftl_write_panic) (int Index, int nSec, void *buf);
    int (*ftl_close)(void);
    int (*ftl_sync)(void);
    int (*proc_bufread)(char *page);
    int (*proc_ftlread)(char *page);
    int (*rknand_schedule_enable)(int en);
    int (*add_rknand_device)(struct rknand_info * prknand_Info);
    int (*get_rknand_device)(struct rknand_info ** prknand_Info);
    void (*rknand_buffer_shutdown)(void);
    int (*GetIdBlockSysData)(char * buf, int Sector);
    char (*GetSNSectorInfo)(char * pbuf);
    char (*GetChipSectorInfo)(char * pbuf);
    int emmc_clk_power_save_en;
    char *pdmaBuf;
    void (*nand_timing_config)(unsigned long AHBnKHz);
    void (*rknand_suspend)(void);
    void (*rknand_resume)(void);
    int reserved[20];
};

extern int rknand_queue_read(int Index, int nSec, void *buf);
extern int rknand_queue_write(int Index, int nSec, void *buf,int mode);
extern int rknand_buffer_init(char * pbuf,int size);
extern void rknand_buffer_data_init(void);
extern void rknand_buffer_shutdown(void);
extern int add_rknand_device(struct rknand_info * prknand_Info);
extern int get_rknand_device(struct rknand_info ** prknand_Info);
extern int rknand_buffer_sync(void);

#endif
