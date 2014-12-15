/*
 * 
 * Copyright (C) 2011 Goodix, Inc.
 * 
 * Author: Scott
 * Date: 2012.01.05
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <mach/gpio.h>
//#include <plat/gpio-cfg.h>
#include <linux/irq.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/mm.h> 
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include "gt82x.h"

//#ifdef AUTO_UPDATE_GUITAR


//****************************升级模块参数******************************************
#define SEARCH_FILE_TIMES    100
#define UPDATE_FILE_PATH_2   "/data/goodix/_goodix_update_.bin"
#define UPDATE_FILE_PATH_1   "/sdcard/goodix/_goodix_update_.bin"

#define PACK_SIZE              64                    //update file package size

#define BIT_NVRAM_STROE        0
#define BIT_NVRAM_RECALL       1
#define BIT_NVRAM_LOCK         2
#define REG_NVRCS_H            0X12
#define REG_NVRCS_L            0X01

#define READ_FW_MSG_ADDR_H     0x0F
#define READ_FW_MSG_ADDR_L     0x7C
#define UPDATE_FW_MSG_ADDR_H   0x40
#define UPDATE_FW_MSG_ADDR_L   0x50
#define READ_MSK_VER_ADDR_H    0xC0
#define READ_MSK_VER_ADDR_L    0x09

#define FW_HEAD_LENGTH         30
#define FILE_HEAD_LENGTH       100
#define IGNORE_LENGTH          100
#define FW_MSG_LENGTH          7
#define UPDATE_DATA_LENGTH     5000

static int guitar_update_proc(void*);
static u8 get_ic_fw_msg(struct goodix_ts_data *);
static int guitar_update_mode(struct goodix_ts_data *);
extern int goodix_init_panel(struct goodix_ts_data *, u8);

#pragma pack(1)
typedef struct 
{
    u8  type;          //产品类型//
    u16 version;       //FW版本号//
    u8  msk_ver[4];    //MASK版本//
    u8  st_addr[2];    //烧录的起始地址//
    u16 lenth;         //FW长度//
    u8  chk_sum[3];
    u8  force_update[6];//强制升级标志,为"GOODIX"则强制升级//
}st_fw_head;
#pragma pack()

typedef struct
{
    u8 force_update;
    u8 fw_flag;
    loff_t gt_loc;
    struct file *file; 
    st_fw_head  ic_fw_msg;
    mm_segment_t old_fs;
}st_update_msg;

st_update_msg update_msg;
//******************************************************************************


static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, int len)
{
    struct i2c_msg msgs[2];
    int ret=-1;

    //发送写地址
    msgs[0].flags=!I2C_M_RD; //写消息
    msgs[0].addr=client->addr;
    msgs[0].len=2;
    msgs[0].buf=&buf[0];
    //接收数据
    msgs[1].flags=I2C_M_RD;//读消息
    msgs[1].addr=client->addr;
    msgs[1].len=len-2;
    msgs[1].buf=&buf[2];

    ret=i2c_transfer(client->adapter,msgs, 2);

    return ret;
}

/*******************************************************	
功能：
	向从机写数据
参数：
	client:	i2c设备，包含设备地址
	buf[0]~buf[1]：	 首字节为写地址
	buf[2]~buf[len]：数据缓冲区
	len：	数据长度	
return：
	执行消息数
*******************************************************/
/*Function as i2c_master_send */
static int i2c_write_bytes(struct i2c_client *client,uint8_t *data,int len)
{
    struct i2c_msg msg;
    int ret=-1;
    
    //发送设备地址
    msg.flags=!I2C_M_RD;//写消息
    msg.addr=client->addr;
    msg.len=len;
    msg.buf=data;        

    ret=i2c_transfer(client->adapter,&msg, 1);

    return ret;
}

/*******************************************************
功能：
	发送后缀命令
	
	ts:	client私有数据结构体
return：

	执行结果码，0表示正常执行
*******************************************************/
static int i2c_end_cmd(struct goodix_ts_data *ts)
{
    int ret;
    u8 end_cmd_data[2]={0x80, 0x00};    

    ret=i2c_write_bytes(ts->client,end_cmd_data,2);
    return ret;//*/
}

static u8 is_equal( u8 *src , u8 *dst , int len )
{
    int i;

    for( i = 0 ; i < len ; i++ )
    {
        if (src[i] != dst[i])
        {
            return false;
        }
    }

    return true;
}


static u8 get_ic_msg(struct goodix_ts_data *ts, u16 addr, u8* msg, s32 len)
{
    s32 i = 0;

    msg[0] = addr >> 8 & 0xff;
    msg[1] = addr & 0xff;

    for (i = 0; i < 5; i++)
    {
        if (i2c_read_bytes(ts->client, msg, ADDR_LENGTH + len) > 0)
        {
            break;
        }
    }
    i2c_end_cmd(ts);

    if (i >= 5)
    {
        DEBUG_UPDATE("Read data from 0x%02x%02x failed!\n", msg[0], msg[1]);
        return fail;
    }

    return success;
}

static u8 clear_mix_flag(struct goodix_ts_data *ts)
{
    s32 i = 0;
    u8 buf[3];
    
    buf[0] = 0x14;
    buf[1] = 0x00;
    buf[2] = 0x80;
    
    for (i = 0; i < 5; i++)
    {
        if (i2c_write_bytes(ts->client, buf, 3) > 0)
        {
            break;
        }
    }
    i2c_end_cmd(ts);

    if (i >= 5)
    {
        DEBUG_UPDATE("Clear mix flag failed!\n");
        return fail;
    }

    return success;
}

static u8 get_ic_fw_msg(struct goodix_ts_data *ts)
{
    s32 ret = 0;
    s32 i = 0;
    u8 buf[32];
    
    if (fail == clear_mix_flag(ts))
    {
        return fail;
    }
    
    //Get the mask version in rom of IC
    if (fail == get_ic_msg(ts, READ_MSK_VER_ADDR_H << 8 | READ_MSK_VER_ADDR_L, buf, 4))
    {
        DEBUG_UPDATE("Read mask version failed!\n");
        return fail;
    }
    
    memcpy(update_msg.ic_fw_msg.msk_ver, &buf[ADDR_LENGTH], 4);
    DEBUG_UPDATE("IC The mask version in rom is %c%c%c%c.\n",
                 update_msg.ic_fw_msg.msk_ver[0],update_msg.ic_fw_msg.msk_ver[1],
                 update_msg.ic_fw_msg.msk_ver[2],update_msg.ic_fw_msg.msk_ver[3]);

#if 1
    //Get the firmware msg in IC, include firmware version and checksum flag
    for (i = 0; i < 2; i++)
    {
        if (fail == get_ic_msg(ts, READ_FW_MSG_ADDR_H<< 8 | READ_FW_MSG_ADDR_L, buf, 4))
        {
            DEBUG_UPDATE("Get firmware msg in IC error.\n");
            return fail;
        }
        update_msg.force_update = buf[ADDR_LENGTH];
        if (i == 0 && update_msg.force_update == 0xAA)
        {
            DEBUG_UPDATE("The check sum in ic is error.\n");
            DEBUG_UPDATE("IC will be reset.\n");
            DEBUG_UPDATE("If the check sum is still error,\n ");
            DEBUG_UPDATE("The IC will be updated by force.\n");

            guitar_reset(ts, 10);
            continue;
            //msleep(100);
        }
        break;
    }
    //ic_fw_msg.type = buf[ADDR_LENGTH + 1];
    update_msg.ic_fw_msg.version = buf[ADDR_LENGTH + 2] << 8 | buf[ADDR_LENGTH + 3];
    DEBUG_UPDATE("IC VID:0x%x\n", (int)update_msg.ic_fw_msg.version);
    DEBUG_UPDATE("IC force update:%x\n", update_msg.force_update);
#endif

    //Cuts the frequency
    buf[0] = 0x15;
    buf[1] = 0x22;
    buf[2] = 0x18;
    ret =  i2c_write_bytes(ts->client, buf, 3);
    if (ret <= 0)
    {
        return fail;
    }
    i2c_end_cmd(ts);
    
    //Get the pid at 0x4011 in nvram
    if (fail == get_ic_msg(ts, 0x4011, buf, 1))
    {
        DEBUG_UPDATE("Read pid failed!\n");
        return fail;
    }
    update_msg.ic_fw_msg.type = buf[ADDR_LENGTH];
    
    DEBUG_UPDATE("IC PID:%x\n", update_msg.ic_fw_msg.type);

//    guitar_reset(ts, 10);
    return success;
}

/*
* Steps of reset guitar
*1. INT脚输出低，延时5ms
*2. RESET脚拉低100ms，转输入悬浮态
*3. I2C寻址GUITAR
*4. 延时100ms读取0xff(3、4轮询80次，直至成功)
*5. Oxff等于0x55则返回成功，否则失败
*/
static int guitar_update_mode( struct goodix_ts_data *ts )
{
    int ret = 1;
    u8 retry;
    unsigned char inbuf[3] = {0,0xff,0};

    // step 1
    guitar_enter_update_mode(ts);
    msleep(5);

    //step 2
    guitar_reset(ts, 100);

    for(retry=0;retry < 80; retry++)
    {
        //step 3
        ret =i2c_write_bytes(ts->client, inbuf, 0);    //Test I2C connection.
        if (ret > 0)
        {
            DEBUG_UPDATE("<Set update mode>I2C is OK!\n");
            //step 4
            msleep(100);
            ret =i2c_read_bytes(ts->client, inbuf, 3);
            if (ret > 0)
            {
                DEBUG_UPDATE("The value of 0x00ff is 0x%02x\n", inbuf[2]);
                //step 5
                if(inbuf[2] == 0x55)
                {
                    return success;
                }
            }
        }
        msleep(10);
    }
    DEBUG_UPDATE(KERN_INFO"Detect address %0X\n", ts->client->addr);

    return fail;
}

u8 load_update_file(struct goodix_ts_data *ts, st_fw_head* fw_head, u8* data, u8* path)
{
    u8 mask_num = 0;
    int ret = 0;
    int i = 0;
    u8 buf[FW_HEAD_LENGTH];

    if (path)
    {
        update_msg.file = filp_open(path, O_RDWR, 0666);
        
        if (IS_ERR(update_msg.file))
        {
            DEBUG_UPDATE("Open update file(%s) error!\n", path);
            return fail;
        }
    }
    else
    {
        //Begin to search update file
        for (i = 0; i < SEARCH_FILE_TIMES; i++)
        {
            update_msg.file = filp_open(UPDATE_FILE_PATH_1, O_RDWR, 0666);
            if (IS_ERR(update_msg.file))
            {
                update_msg.file = filp_open(UPDATE_FILE_PATH_2, O_RDWR, 0666);//O_RDWR
                if (IS_ERR(update_msg.file))
                {
                    DEBUG_UPDATE("%3d:Searching file...\n", i);
                    msleep(3000);
                    continue;
                }
                else
                {
                    break;
                }
            }
            else
            {
                break;
            }
        }
        if (i >= 100)
        {
            DEBUG_UPDATE("Can't find update file.\n");
            return fail;
        }
        DEBUG_UPDATE("Find the update file.\n");
    }
    
    update_msg.old_fs = get_fs();
    set_fs(KERNEL_DS);

    update_msg.file->f_pos = IGNORE_LENGTH;

    //Make sure the file is the right file.(By compare the "Guitar" flag)
    ret = update_msg.file->f_op->read(update_msg.file, (char*)&buf, 6, &update_msg.file->f_pos);
    if (ret < 0)
    {
        DEBUG_UPDATE("Read \"Guitar\" flag error.\n");
        goto load_failed;
    }
    if (false == is_equal(buf, "Guitar", 6))
    {
        DEBUG_UPDATE("The flag is %s.Not equal!\n"
                     "The update file is incorrect!\n", buf);
        goto load_failed; 
    }
    DEBUG_UPDATE("The file flag is :%s.\n", buf);
    
    //Get the total number of masks
    update_msg.file->f_pos++; //ignore one byte.
    ret = update_msg.file->f_op->read(update_msg.file, &mask_num, 1, &update_msg.file->f_pos);
    if (ret < 0)
    {
        DEBUG_UPDATE("Didn't get the mask number from the file.\n");
        goto load_failed;
    }
    DEBUG_UPDATE("FILE The total number of masks is:%d.\n", mask_num);
    update_msg.file->f_pos = FILE_HEAD_LENGTH + IGNORE_LENGTH;

    //Get the correct nvram data
    //The correct conditions: 
    //1. the product id is the same
    //2. the mask id is the same
    //3. the nvram version in update file is greater than the nvram version in ic 
    //or force update flag is marked or the check sum in ic is wrong
    update_msg.gt_loc = -1;
    for ( i = 0; i < mask_num; i++)
    {        
        ret = update_msg.file->f_op->read(update_msg.file, (char*)buf, FW_HEAD_LENGTH, &update_msg.file->f_pos);
        if (ret < 0)
        {
            DEBUG_UPDATE("Read update file head error.\n");
            goto load_failed;
        }
        memcpy(fw_head, buf, sizeof(st_fw_head));
        fw_head->version = buf[1] << 8 | buf[2];
        fw_head->lenth = buf[9] << 8 | buf[10];
        DEBUG_UPDATE("No.%d firmware\n", i);
        DEBUG_UPDATE("FILE PID:%x\n", fw_head->type);
        DEBUG_UPDATE("FILE VID:0x%x\n", fw_head->version);
        DEBUG_UPDATE("FILE mask version:%c%c%c%c.\n", fw_head->msk_ver[0],
                     fw_head->msk_ver[1],fw_head->msk_ver[2],fw_head->msk_ver[3]);
        DEBUG_UPDATE("FILE start address:0x%02x%02x.\n", fw_head->st_addr[0], fw_head->st_addr[1]);
        DEBUG_UPDATE("FILE length:%d\n", (int)fw_head->lenth);
        DEBUG_UPDATE("FILE force update flag:%s\n", fw_head->force_update);
        DEBUG_UPDATE("FILE chksum:0x%02x%02x%02x\n", fw_head->chk_sum[0], 
                                 fw_head->chk_sum[1], fw_head->chk_sum[2]);

        //First two conditions
        if (is_equal(fw_head->msk_ver, update_msg.ic_fw_msg.msk_ver, sizeof(update_msg.ic_fw_msg.msk_ver))
            && update_msg.ic_fw_msg.type == fw_head->type)
        {
            DEBUG_UPDATE("Get the same mask version and same pid.\n");
            //The third condition
            if (fw_head->version > update_msg.ic_fw_msg.version
                || is_equal(fw_head->force_update, "GOODIX", 6) 
                || update_msg.force_update == 0xAA)
            {
               // DEBUG_UPDATE("FILE read position:%d\n", file->f_pos);
               // file->f_pos = FW_HEAD_LENGTH + FILE_HEAD_LENGTH + IGNORE_LENGTH;

                if (is_equal(fw_head->force_update, "GOODIX", 6))
                {
                    update_msg.gt_loc = update_msg.file->f_pos - FW_HEAD_LENGTH + sizeof(st_fw_head) - sizeof(fw_head->force_update);
                }
                
                ret = update_msg.file->f_op->read(update_msg.file, (char*)data, fw_head->lenth, &update_msg.file->f_pos);
                if (ret <= 0)
                {
                    DEBUG_UPDATE("Read firmware data in file error.\n");
                    goto load_failed;
                }
               // DEBUG_ARRAY(data, 512);
               // set_fs(ts->old_fs);
              //  filp_close(ts->file, NULL);
                DEBUG_UPDATE("Load data from file successfully.\n");
                return success;
            }
            DEBUG_UPDATE("Don't meet the third condition.\n");
            goto load_failed;
        }

        update_msg.file->f_pos += UPDATE_DATA_LENGTH;
    }

load_failed:    
    set_fs(update_msg.old_fs);
    filp_close(update_msg.file, NULL);
    return fail;
}

static u8 guitar_nvram_store( struct goodix_ts_data *ts )
{
    int ret;
    int i;
    u8 inbuf[3] = {REG_NVRCS_H,REG_NVRCS_L, 0x18};

    ret = i2c_read_bytes(ts->client, inbuf, 3);
    if ( ret < 0 )
    {
        return fail;
    }

    if ((inbuf[2] & BIT_NVRAM_LOCK ) == BIT_NVRAM_LOCK)
    {
        return fail;
    }

    inbuf[2] = 0x18;
    inbuf[2] |= (1<<BIT_NVRAM_STROE);        //store command

    for ( i = 0 ; i < 300 ; i++ )
    {
        ret = i2c_write_bytes( ts->client, inbuf, 3 );
        if ( ret > 0 )
            return success;
    }

    return fail;
}

static u8 guitar_nvram_recall( struct goodix_ts_data *ts )
{
    int ret;
    u8 inbuf[3] = {REG_NVRCS_H,REG_NVRCS_L,0};

    ret = i2c_read_bytes( ts->client, inbuf, 3 );
    if ( ret < 0 )
    {
        return fail;
    }

    if ( ( inbuf[2]&BIT_NVRAM_LOCK) == BIT_NVRAM_LOCK )
    {
        return fail;
    }

    inbuf[2] = ( 1 << BIT_NVRAM_RECALL );        //recall command
    ret = i2c_write_bytes( ts->client , inbuf, 3);

    if (ret <= 0)
    {
        return fail;
    }
    return success;
}

static u8 guitar_update_nvram(struct goodix_ts_data *ts, st_fw_head* fw_head, u8 *nvram)
{
    int length = 0;
    int ret = 0;
    int write_bytes = 0;
    int retry = 0;
    int i = 0;
    int comp = 0;
    u16 st_addr = 0;
    u8 w_buf[PACK_SIZE + ADDR_LENGTH];
    u8 r_buf[PACK_SIZE + ADDR_LENGTH];

    if (fw_head->lenth > PACK_SIZE)
    {
        write_bytes = PACK_SIZE;
    }
    else
    {
        write_bytes = fw_head->lenth;
    }

    clear_mix_flag(ts);
    st_addr = (fw_head->st_addr[0] << 8) | (fw_head->st_addr[1]&0xff);
    memcpy(&w_buf[2], &nvram[length], write_bytes);
    DEBUG_UPDATE("Total length:%d\n", (int)fw_head->lenth);
    while(length < fw_head->lenth)
    {
        w_buf[0] = st_addr >> 8;
        w_buf[1] = st_addr & 0xff;
        DEBUG_UPDATE("Write address:0x%02x%02x\tlength:%d\n", w_buf[0], w_buf[1], write_bytes);
        ret =  i2c_write_bytes(ts->client, w_buf, ADDR_LENGTH + write_bytes);
        if (ret <= 0)
        {
            if (retry++ > 10)
            {
                DEBUG_UPDATE("Write the same address 10 times.Give up!\n");
                return fail;
            }
            DEBUG_UPDATE("Write error![guitar_update_nvram]\n");
            continue;
        }
        else
        {
//            DEBUG_UPDATE("w_buf:\n");
//            DEBUG_ARRAY(w_buf, ADDR_LENGTH + write_bytes);
/*            r_buf[0] = 0x14;
            r_buf[1] = 0x00;
            r_buf[2] = 0x80;
            i2c_write_bytes(ts->client, r_buf, 3);
            r_buf[0] = 0x14;
            r_buf[1] = 0x00;
            i2c_read_bytes(ts->client, r_buf, 3);
            DEBUG_UPDATE("I2CCS:0x%x\n", r_buf[2]);//*/
            
            r_buf[0] = w_buf[0];
            r_buf[1] = w_buf[1];

            for (i = 0; i < 10; i++)
            {
                ret = i2c_read_bytes(ts->client, r_buf, ADDR_LENGTH + write_bytes);
                if (ret <= 0)
                {
                    continue;
                }
                break;
            }
            if (i >= 10)
            {
                DEBUG_UPDATE("Read error! Can't check the nvram data.\n");
                return fail;
            }
//            DEBUG_UPDATE("r_buf:\n");
//            DEBUG_ARRAY(r_buf, ADDR_LENGTH + write_bytes);
#if 0            
            if (fail == guitar_nvram_store(ts))
            {
                DEBUG_UPDATE("Store nvram failed.\n");
                //continue;
            }
            return fail;
#endif
            if (false == is_equal(r_buf, w_buf, ADDR_LENGTH + write_bytes))
            {   
                if (comp ++ > 10)
                {
                    DEBUG_UPDATE("Compare error!\n");
                    return fail;
                }
                DEBUG_UPDATE("Updating nvram: Not equal!\n");

                DEBUG_UPDATE("r_buf:\n");
                DEBUG_ARRAY(r_buf, ADDR_LENGTH + write_bytes);

                
                DEBUG_UPDATE("w_buf:\n");
//                DEBUG_ARRAY(w_buf, ADDR_LENGTH + write_bytes);
                continue;
                //return fail;
            }
        }
        comp = 0;
        retry = 0;
        length += PACK_SIZE;
        st_addr += PACK_SIZE;
        if ((length + PACK_SIZE) > fw_head->lenth)
        {
            write_bytes = fw_head->lenth - length;
        }
        memcpy(&w_buf[2], &nvram[length], write_bytes);
    }

    return success;
}

static u8 guitar_update_firmware(struct goodix_ts_data *ts, st_fw_head* fw_head, u8 *nvram)
{
    int retry;
    int ret;
    u32 status = 0;
    u8 buf[32];

    //Cuts the frequency
    buf[0] = 0x15;
    buf[1] = 0x22;
    buf[2] = 0x18;
    ret =  i2c_write_bytes(ts->client, buf, 3);
    if (ret <= 0)
    {
        return fail;
    }

    get_ic_msg(ts, 0x1522, buf, 1);
    DEBUG_UPDATE("IC OSC_CAL:0x%02x.\n", buf[2]);

    for (retry = 0; retry < 10; retry++)
    {
        //Write the 1st part (pid and vid)
  /*      if (!(status & 0x01))
        {
            buf[0] = UPDATE_FW_MSG_ADDR_H;
            buf[1] = UPDATE_FW_MSG_ADDR_L;
            buf[2] = fw_head->type;
            buf[3] = fw_head->version >> 8;
            buf[4] = fw_head->version & 0xff;
            ret = i2c_write_bytes(ts->client, buf, 5);
            if (ret <= 0)
            {
                continue;
            }
            else
            {
                DEBUG_UPDATE("Update pid and vid successfully!\n");
                status |= 0x01;
                msleep(1);
            }
        }
*/
        //Write the 2nd part (nvram)
        if (!(status & 0x02))
        {
            if (fail == guitar_update_nvram(ts, fw_head, nvram))
            {
                continue;
            }
            else
            {
                DEBUG_UPDATE("Update nvram successfully!\n");
                status |= 0x02;
                msleep(1);
            }
        }

        //Write the 3rd part (check sum)
        if (1)
        {
            buf[0] = 0x4f;
            buf[1] = 0xf3;
            memcpy(&buf[2], fw_head->chk_sum, sizeof(fw_head->chk_sum));
            ret = i2c_write_bytes(ts->client, buf, 5);
            if (ret <= 0)
            {
                continue;
            }
            else
            {
                DEBUG_UPDATE("Update check sum successfully!\n");
                break;
            }
        }
    }

    if (retry >= 10)
    {
        return fail;
    }
    else
    {
        for (retry = 0; retry < 10; retry++)
        {
            buf[0] = 0x00;
            buf[1] = 0xff;
            buf[2] = 0x44;
            ret = i2c_write_bytes(ts->client, buf, 3);
            if (ret > 0)
            {
                break;
            }
        }

        if (retry >= 10)
        {
            DEBUG_UPDATE("Write address at 0x00ff error!\n");
            return fail;
        }
        msleep(10);
    }

    for (retry = 0; retry < 30; retry++)
    {
        msleep(1);
        if (fail == get_ic_msg(ts, 0x00ff, buf, 1))
        {
            DEBUG_UPDATE("Read address at 0x00ff error!\t retry:%d\n", retry);
            continue;
        }

        if (0xcc == buf[ADDR_LENGTH])
        {
            return success;
        }
        else
        {
            DEBUG_UPDATE("The value of 0x00ff: 0x%02x!\t retry:%d\n", buf[ADDR_LENGTH], retry);
            continue;
        }
    }

    DEBUG_UPDATE("The value of 0x00ff error.\n");
    return fail;
}

static int guitar_update_proc(void *v_ts)
{
    s32 ret;
    u32 retry = 100;
    u32 i = 0;
    struct goodix_ts_data* ts = NULL;
    u8* data = NULL;
    u8* ic_nvram = NULL;
    st_fw_head fw_head;
    u8 buf[32];

    ts = (struct goodix_ts_data*)v_ts;
    data = kzalloc(UPDATE_DATA_LENGTH, GFP_KERNEL);
    if (NULL == data)
    {
        DEBUG_UPDATE("data failed apply for memory.\n");
        return fail;
    }
    
    ic_nvram = kzalloc(UPDATE_DATA_LENGTH, GFP_KERNEL);
    if (NULL == ic_nvram)
    {
        DEBUG_UPDATE("ic_nvram failed apply for memory.\n");
        goto app_mem_failed;
    }
    DEBUG_UPDATE("Apply for memory successfully.memory size: %d.\n", UPDATE_DATA_LENGTH);

    msleep(1000);
    DEBUG_UPDATE("Updating...\n");

    if (fail == load_update_file(ts, &fw_head, &data[2], NULL))
    {
        DEBUG_UPDATE("Load file data failed!\n");
        goto load_failed;
    }
    DEBUG_UPDATE("Load file data successfully!\n");

        if(!ts->irq_is_disable)
        {
            disable_irq(ts->client->irq);
        }
        ts->irq_is_disable = 2;

    for (i = 0; i < 5; i++)
    {
        if (fail == guitar_update_mode(ts))
        {
            DEBUG_UPDATE("Next try![Enter update mode]\n");
            continue;
        }
        else
        {
            DEBUG_UPDATE("Set update mode successfully.\n");
            break;
        }
    }
    if (i >= 5)
    {
        DEBUG_UPDATE("Set update mode failed.\n");
        return fail;
    }
    
    retry = 0;
    while(retry++ < 5)
    {
        if (fail == guitar_update_firmware(ts, &fw_head, &data[2]))
        {
            DEBUG_UPDATE("Update firmware failed.\n");
            continue;
        }
        DEBUG_UPDATE("Update firmware successfully.\n");

        //while(1)  // simulation store operation failed
        if (fail == guitar_nvram_store(ts))
        {
            DEBUG_UPDATE("Store nvram failed.\n");
            continue;
        }

        msleep(100);

        if (fail == get_ic_msg(ts, 0x1201, buf, 1))
        {
            DEBUG_UPDATE("Read NVRCS failed.(Store)\n");
            continue;
        }
        if (buf[ADDR_LENGTH] & 0x01)
        {
            DEBUG_UPDATE("Check NVRCS(0x%02x) failed.(Store)\n", buf[ADDR_LENGTH]);
            continue;
        }

        DEBUG_UPDATE("Store nvram successfully.\n");

        if (fail == guitar_nvram_recall(ts))
        {
            DEBUG_UPDATE("Recall nvram failed.\n");
            continue;
        }
        msleep(5);
        
        if (fail == get_ic_msg(ts, 0x1201, buf, 1))
        {
            DEBUG_UPDATE("Read NVRCS failed.(Recall)\n");
            continue;
        }
        if (buf[ADDR_LENGTH] & 0x02)
        {
            DEBUG_UPDATE("Check NVRCS(0x%02x) failed.(Recall)\n", buf[ADDR_LENGTH]);
            continue;
        }
        DEBUG_UPDATE("Recall nvram successfully.\n");

        ic_nvram[0] = fw_head.st_addr[0];
        ic_nvram[1] = fw_head.st_addr[1];

        for ( i = 0; i < 10; i++)
        {
            ret = i2c_read_bytes(ts->client, ic_nvram, ADDR_LENGTH + fw_head.lenth);
            if (ret <= 0)
            {
                continue;
            }
            break;
        }

        if (i >= 10)
        {
            DEBUG_UPDATE("Read nvram failed!\n");
            continue;
        }
        DEBUG_UPDATE("Read nvram successfully!\n");

        if (false == is_equal(&data[2], &ic_nvram[2], fw_head.lenth))
        {
            DEBUG_UPDATE("Nvram not equal!\n");
            continue;
        }
        DEBUG_UPDATE("Check nvram by byte successfully!\n");
        
        if (update_msg.gt_loc > 0)
        {
            DEBUG_UPDATE("Location:%d, Ret:%d.\n", (s32)update_msg.gt_loc, (s32)ret);
            memset(buf, 0, sizeof(buf));
            ret = update_msg.file->f_op->write(update_msg.file, buf, 6, &update_msg.gt_loc);
            if (ret < 0)
            {
                DEBUG_UPDATE("Didn't clear the focre update flag in file.\n");
            }
            else
            {
                DEBUG_UPDATE("Clear the focre update flag in file.Location:%d, Ret:%d.\n", (s32)update_msg.gt_loc, (s32)ret);
            }
        }
        DEBUG_UPDATE("Update successfully!\n");
        break;
    }
    
    set_fs(update_msg.old_fs);
    filp_close(update_msg.file, NULL);
    guitar_leave_update_mode(ts);
    DEBUG_UPDATE("Leave update mode!\n");
    
    //Reset guitar
    DEBUG_UPDATE("Reset IC and send config!\n");
    guitar_reset(ts, 10);
    for (i = 0; i < 3; i++)
    {
        if (fail == goodix_init_panel(ts, 1))
        {
            msleep(10);
            continue;
        }
        break;
    }
    if (i >= 3)
    {
        DEBUG_UPDATE("Send config data failed.\n");
    }
    
    msleep(10);
        ts->irq_is_disable = 0;
        enable_irq(ts->client->irq);

load_failed:
    kfree(ic_nvram);
app_mem_failed:
    kfree(data);

    if (retry < 5)
    {
        return success;
    }

    DEBUG_UPDATE("Update failed!\n");
    return fail;    
}

s32 init_update_proc(struct goodix_ts_data *ts)
{
    u8 flag = 0;
    struct task_struct *thread = NULL;
    s32 retry = 0;

    DEBUG_MSG("Ready to run update thread.\n");

    update_msg.fw_flag = get_ic_fw_msg(ts);
    if (fail == update_msg.fw_flag)
    {
        DEBUG_UPDATE("Try get ic msg in update mode.\n");
        for (retry = 0; retry < 5; retry++)
        {
            if (success == guitar_update_mode(ts))
            {
                break;
            }
        }
        if (retry >= 5)
        {
            update_msg.fw_flag = fail;
        }
        else
        {
            DEBUG_UPDATE("Get ic msg in update mode.\n");
            update_msg.fw_flag = get_ic_fw_msg(ts);
            update_msg.ic_fw_msg.version = 0xfff0;
            if (update_msg.force_update == 0xAA)
            {
                flag = 0xff;
            }
        }
        guitar_leave_update_mode(ts);
    }
    else
    {
        guitar_reset(ts, 10);
    }

    if (success == update_msg.fw_flag)
    {
        update_msg.gt_loc = -1;
        thread = kthread_run(guitar_update_proc, (void*)ts, "guitar_update");
        if (IS_ERR(thread))
        {
            dev_err(&ts->client->dev, " failed to create update thread\n");
        }
        if (0xff == flag)
        {
            return 0xff;
        }
    }

    return success;
}
//#endif   //endif AUTO_UPDATE_GUITAR
//******************************End of firmware update surpport*******************************
