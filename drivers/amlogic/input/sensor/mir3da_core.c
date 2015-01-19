/* Core file for MiraMEMS 3-Axis Accelerometer's driver. 
 *
 * mir3da_core.c - Linux kernel modules for 3-Axis Accelerometer
 *
 * Copyright (C) 2011-2013 MiraMEMS Sensing Technology Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "mir3da_core.h"
#include "mir3da_cust.h"

#define MIR3DA_REG_PAGE(REG)                (((REG)>>8)&0xFF)
#define MIR3DA_REG_ADDR(REG)                ((REG)&0xFF)

#define MIR3DA_OFFSET_THRESHOLD             10
#define PEAK_LVL                            800                        
#define STICK_LSB                           2000
#define AIX_HISTORY_SIZE                    3

enum {
    GSENSOR_MOD_SOCLE = 0,
    GSENSOR_MOD_NSA_NTO,
    GSENSOR_MOD_NSA_MLM,
};

typedef struct reg_obj_s {
    
    short               addr;
    unsigned char       mask;
    unsigned char       value;
    
} reg_obj_t;

struct gsensor_data_fmt_s {

    unsigned char       msbw;
    unsigned char       lsbw;
    unsigned char       endian;                         /* 0: little endian; 1: big endian */
};

struct gsensor_data_obj_s {

#define MIR3DA_DATA_LEN         6
    reg_obj_t                   data_sect[MIR3DA_DATA_LEN];
    struct gsensor_data_fmt_s   data_fmt;
};

struct gsensor_obj_s {
    
    char                        asic[10];

    reg_obj_t                   chip_id;
    reg_obj_t                   mod_id;
    reg_obj_t                   soft_reset;
    reg_obj_t                   power;

#define MIR3DA_INIT_SECT_LEN    11
#define MIR3DA_OFF_SECT_LEN     MIR3DA_OFFSET_LEN
#define MIR3DA_ODR_SECT_LEN   	3

    reg_obj_t                   init_sect[MIR3DA_INIT_SECT_LEN];
    reg_obj_t                   offset_sect[MIR3DA_OFF_SECT_LEN];
    reg_obj_t                   odr_sect[MIR3DA_ODR_SECT_LEN]; 	
    
    struct gsensor_data_obj_s   data;

    int                         (*calibrate)(MIR_HANDLE handle, int z_dir);
    int                         (*auto_calibrate)(MIR_HANDLE handle, int xyz[3]);
    int                         (*int_ops)(MIR_HANDLE handle, mir_int_ops_t *ops);
    int                         (*get_reg_data)(MIR_HANDLE handle, char *buf);
};

struct gsensor_drv_s {
    
    struct general_op_s         *method;

    struct gsensor_obj_s        *obj;
};

#define MIR3DA_SOCLE_INIT_SECTION                       { SOCLE_REG_TEMP_CFG_REG,       0xFF,   0x08    },                                  \
                                                        { SOCLE_REG_CTRL_REG5,          0xFF,   0x80    },                                  \
                                                        { SOCLE_REG_CTRL_REG4,          0x30,   0x00    },                                  \
                                                        { SOCLE_REG_CTRL_REG1,          0xFF,   0x6F    },                                  \
                                                        { SOCLE_REG_TEMP_CFG_REG,       0xFF,   0x88    },                                  \
                                                        { SOCLE_REG_LDO_REG,            0xFF,   0x02    },                                  \
                                                        { SOCLE_REG_OTP_TRIM_OSC,       0xFF,   0x27    },                                  \
                                                        { SOCLE_REG_LPF_ABSOLUTE,       0xFF,   0x30    },                                  \
                                                        { SOCLE_REG_TEMP_OFF1,          0xFF,   0x3f    },                                  \
                                                        { SOCLE_REG_TEMP_OFF2,          0xFF,   0xff    },                                  \
                                                        { SOCLE_REG_TEMP_OFF3,          0xFF,   0x0f    },                                  \


#define MIR3DA_SOCLE_OFFSET_SECTION                     { SOCLE_REG_OTP_XOFF_L,         0xFF,   0x00    },                                  \
                                                        { SOCLE_REG_OTP_XOFF_H,         0xFF,   0x00    },                                  \
                                                        { SOCLE_REG_OTP_YOFF_L,         0xFF,   0x00    },                                  \
                                                        { SOCLE_REG_OTP_YOFF_H,         0xFF,   0x00    },                                  \
                                                        { SOCLE_REG_OTP_ZOFF_L,         0xFF,   0x00    },                                  \
                                                        { SOCLE_REG_OTP_ZOFF_H,         0xFF,   0x00    },                                  \
                                                        { -1,                           0x00,   0x00    },                                  \
                                                        { -1,                           0x00,   0x00    },                                  \
                                                        { -1,                           0x00,   0x00    },                                  \

#define MIR3DA_SOCLE_ODR_SECTION                        { SOCLE_REG_CTRL_REG1,          0xF0,   0x40    },                                  \
                                                        { SOCLE_REG_CTRL_REG1,          0xF0,   0x50    },                                  \
                                                        { SOCLE_REG_CTRL_REG1,          0xF0,   0x60    },                                  \


#define MIR3DA_SOCLE_DATA_SECTION                     { { (SOCLE_REG_OUT_X_L|0x80),     0xFF,   0x00    },                                  \
                                                        { (SOCLE_REG_OUT_X_H|0x80),     0xFF,   0x00    },                                  \
                                                        { (SOCLE_REG_OUT_Y_L|0x80),     0xFF,   0x00    },                                  \
                                                        { (SOCLE_REG_OUT_Y_H|0x80),     0xFF,   0x00    },                                  \
                                                        { (SOCLE_REG_OUT_Z_L|0x80),     0xFF,   0x00    },                                  \
                                                        { (SOCLE_REG_OUT_Z_H|0x80),     0xFF,   0x00    } },                                \
                                                        { 8,                            4,      0       }


#define MIR3DA_NSA_INIT_SECTION                         { NSA_REG_G_RANGE,              0x03,   0x00    },                                  \
                                                        { NSA_REG_POWERMODE_BW,         0xFF,   0x1e    },                                  \
                                                        { NSA_REG_ODR_AXIS_DISABLE,     0xFF,   0x07    },                                  \
                                                        { NSA_REG_INTERRUPT_SETTINGS2,  0xFF,   0x00    },                                  \
                                                        { NSA_REG_INTERRUPT_MAPPING2,   0xFF,   0x00    },                                  \
                                                        { NSA_REG_ENGINEERING_MODE,     0xFF,   0x83    },                                  \
                                                        { NSA_REG_ENGINEERING_MODE,     0xFF,   0x69    },                                  \
                                                        { NSA_REG_ENGINEERING_MODE,     0xFF,   0xBD    },                                  \
                                                        { NSA_REG_INT_PIN_CONFIG,       0x0F,   0x05    },                                  \
                                                        { -1,                           0x00,   0x00    },                                  \
                                                        { -1,                           0x00,   0x00    },                                  \
                                                        
                                                                                                 
#define MIR3DA_NSA_OFFSET_SECTION                       { NSA_REG_COARSE_OFFSET_TRIM_X, 0xFF,   0x00    },                                  \
                                                        { NSA_REG_COARSE_OFFSET_TRIM_Y, 0xFF,   0x00    },                                  \
                                                        { NSA_REG_COARSE_OFFSET_TRIM_Z, 0xFF,   0x00    },                                  \
                                                        { NSA_REG_FINE_OFFSET_TRIM_X,   0xFF,   0x00    },                                  \
                                                        { NSA_REG_FINE_OFFSET_TRIM_Y,   0xFF,   0x00    },                                  \
                                                        { NSA_REG_FINE_OFFSET_TRIM_Z,   0xFF,   0x00    },                                  \
                                                        { NSA_REG_CUSTOM_OFFSET_X,      0xFF,   0x00    },                                  \
                                                        { NSA_REG_CUSTOM_OFFSET_Y,      0xFF,   0x00    },                                  \
                                                        { NSA_REG_CUSTOM_OFFSET_Z,      0xFF,   0x00    },                                  \

#define MIR3DA_NSA_ODR_SECTION                          { NSA_REG_ODR_AXIS_DISABLE,     0x0F,   0x06    },                                  \
                                                        { NSA_REG_ODR_AXIS_DISABLE,     0x0F,   0x07    },                                  \
                                                        { NSA_REG_ODR_AXIS_DISABLE,     0x0F,   0x08    },                                  \


#define MIR3DA_NSA_DATA_SECTION                       { { NSA_REG_ACC_X_LSB,            0xFF,   0x00    },                                  \
                                                        { NSA_REG_ACC_X_MSB,            0xFF,   0x00    },                                  \
                                                        { NSA_REG_ACC_Y_LSB,            0xFF,   0x00    },                                  \
                                                        { NSA_REG_ACC_Y_MSB,            0xFF,   0x00    },                                  \
                                                        { NSA_REG_ACC_Z_LSB,            0xFF,   0x00    },                                  \
                                                        { NSA_REG_ACC_Z_MSB,            0xFF,   0x00    } },                                \
                                                        { 8,                            4,      0       }
                                         
                                         
static int SOCLE_calibrate(MIR_HANDLE handle, int z_dir);
static int SOCLE_auto_calibrate(MIR_HANDLE handle, int xyz[3]);
static int NSA_MLM_calibrate(MIR_HANDLE handle, int z_dir);
static int NSA_NTO_calibrate(MIR_HANDLE handle, int z_dir);
static int NSA_MLM_auto_calibrate(MIR_HANDLE handle, int xyz[3]);
static int NSA_NTO_auto_calibrate(MIR_HANDLE handle, int xyz[3]);
#if MIR3DA_AUTO_CALIBRATE
static int mir3da_auto_calibrate(MIR_HANDLE handle, int x, int y, int z);
#endif /* !MIR3DA_AUTO_CALIBRATE */
static int SOCLE_interrupt_ops(MIR_HANDLE handle, mir_int_ops_t *ops);
static int NSA_interrupt_ops(MIR_HANDLE handle, mir_int_ops_t *ops);
static int SOCLE_get_reg_data(MIR_HANDLE handle, char *buf);
static int NSA_get_reg_data(MIR_HANDLE handle, char *buf);

#define MIR_SOCLE                       { "SOCLE",      { SOCLE_REG_WHO_AM_I,           0xFF,   0x13    },                                  \
                                                        { SOCLE_REG_WHO_AM_I,           0xFF,   0x13    },                                  \
                                                        { SOCLE_REG_SOFT_RESET,         0xFF,   0xAA    },                                  \
                                                        { SOCLE_REG_TEMP_CFG_REG,       0x20,   0x20    },                                  \
                                                        { MIR3DA_SOCLE_INIT_SECTION                     },                                  \
                                                        { MIR3DA_SOCLE_OFFSET_SECTION                   },                                  \
                                                        { MIR3DA_SOCLE_ODR_SECTION                      },                                  \
                                                        { MIR3DA_SOCLE_DATA_SECTION                     },                                  \
                                                          SOCLE_calibrate                                ,                                  \
                                                          SOCLE_auto_calibrate                           ,                                  \
                                                          SOCLE_interrupt_ops                            ,                                  \
                                                          SOCLE_get_reg_data                             ,                                  \
                                        }

#define MIR_NSA_NTO                     { "NSA_NTO",    { NSA_REG_WHO_AM_I,             0xFF,   0x13    },                                  \
                                                        { NSA_REG_FIFO_CTRL,            0xFF,   0x00    },                                  \
                                                        { NSA_REG_SPI_I2C,              0x24,   0x24    },                                  \
                                                        { NSA_REG_POWERMODE_BW,         0xC0,   0xC0    },                                  \
                                                        { MIR3DA_NSA_INIT_SECTION                       },                                  \
                                                        { MIR3DA_NSA_OFFSET_SECTION                     },                                  \
                                                        { MIR3DA_NSA_ODR_SECTION                   	    },                                  \
                                                        { MIR3DA_NSA_DATA_SECTION                       },                                  \
                                                          NSA_NTO_calibrate                              ,                                  \
                                                          NSA_NTO_auto_calibrate                         ,                                  \
                                                          NSA_interrupt_ops                              ,                                  \
                                                          NSA_get_reg_data                               ,                                  \
                                        }
                                                        
#define MIR_NSA_MLM                     { "NSA_MLM",    { NSA_REG_WHO_AM_I,             0xFF,   0x13    },                                  \
                                                        { NSA_REG_FIFO_CTRL,            0xFF,   0x10    },                                  \
                                                        { NSA_REG_SPI_I2C,              0x24,   0x24    },                                  \
                                                        { NSA_REG_POWERMODE_BW,         0xC0,   0xC0    },                                  \
                                                        { MIR3DA_NSA_INIT_SECTION                       },                                  \
                                                        { MIR3DA_NSA_OFFSET_SECTION                     },                                  \
                                                        { MIR3DA_NSA_ODR_SECTION                        },                                  \
                                                        { MIR3DA_NSA_DATA_SECTION                       },                                  \
                                                          NSA_MLM_calibrate                              ,                                  \
                                                          NSA_MLM_auto_calibrate                         ,                                  \
                                                          NSA_interrupt_ops                              ,                                  \
                                                          NSA_get_reg_data                               ,                                  \
                                        }


/**************************************************************** COMMON ***************************************************************************/
#define MIR3DA_GSENSOR_SCHEME           MIR3DA_SUPPORT_CHIP_LIST


/* this level can be modified while runtime through system attribute */
int                                 Log_level = DEBUG_ERR;//|DEBUG_ASSERT|DEBUG_MSG|DEBUG_FUNC|DEBUG_DATA;
static int                          gsensor_mod = -1;        /* Initial value */
static struct gsensor_obj_s         mir3da_gsensor[] = { MIR3DA_GSENSOR_SCHEME };
struct gsensor_drv_s                mir3da_gsensor_drv;

#define MI_DATA(format, ...)            if(DEBUG_DATA&Log_level){mir3da_gsensor_drv.method->myprintf(MI_TAG format "\n", ## __VA_ARGS__);}
#define MI_MSG(format, ...)             if(DEBUG_MSG&Log_level){mir3da_gsensor_drv.method->myprintf(MI_TAG format "\n", ## __VA_ARGS__);}
#define MI_ERR(format, ...)             if(DEBUG_ERR&Log_level){mir3da_gsensor_drv.method->myprintf(MI_TAG format "\n", ## __VA_ARGS__);}
#define MI_FUN                          if(DEBUG_FUNC&Log_level){mir3da_gsensor_drv.method->myprintf(MI_TAG "%s is called, line: %d\n", __FUNCTION__,__LINE__);}
#define MI_ASSERT(expr)                 \
	if (!(expr)) {\
		mir3da_gsensor_drv.method->myprintf("Assertion failed! %s,%d,%s,%s\n",\
			__FILE__, __LINE__, __func__, #expr);\
	}

#define abs(x) ({ long __x = (x); (__x < 0) ? -__x : __x; })

#if FILTER_AVERAGE_ENHANCE
typedef struct FilterAverageContextTag{
    int sample_l;
    int sample_h;
    int filter_param_l;
    int filter_param_h;
    int filter_threhold;

    int refN_l;
    int refN_h;
        
} FilterAverageContext;

typedef struct mir3da_core_ctx_s{
    struct mir3da_filter_param_s    filter_param;
    FilterAverageContext            tFac[3];   
} mir3da_core_ctx;

static mir3da_core_ctx       core_ctx;
#endif 

#if MIR3DA_SENS_TEMP_SOLUTION
static int bSensZoom = 0;
#endif

#if MIR3DA_OFFSET_TEMP_SOLUTION
static int is_cali = 0;
static char bLoad = 0;
static char readOffsetCnt=-1;
static unsigned char original_offset[9];
static int mir3da_write_offset_to_file(unsigned char* offset);
static int mir3da_read_offset_from_file(unsigned char* offset);
void manual_load_cali_file(MIR_HANDLE handle);
#endif /* !MIR3DA_OFFSET_TEMP_SOLUTION */

#if MIR3DA_STK_TEMP_SOLUTION
static short aixHistort[AIX_HISTORY_SIZE*3] = {0};
static short aixHistoryIndex = 0;
char bxstk = 0;
char bystk = 0;
char bzstk = 0;

static void addAixHistory(short x,short y,short z){
    aixHistort[aixHistoryIndex++] = x;
    aixHistort[aixHistoryIndex++] = y;
    aixHistort[aixHistoryIndex++] = z;    
    aixHistoryIndex = (aixHistoryIndex)%(AIX_HISTORY_SIZE*3);
}

static char isXStick(void){
    int i;
    for (i = 0; i < AIX_HISTORY_SIZE; i++){
        if (abs(aixHistort[i*3]) < STICK_LSB){
            break;
        }
    }
    
    return i == AIX_HISTORY_SIZE; 
}

static char isYStick(void){
    int i;
    for (i = 0; i < AIX_HISTORY_SIZE; i++){
        if (abs(aixHistort[i*3+1]) < STICK_LSB){
            break;
        }
    }
    
    return i == AIX_HISTORY_SIZE;
}

static char isZStick(void){
    int i;
    for (i = 0; i < AIX_HISTORY_SIZE; i++){
        if (abs(aixHistort[i*3+2]) < STICK_LSB){
            break;
        }
    }
    
    return i == AIX_HISTORY_SIZE; 
}

 int squareRoot(int val){
    int r = 0;
    int shift;
    
    if (val < 0){
        return 0;
    }
    
    for(shift=0;shift<32;shift+=2)
    { 
        int x=0x40000000l >> shift;
        if(x + r <= val)
        { 
            val -= x + r;
            r = (r >> 1) | x;
        } else{ 
            r = r >> 1;
        }
    }
    
    return r;
}
#endif /* ! MIR3DA_STK_TEMP_SOLUTION */

#if FILTER_AVERAGE_ENHANCE
#if 0
static short filter_average(short preAve, short sample, int paramN, int* refNum)
{
 #if FILTER_AVERAGE_EX
    if( abs(sample-preAve) > PEAK_LVL  && *refNum < 3  ){ 
         MI_DATA("Hit, sample = %d, preAve = %d, refN =%d\n", sample, preAve, *refNum);
         sample = preAve;
         (*refNum) ++;
    }else{
         if (*refNum == 3){
                preAve = sample;
         }
         
         *refNum  = 0;
    }
#endif

    return preAve + (sample - preAve)/paramN;
}
static int filter_average_enhance(FilterAverageContext* fac, short sample)
{
    if (fac == 0){
        MI_ERR("0 parameter fac");
        return 0;
    }

    if (fac->filter_param_l == fac->filter_param_h){
        fac->sample_l = fac->sample_h = filter_average(fac->sample_l, sample, fac->filter_param_l, &fac->refN_l);
    }else{
        fac->sample_l = filter_average(fac->sample_l, sample, fac->filter_param_l,  &fac->refN_l);
        fac->sample_h= filter_average(fac->sample_h, sample, fac->filter_param_h, &fac->refN_h);  
        if (abs(fac->sample_l- fac->sample_h) > fac->filter_threhold){
            MI_DATA("adjust, fac->sample_l = %d, fac->sample_h = %d\n", fac->sample_l, fac->sample_h); 
            fac->sample_h = fac->sample_l;            
        }
     }

    return fac->sample_h;    
}
#endif
#endif /* ! FILTER_AVERAGE_ENHANCE */

int mir3da_register_read(MIR_HANDLE handle, short addr, unsigned char *data)
{
    unsigned char      cur_page;
    int     res = 0;

    /* check page */
    if(MIR3DA_REG_PAGE(addr) > 0) {
        res = mir3da_gsensor_drv.method->smi.read(handle, 0x0, &cur_page);
        if(res != 0) {
            return res;
        }

        if(cur_page != MIR3DA_REG_PAGE(addr)) {
            res |= mir3da_gsensor_drv.method->smi.write(handle, 0x0, MIR3DA_REG_PAGE(addr));
            if(res != 0) {
                return res;
            }
        }
    }

    res = mir3da_gsensor_drv.method->smi.read(handle, MIR3DA_REG_ADDR(addr), data);

    if(MIR3DA_REG_PAGE(addr) > 0) {
        /* restore page NO. */
        res |= mir3da_gsensor_drv.method->smi.write(handle, 0x0, cur_page);
    }

    return res;
}

int mir3da_register_read_continuously(MIR_HANDLE handle, short addr, unsigned char count, unsigned char *data)
{
    unsigned char      cur_page;
    int     res = 0;

    /* check page */
    if(MIR3DA_REG_PAGE(addr) > 0) {
        res = mir3da_gsensor_drv.method->smi.read(handle, 0x0, &cur_page);
        if(res != 0) {
            return res;
        }

        if(cur_page != MIR3DA_REG_PAGE(addr)) {
            res |= mir3da_gsensor_drv.method->smi.write(handle, 0x0, MIR3DA_REG_PAGE(addr));
            if(res != 0) {
                return res;
            }
        }
    }

    res = (count==mir3da_gsensor_drv.method->smi.read_block(handle, MIR3DA_REG_ADDR(addr), count, data)) ? 0 : 1;

    if(MIR3DA_REG_PAGE(addr) > 0) {
        /* restore page NO. */
        res |= mir3da_gsensor_drv.method->smi.write(handle, 0x0, cur_page);
    }

    return res;
}

int mir3da_register_write(MIR_HANDLE handle, short addr, unsigned char data)
{
    unsigned char      cur_page;
    int     res = 0;

    /* check page */
    if(MIR3DA_REG_PAGE(addr) > 0) {
        res = mir3da_gsensor_drv.method->smi.read(handle, 0x0, &cur_page);
        if(res != 0) {
            return res;
        }

        if(cur_page != MIR3DA_REG_PAGE(addr)) {
            res |= mir3da_gsensor_drv.method->smi.write(handle, 0x0, MIR3DA_REG_PAGE(addr));
            if(res != 0) {
                return res;
            }
        }
    }

    res = mir3da_gsensor_drv.method->smi.write(handle, MIR3DA_REG_ADDR(addr), data);

    if(MIR3DA_REG_PAGE(addr) > 0) {
        /* restore page NO. */
        res |= mir3da_gsensor_drv.method->smi.write(handle, 0x0, cur_page);
    }

    return res;
}

int mir3da_register_mask_write(MIR_HANDLE handle, short addr, unsigned char mask, unsigned char data)
{
    int     res = 0;
    unsigned char      tmp_data;

    res = mir3da_register_read(handle, addr, &tmp_data);
    if(res) {
        return res;
    }

    tmp_data &= ~mask; 
    tmp_data |= data & mask;
    res = mir3da_register_write(handle, addr, tmp_data);

    return res;
}

static int mir3da_read_raw_data(MIR_HANDLE handle, short *x, short *y, short *z)
{
    unsigned char    tmp_data[6] = {0};

    if (mir3da_register_read_continuously(handle, mir3da_gsensor_drv.obj[gsensor_mod].data.data_sect[0].addr, 6, tmp_data) != 0) {
        MI_ERR("i2c block read failed\n");
        return -1;
    }

    *x = ((short)(tmp_data[1] << mir3da_gsensor_drv.obj[gsensor_mod].data.data_fmt.msbw | tmp_data[0]))>> (8-mir3da_gsensor_drv.obj[gsensor_mod].data.data_fmt.lsbw);
    *y = ((short)(tmp_data[3] << mir3da_gsensor_drv.obj[gsensor_mod].data.data_fmt.msbw | tmp_data[2]))>> (8-mir3da_gsensor_drv.obj[gsensor_mod].data.data_fmt.lsbw);
    *z = ((short)(tmp_data[5] << mir3da_gsensor_drv.obj[gsensor_mod].data.data_fmt.msbw | tmp_data[4]))>> (8-mir3da_gsensor_drv.obj[gsensor_mod].data.data_fmt.lsbw);

    MI_DATA("mir3da_raw: x=%d, y=%d, z=%d",  *x, *y, *z);
    
#if MIR3DA_SENS_TEMP_SOLUTION
    if (bSensZoom == 1){
        *z = (*z )*5/4;
        MI_DATA("SensZoom take effect, Zoomed Z = %d", *z);
    }
#endif
    return 0;
}


int mir3da_read_data(MIR_HANDLE handle, short *x, short *y, short *z)
{
    int    rst = 0;
    short temp =0;	

#if MIR3DA_OFFSET_TEMP_SOLUTION
    if(is_cali){
        *x = *y = *z = 0;
        return 0;
    }

    manual_load_cali_file(handle);
#endif

    rst = mir3da_read_raw_data(handle, x, y, z);
    if (rst != 0){
        MI_ERR("mir3da_read_raw_data failed, rst = %d", rst);
        return rst;
    }
#if MIR3DA_AUTO_CALIBRATE
    if(!bLoad) {
		if (GSENSOR_MOD_SOCLE != gsensor_mod ){	
            mir3da_auto_calibrate(handle, *x, *y, *z);
		}
    }
#endif

   if (GSENSOR_MOD_NSA_NTO == gsensor_mod){
    	 temp = *x;
    	*x = -*y;
    	*y = -temp;
    	*z = 0-*z;
        MI_DATA("socle change xyz axis !");
   }
    
#if MIR3DA_STK_TEMP_SOLUTION   
    addAixHistory(*x,*y,*z);

    bxstk = isXStick();
    bystk = isYStick();
    bzstk = isZStick();

    if ((bxstk + bystk+ bzstk) < 2){
        if(bxstk)
        *x = squareRoot(1024*1024 - (*y)*(*y) - (*z)*(*z));
    if(bystk)
        *y = squareRoot(1024*1024 - (*x)*(*x) - (*z)*(*z));
    if(bzstk)
        *z = -squareRoot(1024*1024 - (*x)*(*x) - (*y)*(*y));
    }else{
        // MI_ERR( "CHIP ERR !MORE STK!\n"); 
        return 0;
    }
#endif


#if FILTER_AVERAGE_ENHANCE
   // *x = filter_average_enhance(&core_ctx.tFac[0], *x);
    //*y = filter_average_enhance(&core_ctx.tFac[1], *y);
    //*z = filter_average_enhance(&core_ctx.tFac[2], *z);

   // MI_DATA("mir3da_filt: x=%d, y=%d, z=%d",  *x, *y, *z); 
#endif

    return 0;
}

int cycle_read_xyz(MIR_HANDLE handle, int* x, int* y, int* z, int ncycle)
{
    unsigned int j = 0;
    short raw_x,raw_y,raw_z;    

    *x = *y = *z = 0;

    for (j = 0; j < ncycle; j++)
    {
        raw_x = raw_y = raw_z = 0;
        mir3da_read_raw_data (handle, &raw_x, &raw_y, &raw_z);

        (*x) += raw_x;
        (*y) += raw_y;
        (*z) += raw_z;

        mir3da_gsensor_drv.method->msdelay(5);
    }
    
    (*x) /= ncycle;
    (*y) /= ncycle;
    (*z) /= ncycle;
    
    return 0;
}

#if MIR3DA_OFFSET_TEMP_SOLUTION
int mir3da_read_offset(MIR_HANDLE handle, unsigned char* offset)
{
    int     i, res = 0;
    
    for(i=0;i<MIR3DA_OFF_SECT_LEN;i++) {
        if( mir3da_gsensor_drv.obj[gsensor_mod].offset_sect[i].addr < 0 ) {
            break;
        }

        res = mir3da_register_read(handle, mir3da_gsensor_drv.obj[gsensor_mod].offset_sect[i].addr, &offset[i]);
        if(res != 0) {
            return res;
        } 
    }
    
    return res;
}

int mir3da_write_offset(MIR_HANDLE handle, unsigned char* offset)
{
    int     i, res = 0;

    for(i=0;i<MIR3DA_OFF_SECT_LEN;i++) {
        if( mir3da_gsensor_drv.obj[gsensor_mod].offset_sect[i].addr < 0 ) {
            break;
        }

        res = mir3da_register_write(handle, mir3da_gsensor_drv.obj[gsensor_mod].offset_sect[i].addr, offset[i]);
        if(res != 0) {
            return res;
        } 
    }

    return res;
}

static int mir3da_write_offset_to_file(unsigned char* offset)
{
    int     ret = 0;
    
    ret = mir3da_gsensor_drv.method->data_save(offset);
    
    MI_MSG("====sensor_sync_write, offset = 0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x", offset[0],offset[1],offset[2],offset[3],offset[4],offset[5],offset[6],offset[7],offset[8]);
    
    return ret;
}

static int mir3da_read_offset_from_file(unsigned char* offset)
{
    int     ret = 0;

    ret = mir3da_gsensor_drv.method->data_get(offset);

    MI_MSG("====sensor_sync_read, offset = 0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x", offset[0],offset[1],offset[2],offset[3],offset[4],offset[5],offset[6],offset[7],offset[8]);
    
    return ret;
}

void manual_load_cali_file(MIR_HANDLE handle)
{ 
    unsigned char  offset[MIR3DA_OFFSET_LEN] = {0};
        
    if (!bLoad){
	 		readOffsetCnt++;	
	 		if(readOffsetCnt%10 == 0)
	 		{
	 			if(readOffsetCnt>0)
	 				readOffsetCnt =0;
	     
		 		MI_DATA("====444 manual_load_cali_file(), bLoad = %d, readOffsetCnt=%d.\n", bLoad, readOffsetCnt); 
	      if(!mir3da_read_offset_from_file(offset))
	      {
	      	MI_MSG("========= WRITE OFFSET");
	       	mir3da_write_offset(handle, offset);
	       	bLoad = 1;
	      }
	   	}
    }
}

typedef struct  linearitydata{
    unsigned short  off;
    int                    val; 

}LinearityData;

int check_linearity_offset(MIR_HANDLE handle, int *step)
{

    int                 i,result = 0;
    int                 x, y, z;

#if 1
    unsigned char       xdata_count = 0;
    unsigned char       ydata_count = 0;
    unsigned char       zdata_count = 0;  	
    LinearityData       xdata[2] = {{0}};
    LinearityData       ydata[2] = {{0}};
    LinearityData       zdata[2] = {{0}}; 
     
    for (i = 10; i <= 0x3ff; i+= 50){
        result |= mir3da_register_write(handle, SOCLE_REG_OTP_XOFF_L, i & 0xFF);
        result |= mir3da_register_write(handle, SOCLE_REG_OTP_XOFF_H, (i & 0xFF00) >> 8);
        result |= mir3da_register_write(handle, SOCLE_REG_OTP_YOFF_L, i & 0xFF);
        result |= mir3da_register_write(handle, SOCLE_REG_OTP_YOFF_H, (i & 0xFF00) >> 8);
        result |= mir3da_register_write(handle, SOCLE_REG_OTP_ZOFF_L, i & 0xFF);
        result |= mir3da_register_write(handle, SOCLE_REG_OTP_ZOFF_H, (i & 0xFF00) >> 8);
        result |= cycle_read_xyz(handle, &x, &y, &z, 20);

        MI_MSG ("detect_linearity_ratio: i = %d, x = %d, y = %d, z= %d \n", i, x, y, z); 

        if (result){
            MI_MSG ("detect_linearity_ratio: chip op failed, result = %d \n", result); 
            return result;
        }

        if (abs(x) < 1800 && xdata_count < 2){
            MI_MSG("detect linearity ratio: xdata_count = %d, x = %d i = %d\n", xdata_count, x, i);
            
            xdata[xdata_count].val = x;  
            xdata[xdata_count].off = i;    
            xdata_count ++;
        }

        if (abs(y) < 1800 && ydata_count < 2){
            MI_MSG("detect linearity ratio: ydata_count = %d, y = %d i = %d\n", ydata_count, y, i);
            ydata[ydata_count].val = y;  
            ydata[ydata_count].off = i;    
            ydata_count ++;                       
        }

        if (abs(z) < 1800 && zdata_count < 2){
            MI_MSG("detect linearity ratio: zdata_count = %d, z = %d i = %d\n", zdata_count, z, i);
            zdata[zdata_count].val = z;  
            zdata[zdata_count].off = i;    
            zdata_count ++;                       
        }

        if (xdata_count == 2 && ydata_count == 2 && zdata_count == 2 ){
            MI_MSG ("all linearity_ratio found!");
            step[0] = (xdata[1].val - xdata[0].val)/(xdata[1].off - xdata[0].off);
            step[1] = (ydata[1].val - ydata[0].val)/(ydata[1].off - ydata[0].off);
            step[2] = (zdata[1].val - zdata[0].val)/(zdata[1].off - zdata[0].off);

            MI_MSG("CUSTOM offset step: x = %d, y = %d, z= %d", step[0], step[1], step[2] );

            break;
        }
    }

    if( abs(step[0]) < 10 || abs(step[1]) < 10 || abs(step[2]) < 10 ){
        MI_MSG("detect linearity ratio failed!");
        return -1;
    }  
    
#endif    
    return result;
}

static void mir3da_cali_off_to_lsb(int off, int *coarse, int coarse_step, int *fine, int fine_step)
{
	*coarse = off/coarse_step;
	*fine = 100*(off-(*coarse)*coarse_step)/fine_step;
	
	MI_MSG("off = %d; delta_coarse = %d; delta_fine = %d", off, *coarse, *fine);
}

#if MIR3DA_AUTO_CALIBRATE
static int NSA_once_calibrate(MIR_HANDLE handle, int coarse_step[3], int fine_step[3], int xyz[3])
{
    int     coarse[3] = {0};
    int     coarse_delta[3] = {0};
    int     fine[3] = {0};
    int     fine_delta[3] = {0};
    int     target[3] = {0};
    int     i;
    unsigned char   offset_data[9] = {0};

    if(mir3da_read_offset(handle, offset_data)){
        MI_ERR("Get old offset failed !");
        return -1;
    }
    coarse[0] = offset_data[0] & 0x3f;
    coarse[1] = offset_data[1] & 0x3f;
    coarse[2] = offset_data[2] & 0x3f;
    fine[0] = (((int)offset_data[0] << 2) & 0x300)|offset_data[3];
    fine[1] = (((int)offset_data[1] << 2) & 0x300)|offset_data[4];
    fine[2] = (((int)offset_data[2] << 2) & 0x300)|offset_data[5];
    
    MI_MSG("Old coarse_x = %d; coarse_y = %d; coarse_z = %d; fine_x = %d; fine_y = %d; fine_z = %d;", coarse[0], coarse[1], coarse[2], fine[0], fine[1], fine[2]);
        
    /* 0 means auto detect z direction assume z axis is verticle */
    if ((abs(target[0]) + abs(target[1]) + abs(target[2])) == 0){
        target[2] = (xyz[2] > 0) ? 1024 : (-1024);
    }
    
    for(i = 0;i < 3; i++){
        coarse_step[i] *= coarse[i] >= 32 ? (-1) : 1;
        mir3da_cali_off_to_lsb((xyz[i]-target[i]), &coarse_delta[i], coarse_step[i], &fine_delta[i], fine_step[i]);
        
        coarse[i] += coarse_delta[i];
        fine[i] += fine_delta[i];
        offset_data[i] = coarse[i]|((fine[i]>>2)&0xc0);
        offset_data[i+3] = fine[i]&0xFF;
    }
    
    if(mir3da_write_offset(handle, offset_data)){
        MI_ERR("Update offset failed !");
        return -1;
    }
    /* Discard unstable data after offset register changed */
    cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], 5);
    if(cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], 10)){
        return -1;
    }
    MI_MSG("---calibrate_Done, x = %d, y = %d, z = %d, coarse_x = %d, coarse_y = %d, coarse_z = %d, fine_x = %d, fine_y = %d, fine_z = %d", xyz[0], xyz[1], xyz[2], coarse[0], coarse[1], coarse[2], fine[0], fine[1], fine[2]);
    
    return mir3da_write_offset_to_file(offset_data);
}
#endif /* !MIR3DA_AUTO_CALIBRATE */

static int NSA_calibrate(MIR_HANDLE handle, int coarse_step[3], int fine_step[3], int fine_max, int target[3])
{
    int             i = 0, j = 0;
    unsigned char   ncycle = 20;
    unsigned char   nLoop = 20;
    unsigned char   offset_data[9] = {0};
    unsigned char   fine_ok_map = 0;
    
    int             xyz[3] = {0};  
    int             coarse[3] = {0};
    int             coarse_delta[3] = {0};
    int             fine[3] = {0};
    int             fine_delta[3] = {0};
    
    if( (abs(target[0]) + abs(target[1]) + abs(target[2])) != 0 && (abs(target[0]) + abs(target[1]) + abs(target[2])) != 1024 ) {
        MI_ERR("Invalid argument !");
        return -1;
    }
        
    /* 0 means auto detect z direction assume z axis is verticle */
    if ((abs(target[0]) + abs(target[1]) + abs(target[2])) == 0){
       if(cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], 5)){
            MI_ERR("check z direction failed\n");
            return -1;
       }
       target[2] = (xyz[2] > 0) ? 1024 : (-1024);
    }

    MI_MSG("---Start Calibrate, trim target %d, %d, %d---\n", target[0], target[1], target[2]);

    // Stage1: Coarse tune once
    MI_MSG("---Stage1, coarse tune---");            
    // change to 8G mode
    if(mir3da_register_mask_write(handle, NSA_REG_G_RANGE, 0x03, 2)){
        MI_ERR("i2c mask write failed !\n");
        return -1;
    }

    /* reset coarse offset register */
    mir3da_write_offset(handle, offset_data);  
    /* Discard unstable data after offset register changed */
    cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], 5);
  
    if( cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], ncycle) ){
        goto EXIT_8G_MOD;
    }

    for(i = 0; i < 3; i++){
        /* check rule */
        coarse[i] = (xyz[i] > 0) ? 0 : 32;
        
        xyz[i] *= 4;
        MI_MSG("xyz[%d] = %d, coarse[%d] = 0x%x", i, xyz[i], i, coarse[i]);
        
        coarse_step[i] *= coarse[i] >= 32 ? (-1) : 1;
        mir3da_cali_off_to_lsb((xyz[i]-target[i]), &coarse_delta[i], coarse_step[i], &fine_delta[i], fine_step[i]);

        coarse[i] += coarse_delta[i];
        fine[i] += fine_delta[i];
        mir3da_register_mask_write(handle, NSA_REG_COARSE_OFFSET_TRIM_X+i, 0x3f, (unsigned char)coarse[i]);
    }
    
    /* Discard unstable data after offset register changed */
    cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], 5);
    if(cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], 5)){
        return -1;
    }
    for(i = 0; i < 3; i++){   
        fine[i] += (xyz[i] > 0) ? 0 : fine_max;
        mir3da_register_write(handle, NSA_REG_FINE_OFFSET_TRIM_X+i, (unsigned char)(fine[i]&0xff));
        mir3da_register_mask_write(handle, NSA_REG_COARSE_OFFSET_TRIM_X+i, 0xc0, (unsigned char)(0xc0&(fine[i]>>2)));
    }

EXIT_8G_MOD:
    // change back to 2G mode
    if(mir3da_register_mask_write(handle, NSA_REG_G_RANGE, 0x03, 0)){
        MI_ERR("i2c mask write failed !\n");
        return -1;
    }
    /* Discard unstable data after offset register changed */
    cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], 5);
    if(cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], ncycle)){
        return -1;
    }
    MI_MSG("---Stage1, coarse tune done: x = %d, y = %d, z = %d, coarse_x = %d, coarse_y = %d, coarse_z = %d, fine_x = %d, fine_y = %d, fine_z = %d", xyz[0], xyz[1], xyz[2], coarse[0], coarse[1], coarse[2], fine[0], fine[1], fine[2]);
    
    // Stage2: Fine tune
    MI_MSG("---Stage2, Fine tune---");
    for (i = 0; i < nLoop; i++){
    
        if( 0x07==(fine_ok_map & 0x07) ){
            break;
        }
        /* Discard unstable data after offset register changed */
        cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], 5);
        MI_MSG("---Stage2, Fine loop %d", i);
        if(cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], ncycle)){
            return -1;
        }
        
        for(j = 0; j < 3; j++){
            MI_MSG("xyz[%d] = %d, caorse[%d] = 0x%x, fine[%d] = 0x%x", j, xyz[j], j, coarse[j], j, fine[j]);
            if( abs(xyz[j]-target[j]) < MIR3DA_OFFSET_THRESHOLD ){
                fine_ok_map |= (1<<j);
                offset_data[j] = coarse[j]|((fine[j]>>2)&0xc0);
                offset_data[j+3] = fine[j];
                continue;        
            }
            mir3da_cali_off_to_lsb((xyz[j]-target[j]), &coarse_delta[j], coarse_step[j], &fine_delta[j], fine_step[j]);

            coarse[j] += coarse_delta[j];
            fine[j] += fine_delta[j];
            mir3da_register_write(handle, NSA_REG_FINE_OFFSET_TRIM_X+j, (unsigned char)(fine[j]&0xff));
            mir3da_register_mask_write(handle, NSA_REG_COARSE_OFFSET_TRIM_X+j, 0xc0, (unsigned char)(0xc0&(fine[j]>>2)));
        }                       
    }
    MI_MSG("---Stage2, Fine tune done: x = %d, y = %d, z = %d, coarse_x = %d, coarse_y = %d, coarse_z = %d, fine_x = %d, fine_y = %d, fine_z = %d", xyz[0], xyz[1], xyz[2], coarse[0], coarse[1], coarse[2], fine[0], fine[1], fine[2]);
    
    if( 0x07==(fine_ok_map & 0x07) ){
        goto SUCCESS_EXIT;
    }
#if MIR3DA_STK_TEMP_SOLUTION
    if( 0x03==(fine_ok_map & 0x07) ){
        goto SUCCESS_EXIT;
    }
#endif
    
    MI_MSG("---calibrate Failed !---");
    return -1;
    
SUCCESS_EXIT:
    MI_MSG("---calibrate OK !---");
    return mir3da_write_offset_to_file(offset_data);
}

static int NSA_NTO_cali_step_calc(MIR_HANDLE handle, int coarse[3], int x100_fine[3], int x100_cust[3])
{
    int                i;
    unsigned int       total_gain[3] = {0}; 
    unsigned char      coarse_gain = 0;
    unsigned char      fine_gain[3] = {0};
    unsigned int       const coarse_gain_map[] = {1000, 1125, 1250, 1375, 500, 625, 750, 875};   /* *1000  */
    unsigned char      const fine_dig_gain_map[] = {1, 2, 4, 8};
        
    if(mir3da_register_read_continuously(handle, NSA_REG_SENSITIVITY_TRIM_X, 3, fine_gain) != 0){
        MI_ERR("i2c block read failed\n");
        return -1;
    }

    if(mir3da_register_read(handle, NSA_REG_SENS_COARSE_TRIM, &coarse_gain) != 0){
        MI_ERR("i2c block read failed\n");
        return -1;
    }
    
    for(i = 0;i < 3;i++) {        
        // *100*1000
        total_gain[i] = ((1000 + (fine_gain[i]&0x1F)*1000/32)/15) * fine_dig_gain_map[((fine_gain[i]>>5)&0x03)] * coarse_gain_map[coarse_gain&0x07]; 
        coarse[i] = (int)(total_gain[i] * 500 / 100000);
        x100_fine[i] = (int)(total_gain[i] * 293 / 100000);
        x100_cust[i] = (int)(total_gain[i] * 390 / 100000);
    }
    MI_MSG("coarse_step_x = %d, coarse_step_y = %d, coarse_step_z = %d\n", coarse[0], coarse[1], coarse[2]);
    MI_MSG("fine_step_x = %d, fine_step_y = %d, fine_step_z = %d\n", x100_fine[0], x100_fine[1], x100_fine[2]);
    MI_MSG("custom_step_x = %d, custom_step_y = %d, custom_step_z = %d\n", x100_cust[0], x100_cust[1], x100_cust[2]);
    
    return 0;    
}

static int NSA_MLM_cali_step_calc(MIR_HANDLE handle, int coarse[3], int x100_fine[3], int x100_cust[3])
{
    int                i;
    unsigned int       total_gain[3] = {0};
    unsigned char      gain[3] = {0};
    unsigned int       const coarse_gain_map[] = {1000, 1125, 1250, 1375, 500, 625, 750, 875};   /* *1000  */
        
    if(mir3da_register_read_continuously(handle, NSA_REG_SENSITIVITY_TRIM_X, 3, gain) != 0){
        MI_ERR("i2c block read failed\n");
        return -1;
    }
    
    for(i = 0;i < 3;i++) {    
        // *100*1000
        total_gain[i] = ((1000 + (gain[i]&0x1F)*1000/32)/15) * coarse_gain_map[((gain[i]>>5)&0x07)];
        MI_MSG("total gain = %d", total_gain[i]); 
        coarse[i] = (int)(total_gain[i] * 500 / 100000);
        x100_fine[i] = (int)(total_gain[i] * 589 / 100000);
        x100_cust[i] = (int)(total_gain[i] * 390 / 100000);
    }

    MI_MSG("coarse_step_x = %d, coarse_step_y = %d, coarse_step_z = %d\n", coarse[0], coarse[1], coarse[2]);
    MI_MSG("fine_step_x = %d, fine_step_y = %d, fine_step_z = %d\n", x100_fine[0], x100_fine[1], x100_fine[2]);
    MI_MSG("custom_step_x = %d, custom_step_y = %d, custom_step_z = %d\n", x100_cust[0], x100_cust[1], x100_cust[2]);
    
    return 0;
}

#endif /* !MIR3DA_OFFSET_TEMP_SOLUTION */

static int NSA_NTO_calibrate(MIR_HANDLE handle, int z_dir)
{
    int     result = 0;

#if MIR3DA_OFFSET_TEMP_SOLUTION
    int     coarse_step[3] = {0};
    int     fine_step[3] = {0};
    int     custom_step[3] = {0};
    int     target[3] = {0};

   unsigned char     swap_plarity_old = 0;  
      
    /* compute step */
    if( NSA_NTO_cali_step_calc(handle, coarse_step, fine_step, custom_step) ) {
        MI_ERR("Compute step failed !");
        return -1; 
    }
    target[2] = z_dir*1024;

    // save swap/plarity old setting
    if(mir3da_register_read(handle, NSA_REG_SWAP_POLARITY, &swap_plarity_old)){
        MI_ERR("Get SWAP/PLARITY setting failed !");
        return -1;
    }
    if(mir3da_register_mask_write(handle, NSA_REG_SWAP_POLARITY, 0x0F, 0x00)){
        MI_ERR("Set Plarity failed !");
        return -1;
    }
    
    result=NSA_calibrate(handle, coarse_step, fine_step, 0x3ff, target);

    // Restore swap/plarity setting
    if(mir3da_register_mask_write(handle, NSA_REG_SWAP_POLARITY, 0x0F, swap_plarity_old&0x0F)){
        MI_ERR("Restore SWAP/PLARITY setting failed !");
        return -1;
    }

#endif /* !MIR3DA_OFFSET_TEMP_SOLUTION */
    return result;
}

static int NSA_NTO_auto_calibrate(MIR_HANDLE handle, int xyz[3])
{
    int     result = 0;

#if MIR3DA_AUTO_CALIBRATE
    int     coarse_step[3];
    int     fine_step[3];
    int     custom_step[3] = {0};
   unsigned char     swap_plarity_old = 0;
    int     temp=0;  
      
 
    /* compute step */
    if( NSA_NTO_cali_step_calc(handle, coarse_step, fine_step, custom_step) ) {
        MI_ERR("Compute step failed !");
        return -1; 
    }

    // save swap/plarity old setting
    if(mir3da_register_read(handle, NSA_REG_SWAP_POLARITY, &swap_plarity_old)){
        MI_ERR("Get SWAP/PLARITY setting failed !");
        return -1;
    }
    if((swap_plarity_old & (1<<0))){
       MI_ERR("==xy swap==\n");
       temp = xyz[0];
       xyz[0] = ((swap_plarity_old & (1<<2)) != 0) ? (-xyz[1]) :xyz[1];
       xyz[1] = ((swap_plarity_old & (1<<3)) != 0) ? (-temp) :temp;
    }else{
       MI_ERR("==xy no swap==\n");  
       xyz[0] = ((swap_plarity_old & (1<<3)) != 0) ? (-xyz[0]) :xyz[0];
       xyz[1] = ((swap_plarity_old & (1<<2)) != 0) ? (-xyz[1]) :xyz[1];
    }

    xyz[2] = ((swap_plarity_old & (1<<1)) != 0) ? (-xyz[2]) :xyz[2];
   

    
    result = NSA_once_calibrate(handle, coarse_step, fine_step, xyz);


#endif /* !MIR3DA_AUTO_CALIBRATE */
    return result;
}


static int NSA_MLM_calibrate(MIR_HANDLE handle, int z_dir)
{
    int     result = 0;
#if MIR3DA_OFFSET_TEMP_SOLUTION
    int     coarse_step[3] = {0};
    int     fine_step[3] = {0};
    int     custom_step[3] = {0};
    int     target[3] = {0};
    unsigned char     swap_plarity_old = 0;  
      
    /* compute step */
    if( NSA_MLM_cali_step_calc(handle, coarse_step, fine_step, custom_step) ) {
        MI_ERR("Compute step failed !");
        return -1; 
    }    
    target[2] = z_dir*1024;
    
    // save swap/plarity old setting
    if(mir3da_register_read(handle, NSA_REG_SWAP_POLARITY, &swap_plarity_old)){
        MI_ERR("Get SWAP/PLARITY setting failed !");
        return -1;
    }
    if(mir3da_register_mask_write(handle, NSA_REG_SWAP_POLARITY, 0x0F, 0x0E)){
        MI_ERR("Set Plarity failed !");
        return -1;
    }
    
    result=NSA_calibrate(handle, coarse_step, fine_step, 0xff, target);
    // Restore swap/plarity setting
    if(mir3da_register_mask_write(handle, NSA_REG_SWAP_POLARITY, 0x0F, swap_plarity_old&0x0F)){
        MI_ERR("Restore SWAP/PLARITY setting failed !");
        return -1;
    }
#endif /* !MIR3DA_OFFSET_TEMP_SOLUTION */
    return result;
}

static int NSA_MLM_auto_calibrate(MIR_HANDLE handle, int xyz[3])
{
    int     result = 0;
#if MIR3DA_AUTO_CALIBRATE
    int     coarse_step[3] = {0};
    int     fine_step[3] = {0};
    int     custom_step[3] = {0};
    unsigned char     swap_plarity_old = 0;  
      
    /* compute step */
    if( NSA_MLM_cali_step_calc(handle, coarse_step, fine_step, custom_step) ) {
        MI_ERR("Compute step failed !");
        return -1; 
    }    
    
    // save swap/plarity old setting
    if(mir3da_register_read(handle, NSA_REG_SWAP_POLARITY, &swap_plarity_old)){
        MI_ERR("Get SWAP/PLARITY setting failed !");
        return -1;
    }
    
    xyz[0] = ((swap_plarity_old & (1<<3)) != 0) ? xyz[0] : (-xyz[0]);
    xyz[1] = ((swap_plarity_old & (1<<2)) != 0) ? xyz[1] : (-xyz[1]);
    xyz[2] = ((swap_plarity_old & (1<<1)) != 0) ? xyz[2] : (-xyz[2]);
    
    result=NSA_once_calibrate(handle, coarse_step, fine_step, xyz);

#endif /* !MIR3DA_AUTO_CALIBRATE */
    return result;
}

static int SOCLE_auto_calibrate(MIR_HANDLE handle, int xyz[3])
{
#if MIR3DA_AUTO_CALIBRATE
    int     coarse_step[3] = {-20, -20, -20};
    int     coarse_delta[3] = {0};
    int     coarse[3] = {0};
    int     fine_step[3] = {0};
    int     fine_delta[3] = {0};
    int     target[3] = {0};
    int     i;
    unsigned char   offset_data[9] = {0};
    
    if(mir3da_read_offset(handle, offset_data)){
        MI_ERR("Get old offset failed !");
        return -1;
    }
    
    /* 0 means auto detect z direction assume z axis is verticle */
    if ((abs(target[0]) + abs(target[1]) + abs(target[2])) == 0){
        target[2] = (xyz[2] > 0) ? 1024 : (-1024);
    }
    
    for(i = 0;i < 3; i++){
        coarse[i] = offset_data[i*2+1] & 0xff;   /* high byte */
        coarse[i] = (coarse[i] << 8)|offset_data[i*2]; /* low gyte */

        mir3da_cali_off_to_lsb((xyz[i]-target[i]), &coarse_delta[i], coarse_step[i], &fine_delta[i], fine_step[i]);
        coarse[i] += coarse_delta[i];
        
        offset_data[i*2] = coarse[i] & 0xff;
        offset_data[i*2+1] = (coarse[i] >> 8) & 0xff;
    }
 
    if(mir3da_write_offset(handle, offset_data)){
        MI_ERR("Update offset failed !");
        return -1;
    }

    if(cycle_read_xyz(handle, &xyz[0], &xyz[1], &xyz[2], 10)){
        return -1;
    }
    MI_MSG("---calibrate_Done, x = %d, y = %d, z = %d, off_x = %d, off_y = %d, off_z = %d", xyz[0], xyz[1], xyz[2], coarse[0], coarse[1], coarse[2]);
    
    return mir3da_write_offset_to_file(offset_data);

#endif /* !MIR3DA_AUTO_CALIBRATE */
    return 0;
}

static int SOCLE_calibrate(MIR_HANDLE handle, int z_dir)
{
    int             result = 0;
#if MIR3DA_OFFSET_TEMP_SOLUTION
    short   i;
    unsigned char      tmp_data[6];
    int     x = 0, y = 0, z = 0;
    short     tmp_off = 0, tmp_off2 = 0 ;
    unsigned char      ncycle = 50;
    unsigned char      offset_data[9] = {0};
    int     fine_step[3] = {0};	
	
    unsigned char x_ok=0;
    unsigned char y_ok=0;
    unsigned char z_ok=0;
    unsigned short x_off=0;	
    unsigned short y_off=0;	
    unsigned short z_off=0;	
    unsigned short x_off_original = 0;
    unsigned short y_off_original = 0;
    unsigned short z_off_original = 0;

    /* decide the z direction, if 0 which means auto */
    if (z_dir == 0){
       result = cycle_read_xyz(handle, &x, &y, &z, 5);
       if (result != 0){
            MI_ERR("check z direction failed\n");
            goto fail_exit;
       }
       z_dir = z > 0 ? 1 : (-1);
    }

    if(mir3da_register_read_continuously(handle, (SOCLE_REG_OTP_XOFF_L|0x80), 6, tmp_data))
    {
        MI_ERR("i2c block read failed\n");
        goto fail_exit;
    }

    x_off = (tmp_data[1] << 8) | tmp_data[0] ;
    y_off = (tmp_data[3] << 8) | tmp_data[2] ;
    z_off = (tmp_data[5] << 8) | tmp_data[4] ;

#if MIR3DA_STK_TEMP_SOLUTION 
    x_off_original = x_off;
    y_off_original = y_off;
    z_off_original = z_off;
#endif

    if (0 != check_linearity_offset(handle, fine_step)){
        fine_step[0] = fine_step[1] = fine_step[2] = -20;
    }   

    result = mir3da_register_write(handle, SOCLE_REG_OTP_XOFF_L, x_off & 0xFF);
    MI_ASSERT(result==0);
    result = mir3da_register_write(handle, SOCLE_REG_OTP_XOFF_H, (x_off & 0xFF00) >> 8);
    MI_ASSERT(result==0);
    result = mir3da_register_write(handle, SOCLE_REG_OTP_YOFF_L, y_off & 0xFF);
    MI_ASSERT(result==0);
    result = mir3da_register_write(handle, SOCLE_REG_OTP_YOFF_H, (y_off & 0xFF00) >> 8);
    MI_ASSERT(result==0);        
    result = mir3da_register_write(handle, SOCLE_REG_OTP_ZOFF_L, z_off & 0xFF);
    MI_ASSERT(result==0);
    result = mir3da_register_write(handle, SOCLE_REG_OTP_ZOFF_H, (z_off & 0xFF00) >> 8);
    MI_ASSERT(result==0);

    MI_MSG("---Start Calibrate, z direction = %d---\n", z_dir);

    for (i = 0; i < 20 ; i++)
    {
        x = y = z = 0;
        
       result = cycle_read_xyz(handle, &x, &y, &z, ncycle);
       if (result != 0){
            MI_ERR("i2c block read failed\n");
            goto fail_exit;
       }

        MI_MSG("----loop %d: x = %d, y = %d, z = %d; x_off = 0x%x, y_off = 0x%x, z_off = 0x%x\n", i, x, y, z, x_off, y_off, z_off);

        if (! x_ok)
        {
            if ( abs(x) <= MIR3DA_OFFSET_THRESHOLD )
            {
                x_ok = 1 ;
                MI_MSG("------X is OK, 0x%X-------\n", x_off); 
            }
            else
            {
                tmp_off = x/fine_step[0];                

                tmp_off2 = (short)x_off - tmp_off;
                if (tmp_off2 > 0x3ff){
                     tmp_off2 = 0x3ff;
                }else if (tmp_off2 < 0){
                    tmp_off2 = 0x01;
                }
                
                x_off = (unsigned short)tmp_off2;
                MI_MSG("tmp_off = %d, tmp_off2 = %d,  x_off = %d\n", tmp_off, tmp_off2,  x_off);
               
                result = mir3da_register_write(handle, SOCLE_REG_OTP_XOFF_L, x_off & 0xFF);
                MI_ASSERT(result==0);
                result = mir3da_register_write(handle, SOCLE_REG_OTP_XOFF_H, (x_off & 0xFF00) >> 8);
                MI_ASSERT(result==0);
            }
            
        }

        if (! y_ok)
        {
            if ( abs(y) <= MIR3DA_OFFSET_THRESHOLD )
            {
                y_ok = 1 ;
                MI_MSG("------Y is OK, 0x%X-------\n", y_off); 
            }
            else
            {
                 tmp_off = y/fine_step[1];                

                tmp_off2 = (short)y_off - tmp_off;
                if (tmp_off2 > 0x3ff){
                     tmp_off2 = 0x3ff;
                }else if (tmp_off2 < 0){
                    tmp_off2 = 0x01;
                }
                
                y_off = (unsigned short)tmp_off2;
                MI_MSG("tmp_off = %d, tmp_off2 = %d,  y_off = %d\n", tmp_off, tmp_off2,  y_off);

                result = mir3da_register_write(handle, SOCLE_REG_OTP_YOFF_L, y_off & 0xFF);
                MI_ASSERT(result==0);
                result = mir3da_register_write(handle, SOCLE_REG_OTP_YOFF_H, (y_off & 0xFF00) >> 8);
                MI_ASSERT(result==0);
            }
            
        }

        if (! z_ok)
        {
            if ( abs(z - (z_dir > 0 ? 1024 : -1024)) <= MIR3DA_OFFSET_THRESHOLD )
            {
                z_ok = 1 ;
                MI_MSG("------Z is OK, 0x%X-------\n", z_off); 
            }
            else
            {
                tmp_off = (z - (z_dir > 0 ? 1024 : -1024)) /fine_step[2];                

                tmp_off2 = (short)z_off - tmp_off;
                if (tmp_off2 > 0x3ff){
                     tmp_off2 = 0x3ff;
                }else if (tmp_off2 < 0){
                    tmp_off2 = 0x01;
                }
                
                z_off = (unsigned short)tmp_off2;
                MI_MSG("tmp_off = %d, tmp_off2 = %d,  z_off = %d\n", tmp_off, tmp_off2,  z_off);
                

                result = mir3da_register_write(handle, SOCLE_REG_OTP_ZOFF_L, z_off & 0xFF);
                MI_ASSERT(result==0);
                result = mir3da_register_write(handle, SOCLE_REG_OTP_ZOFF_H, (z_off & 0xFF00) >> 8);
                MI_ASSERT(result==0);
            }
            
        }

        if(x_ok && y_ok && z_ok )
        {
            MI_MSG("--- Calibrate done ---\n");
            goto success_exit;
        }
    }

#if MIR3DA_STK_TEMP_SOLUTION   
     if(x_ok + y_ok + z_ok  == 2){

       if(x_ok == 0){
        x_off = x_off_original;
        result = mir3da_register_write(handle, SOCLE_REG_OTP_XOFF_L, x_off & 0xFF);
        MI_ASSERT(result==0);
        result = mir3da_register_write(handle, SOCLE_REG_OTP_XOFF_H, (x_off & 0xFF00) >> 8);
        MI_ASSERT(result==0);

        MI_MSG("--- Calibrate done but x skipped---\n");    

       }else 
       if(y_ok == 0){
           
        y_off = y_off_original;
        result = mir3da_register_write(handle, SOCLE_REG_OTP_YOFF_L, y_off & 0xFF);
        MI_ASSERT(result==0);
        result = mir3da_register_write(handle, SOCLE_REG_OTP_YOFF_H, (y_off & 0xFF00) >> 8);
        MI_ASSERT(result==0);

        MI_MSG("--- Calibrate done but y skipped---\n");    

       }else
        if(z_ok == 0){

        z_off = z_off_original;
        result = mir3da_register_write(handle, SOCLE_REG_OTP_ZOFF_L, z_off & 0xFF);
        MI_ASSERT(result==0);
        result = mir3da_register_write(handle, SOCLE_REG_OTP_ZOFF_H, (z_off & 0xFF00) >> 8);
        MI_ASSERT(result==0);

        MI_MSG("--- Calibrate done but z skipped---\n");    
        }

         goto success_exit;
        }
#endif

fail_exit:
    return -1;

success_exit:
   offset_data[0]= x_off & 0xFF;
   offset_data[1]= (x_off & 0xFF00) >> 8;
   offset_data[2]= y_off & 0xFF;
   offset_data[3]= (y_off & 0xFF00) >> 8;
   offset_data[4]= z_off & 0xFF;
   offset_data[5]= (z_off & 0xFF00) >> 8;   
   result = mir3da_write_offset_to_file(offset_data); 
   
#endif /* !MIR3DA_OFFSET_TEMP_SOLUTION */ 
   return result;
}

int mir3da_calibrate(MIR_HANDLE handle, int z_dir)
{
    int     res = 0;
#if MIR3DA_OFFSET_TEMP_SOLUTION
    if( is_cali )
        return -1;
    is_cali = 1;

    /* restore original direction if last calibration was done in a wrong direction */
    mir3da_write_offset(handle, original_offset);
 
    res = mir3da_gsensor_drv.obj[gsensor_mod].calibrate(handle, z_dir); 
    if (res != 0){
	 MI_ERR("Calibrate failed !");
	 mir3da_write_offset(handle, original_offset); 
    }
    bLoad = 1;
    is_cali = 0;
#endif /* !MIR3DA_OFFSET_TEMP_SOLUTION */
    return res;
}

#if MIR3DA_AUTO_CALIBRATE
#define STABLE_CHECK_SAMPLE_NUM     10
#define STABLE_CHECK_THRESHOLD      50000
#define AUTO_CALI_THRESHOLD_XY      300
#define AUTO_CALI_THRESHOLD_Z       500
static unsigned char    stable_sample_cnt = 0;
static int              stable_sample_pow_sum[STABLE_CHECK_SAMPLE_NUM] = {0};
static int              stable_sample_sum[3] = {0};

static int mir3da_auto_cali_condition_confirm(int x, int y, int z, int ave_xyz[3])
{
    int    max = 0, min = 0;
    int    i;
    
    stable_sample_pow_sum[stable_sample_cnt] = x*x + y*y + z*z;
    stable_sample_sum[0] += x;
    stable_sample_sum[1] += y;
    stable_sample_sum[2] += z;
    stable_sample_cnt++;
    
    MI_MSG("---stable_sample_cnt = %d", stable_sample_cnt);
    
    if( stable_sample_cnt < STABLE_CHECK_SAMPLE_NUM )
        return -1;
    stable_sample_cnt = 0;
    
    max = stable_sample_pow_sum[0];
    min = stable_sample_pow_sum[0];
    stable_sample_pow_sum[0] = 0;
    for(i = 1; i < STABLE_CHECK_SAMPLE_NUM; i++){
        if( stable_sample_pow_sum[i] > max )
            max = stable_sample_pow_sum[i];
        if( stable_sample_pow_sum[i] < min )
            min = stable_sample_pow_sum[i];
        stable_sample_pow_sum[i] = 0;
    }
    MI_MSG("---max = %d; min = %d", max, min);

    ave_xyz[0] = stable_sample_sum[0]/STABLE_CHECK_SAMPLE_NUM;
    stable_sample_sum[0] = 0;
    ave_xyz[1] = stable_sample_sum[1]/STABLE_CHECK_SAMPLE_NUM;
    stable_sample_sum[1] = 0;
    ave_xyz[2] = stable_sample_sum[2]/STABLE_CHECK_SAMPLE_NUM;
    stable_sample_sum[2] = 0;
    
    MI_MSG("ave_x = %d, ave_y = %d, ave_z = %d", ave_xyz[0], ave_xyz[1], ave_xyz[2]);
        
    if( (abs(max-min) > STABLE_CHECK_THRESHOLD) || abs(ave_xyz[0]) > AUTO_CALI_THRESHOLD_XY || abs(ave_xyz[1]) > AUTO_CALI_THRESHOLD_XY || abs(abs(ave_xyz[2])-1024) > AUTO_CALI_THRESHOLD_Z) {
        return -1;
    }
    return 0;
}

static int mir3da_auto_calibrate(MIR_HANDLE handle, int x, int y, int z)
{
    int     res = 0;
    int     xyz[3] = {0};

    if( is_cali )
        return -1;
    is_cali = 1;

    if( mir3da_auto_cali_condition_confirm(x, y, z, xyz) ){
        res = -1;
        goto EXIT;
    }
    
    /* restore original direction if last calibration was done in a wrong direction */
    mir3da_write_offset(handle, original_offset);
 
    res = mir3da_gsensor_drv.obj[gsensor_mod].auto_calibrate(handle, xyz); 
    if (res != 0){
	    MI_ERR("Calibrate failed !");
	    mir3da_write_offset(handle, original_offset); 
    }

EXIT:    
    is_cali = 0;

    return res;
}
#endif /* !MIR3DA_AUTO_CALIBRATE */

static int SOCLE_interrupt_ops(MIR_HANDLE handle, mir_int_ops_t *ops)
{
    int                 res = 0;
    
    switch(ops->type)
    {
        case INTERRUPT_OP_INIT:
        
            /* active level */
            mir3da_register_mask_write(handle, SOCLE_REG_CTRL_REG6, (1<<1), (ops->data.init.level<<1));
            /* latch */
            mir3da_register_mask_write(handle, SOCLE_REG_CTRL_REG5, (1<<3), (ops->data.init.latch<<3));
            mir3da_register_mask_write(handle, SOCLE_REG_CLICK_CFG, (1<<6), (ops->data.init.latch<<6));
            
            break;
            
        case INTERRUPT_OP_ENABLE:
        
            switch( ops->data.int_src )
            {            
                case INTERRUPT_ACTIVITY:
                        
                    mir3da_register_write(handle, SOCLE_REG_INT1_CFG, 0x7f);
                    mir3da_register_write(handle, SOCLE_REG_INT2_CFG, 0x7f);
                    break;
                    
                case INTERRUPT_CLICK:
                    /* Enable all directions click and double click detect */
                    mir3da_register_mask_write(handle, SOCLE_REG_CLICK_CFG, 0x3f, 0x3f);
                    break;
            }
            break;
            
        case INTERRUPT_OP_CONFIG:
        
            switch( ops->data.cfg.int_src )
            {
                case INTERRUPT_ACTIVITY:
                    
                    if(ops->data.cfg.pin == INTERRUPT_PIN1) {
                        /* enable this int on INT1 */
                        mir3da_register_mask_write(handle, SOCLE_REG_CTRL_REG3, (1<<6), (1<<6));

                        mir3da_register_mask_write(handle, SOCLE_REG_INT1_THS, 0x7f, ops->data.cfg.int_cfg.act.threshold);
                        mir3da_register_mask_write(handle, SOCLE_REG_INT1_DURATION, 0x7f, ops->data.cfg.int_cfg.act.duration);
                    }
                    else if(ops->data.cfg.pin == INTERRUPT_PIN2) {
                        /* enable this int on INT2 */
                        mir3da_register_mask_write(handle, SOCLE_REG_CTRL_REG6, (1<<5), (1<<5));
                        
                        mir3da_register_mask_write(handle, SOCLE_REG_INT2_THS, 0x7f, ops->data.cfg.int_cfg.act.threshold);
                        mir3da_register_mask_write(handle, SOCLE_REG_INT2_DURATION, 0x7f, ops->data.cfg.int_cfg.act.duration);
                    }
                    break;
                    
                case INTERRUPT_CLICK:
                    
                    mir3da_register_mask_write(handle, SOCLE_REG_CLICK_THS, 0x7f, ops->data.cfg.int_cfg.clk.threshold);
                    mir3da_register_mask_write(handle, SOCLE_REG_TIME_LIMIT, 0x7f, ops->data.cfg.int_cfg.clk.click_time);
                    mir3da_register_write(handle, SOCLE_REG_TIME_LATENCY, ops->data.cfg.int_cfg.clk.quiet_time);
                    mir3da_register_write(handle, SOCLE_REG_TIME_WINDOW, ops->data.cfg.int_cfg.clk.window);
                    
                    if(ops->data.cfg.pin == INTERRUPT_PIN1) {
                        mir3da_register_mask_write(handle, SOCLE_REG_CTRL_REG3, (1<<7), (1<<7));
                    }
                    else if(ops->data.cfg.pin == INTERRUPT_PIN2) {
                        mir3da_register_mask_write(handle, SOCLE_REG_CTRL_REG6, (1<<7), (1<<7));
                    }
                    break;
            }
            break;
            
        case INTERRUPT_OP_DISABLE:
            switch( ops->data.int_src )
            {
                case INTERRUPT_ACTIVITY:
                        
                    mir3da_register_write(handle, SOCLE_REG_INT1_CFG, 0);
                    mir3da_register_write(handle, SOCLE_REG_INT2_CFG, 0);
                    break;
                    
                case INTERRUPT_CLICK:
                    /* Enable all directions click and double click detect */
                    mir3da_register_mask_write(handle, SOCLE_REG_CLICK_CFG, 0x3f, 0);
                    break;
            }
            break;
            
        default:
            MI_ERR("Unsupport operation !");
    }
    
    return res;
}

static int NSA_interrupt_ops(MIR_HANDLE handle, mir_int_ops_t *ops)
{   
    switch(ops->type)
    {
        case INTERRUPT_OP_INIT:
        
            /* latch */
            mir3da_register_mask_write(handle, NSA_REG_INT_LATCH, 0x0f, ops->data.init.latch);
            /* active level & output mode */
            mir3da_register_mask_write(handle, NSA_REG_INT_PIN_CONFIG, 0x0f, ops->data.init.level|(ops->data.init.pin_mod<<1)|(ops->data.init.level<<2)|(ops->data.init.pin_mod<<3));
            
            break;
            
        case INTERRUPT_OP_ENABLE:
            switch( ops->data.int_src )
            { 
                case INTERRUPT_ACTIVITY:
                    /* Enable active interrupt */
                    mir3da_register_mask_write(handle, NSA_REG_INTERRUPT_SETTINGS1, 0x07, 0x07);                    
                    break;
                case INTERRUPT_CLICK:
                    /* Enable single and double tap detect */
                    mir3da_register_mask_write(handle, NSA_REG_INTERRUPT_SETTINGS1, 0x30, 0x30);                   
                    break;
            }
            break;
            
        case INTERRUPT_OP_CONFIG:
            
                            
            switch( ops->data.cfg.int_src )
            {
                case INTERRUPT_ACTIVITY:
                         
                    mir3da_register_write(handle, NSA_REG_ACTIVE_THRESHOLD, ops->data.cfg.int_cfg.act.threshold);
                    mir3da_register_mask_write(handle, NSA_REG_ACTIVE_DURATION, 0x03, ops->data.cfg.int_cfg.act.duration);
                    
                    /* Int mapping */
                    if(ops->data.cfg.pin == INTERRUPT_PIN1) {  
                        mir3da_register_mask_write(handle, NSA_REG_INTERRUPT_MAPPING1, (1<<2), (1<<2));
                    }
                    else if(ops->data.cfg.pin == INTERRUPT_PIN2) {
                        mir3da_register_mask_write(handle, NSA_REG_INTERRUPT_MAPPING3, (1<<2), (1<<2));
                    }
                    break;
                    
                case INTERRUPT_CLICK:
                    
                    mir3da_register_mask_write(handle, NSA_REG_TAP_THRESHOLD, 0x1f, ops->data.cfg.int_cfg.clk.threshold);
                    mir3da_register_mask_write(handle, NSA_REG_TAP_DURATION, (0x03<<5)|(0x07), (ops->data.cfg.int_cfg.clk.quiet_time<<7)|(ops->data.cfg.int_cfg.clk.click_time<<6)|(ops->data.cfg.int_cfg.clk.window));
                    
                    if(ops->data.cfg.pin == INTERRUPT_PIN1) {
                        mir3da_register_mask_write(handle, NSA_REG_INTERRUPT_MAPPING1, 0x30, 0x30);
                    }
                    else if(ops->data.cfg.pin == INTERRUPT_PIN2) {
                        mir3da_register_mask_write(handle, NSA_REG_INTERRUPT_MAPPING3, 0x30, 0x30);
                    }
                    break;
            }
            break;
            
        case INTERRUPT_OP_DISABLE:
            switch( ops->data.int_src )
            {
                case INTERRUPT_ACTIVITY:
                    /* Enable active interrupt */
                    mir3da_register_mask_write(handle, NSA_REG_INTERRUPT_SETTINGS1, 0x07, 0x00);
                    break;
                    
                case INTERRUPT_CLICK:
                    /* Enable single and double tap detect */
                    mir3da_register_mask_write(handle, NSA_REG_INTERRUPT_SETTINGS1, 0x30, 0x00);
                    break;
            }
            break;
            
        default:
            MI_ERR("Unsupport operation !");
    }
    return 0;
}

int mir3da_interrupt_ops(MIR_HANDLE handle, mir_int_ops_t *ops)
{
    int res = 0;
    
    res = mir3da_gsensor_drv.obj[gsensor_mod].int_ops(handle, ops);
    return res;
}

#if FILTER_AVERAGE_ENHANCE
int mir3da_get_filter_param(struct mir3da_filter_param_s* param){
    if (param == 0){
        MI_ERR("Invalid param!");
        return -1;
    }

    param->filter_param_h = core_ctx.tFac[0].filter_param_h;
    param->filter_param_l = core_ctx.tFac[0].filter_param_l;
    param->filter_threhold = core_ctx.tFac[0].filter_threhold;

    MI_MSG("FILTER param is get: filter_param_h = %d, filter_param_l = %d, filter_threhold = %d", param->filter_param_h, param->filter_param_l, param->filter_threhold);
    
    return 0;
}

int mir3da_set_filter_param(struct mir3da_filter_param_s* param){

    if (param == 0){
        MI_ERR("Invalid param!");
        return -1;
    }

    MI_MSG("FILTER param is set: filter_param_h = %d, filter_param_l = %d, filter_threhold = %d", param->filter_param_h, param->filter_param_l, param->filter_threhold);

    core_ctx.tFac[1].filter_param_l = core_ctx.tFac[2].filter_param_l = core_ctx.tFac[0].filter_param_l = param->filter_param_l;
    core_ctx.tFac[1].filter_param_h =core_ctx.tFac[2].filter_param_h = core_ctx.tFac[0].filter_param_h  = param->filter_param_h;
    core_ctx.tFac[1].filter_threhold = core_ctx.tFac[2].filter_threhold =core_ctx.tFac[0].filter_threhold = param->filter_threhold;

    return 0;
}
#endif //#if FILTER_AVERAGE_ENHANCE

int mir3da_get_enable(MIR_HANDLE handle, char *enable)
{
	unsigned char              reg_data;    
    int             res = 0;
 
    res = mir3da_register_read(handle, mir3da_gsensor_drv.obj[gsensor_mod].power.addr, &reg_data);
    if(res != 0) {
		return res;
	}
    
    *enable = ( reg_data|mir3da_gsensor_drv.obj[gsensor_mod].power.mask ) ? 1 : 0;

    return res;
}

int mir3da_set_enable(MIR_HANDLE handle, char enable)
{
    int             res = 0;
    unsigned char              reg_data = 0;

    if(!enable) {
        reg_data = mir3da_gsensor_drv.obj[gsensor_mod].power.value;
    }
    
    res = mir3da_register_mask_write(handle, mir3da_gsensor_drv.obj[gsensor_mod].power.addr, mir3da_gsensor_drv.obj[gsensor_mod].power.mask, reg_data);

    return res;
}

static int SOCLE_get_reg_data(MIR_HANDLE handle, char *buf)
{
    int     i, count = 0;
    unsigned char   val;
    
    count += mir3da_gsensor_drv.method->mysprintf(buf+count, "---------page 0---------");
    for (i = 0; i <= 0x003d; i++){
        if(i%16 == 0)
            count += mir3da_gsensor_drv.method->mysprintf(buf+count, "\n%02x\t", i);
        mir3da_register_read(handle, i, &val); 
        count += mir3da_gsensor_drv.method->mysprintf(buf+count, "%02X ", val);
    }

    count += mir3da_gsensor_drv.method->mysprintf(buf+count, "\n---------page 1---------");
    for (i = 0x0100; i <= 0x012a; i++){
        if((i&0xff)%16 == 0)
            count += mir3da_gsensor_drv.method->mysprintf(buf+count, "\n%02x\t", (i & 0xff));
        mir3da_register_read(handle, i, &val); 
        count += mir3da_gsensor_drv.method->mysprintf(buf+count, "%02X ", val);
        
    }
    count += mir3da_gsensor_drv.method->mysprintf(buf+count, "\n---------end---------\n");
    
    return count;
}

static int NSA_get_reg_data(MIR_HANDLE handle, char *buf)
{
    int                 count = 0;
    int                 i;
    unsigned char       val;
        
    count += mir3da_gsensor_drv.method->mysprintf(buf+count, "---------start---------");
    for (i = 0; i <= 0xd2; i++){
        if(i%16 == 0)
            count += mir3da_gsensor_drv.method->mysprintf(buf+count, "\n%02x\t", i);
        mir3da_register_read(handle, i, &val); 
        count += mir3da_gsensor_drv.method->mysprintf(buf+count, "%02X ", val);
    }

    count += mir3da_gsensor_drv.method->mysprintf(buf+count, "\n--------end---------\n");    
    return count;
}

int mir3da_get_reg_data(MIR_HANDLE handle, char *buf)
{     
    return mir3da_gsensor_drv.obj[gsensor_mod].get_reg_data(handle, buf);
}

int mir3da_set_odr(MIR_HANDLE handle, int delay)
{
    int     res = 0;
    int     odr = 0;

    if(delay <= 5)
    {
       odr = MIR3DA_ODR_200HZ;
    }
    else if(delay <= 10)
    {
       odr = MIR3DA_ODR_100HZ;
    }
    else
    {
       odr = MIR3DA_ODR_50HZ;
    }		

    res = mir3da_register_mask_write(handle, mir3da_gsensor_drv.obj[gsensor_mod].odr_sect[odr].addr, 
			mir3da_gsensor_drv.obj[gsensor_mod].odr_sect[odr].mask,mir3da_gsensor_drv.obj[gsensor_mod].odr_sect[odr].value);
    if(res != 0) {
        return res;
    } 

    return res;	
}

static int mir3da_soft_reset(MIR_HANDLE handle)
{
    int             res = 0;
    unsigned char   reg_data;

    reg_data = mir3da_gsensor_drv.obj[gsensor_mod].soft_reset.value;
    res = mir3da_register_mask_write(handle, mir3da_gsensor_drv.obj[gsensor_mod].soft_reset.addr, mir3da_gsensor_drv.obj[gsensor_mod].soft_reset.mask, reg_data);
    mir3da_gsensor_drv.method->msdelay(5);
    
    return res;
}

int mir3da_module_detect(PLAT_HANDLE handle)
{
    int             i, res = 0;
    unsigned char   cid, mid;
    int             is_find = -1;

    /* Probe gsensor module */  
    for(i=0;i<sizeof(mir3da_gsensor)/sizeof(mir3da_gsensor[0]);i++) {
        res = mir3da_register_read(handle, mir3da_gsensor[i].chip_id.addr, &cid);
	    if(res != 0) {
		    return res;
	    }
        
        cid &= mir3da_gsensor[i].chip_id.mask;
        if(mir3da_gsensor[i].chip_id.value == cid) {
            res = mir3da_register_read(handle, mir3da_gsensor[i].mod_id.addr, &mid);
	        if(res != 0) {
		        return res;
	        }

            mid &= mir3da_gsensor[i].mod_id.mask;
            if( mir3da_gsensor[i].mod_id.value == mid ){
                MI_MSG("Found Gsensor MIR3DA !");
                gsensor_mod = i;
                is_find =0;
                break;
            }
        }
    }

    return is_find;
}

int mir3da_install_general_ops(struct general_op_s *ops)
{    
    if(0 == ops){
        return -1;
    }
    
    mir3da_gsensor_drv.method = ops;
    return 0;
}

MIR_HANDLE mir3da_core_init(PLAT_HANDLE handle)
{
    int             res = 0;
    unsigned char   data;

#if FILTER_AVERAGE_ENHANCE 
int i =0;
#endif

    mir3da_gsensor_drv.obj = mir3da_gsensor;

    if(gsensor_mod < 0){
        res = mir3da_module_detect(handle);
        if(res) {
	     MI_ERR("Can't find Mir3da gsensor!!");	
            return 0;
        }

        /* No miramems gsensor instance found */
        if(gsensor_mod < 0) {   
            return 0;
        }
    }

    MI_MSG("Probe gsensor module: %s", mir3da_gsensor[gsensor_mod].asic);

#if MIR3DA_SENS_TEMP_SOLUTION
    if(GSENSOR_MOD_SOCLE == gsensor_mod) {
        mir3da_register_read(handle, SOCLE_REG_OTP_TRIM_THERM_H, &data);
        if (data == 2){
            MI_ERR("Enable sens temp solution !");
            bSensZoom = 1;
        }
    }
#endif

#if FILTER_AVERAGE_ENHANCE 
    /* configure default filter param */    
    for (i = 0; i < 3;i++){     
        core_ctx.tFac[i].filter_param_l = 2;
        core_ctx.tFac[i].filter_param_h = 8;
        core_ctx.tFac[i].filter_threhold = 60;

        core_ctx.tFac[i].refN_l = 0;
        core_ctx.tFac[i].refN_h = 0;
    }    
#endif

	res = mir3da_chip_resume(handle);
	if(res) {
		MI_ERR("chip resume fail!!\n");
		return 0;
	}

    return handle;
}

int mir3da_chip_resume(MIR_HANDLE handle)
{
    int     res = 0;
    unsigned char      reg_data;
    unsigned char      i = 0;
	
    res = mir3da_soft_reset(handle); 
    if(res) {
        MI_ERR("Do softreset failed !");
        return res;
    }
    
    for(i=0;i<MIR3DA_INIT_SECT_LEN;i++) {
        if( mir3da_gsensor_drv.obj[gsensor_mod].init_sect[i].addr < 0 ) {
            break;
        }

        reg_data = mir3da_gsensor_drv.obj[gsensor_mod].init_sect[i].value;
        res = mir3da_register_mask_write(handle, mir3da_gsensor_drv.obj[gsensor_mod].init_sect[i].addr, mir3da_gsensor_drv.obj[gsensor_mod].init_sect[i].mask, reg_data);
        if(res != 0) {
            return res;
        } 
    }
	
#if MIR3DA_OFFSET_TEMP_SOLUTION
    res = mir3da_read_offset(handle, original_offset);
    if (res != 0){
        MI_ERR("Read offset failed !");
        return res;    
    }
	
    bLoad = 0;
		readOffsetCnt = -1;    
    manual_load_cali_file(handle);
#endif

    return res;
}
