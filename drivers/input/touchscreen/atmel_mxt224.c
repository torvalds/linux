/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************************
 * driver/input/touchscreen/atmel_mxt224.c
 *Copyright 	:ROCKCHIP  Inc
 *Author	: 	dqz
 *Date		:  2011.2.28
 *
 *description£º
 ********************************************************************************************/
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <mach/gpio.h>
#include <linux/irq.h>
#include <mach/board.h>

#define FEATURE_CFG_DUMP

static int mxt224_i2c_write(struct i2c_client *client, u16 offset,void *buf,int size);
static int mxt224_i2c_read(struct i2c_client *client, u16 offset,void *buf,int size);
static int mXT224_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int mXT224_remove(struct i2c_client *client);

#ifdef FEATURE_CFG_DUMP
static int total_size = 0;
static u8 *cfg_dmup;
#endif
#define VERSION_20

#define local_debug //printk

#define ID_INFORMATION_SIZE        0x7
#define OBJECT_TABLE_ELEMENT_SIZE  0x6
#define CRC_SIZE                   0x3
#define TABLE_SIZE_ADDR            ID_INFORMATION_SIZE-1
#define MESSAGE_T5                 0x5

#define MXT224_REPORTID_T9_OFFSET 9
#define MXT224_MT_SCAN_POINTS 2

struct mxt224_id_info{
    u8   family_id;
    u8   variant_id;
    u8   version;
    u8   build;
    u8   matrix_x_size;
    u8   matrix_y_size;
    u8   object_num;
}__packed;

struct mxt224_table_info{
    u8   obj_type;
    u8   start_addr_lsb;
    u8   start_addr_msb;
    u8   size;
    u8   instance;
    u8   report_id;
}__packed;

#define CFGERR_MASK (0x01<<3)

union msg_body{
    u8 msg[7];
    struct{
        u8  status;
        u32 checksum:24;
    }t6;
    struct{
        u8  status;
        u8  x_msb;
        u8  y_msb;
        u8  xy_poslisb;
        u8  area;
        u8  tchamplitude;
        u8  tchvector;
    }t9;
}__packed;

 
struct message_t5{
    u8 report_id;
    union msg_body body;
#ifndef VERSION_20	
    u8 checksum;
#endif
}__packed;

struct mxt224_obj{
    struct mxt224_id_info     id_info;
    u8                       *table_info_byte;
    struct mxt224_table_info *table_info;
    u8                        table_size;
    u16                       msg_t5_addr;
    u32                       info_crc;
};

struct mXT224_info {
    int int_gpio;
    int reset_gpio;
    int cfg_delay;
	int last_key_index;
    u16 last_read_addr;

	char phys[32];
    //struct hrtimer timer;
    struct i2c_client *client;
    struct input_dev *input_dev;
	struct delayed_work	work;
	struct workqueue_struct *mxt224_wq;

    int (*power)(int on);
    /* Object table relation */
    struct mxt224_obj obj;
};

static struct mXT224_info ts_data = {
    .int_gpio = RK29_PIN0_PA2,
    .reset_gpio = RK29_PIN6_PC3,
    .cfg_delay = 0,
    .last_key_index = 0,
    .last_read_addr = 0,
};

struct report_id_table{
    u8 report_start;
    u8 report_end;
    u8 obj_type;
};

static struct report_id_table* id_table = NULL;
static u8 T9_cfg[31] = {0};
struct mxt224_cfg{
    u8 type;
    const u8* data;
    int size;
};


struct mxt224_key_info{
    u32  start;
    u32  end;
    u32  code;
};
const struct mxt224_key_info key_info[] = {
	{0, 0, KEY_BACK},
    {0, 0, KEY_MENU},
    {0, 0, KEY_HOME},
    {0, 0, KEY_SEARCH},
};

#if 1
const u8 T7[] =  {0xff, 0xff, 0x32};
const u8 T8[] =  {0x08, 0x05, 0x14, 0x14, 0x00, 0x00, 0x0a, 0x0f};
const u8 T9[] =  {0x83, 0x00, 0x00, 0x0D, 0x0A, 0x00, 0x11, 0x28,
                  0x02, 0x01, 0x00, 0x01, 0x01, 0x00, 0x0A, 0x0A,
                  0x0A, 0x0A, 0x01, 0x5A, 0x00, 0xEF, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  ///////////////////////////////////////
                  //0x83, 0x0D, 0x0A, 0x03, 0x03, 0x00, 0x11, 0x28,
                  //0x02, 0x03, 0x00, 0x01, 0x01, 0x00, 0x0A, 0x0A,
                  //0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  //0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
                  };
const u8 T15[] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x41, 0x1E,
                  0x02, 0x00, 0x00 };
const u8 T18[] = {0x00, 0x00 };
const u8 T19[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const u8 T20[] = {0x00, 0x64, 0x64, 0x64, 0x64, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00 };
const u8 T22[] = {0x15, 0x00, 0x00, 0x00, 0x19, 0xFF, 0xE7, 0x04,
                  0x32, 0x00, 0x01, 0x0A, 0x0F, 0x14, 0x19, 0x1E,
                  0x04,
                  /////////////////////////////////////
                  //0x15, 0x00, 0x00, 0x00, 0x19, 0xff, 0xe7, 0x04,
                  //0x32, 0x00, 0x01, 0x0a, 0x0f, 0x14, 0x19, 0x1e,
                  //0x04,
                  };
const u8 T23[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00 };
const u8 T24[] = {0x03, 0x04, 0x03, 0xFF, 0x00, 0x64, 0x64, 0x01,
                  0x0A, 0x14, 0x28, 0x00, 0x4B, 0x00, 0x02, 0x00,
                  0x64, 0x00, 0x19 };
const u8 T25[] = {0x00, 0x00, 0x2E, 0xE0, 0x1B, 0x58, 0x36, 0xB0,
                  0x01, 0xF4, 0x00, 0x00, 0x00, 0x00 };
const u8 T27[] = {0x03, 0x02, 0x00, 0xE0, 0x03, 0x00, 0x23};
const u8 T28[] = {0x00, 0x00, 0x00, 0x04, 0x08, 0xF6 };
const u8 T38[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const struct mxt224_cfg cfg_table[] = {
    {7,  T7,  sizeof(T7)},
    {8,  T8,  sizeof(T8)},
    {9,  T9,  sizeof(T9)},
    {15, T15, sizeof(T15)},
    {18, T18, sizeof(T18)},
    {19, T19, sizeof(T19)},
    {20, T20, sizeof(T20)},
    {22, T22, sizeof(T22)},
    {23, T23, sizeof(T23)},
    {24, T24, sizeof(T24)},
    {25, T25, sizeof(T25)},
    {27, T27, sizeof(T27)},
    {28, T28, sizeof(T28)},
    {38, T38, sizeof(T38)}
};
#endif


enum mXT224_type{
    MSG_T9_MT_1 = 1,
    MSG_T9_MT_2,
    MSG_T9_MT_3,
    MSG_T9_MT_4,
    MSG_T9_MT_5,
    MSG_T9_MT_6,
    MSG_T9_MT_7,
    MSG_T9_MT_8,
    MSG_T9_MT_9,
    MSG_T9_KEY_PRESS,

    MSG_T6,
};

enum mXT224_touch_status{
    STATUS_RELEASE = 0,
    STATUS_PRESS,
};


u32 static crc24(u32 crc, u8 firstbyte, u8 secondbyte)
{
    const u32 crcpoly = 0x80001b;
    u32 result;
    u16 data_word;

    data_word = (u16)((u16)(secondbyte<<8u)|firstbyte);
    result = ((crc<<1u)^(u32)data_word);
    if(result & 0x1000000){
        result ^= crcpoly;
    }
    return result;
}

u32 static get_crc24(const u8* src, int cnt)
{
    int index = 0;
    u32 crc = 0;

    while(index < (cnt-1)){
        crc = crc24(crc, *(src+index), *(src+index+1));
        index += 2;
    }
    //1 TODO:
    if(index != cnt){
        crc = crc24(crc, *(src+index), 0);
    }
    crc = (crc & 0x00ffffff);
    return crc;
}

static u32 mXT224_cfg_crc(void)
{
    int index;
    int sub_index = 0;
    u32 crc = 0;

    /* Remove T38 */
    for(index=0; index<((sizeof(cfg_table)/sizeof(cfg_table[0]))-1); index++){
        const u8* data = cfg_table[index].data;
        while(sub_index <(cfg_table[index].size-1)){
            crc = crc24(crc, *(data+sub_index), *(data+sub_index+1));
            sub_index += 2;
        }
        if(sub_index != cfg_table[index].size){
            if(index == ((sizeof(cfg_table)/sizeof(cfg_table[0]))-1)){
                crc = crc24(crc, *(data+sub_index), 0);
            }else{
                const u8* next_data = cfg_table[index+1].data;
                crc = crc24(crc, *(data+sub_index), *(next_data));
                crc = (crc & 0x00ffffff);
                sub_index = 1;
                continue;
            }            
        }
        sub_index = 0;
        crc = (crc & 0x00ffffff);
    }
    //1 TODO:
    //crc = crc24(crc, 0, 0);
    crc = (crc & 0x00ffffff);
    return crc;
}

static void mxt224_mem_dbg(const void *src, int cnt)
{
#if 0
    int index;
    const u8* disp = (u8*)src;
    local_debug(KERN_INFO "%s: start...\n", __func__);
    for(index=0; index < cnt; index++){
	local_debug(KERN_INFO "0x%2x  ", disp[index]);
    }
    local_debug(KERN_INFO "\n%s: ...end\n", __func__);
#endif
}



static irqreturn_t mXT224_ts_interrupt(int irq, void *handle)
{
	struct mXT224_info *ts = handle;
	local_debug(KERN_INFO "%s\n", __func__);

	disable_irq_nosync(ts->int_gpio);
	queue_delayed_work(ts->mxt224_wq, &ts->work, 0);

	return IRQ_HANDLED;
}


static u16 mXT224_get_obj_addr(u8 type, struct mxt224_table_info *info, u8 info_size)
{
    int index;
    u16 addr = 0;

    for(index=0; index < info_size; index++){
		if(type == info[index].obj_type){
            addr = info[index].start_addr_msb;
            addr <<= 8;
            addr |= info[index].start_addr_lsb;
            return addr;
        }
    }
    return addr;
}


static struct report_id_table* mXT224_build_report_id_table(
        struct mxt224_table_info *info, u8 info_size)
{
    int index;
    int write_index;
    u8  offset = 0;
    id_table = (struct report_id_table*)kzalloc(info_size*sizeof(struct report_id_table), GFP_KERNEL);
    
    if(!id_table){
        local_debug(KERN_INFO "%s: Can't get memory!\n", __func__);
        return NULL;
    }
    
    write_index = 0;
    
    for(index = 0; index < info_size; index++){     
#ifdef FEATURE_CFG_DUMP
        total_size += ((info[index].size+1)*(info[index].instance+1));
#endif
        if(info[index].obj_type == 0x5)
            continue;
        if(info[index].report_id == 0x00)
            continue;
        
        id_table[write_index].obj_type = info[index].obj_type;
        id_table[write_index].report_start = (offset+1);
        id_table[write_index].report_end = id_table[write_index].report_start+
                                                info[index].report_id*(info[index].instance+1)-1;
        
        offset = id_table[write_index].report_end;
        write_index++;
    }
    
#ifdef FEATURE_CFG_DUMP
    for(index = 0; index < info_size; index++){
		local_debug(KERN_INFO "%s: Object type:%d, size:[%d]\n", __func__, 
                            info[index].obj_type, info[index].size+1);
    }
#endif
    
    return id_table;
}


static u8 mXT224_get_obj_type(u8 id, struct report_id_table* table, u8 table_size)
{
    int index;

    for(index=0; index < table_size; index++){    
		local_debug(KERN_INFO "%s: ID:%d, start:[%d], end:[%d], type:[%d]\n", __func__, 
                         id, table[index].report_start, table[index].report_end, 
		table[index].obj_type);
        if(id>=table[index].report_start && id<=table[index].report_end){
            break;
        }
    }
    
    switch(table[index].obj_type){
        case 6:
            return MSG_T6;
            
        case 9:
        {
            int t9_offset = id-table[index].report_start;
            if(t9_offset < MXT224_REPORTID_T9_OFFSET){
                return MSG_T9_MT_1+t9_offset;
            }else{
                return 0;
            }
        }   
        case 15:
		{
			return MSG_T9_KEY_PRESS;
		}
        default:
            return 0;
    }
}


#define TS_POLL_PERIOD 10000*1000

 

static void mXT224_load_cfg(void)
{
    int index;
    u8  buf[6] = {0};
    u16 addr;
    int rc;
    
    local_debug(KERN_INFO "%s\n", __func__);

    if(ts_data.cfg_delay){
        return;
    }
    ts_data.cfg_delay = 1;
    
   // hrtimer_start(&ts_data.timer, ktime_set(0, TS_POLL_PERIOD), HRTIMER_MODE_REL);
    
    for(index=0; index<(sizeof(cfg_table)/sizeof(cfg_table[0])); index++){

        const u8* data = cfg_table[index].data;
        u16 addr = mXT224_get_obj_addr(cfg_table[index].type, ts_data.obj.table_info,
                                                              ts_data.obj.table_size);
        rc = mxt224_i2c_write(ts_data.client, addr, data, cfg_table[index].size);
        if(rc){
            local_debug(KERN_INFO "%s: Load mXT224 config failed, addr: 0x%x!\n", __func__, addr);
        }
    }
    
    addr = mXT224_get_obj_addr(6, ts_data.obj.table_info, ts_data.obj.table_size);
    
    //buf[0] = 0x05;
    buf[1] = 0x55;
    
    rc = mxt224_i2c_write(ts_data.client, addr, buf, 6);
    
    if(rc){
        local_debug(KERN_INFO "%s: Back up NV failed!\n", __func__);
    }
    
    /* Reset mXT224 */
    msleep(5);
    
    gpio_set_value(ts_data.reset_gpio, 0);
    msleep(1);
    
    gpio_set_value(ts_data.reset_gpio, 1);
    msleep(50);
    
}



#define DETECT_MASK     (0x01<<7)
#define RELEASE_MASK    (0x01<<5)
#define MOVE_MASK       (0x01<<4)


static u32 cfg_crc;


static int mXT224_process_msg(u8 id, u8 *msg)
{

    switch(id){
        case MSG_T6:
        {
			local_debug(KERN_INFO "%s: Process mXT224 msg MSG_T6!\n", __func__);
            u32 checksum = ((union msg_body*)msg)->t6.checksum;
            
            u8  status = ((union msg_body*)msg)->t6.status;
            
            if(status & CFGERR_MASK){
                local_debug(KERN_INFO "%s: Process mXT224 cfg error!\n", __func__);
               // mXT224_load_cfg();
            }
            /*
            if(checksum!=cfg_crc){
                local_debug(KERN_INFO "%s: Process mXT224 cfg CRC error!\n", __func__);
                local_debug(KERN_INFO "%s: Read CRC:[0x%x], Our CRC:[0x%x]\n", __func__, checksum, cfg_crc);
                mXT224_load_cfg();
            }
            */
            break;
        }
        
        case MSG_T9_MT_1:
          
        case MSG_T9_MT_2:
          
        case MSG_T9_MT_3:
		case MSG_T9_MT_4:
		case MSG_T9_MT_5:
        {
			local_debug(KERN_INFO "%s: Process mXT224 msg MSG_T9_MT!\n", __func__);
            u32 x, y;
	    int tcStatus = 0;
            x = ((union msg_body*)msg)->t9.x_msb;
            x <<= 4;
            x |= (((union msg_body*)msg)->t9.xy_poslisb>>4);

            y = ((union msg_body*)msg)->t9.y_msb;
            y <<= 4;
            ((union msg_body*)msg)->t9.xy_poslisb &= 0x0f;
            
            y |= ((union msg_body*)msg)->t9.xy_poslisb;
            
            local_debug(KERN_INFO "%s: X[%d], Y[%d]\n", __func__, x, y);

			if(((union msg_body*)msg)->t9.status & DETECT_MASK)
			{
				tcStatus = STATUS_PRESS;
			}else if(((union msg_body*)msg)->t9.status & RELEASE_MASK)
			{
				tcStatus = STATUS_RELEASE;
			}
			
            input_report_abs(ts_data.input_dev, ABS_MT_TRACKING_ID, id - 1);							
            input_report_abs(ts_data.input_dev, ABS_MT_TOUCH_MAJOR, tcStatus);				
            input_report_abs(ts_data.input_dev, ABS_MT_WIDTH_MAJOR, 0);	
            input_report_abs(ts_data.input_dev, ABS_MT_POSITION_X, x);				
            input_report_abs(ts_data.input_dev, ABS_MT_POSITION_Y, y);				
            input_mt_sync(ts_data.input_dev);   
			local_debug(KERN_INFO "%s,input_report_abs x is %d,y is %d. status is [%d].\n", __func__, x, y, tcStatus);
            break;
        }       
        case MSG_T9_KEY_PRESS:
        {
            int keyStatus, keyIndex;
			
            keyStatus = ((union msg_body*)msg)->t9.status >> 7;
			if(keyStatus) //press.
            {
                keyIndex = ((union msg_body*)msg)->t9.x_msb;
				ts_data.last_key_index = keyIndex;
			}else{							
			    keyIndex = ts_data.last_key_index;
			}
		    switch(keyIndex){
		        case 1:
		            {
						keyIndex = 0;
						break;
		            }
				case 2:
					{
						keyIndex = 1;
						break;
				    }
				case 4:
					{
						keyIndex = 2;
						break;
					}
				case 8:
					{
						keyIndex = 3;
						break;
					}
		    default:
                local_debug(KERN_INFO "%s: Default keyIndex [0x%x]\n", __func__, keyIndex);        
            break;
		    }

            local_debug(KERN_INFO "%s: Touch KEY code is [%d], keyStatus is [%d]\n", __func__, key_info[keyIndex].code, keyStatus);
            
            input_report_key(ts_data.input_dev, key_info[keyIndex].code, keyStatus);
            
            break;
        }


        default:
            local_debug(KERN_INFO "%s: Default id[0x%x]\n", __func__, id);
            
            break;
    }
    
    return 0;
}

 

static void mXT224_work_func(struct work_struct *work)
{
    u8 track[MXT224_MT_SCAN_POINTS];
    int index, ret, read_points;
    struct message_t5 msg_t5_array[MXT224_MT_SCAN_POINTS];
	
    local_debug(KERN_INFO "%s\n", __func__);
    
    
    ret = mxt224_i2c_read(ts_data.client, ts_data.obj.msg_t5_addr, 
                        (u8*)&msg_t5_array[0], sizeof(struct message_t5) * MXT224_MT_SCAN_POINTS);
	read_points = ret / sizeof(struct message_t5);

	local_debug(KERN_INFO "%s, this time read_points is %d\n", __func__, read_points);
    mxt224_mem_dbg((u8*)&msg_t5_array[0], sizeof(struct message_t5) * read_points);

	for(index = 0; index < read_points; index++)
	{
		if(msg_t5_array[index].report_id == 0xFF) // dirty message, don't process.
		{
			track[index] = 0xFF;
			continue;
		}
		track[index] = mXT224_get_obj_type(msg_t5_array[index].report_id, id_table, ts_data.obj.table_size);

		if(track[index] == 0){
			local_debug(KERN_INFO "%s: Get object type failed!, report id[0x%x]\n", __func__, msg_t5_array[index].report_id);
			goto end;
		}
		
		local_debug(KERN_INFO "%s,object's msg type is %d.\n", __func__, track[index]);
	}

	for(index = 0; index < read_points; index++)
	{
		if(track[index] == 0xFF)
			continue;
	    mXT224_process_msg(track[index], (u8*)&msg_t5_array[index].body);
		if(track[index] == track[read_points - 1] || track[read_points - 1] == 0xFF)
		{
		    input_sync(ts_data.input_dev);
			local_debug(KERN_INFO "%s,input_sync ts_data.input_dev.\n", __func__);
		}
	}
	
end:
	enable_irq(ts_data.int_gpio);
}


static int mxt224_i2c_write(struct i2c_client *client, u16 offset,void *buf,int size)
{
	unsigned char objectAddr[2+size];
	int retlen;

	objectAddr[0] = offset & 0x00FF;
	objectAddr[1] = offset >> 8;
	memcpy(&objectAddr[2], (char *)buf, size);
	retlen = i2c_master_normal_send(client, objectAddr,2 + size, 200*1000);
        
    return retlen;
}

static int mxt224_i2c_read(struct i2c_client *client, u16 offset,void *buf,int size)
{
	unsigned char objectAddr[2];
	int retlen;

	if(ts_data.last_read_addr != offset)
	{
	ts_data.last_read_addr = offset;
	objectAddr[0] = offset & 0x00FF;
	objectAddr[1] = offset >> 8;
	retlen = i2c_master_normal_send(client, objectAddr,2, 200*1000);
	if(retlen <= 0)
		return retlen;
	}
	retlen = i2c_master_normal_recv(client, (char *)buf, size, 200*1000);
	
	return retlen;
}

static int mXT224_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int     rc;
    int     index;
    int     info_size;
    u32     crc;
    u16     object_addr;
	
	struct message_t5 msg_t5;

	 if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		 return -EIO;
	 /* Try get GPIO */
	 rc = gpio_request(ts_data.int_gpio, "Touch_int");
	 
	 if(rc)
	 {
		 local_debug(KERN_INFO "%s: Request GPIO failed!\n", __func__);
		 goto failed;
	 }

	 rc = gpio_request(ts_data.reset_gpio, "Touch_reset");
	 
	 if(rc)
	 {
		 local_debug(KERN_INFO "%s: Request mXT224 reset GPIO failed!\n", __func__);
		 goto failed;
	 }

     /* store the value */
    i2c_set_clientdata(client, &ts_data);
    ts_data.client = client;
	ts_data.int_gpio= client->irq;
    //client->driver = &mXT224_driver;  

	ts_data.mxt224_wq = create_rt_workqueue("mxt224_wq");
	INIT_DELAYED_WORK(&ts_data.work, mXT224_work_func);	
	
    /* Reset mXT224 */
	gpio_pull_updown(ts_data.int_gpio, 1);
	
	gpio_direction_output(ts_data.reset_gpio, 0);
    gpio_set_value(ts_data.reset_gpio, GPIO_LOW);
    msleep(10);
    gpio_set_value(ts_data.reset_gpio, GPIO_HIGH);
    msleep(500);

    /* Try get mXT224 table size */
    
     rc = mxt224_i2c_read(client, TABLE_SIZE_ADDR, &ts_data.obj.table_size, 1);
    
    if(rc <= 0)
    {
        local_debug(KERN_INFO "%s: Get table size failed!\n", __func__);
        goto failed;
    }


    
    /* Try get mXT224 device info */
    info_size = CRC_SIZE+ID_INFORMATION_SIZE+ts_data.obj.table_size*OBJECT_TABLE_ELEMENT_SIZE;
    
    ts_data.obj.table_info_byte = (u8*)kzalloc(info_size, GFP_KERNEL);
    
    if(!ts_data.obj.table_info_byte)
    {
        local_debug(KERN_INFO "%s: Can't get memory!\n", __func__);
        rc = -1;
        goto failed;
    }
    
    rc = mxt224_i2c_read(client, 0, ts_data.obj.table_info_byte, info_size);
    
    if(rc <= 0)
    {
        local_debug(KERN_INFO "%s: Get mXT224 info failed!\n", __func__);
        goto get_info_failed;
    }
    
    ts_data.obj.table_info = (struct mxt224_table_info*)(ts_data.obj.table_info_byte+ID_INFORMATION_SIZE);
    mxt224_mem_dbg(ts_data.obj.table_info_byte, info_size);

 

#if 0
    /* Try get and check CRC */
    ts_data.obj.info_crc = (ts_data.obj.table_info_byte[info_size-3])|
                           (ts_data.obj.table_info_byte[info_size-2]<<8)|
                           (ts_data.obj.table_info_byte[info_size-1]<<16);
    crc = get_crc24(ts_data.obj.table_info_byte, info_size-CRC_SIZE);
    
    if(ts_data.obj.info_crc != crc)
    {
        //1 TODO: Need set config table
        
        local_debug(KERN_INFO "%s:CRC failed, read CRC:[0x%x], get CRC:[0x%x]\n", __func__, ts_data.obj.info_crc, crc);
        
        mXT224_load_cfg();
    }


    /* Build cfg CRC */
    cfg_crc = mXT224_cfg_crc();
#endif 

 

    /* Build report id table */
    mXT224_build_report_id_table(ts_data.obj.table_info, ts_data.obj.table_size);

 

    /* Dump mXT224 config setting */
#ifdef FEATURE_CFG_DUMP
    
    local_debug(KERN_INFO "%s: Config size: %d\n", __func__, total_size);
    
    cfg_dmup = (u8*)kzalloc(info_size+total_size, GFP_KERNEL);
    
    if(!cfg_dmup)
    {
        local_debug(KERN_INFO "%s: Cannot get memory!\n", __func__);
        goto failed;
    }
    
    mxt224_i2c_read(client, 0, cfg_dmup, info_size+total_size);
    mxt224_mem_dbg(cfg_dmup, info_size+total_size);
#endif


	/* Try get mXT224 ID info */
	 rc = mxt224_i2c_read(client, 0, &ts_data.obj.id_info, ID_INFORMATION_SIZE);
	
	 local_debug(KERN_INFO "%s: ID version is 0x%x.\n", __func__, ts_data.obj.id_info.version);


    /* Try get message T5 info */
    if(gpio_get_value(ts_data.int_gpio))
    {
        //1 TODO: Need check touch interrput pin
        
        local_debug(KERN_INFO "%s: GPIO status error!\n", __func__);
        
        rc = -1;
        goto failed;
    }

    ts_data.obj.msg_t5_addr = mXT224_get_obj_addr(0x5, ts_data.obj.table_info, ts_data.obj.table_size);
    
    rc = mxt224_i2c_read(client, ts_data.obj.msg_t5_addr, (u8*)&msg_t5, sizeof(struct message_t5));
    
    if(rc <= 0)
    {
        local_debug(KERN_INFO "%s:Can't get message T5!\n", __func__);
        goto failed;
    }
    
    mxt224_mem_dbg((u8*)&msg_t5, sizeof(struct message_t5));
    mXT224_process_msg(mXT224_get_obj_type(msg_t5.report_id, id_table, ts_data.obj.table_size), (u8*)&msg_t5.body);

    object_addr = mXT224_get_obj_addr(0x9, ts_data.obj.table_info, ts_data.obj.table_size);
    
    rc = mxt224_i2c_read(client, object_addr, (u8*)&T9_cfg[0], 31);
    
    if(rc <= 0)
    {
        local_debug(KERN_INFO "%s:Can't get message T9!\n", __func__);
        goto failed;
    }
    
    mxt224_mem_dbg((u8*)&T9_cfg[0], 31);

    local_debug(KERN_INFO "%s:Change T9 orient to [0]!\n", __func__);
	
	T9_cfg[9] = 0;
	rc = mxt224_i2c_write(client, object_addr, (u8*)&T9_cfg[0], 31);
    if(rc <= 0)
    {
        local_debug(KERN_INFO "%s:Can't write message T9!\n", __func__);
        goto failed;
    }

    rc = mxt224_i2c_read(client, object_addr, (u8*)&T9_cfg[0], 31);
    
    if(rc <= 0)
    {
        local_debug(KERN_INFO "%s:Can't get message T9!\n", __func__);
        goto failed;
    }
	mxt224_mem_dbg((u8*)&T9_cfg[0], 31);
	
    //local_debug(KERN_INFO "%s:Find obj report: 0x%x\n", __func__, 
    //            mXT224_get_obj_type(msg_t5.report_id, id_table, obj_table_size));
    msleep(15);
    
    if(gpio_get_value(ts_data.int_gpio))
    {
        local_debug(KERN_INFO "%s: GPIO value is high\n", __func__);
    }
   

    ts_data.input_dev = input_allocate_device();
    if (!ts_data.input_dev)
    {
        rc = -ENOMEM;
        goto failed;
    }
    
    input_set_drvdata(ts_data.input_dev, &ts_data);

	snprintf(ts_data.phys, sizeof(ts_data.phys),
		 "%s/input0", dev_name(&client->dev));

    ts_data.input_dev->name = "mXT224_touch";
    ts_data.input_dev->id.bustype = BUS_I2C;
	ts_data.input_dev->phys = ts_data.phys;

#if 0    
    set_bit(EV_SYN, ts_data.input_dev->evbit);
    set_bit(EV_KEY, ts_data.input_dev->evbit);
    set_bit(BTN_TOUCH, ts_data.input_dev->keybit);
    set_bit(BTN_2, ts_data.input_dev->keybit);
    set_bit(EV_ABS, ts_data.input_dev->evbit);
    
    input_set_abs_params(ts_data.input_dev, ABS_X, 0, 240, 0, 0);
    input_set_abs_params(ts_data.input_dev, ABS_Y, 0, 320, 0, 0);
    
    for(index=0; index<(sizeof(key_info)/sizeof(key_info[0])); index++)
     {
        input_set_capability(ts_data.input_dev, EV_KEY, key_info[index].code);
    }
#else
	ts_data.input_dev->evbit[0] = BIT_MASK(EV_ABS)|BIT_MASK(EV_KEY)|BIT_MASK(EV_SYN);
	ts_data.input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	//ts_data.input_dev->keybit[BIT_WORD(BTN_2)] = BIT_MASK(BTN_2); 

    for(index=0; index<(sizeof(key_info)/sizeof(key_info[0])); index++)
     {
        input_set_capability(ts_data.input_dev, EV_KEY, key_info[index].code);
    }
	input_set_abs_params(ts_data.input_dev, ABS_X, 0, CONFIG_MXT224_MAX_X, 0, 0);
	input_set_abs_params(ts_data.input_dev, ABS_Y, 0, CONFIG_MXT224_MAX_Y, 0, 0);
	input_set_abs_params(ts_data.input_dev, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts_data.input_dev, ABS_TOOL_WIDTH, 0, 15, 0, 0);
	input_set_abs_params(ts_data.input_dev, ABS_HAT0X, 0, CONFIG_MXT224_MAX_X, 0, 0);
	input_set_abs_params(ts_data.input_dev, ABS_HAT0Y, 0, CONFIG_MXT224_MAX_Y, 0, 0);
	input_set_abs_params(ts_data.input_dev, ABS_MT_POSITION_X,0, CONFIG_MXT224_MAX_X, 0, 0);
	input_set_abs_params(ts_data.input_dev, ABS_MT_POSITION_Y, 0, CONFIG_MXT224_MAX_Y, 0, 0);
	input_set_abs_params(ts_data.input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts_data.input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts_data.input_dev, ABS_MT_TRACKING_ID, 0, 10, 0, 0);  

#endif
    rc = input_register_device(ts_data.input_dev);
    
    if (rc) 
    {
        dev_err(&client->dev, "mXT224: input_register_device rc=%d\n", rc);
        goto failed;
    }
	
	ts_data.int_gpio = gpio_to_irq(ts_data.int_gpio);

    rc = request_irq(ts_data.int_gpio, mXT224_ts_interrupt,
                     IRQF_TRIGGER_FALLING, "mXT224_touch", &ts_data);
	if(rc)
	{
		local_debug(KERN_INFO "mXT224 request interrput failed!\n");
	}else{
		local_debug(KERN_INFO "mXT224 request interrput successed!\n");
	}

    
    return 0;

get_info_failed:
    /* Free mXT224 info */
failed:

	if(ts_data.input_dev != NULL)
	input_free_device(ts_data.input_dev);

#ifdef FEATURE_CFG_DUMP
	kfree(cfg_dmup);
#endif
	kfree(ts_data.obj.table_info_byte);

	if(id_table != NULL)
	{
		kfree(id_table);
		id_table = NULL;
	}

    return rc;
}


static int __devexit mXT224_remove(struct i2c_client *client)
{
	struct mXT224_info *ts = i2c_get_clientdata(client);
	
	free_irq(ts->int_gpio, ts);
	if (cancel_delayed_work_sync(&ts->work)) {
		/*
		 * Work was pending, therefore we need to enable
		 * IRQ here to balance the disable_irq() done in the
		 * interrupt handler.
		 */
		enable_irq(ts->int_gpio);
	}

	input_unregister_device(ts->input_dev);
	
	if(id_table != NULL)
	{
		kfree(id_table);
		id_table = NULL;
	}
	return 0;
}

static const struct i2c_device_id mXT224_ts_id[] = {
    { "mXT224_touch", 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, mXT224_ts_id);

static struct i2c_driver mXT224_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "mXT224_touch"
	},
	.id_table	= mXT224_ts_id,
	.probe		= mXT224_probe,
	.remove		= __devexit_p(mXT224_remove),
};

static void __init mXT_init_async(void *unused, async_cookie_t cookie)
{
	local_debug("--------> %s <-------------\n",__func__);
	i2c_add_driver(&mXT224_driver);
}


static int __init mXT_init(void)
{
	async_schedule(mXT_init_async, NULL);
	return 0;

}

static void __exit mXT_exit(void)
{
 i2c_del_driver(&mXT224_driver);
}


module_init(mXT_init);
module_exit(mXT_exit);

MODULE_LICENSE("GPL");
