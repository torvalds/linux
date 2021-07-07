/*
 * drivers/input/touchscreen/gslX680.c
 *
 * Copyright (c) 2012 Shanghai Basewin
 *	Guan Yuwei<guanyuwei@basewin.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */


#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include "tp_suspend.h"

#include "gslx680_pad.h"
#include <linux/wakelock.h>
//#define GSL_DEBUG
#define REPORT_DATA_ANDROID_4_0
//#define SLEEP_CLEAR_POINT
//#define FILTER_POINT
#ifdef FILTER_POINT
#define FILTER_MAX	9
#endif

#define GSLX680_I2C_NAME 	"gslX680-pad"
#define GSLX680_I2C_ADDR 	0x40

int g_wake_pin=0;
int g_irq_pin=0;

#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG		0xe0
#define GSL_PAGE_REG		0xf0

#define TPD_PROC_DEBUG
#ifdef TPD_PROC_DEBUG
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>  //lzk
//static struct proc_dir_entry *gsl_config_proc = NULL;
#define GSL_CONFIG_PROC_FILE "gsl_config"
#define CONFIG_LEN 31
static char gsl_read[CONFIG_LEN];
static u8 gsl_data_proc[8] = {0};
static u8 gsl_proc_flag = 0;

#endif

#define PRESS_MAX    			255
#define MAX_FINGERS 		10
#define MAX_CONTACTS 		10
#define DMA_TRANS_LEN		0x20
static struct i2c_client *gsl_client = NULL;

struct gsl_ts_data {
    u8 x_index;
    u8 y_index;
    u8 z_index;
    u8 id_index;
    u8 touch_index;
    u8 data_reg;
    u8 status_reg;
    u8 data_size;
    u8 touch_bytes;
    u8 update_data;
    u8 touch_meta_data;
    u8 finger_size;
};

static struct gsl_ts_data devices[] = {
    {
        .x_index = 6,
        .y_index = 4,
        .z_index = 5,
        .id_index = 7,
        .data_reg = GSL_DATA_REG,
        .status_reg = GSL_STATUS_REG,
        .update_data = 0x4,
        .touch_bytes = 4,
        .touch_meta_data = 4,
        .finger_size = 70,
    },
};

struct gsl_ts {
    struct i2c_client *client;
    struct input_dev *input;
    struct work_struct work;
    struct workqueue_struct *wq;
    struct gsl_ts_data *dd;
    u8 *touch_data;
    u8 device_id;
    int irq;
    int irq_pin;
    int wake_pin;
    struct  tp_device  tp;
    int screen_max_x;
    int screen_max_y;
    struct gsl_touch_chip_info *gsl_chip_info;
    struct work_struct download_fw_work;
    struct work_struct resume_work;
};

#ifdef GSL_DEBUG
#define print_info(fmt, args...)   \
    do{                              \
        printk(fmt, ##args);     \
    }while(0)
#else
#define print_info(fmt, args...)
#endif


static u32 id_sign[MAX_CONTACTS+1] = {0};
static u8 id_state_flag[MAX_CONTACTS+1] = {0};
static u8 id_state_old_flag[MAX_CONTACTS+1] = {0};
static u16 x_old[MAX_CONTACTS+1] = {0};
static u16 y_old[MAX_CONTACTS+1] = {0};
static u16 x_new = 0;
static u16 y_new = 0;

static struct gsl_touch_chip_info gsl_chip_info[] = {
   {GSL680,GSLX680_FW,ARRAY_SIZE(GSLX680_FW),gsl_config_data_id,ARRAY_SIZE(gsl_config_data_id)},
   {GSL3676,GSL3675B_FW_HK,ARRAY_SIZE(GSL3675B_FW_HK),gsl3678_config_data_id,ARRAY_SIZE(gsl3678_config_data_id)},
};


static int gslX680_init(void)
{
    gpio_set_value(g_wake_pin,1);
    gpio_set_value(g_irq_pin,1);
    return 0;
}

static int gslX680_shutdown_low(void)
{
    if(g_wake_pin !=0)
    {
        gpio_direction_output(g_wake_pin, 0);
        gpio_set_value(g_wake_pin,0);
    }
    return 0;
}

static int gslX680_shutdown_high(void)
{
    if(g_wake_pin !=0)
    {
        gpio_direction_output(g_wake_pin, 0);
        gpio_set_value(g_wake_pin,1);
    }
    return 0;
}

static inline u16 join_bytes(u8 a, u8 b)
{
    u16 ab = 0;
    ab = ab | a;
    ab = ab << 8 | b;
    return ab;
}

static u32 gsl_write_interface(struct i2c_client *client, const u8 reg, u8 *buf, u32 num)
{
    struct i2c_msg xfer_msg[1];

    buf[0] = reg;

    xfer_msg[0].addr = client->addr;
    xfer_msg[0].len = num + 1;
    xfer_msg[0].flags = client->flags & I2C_M_TEN;
    xfer_msg[0].buf = buf;
    //xfer_msg[0].scl_rate = 400*1000; //RK3066 RK2926 I2C±¨´íÊ±´ò¿ªÕâ¸ö

    return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

static int gsl_ts_write(struct i2c_client *client, u8 addr, u8 *pdata, int datalen)
{
    int ret = 0;
    u8 tmp_buf[128];
    unsigned int bytelen = 0;
    if (datalen > 125)
    {
        dev_err(&client->dev,"%s too big datalen = %d!\n", __func__, datalen);
        return -1;
    }

    tmp_buf[0] = addr;
    bytelen++;

    if (datalen != 0 && pdata != NULL)
    {
        memcpy(&tmp_buf[bytelen], pdata, datalen);
        bytelen += datalen;
    }

    ret = i2c_master_send(client, tmp_buf, bytelen);
    return ret;
}

static int gsl_ts_read(struct i2c_client *client, u8 addr, u8 *pdata, unsigned int datalen)
{
    int ret = 0;

    if (datalen > 126)
    {
        dev_err(&client->dev, "%s too big datalen = %d!\n", __func__, datalen);
        return -1;
    }

    ret = gsl_ts_write(client, addr, NULL, 0);
    if (ret < 0)
    {
        dev_err(&client->dev, "%s set data address fail!\n", __func__);
        return ret;
    }

    return i2c_master_recv(client, pdata, datalen);
}

static __inline__ void fw2buf(u8 *buf, const u32 *fw)
{
    u32 *u32_buf = (int *)buf;
    *u32_buf = *fw;
}

static void gsl_load_fw(struct i2c_client *client)
{
    u8 buf[DMA_TRANS_LEN*4 + 1] = {0};
    u8 send_flag = 1;
    u8 *cur = buf + 1;
    u32 source_line = 0;
    u32 source_len;
    struct gsl_ts *ts = i2c_get_clientdata(gsl_client);
    struct fw_data *ptr_fw;

    ptr_fw = ts->gsl_chip_info->ptr_fw;

    source_len = ts->gsl_chip_info->ptr_fw_len;
    for (source_line = 0; source_line < source_len; source_line++)
    {
        /* init page trans, set the page val */
        if (GSL_PAGE_REG == ptr_fw[source_line].offset)
        {
            fw2buf(cur, &ptr_fw[source_line].val);
            gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
            //i2c_smbus_write_i2c_block_data(client, GSL_PAGE_REG,4, buf);
            send_flag = 1;
        }
        else
        {
            if (1 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20))
                buf[0] = (u8)ptr_fw[source_line].offset;

            fw2buf(cur, &ptr_fw[source_line].val);
            cur += 4;

            if (0 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20))
            {
                gsl_write_interface(client, buf[0], buf, cur - buf - 1);
                //i2c_smbus_write_i2c_block_data(client, buf[0], cur - buf - 1, buf);
                cur = buf + 1;
            }

            send_flag++;
        }
    }
}

static int test_i2c(struct i2c_client *client)
{
    u8 read_buf = 0;
    u8 write_buf = 0x12;
    int ret, rc = 1;

    ret = gsl_ts_read( client, 0xf0, &read_buf, sizeof(read_buf) );
    if  (ret  < 0)
        rc --;
    else
        dev_info(&client->dev, "I read reg 0xf0 is %x\n", read_buf);

    mdelay(2);
    ret = gsl_ts_write(client, 0xf0, &write_buf, sizeof(write_buf));
    if(ret  >=  0 )
        dev_info(&client->dev, "I write reg 0xf0 0x12\n");

    mdelay(2);
    ret = gsl_ts_read( client, 0xf0, &read_buf, sizeof(read_buf) );
    if(ret <  0 )
        rc --;
    else
        dev_info(&client->dev, "I read reg 0xf0 is 0x%x\n", read_buf);

    return rc;
}


static void startup_chip(struct i2c_client *client)
{
    u8 tmp = 0x00;

#ifdef GSL_NOID_VERSION
    struct gsl_ts *ts = i2c_get_clientdata(client);
    gsl_DataInit(ts->gsl_chip_info->conf_in);
#endif
    gsl_ts_write(client, 0xe0, &tmp, 1);
}

static void reset_chip(struct i2c_client *client)
{
    u8 tmp = 0x88;
    u8 buf[4] = {0x00};

    gsl_ts_write(client, 0xe0, &tmp, sizeof(tmp));
    mdelay(5);
    tmp = 0x04;
    gsl_ts_write(client, 0xe4, &tmp, sizeof(tmp));
    mdelay(5);
    gsl_ts_write(client, 0xbc, buf, sizeof(buf));
}

static void clr_reg(struct i2c_client *client)
{
    u8 write_buf[4]	= {0};

    write_buf[0] = 0x88;
    gsl_ts_write(client, 0xe0, &write_buf[0], 1);
    msleep(20);
    write_buf[0] = 0x03;
    gsl_ts_write(client, 0x80, &write_buf[0], 1);
    msleep(5);
    write_buf[0] = 0x04;
    gsl_ts_write(client, 0xe4, &write_buf[0], 1);
    msleep(5);
    write_buf[0] = 0x00;
    gsl_ts_write(client, 0xe0, &write_buf[0], 1);
    msleep(20);
}

static void init_chip(struct i2c_client *client)
{
    int rc;
    struct gsl_ts *ts = i2c_get_clientdata(client);

    gslX680_shutdown_low();
    mdelay(20);
    gslX680_shutdown_high();
    gpio_set_value(g_irq_pin,1);
    msleep(20);
    rc = test_i2c(client);
    if(rc < 0)
    {
        dev_err(&client->dev, "------gslX680 test_i2c error------\n");
        return;
    }
    queue_work(ts->wq, &ts->download_fw_work);  
}

static void check_mem_data(struct i2c_client *client)
{
    u8 read_buf[4]  = {0};

    gsl_ts_read(client,0xb0, read_buf, sizeof(read_buf));

    if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a || read_buf[0] != 0x5a)
    {
        dev_info(&client->dev, "#########check mem read 0xb0 = %x %x %x %x #########\n", read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
        init_chip(client);
    }
}


#ifdef TPD_PROC_DEBUG
static int char_to_int(char ch)
{
    if(ch>='0' && ch<='9')
        return (ch-'0');
    else
        return (ch-'a'+10);
}


static int gsl_read_interface(struct i2c_client *client, u8 reg, u8 *buf, u32 num)
{

    int err = 0;
    u8 temp = reg;
    //mutex_lock(&gsl_i2c_lock);
    if(temp < 0x80)
    {
        temp = (temp+8)&0x5c;
        i2c_master_send(client,&temp,1);
        err = i2c_master_recv(client,&buf[0],4);

        temp = reg;
        i2c_master_send(client,&temp,1);
        err = i2c_master_recv(client,&buf[0],4);
    }
    i2c_master_send(client,&reg,1);
    err = i2c_master_recv(client,&buf[0],num);
    //mutex_unlock(&gsl_i2c_lock);
    return err;
}

static int gsl_config_read_proc(struct seq_file *m,void *v)
{
    //char *ptr = page;
    char temp_data[5] = {0};
    unsigned int tmp=0;

    if('v'==gsl_read[0]&&'s'==gsl_read[1])
    {
#ifdef GSL_NOID_VERSION
        tmp=gsl_version_id();
#else
        tmp=0x20121215;
#endif
        seq_printf(m,"version:%x\n",tmp);
    }
    else if('r'==gsl_read[0]&&'e'==gsl_read[1])
    {
        if('i'==gsl_read[3])
        {
#ifdef GSL_NOID_VERSION
            tmp=(gsl_data_proc[5]<<8) | gsl_data_proc[4];
#endif
        }
        else
        {
            gsl_ts_write(gsl_client,0xf0,&gsl_data_proc[4],4);
            gsl_read_interface(gsl_client,gsl_data_proc[0],temp_data,4);
            seq_printf(m,"offset : {0x%02x,0x",gsl_data_proc[0]);
            seq_printf(m,"%02x",temp_data[3]);
            seq_printf(m,"%02x",temp_data[2]);
            seq_printf(m,"%02x",temp_data[1]);
            seq_printf(m,"%02x};\n",temp_data[0]);
        }
    }
    return 0;
}

ssize_t gsl_config_write_proc(struct file *file, const char *buffer, size_t count, loff_t *data)
{
    u8 buf[8] = {0};
    char temp_buf[CONFIG_LEN];
    char *path_buf;
    int tmp = 0;
    int tmp1 = 0;
    struct gsl_ts *ts = i2c_get_clientdata(gsl_client);

    print_info("[tp-gsl][%s] \n",__func__);
    if(count > 512)
    {
        pr_err("size not match [%d:%ld]\n", CONFIG_LEN, count);
        return -EFAULT;
    }
    path_buf=kzalloc(count,GFP_KERNEL);
    if(!path_buf)
    {
        pr_err("alloc path_buf memory error \n");
    }
    if(copy_from_user(path_buf, buffer, count))
    {
        pr_err("copy from user fail\n");
        goto exit_write_proc_out;
    }
    memcpy(temp_buf,path_buf,(count<CONFIG_LEN?count:CONFIG_LEN));

    buf[3]=char_to_int(temp_buf[14])<<4 | char_to_int(temp_buf[15]);
    buf[2]=char_to_int(temp_buf[16])<<4 | char_to_int(temp_buf[17]);
    buf[1]=char_to_int(temp_buf[18])<<4 | char_to_int(temp_buf[19]);
    buf[0]=char_to_int(temp_buf[20])<<4 | char_to_int(temp_buf[21]);

    buf[7]=char_to_int(temp_buf[5])<<4 | char_to_int(temp_buf[6]);
    buf[6]=char_to_int(temp_buf[7])<<4 | char_to_int(temp_buf[8]);
    buf[5]=char_to_int(temp_buf[9])<<4 | char_to_int(temp_buf[10]);
    buf[4]=char_to_int(temp_buf[11])<<4 | char_to_int(temp_buf[12]);
    if('v'==temp_buf[0]&& 's'==temp_buf[1])//version //vs
    {
        memcpy(gsl_read,temp_buf,4);
    }
    else if('s'==temp_buf[0]&& 't'==temp_buf[1])//start //st
    {
        gsl_proc_flag = 1;
        reset_chip(gsl_client);
    }
    else if('e'==temp_buf[0]&&'n'==temp_buf[1])//end //en
    {
        msleep(20);
        reset_chip(gsl_client);
        startup_chip(gsl_client);
        gsl_proc_flag = 0;
    }
    else if('r'==temp_buf[0]&&'e'==temp_buf[1])//read buf //
    {
        memcpy(gsl_read,temp_buf,4);
        memcpy(gsl_data_proc,buf,8);
    }
    else if('w'==temp_buf[0]&&'r'==temp_buf[1])//write buf
    {
        gsl_ts_write(gsl_client,buf[4],buf,4);
    }
#ifdef GSL_NOID_VERSION
    else if('i'==temp_buf[0]&&'d'==temp_buf[1])//write id config //
    {
        tmp1=(buf[7]<<24)|(buf[6]<<16)|(buf[5]<<8)|buf[4];
        tmp=(buf[3]<<24)|(buf[2]<<16)|(buf[1]<<8)|buf[0];
        if(tmp1>=0 && tmp1<ts->gsl_chip_info->conf_in_len)
        {
            ts->gsl_chip_info->conf_in[tmp1] = tmp;
        }
    }
#endif
exit_write_proc_out:
    kfree(path_buf);
    return count;
}

static int gsl_server_list_open(struct inode *inode,struct file *file)
{
    return single_open(file,gsl_config_read_proc,NULL);
}
static const struct proc_ops gsl_seq_fops = {
    .proc_open = gsl_server_list_open,
    .proc_read = seq_read,
    .proc_release = single_release,
    .proc_write = gsl_config_write_proc,
};
#endif

#ifdef FILTER_POINT
static void filter_point(u16 x, u16 y , u8 id)
{
    u16 x_err =0;
    u16 y_err =0;
    u16 filter_step_x = 0, filter_step_y = 0;

    id_sign[id] = id_sign[id] + 1;
    if(id_sign[id] == 1)
    {
        x_old[id] = x;
        y_old[id] = y;
    }

    x_err = x > x_old[id] ? (x -x_old[id]) : (x_old[id] - x);
    y_err = y > y_old[id] ? (y -y_old[id]) : (y_old[id] - y);

    if( (x_err > FILTER_MAX && y_err > FILTER_MAX/3) || (x_err > FILTER_MAX/3 && y_err > FILTER_MAX) )
    {
        filter_step_x = x_err;
        filter_step_y = y_err;
    }
    else
    {
        if(x_err > FILTER_MAX)
            filter_step_x = x_err;
        if(y_err> FILTER_MAX)
            filter_step_y = y_err;
    }

    if(x_err <= 2*FILTER_MAX && y_err <= 2*FILTER_MAX)
    {
        filter_step_x >>= 2;
        filter_step_y >>= 2;
    }
    else if(x_err <= 3*FILTER_MAX && y_err <= 3*FILTER_MAX)
    {
        filter_step_x >>= 1;
        filter_step_y >>= 1;
    }
    else if(x_err <= 4*FILTER_MAX && y_err <= 4*FILTER_MAX)
    {
        filter_step_x = filter_step_x*3/4;
        filter_step_y = filter_step_y*3/4;
    }

    x_new = x > x_old[id] ? (x_old[id] + filter_step_x) : (x_old[id] - filter_step_x);
    y_new = y > y_old[id] ? (y_old[id] + filter_step_y) : (y_old[id] - filter_step_y);

    x_old[id] = x_new;
    y_old[id] = y_new;
}
#else
static void record_point(u16 x, u16 y , u8 id)
{
    u16 x_err =0;
    u16 y_err =0;
    return;
    id_sign[id]=id_sign[id]+1;

    if(id_sign[id]==1){
        x_old[id]=x;
        y_old[id]=y;
    }

    x = (x_old[id] + x)/2;
    y = (y_old[id] + y)/2;

    if(x>x_old[id]){
        x_err=x -x_old[id];
    }
    else{
        x_err=x_old[id]-x;
    }

    if(y>y_old[id]){
        y_err=y -y_old[id];
    }
    else{
        y_err=y_old[id]-y;
    }

    if( (x_err > 3 && y_err > 1) || (x_err > 1 && y_err > 3) ){
        x_new = x;     x_old[id] = x;
        y_new = y;     y_old[id] = y;
    }
    else{
        if(x_err > 3){
            x_new = x;     x_old[id] = x;
        }
        else
            x_new = x_old[id];
        if(y_err> 3){
            y_new = y;     y_old[id] = y;
        }
        else
            y_new = y_old[id];
    }

    if(id_sign[id]==1){
        x_new= x_old[id];
        y_new= y_old[id];
    }

}
#endif

static void report_data(struct gsl_ts *ts, u16 x, u16 y, u8 pressure, u8 id)
{

    if(revert_xy)
        swap(x, y);

    if(x > ts->screen_max_x || y > ts->screen_max_y)
    {
        return;
    }

    if(revert_x)
        x = ts->screen_max_x-x;
    if(revert_y) {
        y = ts->screen_max_y-y;
    }

#ifdef REPORT_DATA_ANDROID_4_0
    input_mt_slot(ts->input, id);
    input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
    input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
    input_report_abs(ts->input, ABS_MT_POSITION_X, x);
    input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
    input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
#else
    input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
    input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
    input_report_abs(ts->input, ABS_MT_POSITION_X,x);
    input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
    input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
    input_mt_sync(ts->input);
#endif
}

static void gslX680_ts_worker(struct work_struct *work)
{
    int rc, i;
    u8 id, touches;
    u16 x, y;
#ifdef GSL_NOID_VERSION
    unsigned int tmp1;
    u8 buf[4] = {0};
    struct gsl_touch_info cinfo = {{0}};
#endif
    struct gsl_ts *ts = container_of(work, struct gsl_ts,work);

#ifdef TPD_PROC_DEBUG
    if(gsl_proc_flag == 1)
        goto schedule;
#endif

    rc = gsl_ts_read(ts->client, 0x80, ts->touch_data, ts->dd->data_size);
    if (rc < 0)
    {
        dev_err(&ts->client->dev, "read failed\n");
        reset_chip(ts->client);
        startup_chip(ts->client);
        goto schedule;
    }

    touches = ts->touch_data[ts->dd->touch_index];
#ifdef GSL_NOID_VERSION
    cinfo.finger_num = touches;
    for(i = 0; i < (touches < MAX_CONTACTS ? touches : MAX_CONTACTS); i ++)
    {
        cinfo.x[i] = join_bytes( ( ts->touch_data[ts->dd->x_index  + 4 * i + 1] & 0xf),
                ts->touch_data[ts->dd->x_index + 4 * i]);
        cinfo.y[i] = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
                ts->touch_data[ts->dd->y_index + 4 * i ]);
        cinfo.id[i] = ((ts->touch_data[ts->dd->x_index  + 4 * i + 1] & 0xf0)>>4);
    }
    cinfo.finger_num=(ts->touch_data[3]<<24)|(ts->touch_data[2]<<16)
        |(ts->touch_data[1]<<8)|(ts->touch_data[0]);
    gsl_alg_id_main(&cinfo);
    tmp1=gsl_mask_tiaoping();
    if(tmp1>0&&tmp1<0xffffffff)
    {
        buf[0]=0xa;buf[1]=0;buf[2]=0;buf[3]=0;
        gsl_ts_write(ts->client,0xf0,buf,4);
        buf[0]=(u8)(tmp1 & 0xff);
        buf[1]=(u8)((tmp1>>8) & 0xff);
        buf[2]=(u8)((tmp1>>16) & 0xff);
        buf[3]=(u8)((tmp1>>24) & 0xff);
        gsl_ts_write(ts->client,0x8,buf,4);
    }
    touches = cinfo.finger_num;
#endif

    for(i = 1; i <= MAX_CONTACTS; i ++)
    {
        if(touches == 0)
            id_sign[i] = 0;
        id_state_flag[i] = 0;
    }
    for(i= 0;i < (touches > MAX_FINGERS ? MAX_FINGERS : touches);i ++)
    {
#ifdef GSL_NOID_VERSION
        {
            id = cinfo.id[i];
            x =  cinfo.x[i];
            y =  cinfo.y[i];
        }
#else
        {
            x = join_bytes( ( ts->touch_data[ts->dd->x_index  + 4 * i + 1] & 0xf),
                    ts->touch_data[ts->dd->x_index + 4 * i]);
            y = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
                    ts->touch_data[ts->dd->y_index + 4 * i ]);
            id = ts->touch_data[ts->dd->id_index + 4 * i] >> 4;
        }
#endif
        if(1 <=id && id <= MAX_CONTACTS)
        {
#ifdef FILTER_POINT
            filter_point(x, y ,id);
#else
            record_point(x, y , id);
#endif
            //report_data(ts, x_new, y_new, 10, id);
            report_data(ts, x, y, 10, id);

            id_state_flag[id] = 1;
        }
    }
    for(i = 1; i <= MAX_CONTACTS; i ++)
    {
        if( (0 == touches) || ((0 != id_state_old_flag[i]) && (0 == id_state_flag[i])) )
        {
#ifdef REPORT_DATA_ANDROID_4_0
            input_mt_slot(ts->input, i);
            input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
            input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
#endif
            id_sign[i]=0;
        }
        id_state_old_flag[i] = id_state_flag[i];
    }
    if(0 == touches)
    {
#ifndef REPORT_DATA_ANDROID_4_0
        input_mt_sync(ts->input);
#endif
    }
    input_sync(ts->input);

schedule:
    enable_irq(ts->irq);

}

static irqreturn_t gsl_ts_irq(int irq, void *dev_id)
{
    struct gsl_ts *ts = dev_id;

    disable_irq_nosync(ts->irq);

    if (!work_pending(&ts->work))
    {
        queue_work(ts->wq, &ts->work);
    }

    return IRQ_HANDLED;

}

static int gslX680_ts_init(struct i2c_client *client, struct gsl_ts *ts)
{
    struct input_dev *input_device;
    int rc = 0;

    ts->dd = &devices[ts->device_id];

    if (ts->device_id == 0) {
        ts->dd->data_size = MAX_FINGERS * ts->dd->touch_bytes + ts->dd->touch_meta_data;
        ts->dd->touch_index = 0;
    }

    ts->touch_data = kzalloc(ts->dd->data_size, GFP_KERNEL);
    if (!ts->touch_data) {
        pr_err("%s: Unable to allocate memory\n", __func__);
        return -ENOMEM;
    }

    input_device = input_allocate_device();
    if (!input_device) {
        rc = -ENOMEM;
        goto error_alloc_dev;
    }

    ts->input = input_device;
    input_device->name = GSLX680_I2C_NAME;
    input_device->id.bustype = BUS_I2C;
    input_device->dev.parent = &client->dev;
    input_set_drvdata(input_device, ts);

#ifdef REPORT_DATA_ANDROID_4_0
    __set_bit(EV_ABS, input_device->evbit);
    __set_bit(EV_KEY, input_device->evbit);
    __set_bit(EV_REP, input_device->evbit);
    __set_bit(INPUT_PROP_DIRECT, input_device->propbit);
    input_mt_init_slots(input_device, (MAX_CONTACTS+1),0);
#else
    input_set_abs_params(input_device,ABS_MT_TRACKING_ID, 0, (MAX_CONTACTS+1), 0, 0);
    set_bit(EV_ABS, input_device->evbit);
    set_bit(EV_KEY, input_device->evbit);
    __set_bit(INPUT_PROP_DIRECT, input_device->propbit);
    input_device->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
#endif

    set_bit(ABS_MT_POSITION_X, input_device->absbit);
    set_bit(ABS_MT_POSITION_Y, input_device->absbit);
    set_bit(ABS_MT_TOUCH_MAJOR, input_device->absbit);
    set_bit(ABS_MT_WIDTH_MAJOR, input_device->absbit);

    input_set_abs_params(input_device,ABS_MT_POSITION_X, 0, ts->screen_max_x, 0, 0);
    input_set_abs_params(input_device,ABS_MT_POSITION_Y, 0, ts->screen_max_y, 0, 0);
    input_set_abs_params(input_device,ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
    input_set_abs_params(input_device,ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);

    ts->wq = create_singlethread_workqueue("kworkqueue_ts");
    if (!ts->wq) {
        dev_err(&client->dev, "Could not create workqueue\n");
        goto error_wq_create;
    }
    flush_workqueue(ts->wq);
    INIT_WORK(&ts->work, gslX680_ts_worker);
    rc = input_register_device(input_device);
    if (rc)
        goto error_unreg_device;
    return 0;

error_unreg_device:
    destroy_workqueue(ts->wq);
error_wq_create:
    input_free_device(input_device);
error_alloc_dev:
    kfree(ts->touch_data);
    return rc;
}

static int gsl_ts_suspend(struct device *dev)
{
    struct gsl_ts *ts = dev_get_drvdata(dev);
#ifdef SLEEP_CLEAR_POINT
#ifdef REPORT_DATA_ANDROID_4_0
    int i;
#endif
#endif

    disable_irq_nosync(ts->irq);
    gslX680_shutdown_low();

#ifdef SLEEP_CLEAR_POINT
    msleep(10);
#ifdef REPORT_DATA_ANDROID_4_0
    for(i = 1; i <= MAX_CONTACTS ;i ++)
    {
        input_mt_slot(ts->input, i);
        input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
        input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
    }
#else
    input_mt_sync(ts->input);
#endif
    input_sync(ts->input);
    msleep(10);
    report_data(ts, 1, 1, 10, 1);
    input_sync(ts->input);
#endif

    return 0;
}

static int gsl_ts_resume(struct device *dev)
{
    struct gsl_ts *ts = dev_get_drvdata(dev);

    queue_work(ts->wq, &ts->resume_work);

    return 0;
}

static int gsl_ts_early_suspend(struct tp_device *tp_d)
{
	struct gsl_ts *ts = container_of(tp_d, struct gsl_ts, tp);

	gsl_ts_suspend(&ts->client->dev);

	return 0;
}

static int gsl_ts_late_resume(struct tp_device *tp_d)
{
	struct gsl_ts *ts = container_of(tp_d, struct gsl_ts, tp);

	gsl_ts_resume(&ts->client->dev);

	return 0;
}

static void gsl_download_fw_work(struct work_struct *work)
{
    struct gsl_ts *ts = dev_get_drvdata(&gsl_client->dev);

    clr_reg(ts->client);
    reset_chip(ts->client);
    gsl_load_fw(ts->client);
    startup_chip(ts->client);
    reset_chip(ts->client);
    startup_chip(ts->client);
}

static void  gsl_resume_work(struct work_struct *work)
{
    struct gsl_ts *ts = dev_get_drvdata(&gsl_client->dev);
#ifdef SLEEP_CLEAR_POINT
#ifdef REPORT_DATA_ANDROID_4_0
    int i;
#endif
#endif
    gslX680_shutdown_high();
    msleep(20);
    //reset_chip(ts->client);
    //startup_chip(ts->client);
    check_mem_data(ts->client);
    check_mem_data(ts->client);

#ifdef SLEEP_CLEAR_POINT
#ifdef REPORT_DATA_ANDROID_4_0
    for(i =1;i<=MAX_CONTACTS;i++)
    {
        input_mt_slot(ts->input, i);
        input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
        input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
    }
#else
    input_mt_sync(ts->input);
#endif
    input_sync(ts->input);
#endif
   enable_irq(ts->irq);
}

static int  gsl_ts_probe(struct i2c_client *client,
        const struct i2c_device_id *id)
{
    struct device_node *np = client->dev.of_node;
    enum of_gpio_flags wake_flags, irq_flags;
    struct gsl_ts *ts;
    int rc;
    int gsl_chip_id = 0;
    int i,ret;

    printk("GSLX680 Enter %s\n", __func__);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "I2C functionality not supported\n");
        return -ENODEV;
    }

    ts = kzalloc(sizeof(*ts), GFP_KERNEL);
    if (!ts)
        return -ENOMEM;

    ts->client = client;
    i2c_set_clientdata(client, ts);
    ts->device_id = id->driver_data;


    of_property_read_u32(np,"screen_max_x",&(ts->screen_max_x));
    of_property_read_u32(np,"screen_max_y",&(ts->screen_max_y));

    dev_info(&ts->client->dev, "[tp-gsl] screen_max_x =[%d] \n",ts->screen_max_x);
    dev_info(&ts->client->dev, "[tp-gsl] screen_max_y =[%d] \n",ts->screen_max_y);

    of_property_read_u32(np, "revert_x", &revert_x);
    of_property_read_u32(np, "revert_y", &revert_y);
    of_property_read_u32(np, "revert_xy", &revert_xy);

    dev_info(&ts->client->dev, "[tp-gsl] revert_x =[%d] \n",revert_x);
    dev_info(&ts->client->dev, "[tp-gsl] revert_y =[%d] \n",revert_y);
    dev_info(&ts->client->dev, "[tp-gsl] revert_xy =[%d] \n",revert_xy);

    ts->irq_pin=of_get_named_gpio_flags(np, "touch-gpio", 0, &irq_flags);
    ts->wake_pin=of_get_named_gpio_flags(np, "reset-gpio", 0, &wake_flags);

    ret = of_property_read_u32(np, "chip_id", &gsl_chip_id);
    if(ret)
       gsl_chip_id = GSL680;

       dev_info(&ts->client->dev, "[tp-gsl] gsl_chip_id =[%d] \n",gsl_chip_id);
       for(i=0; i<ARRAY_SIZE(gsl_chip_info); i++) {
           if (gsl_chip_info[i].chip_id == gsl_chip_id) {
               ts->gsl_chip_info =  &gsl_chip_info[i];
               break;
           }
    }

    if (gpio_is_valid(ts->wake_pin)) {
        rc = devm_gpio_request_one(&ts->client->dev, ts->wake_pin, (wake_flags & OF_GPIO_ACTIVE_LOW) ? GPIOF_OUT_INIT_LOW : GPIOF_OUT_INIT_HIGH, "gslX680 wake pin");
        if (rc != 0) {
            dev_err(&ts->client->dev, "gslX680 wake pin error\n");
            return -EIO;
        }
        g_wake_pin = ts->wake_pin;
    } else {
        dev_info(&ts->client->dev, "wake pin invalid\n");
    }
    if (gpio_is_valid(ts->irq_pin)) {
        rc = devm_gpio_request_one(&ts->client->dev, ts->irq_pin, (irq_flags & OF_GPIO_ACTIVE_LOW) ? GPIOF_OUT_INIT_LOW : GPIOF_OUT_INIT_HIGH, "gslX680 irq pin");
        if (rc != 0) {
            dev_err(&ts->client->dev, "gslX680 irq pin error\n");
            return -EIO;
        }
        g_irq_pin = ts->irq_pin;
    } else {
        dev_info(&ts->client->dev, "irq pin invalid\n");
    }

    INIT_WORK(&ts->download_fw_work, gsl_download_fw_work);
    INIT_WORK(&ts->resume_work, gsl_resume_work);

    gslX680_init();
    rc = gslX680_ts_init(client, ts);
    if (rc < 0) {
        dev_err(&client->dev, "GSLX680 init failed\n");
        goto error_mutex_destroy;
    }

    gsl_client = client;
    init_chip(ts->client);
    check_mem_data(ts->client);

    ts->irq=gpio_to_irq(ts->irq_pin);		//If not defined in client
    if (ts->irq)
    {
        rc = devm_request_threaded_irq(&client->dev, ts->irq, NULL, gsl_ts_irq, irq_flags | IRQF_ONESHOT, client->name, ts);
        if (rc != 0) {
            dev_err(&client->dev, "Cannot allocate ts INT!ERRNO:%d\n", rc);
            goto error_req_irq_fail;
        }
        disable_irq(ts->irq);
    }
    else
    {
        dev_err(&client->dev, "gsl x680 irq req fail\n");
        goto error_req_irq_fail;
    }

    /* create debug attribute */
#ifdef TPD_PROC_DEBUG
    proc_create(GSL_CONFIG_PROC_FILE,0666,NULL,&gsl_seq_fops);
    gsl_proc_flag = 0;
#endif

    gpio_set_value(ts->irq_pin, 0);
    enable_irq(ts->irq);

    ts->tp.tp_resume = gsl_ts_late_resume;
    ts->tp.tp_suspend = gsl_ts_early_suspend;
    tp_register_fb(&ts->tp);

    return 0;

    //exit_set_irq_mode:
error_req_irq_fail:
    free_irq(ts->irq, ts);

error_mutex_destroy:
    input_free_device(ts->input);
    kfree(ts);
    return rc;
}

static int gsl_ts_remove(struct i2c_client *client)
{
    struct gsl_ts *ts = i2c_get_clientdata(client);

    device_init_wakeup(&client->dev, 0);
    cancel_work_sync(&ts->work);
    free_irq(ts->irq, ts);
    destroy_workqueue(ts->wq);
    input_unregister_device(ts->input);

    kfree(ts->touch_data);
    kfree(ts);

    return 0;
}

static struct of_device_id gsl_ts_ids[] = {
    {.compatible = "gslX680-pad"},
    {}
};

static const struct i2c_device_id gsl_ts_id[] = {
    {GSLX680_I2C_NAME, 0},
    {}
};
MODULE_DEVICE_TABLE(i2c, gsl_ts_id);

#if !defined(CONFIG_DRM_FBDEV_EMULATION) && defined(CONFIG_PM)
static const struct dev_pm_ops gsl_pm_ops = {
     .suspend    = gsl_ts_suspend,
     .resume     = gsl_ts_resume,
};
#endif

static struct i2c_driver gsl_ts_driver = {
    .driver = {
        .name = GSLX680_I2C_NAME,
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(gsl_ts_ids),
#if !defined(CONFIG_DRM_FBDEV_EMULATION) && defined(CONFIG_PM)
        .pm = &gsl_pm_ops,
#endif
    },
    .probe		= gsl_ts_probe,
    .remove		= gsl_ts_remove,
    .id_table	= gsl_ts_id,
};

static int __init gsl_ts_init(void)
{
    int ret;
    ret = i2c_add_driver(&gsl_ts_driver);
    return ret;
}
static void __exit gsl_ts_exit(void)
{
    i2c_del_driver(&gsl_ts_driver);
    return;
}

module_init(gsl_ts_init);
module_exit(gsl_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GSLX680 touchscreen controller driver");
MODULE_AUTHOR("Guan Yuwei, guanyuwei@basewin.com");
MODULE_ALIAS("platform:gsl_ts");
